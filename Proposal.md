# LightSSH-IDE: Zero-Agent Cross-Platform Remote IDE Proposal

For remote development on legacy Linux servers or resource-constrained SBCs like the **Raspberry Pi 3 (1GB RAM, 32-bit ARM)**, the primary bottleneck of modern remote tools (such as VS Code Remote-SSH) is that **spawning a server-side Node.js/Python agent consumes excessive RAM and CPU, frequently triggering Out-Of-Memory (OOM) freezes**.

This proposal defines a **"Zero-Agent / Client-Driven"** lightweight IDE designed specifically to connect to older Linux servers over standard SSH:
1. **Zero Server Overhead:** The remote Linux server relies solely on its built-in `sshd`, `sftp-server`, and standard toolchain (`gcc`, `gdb`, `make`). All UI rendering, file caching, syntax parsing, and state management happen strictly on the client.
2. **True Cross-Platform Client (Windows & Android):** The client codebase is engineered in standard C++17 and Qt to compile cleanly across:
   - **Windows Desktop** (Native `.exe` via MSVC / MinGW).
   - **Android Native App** (`.apk` package via Qt for Android & NDK).
   - **Android Termux Binary** (Native ELF executable compiled and run directly inside Termux via `Termux:X11` or VNC).
3. **Zero-Friction Dependencies:** Instead of heavy, hard-to-cross-compile editor frameworks (`QScintilla` or KDE's `KSyntaxHighlighting`), this architecture embeds **`Waqar144/QSourceHighlite`**—a dependency-free, regex-free Qt syntax highlighter consisting of just three C++ source files—ensuring hassle-free builds on Windows, Android NDK, and Termux alike.

---

## 1. Tech Stack Selection & Evaluation

| Module | Recommended | Alternative | Rationale & Cross-Platform Ease |
| --- | --- | --- | --- |
| **GUI Framework** | **Qt 6 / Qt 5 (C++ Widgets)** | Dear ImGui + SDL2 | Low memory overhead (~30–50MB on startup) with mature `QTreeView`, `QSplitter`, `QPlainTextEdit`, kinetic touch scrolling (`QScroller`), and official support for Windows, Android `.apk`, and Termux (`qt6-qtbase`). |
| **Text Editing & Syntax Highlighting** | **`QPlainTextEdit` + `Waqar144/QSourceHighlite`** | QScintilla / KSyntaxHighlighting | `QSourceHighlite` subclasses `QSyntaxHighlighter` with zero external dependencies beyond Qt GUI. It uses a fast non-regex lexer (highlighting 100k lines in ~0.4s) and compiles directly as 3 `.cpp` files, avoiding complex cross-compilation failures on Android/Termux. |
| **SSH / SFTP Layer** | **libssh2** (with OpenSSL / mbedTLS) | libssh / raw `ssh` subprocess | Pure C library (~1–2MB statically linked) with non-blocking async channel & SFTP APIs. Easily installed via `vcpkg` (Windows), `pkg install libssh2` (Termux), or CMake `FetchContent` (Android NDK). |
| **Terminal Emulation (PTY)** | **libvterm** + Custom `QWidget` | Custom ANSI Parser | Pure C VT100/xterm state machine with zero OS-specific PTY dependencies (since the PTY lives on the remote SSH channel), making it 100% portable to Windows and Android. |
| **Build System** | **CMake 3.20+** | QMake / Meson | Seamless single-project support for Windows (`cmake`), Android APK (`qt-cmake` + Gradle), and Termux native ELF compilation. |

---

## 2. System Architecture

```text
+-----------------------------------------------------------------------------+
|            LightSSH-IDE Client (Windows .exe | Android .apk | Termux)       |
|                                                                             |
|  +----------------------+  +---------------------------------------------+  |
|  |   Remote Tree View   |  |               Editor Workspace              |  |
|  |   (Lazy SFTP Tree)   |  |  - Custom QPlainTextEdit (Line Numbers)     |  |
|  |  - Touch & Mouse UI  |  |  - Waqar144/QSourceHighlite (Zero-Regex)    |  |
|  |                      |  |  - QStandardPaths Local Temp Cache Mirror   |  |
|  +----------+-----------+  +----------------------+----------------------+  |
|             |                                     |                         |
|             +------------------+  +---------------+                         |
|                                |  |                                         |
|  +-----------------------------v--v--------------------------------------+  |
|  |               Integrated Terminal (libvterm + QWidget)                |  |
|  |  - Interactive Remote Shell / Build Output (gcc, make, cmake, gdb)    |  |
|  |  - Mobile Extra-Key Bar (ESC, TAB, CTRL, Arrows for Android/Termux)   |  |
|  +------------------------------------+----------------------------------+  |
|                                       |                                     |
|  +------------------------------------v----------------------------------+  |
|  |                    Network Engine (libssh2 Layer)                     |  |
|  |    [SFTP Worker Thread]                [Async SSH PTY Channel Thread] |  |
|  +------------------------------------+----------------------------------+  |
+---------------------------------------|-------------------------------------+
                                        | SSH / SFTP (TCP Port 22)
                                        v
+-----------------------------------------------------------------------------+
|            Older Linux Server / Raspberry Pi 3 (32-bit ARM / x86)           |
|                                                                             |
|  - Standard OpenSSH Server (sshd) & sftp-server subsystem                   |
|  - Native Shell & Toolchain: bash / sh, gcc, g++, gdb, make, cmake          |
|  * Strictly Zero-Agent: No Node.js, Python, or background daemons required  |
+-----------------------------------------------------------------------------+
```

---

## 3. Core Feature Implementation

### 3.1 Terminal & SSH Connection Module

- **Asynchronous SSH Session Setup:**
  - Run connection handshakes in a dedicated worker `QThread` via `libssh2_session_handshake`, supporting Password and Ed25519/RSA Public Key authentication.
  - Allocate a `LIBSSH2_CHANNEL` and request a remote PTY:
    `libssh2_channel_request_pty_ex(channel, "xterm-256color", ...)`.
  - Start the interactive shell via `libssh2_channel_shell(channel)` and dynamically sync terminal resize events (`resizeEvent` -> `libssh2_channel_request_pty_size`).
- **Terminal Interaction & Remote Compilation:**
  - Keystrokes are forwarded to `libssh2_channel_write`. Incoming bytes from `libssh2_channel_read` feed into `libvterm` to update screen cells, attributes, and cursor states on a custom `QPainter` canvas.
  - Supports interactive execution of `gcc -O2 main.c -o app`, `make -j2`, `gdb`, and control signals (`Ctrl+C`, `Ctrl+D`, `Ctrl+Z`).

### 3.2 Lazy-Loading SFTP Tree View

To prevent UI freezes and excessive memory consumption when browsing deep remote filesystems over high-latency links, a **Virtual Lazy-Loading Tree Model** is used:

1. **Model Design:** Subclass `QAbstractItemModel`. Each node stores `remote_path`, `is_directory`, `is_expanded`, and `children_loaded`.
2. **Initial Root Fetch:** Upon session establishment, fetch only the target root directory (e.g., `/home/pi`):
   ```c
   LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(sftp, remote_path);
   // Read only immediate child entries; close handle immediately after
   ```
3. **On-Demand Subtree Expansion:**
   - Unexpanded directories contain a single placeholder (`Dummy Node`) so `QTreeView` renders an expand arrow (`>`).
   - Intercept `QTreeView::expanded(const QModelIndex &)`:
     1. Set placeholder text to `Loading...`.
     2. Enqueue an asynchronous SFTP `readdir` task to the SFTP worker thread.
     3. Emit a signal back to the UI thread to replace the placeholder with actual file/folder nodes.
   - Collapsing a directory retains cached nodes in memory; a context-menu **Refresh** action invalidates and re-queries the selected directory.

### 3.3 Code Editor & `Waqar144/QSourceHighlite` Integration

- **Cross-Platform Local File Caching:**
  - Use `QStandardPaths::writableLocation(QStandardPaths::CacheLocation)` to resolve a valid temp directory across Windows (`%LOCALAPPDATA%/Cache`), Android APK internal sandbox, and Termux (`~/.cache`).
  - Opening a remote file downloads it asynchronously via SFTP into the local cache. Pressing `Ctrl+S` (or tapping the Save button on Android) writes locally and triggers a background `libssh2_sftp_write` back to the server.
- **Lightweight Syntax Highlighting (`QSourceHighlite`):**
  - Instead of compiling heavy external libraries, vendor `Waqar144/QSourceHighlite` (`qsourcehighliter.cpp`, `qsourcehighliterthemes.cpp`, `languagedata.cpp`) directly into the project.
  - Attach `QSourceHighliter` to the editor's `QTextDocument` and map file extensions to language enums:
    ```cpp
    #include "qsourcehighliter.h"

    using namespace QSourceHighlite;

    void CodeEditor::setupHighlighter(const QString &ext) {
        if (!m_highlighter) {
            m_highlighter = new QSourceHighliter(this->document());
            m_highlighter->setTheme(QSourceHighliter::Themes::Monokai);
        }

        if (ext == "c") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeC);
        } else if (ext == "cpp" || ext == "h" || ext == "hpp") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeCpp);
        } else if (ext == "py") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodePython);
        } else if (ext == "sh" || ext == "bash") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeBash);
        } else if (ext == "cmake" || ext == "txt") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeCMake);
        } else if (ext == "js") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeJs);
        } else if (ext == "json") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeJson);
        } else if (ext == "rs") {
            m_highlighter->setCurrentLanguage(QSourceHighliter::CodeRust);
        }
    }
    ```

### 3.4 Multi-Platform Client Adaptations (Windows & Android)

- **Windows Desktop (`.exe`):**
  - Multi-panel `QDockWidget` / `QSplitter` layout with full keyboard shortcut bindings (`Ctrl+S`, `Ctrl+Shift+B` to trigger build in terminal, `Ctrl+P` quick file open).
- **Android APK & Termux Environment:**
  - **Adaptive Layout & Touch Support:** Automatically enable `QScroller::grabGesture(view, QScroller::LeftMouseButtonGesture)` on the SFTP Tree View and Code Editor for smooth kinetic touch scrolling.
  - **Virtual Extra-Key Toolbar:** When running on Android or narrow screens, display a dockable strip above the virtual keyboard providing `ESC`, `TAB`, `CTRL`, `ALT`, `{`, `}`, `;`, and Arrow keys.
  - **Dual Android Execution Modes:**
    1. **Standalone `.apk`:** Built with Qt for Android; runs natively with zero external apps required.
    2. **Termux Runnable Binary (ELF):** Compiled inside Termux using clang and `qt6-qtbase`; launched directly from the Termux shell via `Termux:X11` (`DISPLAY=:0 ./LightSSH_IDE`) alongside local Termux tools (`git`, `openssh`, `rsync`).

---

## 4. Module & Project Structure

```text
light-ssh-ide/
├── CMakeLists.txt
├── cmake/
│   └── Dependencies.cmake           # FetchContent for QSourceHighlite / libvterm
├── 3rdparty/
│   └── QSourceHighlite/             # Vendored Waqar144/QSourceHighlite (3 .cpp/.h pairs)
│       ├── languagedata.h / .cpp
│       ├── qsourcehighliter.h / .cpp
│       └── qsourcehighliterthemes.h / .cpp
├── android/                         # AndroidManifest.xml & Gradle templates for .apk build
└── src/
    ├── main.cpp
    ├── core/
    │   ├── ssh_client.h / .cpp      # libssh2 session lifecycle & worker threads
    │   ├── sftp_manager.h / .cpp    # Async SFTP read/write & lazy readdir queue
    │   └── remote_file_info.h       # Remote entry metadata structs
    ├── ui/
    │   ├── main_window.h / .cpp     # Responsive Splitter / Tab layout (Desktop vs Mobile)
    │   ├── terminal_widget.h / .cpp # libvterm PTY widget + Mobile Extra-Key bar
    │   ├── tree_model.h / .cpp      # QAbstractItemModel (Lazy-loading SFTP tree)
    │   ├── tree_view.h / .cpp       # Remote file tree with touch kinetic scrolling
    │   └── code_editor.h / .cpp     # QPlainTextEdit + LineNumberArea + QSourceHighlite
    └── resources/
        └── resources.qrc            # Built-in dark stylesheet & SVG icons
```

---

## 5. Cross-Platform `CMakeLists.txt` (Windows, Android APK, Termux)

```cmake
cmake_minimum_required(VERSION 3.20)
project(LightSSH_IDE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 1. Qt Dependencies (Supports Qt6 with fallback to Qt5)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)

# 2. Fetch Waqar144/QSourceHighlite automatically (or use vendored 3rdparty/)
include(FetchContent)
FetchContent_Declare(
    QSourceHighlite
    GIT_REPOSITORY [https://github.com/Waqar144/QSourceHighlite.git](https://github.com/Waqar144/QSourceHighlite.git)
    GIT_TAG        master
)
FetchContent_MakeAvailable(QSourceHighlite)

set(QSOURCEHIGHLITE_SOURCES
    ${qsourcehighlite_SOURCE_DIR}/qsourcehighliter.cpp
    ${qsourcehighlite_SOURCE_DIR}/qsourcehighliterthemes.cpp
    ${qsourcehighlite_SOURCE_DIR}/languagedata.cpp
)

# 3. SSH & Terminal Dependencies (vcpkg on Windows, pkg-config on Termux/Linux, or FetchContent)
find_package(OpenSSL REQUIRED)
find_package(libssh2 CONFIG QUIET)
if(NOT libssh2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSSH2 REQUIRED IMPORTED_TARGET libssh2)
endif()

set(PROJECT_SOURCES
    src/main.cpp
    src/core/ssh_client.cpp
    src/core/sftp_manager.cpp
    src/ui/main_window.cpp
    src/ui/terminal_widget.cpp
    src/ui/tree_model.cpp
    src/ui/tree_view.cpp
    src/ui/code_editor.cpp
    ${QSOURCEHIGHLITE_SOURCES}
)

# 4. Target Definition (Handles Windows .exe, Android .apk, and Termux ELF executable)
if(ANDROID)
    # Native Android APK build via qt-cmake
    qt_add_executable(${PROJECT_NAME} MANUAL_FINALIZATION ${PROJECT_SOURCES})
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
else()
    # Windows (.exe) or Termux / Linux runnable binary
    if(WIN32)
        add_executable(${PROJECT_NAME} WIN32 ${PROJECT_SOURCES})
    else()
        add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    src/
    ${qsourcehighlite_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    OpenSSL::SSL
    OpenSSL::Crypto
    $<IF:$<TARGET_EXISTS:libssh2::libssh2>,libssh2::libssh2,PkgConfig::LIBSSH2>
)

if(ANDROID)
    qt_finalize_executable(${PROJECT_NAME})
endif()

# 5. Platform Compiler Flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2)
endif()
```

---

## 6. Build & Deployment Guide Across Target Platforms

### 6.1 Windows (Native `.exe`)
```powershell
# Configure and compile
cmake -B build
cmake --build build --config Release
```

### 6.2 Android Option A: Termux Runnable Binary (Fastest On-Device Build)
Runs directly on an Android phone/tablet inside Termux with `Termux:X11`, requiring zero Android SDK setup:
```bash
# Inside Termux shell on Android
pkg update
pkg install x11-repo
pkg install clang cmake ninja pkg-config git libssh2 openssl qt6-qtbase termux-x11-nightly

# Clone and compile directly into a Termux runnable ELF object/binary
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Launch with Termux:X11 display server
termux-x11 :0 &
env DISPLAY=:0 ./build/LightSSH_IDE
```

### 6.3 Android Option B: Standalone `.apk` Package
Cross-compiled on a host machine using the Qt for Android SDK and Android NDK:
```bash
$QT_HOST_PATH/bin/qt-cmake -B build-android \
    -DQT_ANDROID_BUILD_ALL_ABIS=OFF \
    -DQT_ANDROID_ABIS="arm64-v8a" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build-android --target apk
# Output APK: build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
```

---

## 7. Development Milestones

```text
[Phase 1: SSH & Terminal] ---> [Phase 2: SFTP Lazy Tree] ---> [Phase 3: QSourceHighlite Editor] ---> [Phase 4: Windows & Android Polish]
- Async libssh2 handshake      - QAbstractItemModel root      - Local temp cache sync (Ctrl+S)       - Windows .exe & Termux ELF builds
- PTY channel & libvterm       - On-demand child expansion    - Waqar144/QSourceHighlite integration - Android APK & Touch Extra-Key bar
- Remote gcc/make execution    - Non-blocking SFTP worker     - Line numbers & Monokai dark theme    - Keep-alive & Pi 3 SFTP throttling
```

1. **Phase 1: Core Connectivity & Terminal Execution**
   - Implement `ssh_client` worker thread and `terminal_widget` backed by `libvterm`.
   - Validate connection to an older Linux target (`ssh pi@192.168.x.x`), interactive shell I/O, and remote compilation (`gcc`, `make`).
2. **Phase 2: SFTP Virtual File Tree & Lazy Loading**
   - Implement `tree_model` with dummy child nodes and asynchronous SFTP `readdir` expansion.
   - Verify zero GUI frame drops when expanding directories over simulated high-latency links.
3. **Phase 3: Editor Workspace & `QSourceHighlite` Integration**
   - Implement `code_editor` (`QPlainTextEdit` with custom line-number gutter) and integrate `Waqar144/QSourceHighlite`.
   - Wire double-click file download to `QStandardPaths::CacheLocation` and `Ctrl+S` auto-upload via SFTP.
4. **Phase 4: Multi-Platform Packaging & Legacy Server Optimizations**
   - Validate builds across Windows (MSVC/MinGW), Android Termux (`clang` + `qt6-qtbase`), and Android `.apk` (`qt-cmake`).
   - Add SSH keep-alive heartbeats and throttle concurrent SFTP transfers (max 2 parallel streams) to protect low-memory Raspberry Pi / legacy Linux hosts.
