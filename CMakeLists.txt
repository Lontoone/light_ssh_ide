cmake_minimum_required(VERSION 3.24)
project(LightSSH_IDE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# 1. 自動下載與設定 Qt 6.10.0 (僅限 Windows)
# ---------------------------------------------------------------------------
if(WIN32)
    set(QT_DIR_RELATIVE "third-party/qt")
    get_filename_component(QT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${QT_DIR_RELATIVE}" ABSOLUTE)
    set(QT_MSVC_PATH "${QT_ROOT_DIR}/6.10.0/msvc2022_64")

    if(NOT EXISTS "${QT_MSVC_PATH}/bin/qmake.exe")
        message(STATUS "Qt not found. Downloading Qt 6.10.0 via aqtinstall...")
        file(MAKE_DIRECTORY "${QT_ROOT_DIR}")

        execute_process(
            COMMAND aqt install-qt windows desktop 6.10.0 win64_msvc2022_64 
                    --base https://qt-mirror.dannhauer.de/ 
                    --outputdir "${QT_ROOT_DIR}"
            WORKING_DIRECTORY "${QT_ROOT_DIR}"
            RESULT_VARIABLE AQT_RESULT
        )

        if(NOT AQT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download Qt using aqt. Return code: ${AQT_RESULT}")
        endif()
    else()
        message(STATUS "Found existing Qt at: ${QT_MSVC_PATH}")
    endif()

    list(APPEND CMAKE_PREFIX_PATH "${QT_MSVC_PATH}")
    cmake_path(CONVERT "${QT_MSVC_PATH}/bin" TO_NATIVE_PATH_LIST QT_BIN_NATIVE)
    set(ENV{PATH} "${QT_BIN_NATIVE};$ENV{PATH}")
endif()

# ---------------------------------------------------------------------------
# 2. Qt 模組搜尋與設定
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Xml Network)

# ==============================================================================
# 3. 加密後端設定 (Windows 優先使用系統原生 WinCNG)
# ==============================================================================
if(WIN32)
    message(STATUS "Using native Windows CNG as libssh2 crypto backend.")
    set(CRYPTO_TYPE "WinCNG")
    set(CRYPTO_TARGET "")
else()
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "Found system OpenSSL.")
        set(CRYPTO_TYPE "OpenSSL")
        set(CRYPTO_TARGET OpenSSL::SSL OpenSSL::Crypto)
    else()
        message(FATAL_ERROR "OpenSSL is required on non-Windows platforms.")
    endif()
endif()

# ==============================================================================
# 4. libssh2 偵測與自動下載
# ==============================================================================
find_package(libssh2 QUIET CONFIG)

if(TARGET libssh2::libssh2 OR TARGET libssh2::libssh2_shared OR TARGET libssh2::libssh2_static)
    message(STATUS "Found installed libssh2.")
    if(TARGET libssh2::libssh2_static)
        set(LIBSSH2_TARGET libssh2::libssh2_static)
    else()
        set(LIBSSH2_TARGET libssh2::libssh2)
    endif()
else()
    message(STATUS "libssh2 not found. Downloading via FetchContent...")

    set(CRYPTO_BACKEND "${CRYPTO_TYPE}" CACHE STRING "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    set(ENABLE_INSTALL_DOC OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXPORT OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        libssh2
        GIT_REPOSITORY https://github.com/libssh2/libssh2.git
        GIT_TAG        libssh2-1.11.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(libssh2)
    set(LIBSSH2_TARGET libssh2_static)
endif()

# ==============================================================================
# 5. 語法高亮庫 (KSyntaxHighlighting) 整合 (使用本地 Git Submodule)
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
    QSourceHighlite
    GIT_REPOSITORY https://github.com/Waqar144/QSourceHighlite.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(QSourceHighlite)

set(QSOURCEHIGHLITE_SOURCES
    ${qsourcehighlite_SOURCE_DIR}/qsourcehighliter.cpp
    ${qsourcehighlite_SOURCE_DIR}/qsourcehighliterthemes.cpp
    ${qsourcehighlite_SOURCE_DIR}/languagedata.cpp
)


# ==============================================================================
# 6. 專案執行檔與原始碼配置
# ==============================================================================
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)

if(NOT SOURCES)
    set(SOURCES 
    src/main.cpp
    ${QSOURCEHIGHLITE_SOURCES}
    )
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${qsourcehighlite_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Xml
    Qt6::Network
    ${LIBSSH2_TARGET}
    ${CRYPTO_TARGET}
    ${HIGHLIGHTING_TARGET}
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# 部署 Windows 執行檔所需的 Qt DLL
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND Qt6::windeployqt $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Running windeployqt to copy required DLLs..."
    )
endif()