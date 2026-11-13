cmake_minimum_required(VERSION 3.20)
project(LightSSH_IDE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt discovery: prefer Qt6, but allow Qt5 for compatibility with older toolchains.
if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
    set(QT_MAJOR_VERSION 6)
else()
    find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Network)
    if(Qt6_FOUND)
        set(QT_MAJOR_VERSION 6)
    else()
        find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)
        set(QT_MAJOR_VERSION 5)
    endif()
endif()

# libssh2 and crypto backend.
set(LIBSSH2_TARGET "")
set(CRYPTO_TARGET "")

if(WIN32)
    message(STATUS "Using native Windows CNG as libssh2 crypto backend.")
else()
    find_package(OpenSSL REQUIRED)
    set(CRYPTO_TARGET OpenSSL::SSL OpenSSL::Crypto)
endif()

find_package(libssh2 CONFIG QUIET)
if(TARGET libssh2::libssh2_static)
    set(LIBSSH2_TARGET libssh2::libssh2_static)
elseif(TARGET libssh2::libssh2)
    set(LIBSSH2_TARGET libssh2::libssh2)
elseif(TARGET libssh2::libssh2_shared)
    set(LIBSSH2_TARGET libssh2::libssh2_shared)
else()
    if(ANDROID)
        message(WARNING "libssh2 was not found for Android. Add libssh2 to the NDK toolchain or use a package manager before Android packaging.")
    else()
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(ENABLE_INSTALL_DOC OFF CACHE BOOL "" FORCE)
        set(ENABLE_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
        set(ENABLE_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            libssh2
            GIT_REPOSITORY https://github.com/libssh2/libssh2.git
            GIT_TAG        libssh2-1.11.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(libssh2)
        set(LIBSSH2_TARGET libssh2_static)
    endif()
endif()

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

if(NOT PROJECT_SOURCES)
    message(FATAL_ERROR "No project sources were found under src/. Add the required source files.")
endif()

# Android uses a Qt-specific executable target. Desktop and Termux use a standard app binary.
if(ANDROID)
    qt_add_executable(${PROJECT_NAME} MANUAL_FINALIZATION ${PROJECT_SOURCES})
    set_property(TARGET ${PROJECT_NAME} PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/android")
else()
    add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
    if(WIN32)
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(QT_MAJOR_VERSION EQUAL 6)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        ${LIBSSH2_TARGET}
        ${CRYPTO_TARGET}
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
        ${LIBSSH2_TARGET}
        ${CRYPTO_TARGET}
    )
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

if(WIN32 AND TARGET Qt${QT_MAJOR_VERSION}::windeployqt)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND Qt${QT_MAJOR_VERSION}::windeployqt $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Running windeployqt to copy required Qt DLLs..."
    )
endif()

if(ANDROID)
    qt_finalize_executable(${PROJECT_NAME})
endif()