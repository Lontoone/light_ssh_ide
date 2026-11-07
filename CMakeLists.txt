cmake_minimum_required(VERSION 3.24)
project(LightSSH_IDE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# 1. 自動下載與設定 Qt 6.8.1 (僅限 Windows)
# ---------------------------------------------------------------------------
if(WIN32)
    set(QT_DIR_RELATIVE "third-party/qt")
    get_filename_component(QT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${QT_DIR_RELATIVE}" ABSOLUTE)
    set(QT_MSVC_PATH "${QT_ROOT_DIR}/6.8.1/msvc2022_64")

    if(NOT EXISTS "${QT_MSVC_PATH}/bin/qmake.exe")
        message(STATUS "Qt not found. Downloading Qt 6.8.1 via aqtinstall...")
        file(MAKE_DIRECTORY "${QT_ROOT_DIR}")

        execute_process(
            COMMAND aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 
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
endif()

list(APPEND CMAKE_PREFIX_PATH "${QT_MSVC_PATH}")
    # 確保子程序在執行時能找到 qtpaths.exe 與 Qt 工具鏈
    cmake_path(CONVERT "${QT_MSVC_PATH}/bin" TO_NATIVE_PATH_LIST QT_BIN_NATIVE)
    set(ENV{PATH} "${QT_BIN_NATIVE};$ENV{PATH}")
# ---------------------------------------------------------------------------
# 2. Qt 模組搜尋與設定
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

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

    # 指定加密後端為 WinCNG (Windows) 或 OpenSSL
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
# 5. 語法高亮庫 (KSyntaxHighlighting) 外部編譯與整合
# ==============================================================================
find_package(KF6SyntaxHighlighting QUIET)

if(TARGET KF6::SyntaxHighlighting)
    message(STATUS "Found installed KF6SyntaxHighlighting.")
    set(HIGHLIGHTING_TARGET KF6::SyntaxHighlighting)
else()
    message(STATUS "Building syntax-highlighting and dependencies via external build...")

    set(REQUIRED_QT_VERSION 6.8.0)
    include(ExternalProject)
    set(THIRD_PARTY_INSTALL_DIR "${CMAKE_BINARY_DIR}/third_party_install")

    ExternalProject_Add(
        build_ecm
        GIT_REPOSITORY      https://invent.kde.org/frameworks/extra-cmake-modules.git
        GIT_TAG             v6.8.0
        GIT_SHALLOW         TRUE
        PREFIX              "${CMAKE_BINARY_DIR}/ecm"
        CMAKE_ARGS          
            -DCMAKE_INSTALL_PREFIX=${THIRD_PARTY_INSTALL_DIR}
            -DCMAKE_PREFIX_PATH=${QT_MSVC_PATH}
            -DQT_INSTALL_PREFIX=${QT_MSVC_PATH}
            -DBUILD_TESTING=OFF
            -DQT_MAJOR_VERSION=6
        BUILD_ALWAYS        OFF
    )

    # 2. 依照指令建置 syntax-highlighting，並注入 ECM 與 Qt 前綴
    ExternalProject_Add(
        build_syntax_highlighting
        DEPENDS             build_ecm
        GIT_REPOSITORY      https://invent.kde.org/frameworks/syntax-highlighting.git
        GIT_TAG             v6.8.0
        GIT_SHALLOW         TRUE
        PREFIX              "${CMAKE_BINARY_DIR}/syntax-highlighting"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${THIRD_PARTY_INSTALL_DIR}
            -DQT_INSTALL_PREFIX=${QT_MSVC_PATH}
            -DBUILD_TESTING=OFF
            -DBUILD_QCH=OFF
            -DQT_MAJOR_VERSION=6
        CMAKE_CACHE_ARGS
            -DCMAKE_PREFIX_PATH:PATH=${THIRD_PARTY_INSTALL_DIR};${QT_MSVC_PATH}
        BUILD_ALWAYS        OFF
    )

    # 建立 Imported Target 給主專案使用
    file(MAKE_DIRECTORY "${THIRD_PARTY_INSTALL_DIR}/include/KF6/KSyntaxHighlighting")
    
    add_library(KF6SyntaxHighlighting_Imported STATIC IMPORTED GLOBAL)
    add_dependencies(KF6SyntaxHighlighting_Imported build_syntax_highlighting)

    set_target_properties(KF6SyntaxHighlighting_Imported PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_INSTALL_DIR}/include/KF6/KSyntaxHighlighting;${THIRD_PARTY_INSTALL_DIR}/include/KF6"
        IMPORTED_LOCATION "${THIRD_PARTY_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}KF6SyntaxHighlighting${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )

    set(HIGHLIGHTING_TARGET KF6SyntaxHighlighting_Imported)
endif()
# ==============================================================================
# 6. 專案執行檔與原始碼配置
# ==============================================================================
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)

if(NOT SOURCES)
    set(SOURCES src/main.cpp)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
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