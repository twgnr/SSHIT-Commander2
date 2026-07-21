cmake_minimum_required(VERSION 3.24)
project(sshit_commander2 VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# C++-Port von SSHIT-Commander (Python/PySide6 -> C++/Qt6).
# Build:  cmake --preset default && cmake --build --preset default
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.8.2/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Svg)

# --- libssh2 (statisch, WinCNG-Crypto: kein OpenSSL noetig) ----------------
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
set(ENABLE_ZLIB_COMPRESSION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libssh2
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG        libssh2-1.11.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libssh2)

# --- Quellen ---------------------------------------------------------------
file(GLOB_RECURSE NCSSH_SOURCES CONFIGURE_DEPENDS
    src/*.cpp src/*.hpp)

qt_add_executable(sshit-commander WIN32
    ${NCSSH_SOURCES}
    resources/resources.qrc)

target_include_directories(sshit-commander PRIVATE src)

target_link_libraries(sshit-commander PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Svg
    libssh2::libssh2_static
    ws2_32 crypt32 bcrypt advapi32 iphlpapi ole32 shell32 user32)

if(MSVC)
    target_compile_options(sshit-commander PRIVATE /utf-8 /EHsc /W3 /Zc:__cplusplus /permissive-)
    target_compile_definitions(sshit-commander PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

# Windows-Icon
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/resources/app.rc")
    target_sources(sshit-commander PRIVATE resources/app.rc)
endif()

# Qt-DLLs neben die EXE deployen (Post-Build, nur wenn windeployqt existiert)
find_program(WINDEPLOYQT windeployqt HINTS "${CMAKE_PREFIX_PATH}/bin")
if(WINDEPLOYQT)
    add_custom_command(TARGET sshit-commander POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-quick-import
                "$<TARGET_FILE:sshit-commander>"
        COMMENT "windeployqt")
endif()
