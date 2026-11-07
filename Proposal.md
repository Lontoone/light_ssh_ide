
For remote development on a Raspberry Pi 3 (1GB RAM, 32-bit ARM), the primary bottleneck of VS Code Remote-SSH is that **spawning the Node.js Agent on the Pi consumes excessive memory and CPU, frequently triggering Out-Of-Memory (OOM) errors**.

This proposal adopts a **"Zero-Agent / Client-Driven"** architecture: the Raspberry Pi relies solely on the built-in system `sshd` and `sftp-server`. All UI rendering, syntax parsing, and state management are handled entirely by the local client, achieving a lightweight footprint, high performance, and near-instant startup.

---

## 1. Tech Stack Selection & Evaluation

| Module | Recommended | Alternative | Rationale |
| --- | --- | --- | --- |
| **GUI Framework** | **Qt 6 / Qt 5 (C++ Widgets)** | Dear ImGui + GLFW | Qt Widgets has low memory overhead (~30–50MB on startup) and natively offers a mature Virtual TreeView, Splitter, Docking, and an asynchronous event loop. While Dear ImGui is lighter, the engineering cost of implementing a full-fledged text editor and terminal within it is excessively high. |
| **SSH / SFTP Layer** | **libssh2** | libssh / raw ssh subprocess | Pure C implementation with a minimal footprint (~1–2MB statically compiled). Natively supports non-blocking asynchronous channels and SFTP, with excellent cross-platform compatibility. |
| **Terminal Emulation (PTY)** | **libvterm** + Custom Widget / **QTermWidget** | ANSI Escape Parser | `libvterm` provides rigorous VT100/xterm state-machine processing, integrates smoothly with `libssh2` PTY channels, and delivers high rendering performance. |
| **Text Editing & Syntax Highlighting** | **QScintilla** or **KSyntaxHighlighting** | ImGuiColorTextEdit | QScintilla wraps the Scintilla core, natively supporting chunked loading for large files, code folding, and multi-language lexers. If keeping a pure Qt dependency tree, KSyntaxHighlighting supports Kate syntax definition files (covering 300+ languages). |
| **Build System** | **CMake 3.20+** | Meson / Ninja | Industry standard for cross-platform builds; easily integrates with `FetchContent` or `vcpkg`. |

---

## 2. System Architecture


```c
+------------------------------------------------------------------------+
|                         Lightweight IDE (Host)                         |
|                                                                        |
|  +--------------------+  +------------------------------------------+  |
|  |  Remote Tree View  |  |             Editor Workspace             |  |
|  |  (Lazy SFTP Tree)  |  |   - QScintilla / Syntax Highlighting     |  |
|  |                    |  |   - Memory Cache & Local Temp Mirror     |  |
|  +---------+----------+  +--------------------+---------------------+  |
|            |                                  |                        |
|            +-----------------+  +-------------+                        |
|                              |  |                                      |
|  +---------------------------v--v-----------------------------------+  |
|  |             Integrated Terminal (libvterm / PTY)                 |  |
|  |  - Interactive Shell / Compilation Output (gcc, make, cmake)     |  |
|  +-----------------------------------+------------------------------+  |
|                                      |                                 |
|  +-----------------------------------v------------------------------+  |
|  |                  Network Engine (libssh2 Layer)                  |  |
|  |   [SFTP Worker Thread]          [Interactive SSH Channel Thread] |  |
|  +-----------------------------------+------------------------------+  |
+--------------------------------------|---------------------------------+
| SSH / SFTP (Port 22)
v
+------------------------------------------------------------------------+
|                    Raspberry Pi 3 (Linux 32-bit ARM)                   |
|                                                                        |
|  - OpenSSH Server (sshd)                                               |
|  - sftp-server subsystem                                               |
|  - bash / zsh / sh (Compilation: gcc, g++, gdb, cmake)                 |
|  * Completely zero-agent: No Node.js / Python daemons required         |
+------------------------------------------------------------------------+

```

---

## 3. Core Feature Implementation

### 3.1 Terminal & SSH Connection Module (Features 1 & 3)

- **Interactive SSH Session Setup:**
  - Establish the connection in a background worker thread via `libssh2_session_handshake`, supporting both Password and Public Key authentication.
  - Allocate a `LIBSSH2_CHANNEL` and request a PTY: `libssh2_channel_request_pty_ex(channel, "xterm-256color", ...)`.
  - Start the remote shell: `libssh2_channel_shell(channel)`.

- **Terminal Interaction & Command Execution:**
  - User keystrokes are written directly to `libssh2_channel_write`. Data returned from the host is read via `libssh2_channel_read` and piped into `libvterm` to parse colors, styles, and cursor positions before rendering onto the terminal canvas.
  - Users can run standard commands such as `gcc -O2 main.c -o app`, `make`, `cmake --build .`, or `./app`, with full support for interrupt signals like `Ctrl+C`.

### 3.2 Lazy-Loading SFTP Tree View (Feature 2)

To prevent connection latency and memory bloat from recursive remote directory traversal, a **virtual tree model** is used:

1. **Model Architecture:** Subclass `QAbstractItemModel`. Each node represents a file or directory, storing `remote_path`, `is_directory`, `is_expanded`, and `children_loaded`.
2. **Post-Connection Initialization:** Once the SSH session connects, initiate an SFTP request for the target root directory (e.g., `/home/pi`):
   ```c
   LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(sftp, remote_path);
   // Read only immediate children; close the handle once complete


3. **Dynamic Lazy Loading:**
* Unexpanded directories populate a placeholder node (Dummy Node) so the UI displays the expandable arrow (`>`).
* When the user expands a node, capture the `expand` event and verify its loaded state:
1. Mark the Dummy Node as `Loading...`.
2. Dispatch an SFTP `readdir` call to a background worker thread.
3. Upon retrieval, update the model via Qt signals and replace the Dummy Node with real entries.


* Collapsing a folder merely toggles view visibility while preserving cache; an explicit "Refresh" action can invalidate and reload the current subtree.



### 3.3 Code Editor & Syntax Highlighting (Feature 4)

* **File Synchronization & Caching:**
* Double-clicking a file asynchronously pulls content via SFTP into a local cache directory (`~/.cache/ide/remote_cache/...`).
* The editor opens the local cached file directly. Saving (`Ctrl+S`) commits locally and fires an SFTP write operation (`libssh2_sftp_open(..., LIBSSH2_FXF_WRITE)`) to sync back to the Pi.


* **Syntax Highlighting:**
* **QScintilla Integration:** Uses bundled lexers (C/C++, Python, Shell, Rust, etc.) dynamically assigned by file extension:
```cpp
if (ext == "cpp" || ext == "c" || ext == "h") {
    editor->setLexer(new QsciLexerCPP(this));
} else if (ext == "py") {
    editor->setLexer(new QsciLexerPython(this));
} else if (ext == "sh") {
    editor->setLexer(new QsciLexerBash(this));
}
```


* **Theme & Palettes:** Configurable color schemes aligning with dark themes (e.g., VS Code Dark) for keywords, constants, strings, and comments.



---

## 4. Module & Project Structure

```
light-ssh-ide/
├── CMakeLists.txt
├── cmake/
│   └── Dependencies.cmake
└── src/
    ├── main.cpp
    ├── core/
    │   ├── ssh_client.h / .cpp        # libssh2 session management & worker threads
    │   ├── sftp_manager.h / .cpp      # SFTP upload, download, and readdir queues
    │   └── remote_file_info.h         # Remote entry data abstractions
    ├── ui/
    │   ├── main_window.h / .cpp       # Main window, Splitter, and Dock layout
    │   ├── terminal_widget.h / .cpp   # PTY terminal widget (bridged to SSH channel)
    │   ├── tree_model.h / .cpp        # QAbstractItemModel (Lazy-loading implementation)
    │   ├── tree_view.h / .cpp         # Remote file tree component
    │   └── code_editor.h / .cpp       # Editor canvas & syntax highlighting glue
    └── resources/
        └── icons/                     # UI assets for file and directory states

```

---

## 5. Sample Cross-Platform CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(LightSSH_IDE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt Dependencies
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# libssh2 and OpenSSL Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED libssh2)
find_package(OpenSSL REQUIRED)

# Third-party libraries (via Submodule, system library, or vcpkg)
find_package(QScintilla REQUIRED)

include_directories(
    src/
    ${LIBSSH2_INCLUDE_DIRS}${OPENSSL_INCLUDE_DIR}
)

set(SOURCES
    src/main.cpp
    src/core/ssh_client.cpp
    src/core/sftp_manager.cpp
    src/ui/main_window.cpp
    src/ui/terminal_widget.cpp
    src/ui/tree_model.cpp
    src/ui/code_editor.cpp
)

add_executable(${PROJECT_NAME}${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${LIBSSH2_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    qscintilla2_qt6
)

# Compiler warnings and platform flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

```

---

## 6. Development Milestones

```
[Phase 1: SSH & Terminal]  ---> [Phase 2: SFTP Virtual Tree] ---> [Phase 3: Editor & Highlighting] ---> [Phase 4: Optimization]
- Auth dialog / Input loop       - Asynchronous root loading      - Local caching & two-way sync   - Large directory pagination
- libssh2 PTY & terminal view    - Lazy-loading child expansion   - QScintilla lexer switching      - Auto-reconnect handling
- Remote make/gcc validation     - Node states (file/directory)   - Editor shortcuts & auto-upload  - Memory profiling on Pi 3

```

1. **Phase 1: Core Connectivity & Terminal Execution**
* Implement `ssh_client` and the base PTY terminal widget.
* Validate end-to-end functionality: establish connection (`ssh pi@192.168.x.x`), verify terminal interaction, and successfully execute remote builds via `make` / `gcc`.


2. **Phase 2: SFTP File Tree & Lazy Loading**
* Asynchronously fetch the root directory upon connection; implement `canFetchMore` / `fetchMore` in `QAbstractItemModel` or hook into node expansion slots.
* Benchmark UI responsiveness under artificial network latency to guarantee zero UI blocking during tree expansion.


3. **Phase 3: Editor Integration & Syntax Highlighting**
* Fetch remote files to temporary storage on item click; hook into the appropriate QScintilla lexers.
* Bind `Ctrl+S` to write changes locally and trigger an SFTP push to the Pi, enabling seamless transitions between editing and terminal compilation.


4. **Phase 4: Raspberry Pi 3 Specific Optimizations**
* Implement SSH keep-alive packets to prevent idle connection drops.
* Throttle concurrent SFTP requests (limit queue concurrency to 2–4 parallel transfers) to avoid thrashing the Pi's CPU and memory.

