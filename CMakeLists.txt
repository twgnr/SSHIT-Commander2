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

# --- libssh2 (statisch) ----------------------------------------------------
# Krypto-Backend: standardmaessig WinCNG (kein OpenSSL noetig, aber ohne
# ed25519/curve25519). Mit -DUSE_OPENSSL_BACKEND=ON wird stattdessen OpenSSL
# genutzt (ed25519-Host-/Client-Keys + curve25519-KEX). WinCNG bleibt die
# getestete Rueckfallebene.
option(USE_OPENSSL_BACKEND
    "libssh2 gegen OpenSSL statt WinCNG bauen (ed25519/curve25519)" OFF)

include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB_COMPRESSION OFF CACHE BOOL "" FORCE)

if(USE_OPENSSL_BACKEND)
    include(${CMAKE_SOURCE_DIR}/cmake/OpenSSLBackend.cmake)
    provide_openssl()  # setzt OPENSSL_ROOT_DIR (vorgebaut oder aus Quellcode)
    set(OPENSSL_USE_STATIC_LIBS ON)
    set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
    # OpenSSL::Crypto im Top-Scope verfuegbar machen, damit die App die
    # statischen OpenSSL-Objekte sicher mitlinkt (nicht nur transitiv ueber
    # libssh2). REQUIRED: fehlt OpenSSL, bricht die Konfiguration klar ab.
    find_package(OpenSSL REQUIRED)
else()
    set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
    # WinCNG kann ECDSA/ECDH — standardmaessig aber aus. Ohne das bietet der
    # Client NUR Diffie-Hellman-Gruppen (KEX) und RSA/DSS-Hostkeys an; moderne
    # Server, die ecdh-sha2-nistp*/ecdsa erwarten, scheitern dann mit "Unable to
    # exchange encryption keys". Aktiviert ecdh-sha2-nistp256/384/521 +
    # ecdsa-Hostkeys (benoetigt Windows 10 oder neuer).
    set(ENABLE_ECDSA_WINCNG ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(libssh2
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG        libssh2-1.11.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libssh2)

# --- Quellen ---------------------------------------------------------------
file(GLOB_RECURSE NCSSH_SOURCES CONFIGURE_DEPENDS
    src/*.cpp src/*.hpp)
# main.cpp gehoert nur zur App — die Tests bringen ihr eigenes main mit.
list(REMOVE_ITEM NCSSH_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

# Gemeinsame Bibliothek fuer App und Tests (spart doppelte Uebersetzung).
add_library(ncssh STATIC ${NCSSH_SOURCES})
target_include_directories(ncssh PUBLIC src "${CMAKE_SOURCE_DIR}")
target_link_libraries(ncssh PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Svg
    libssh2::libssh2_static
    ws2_32 crypt32 bcrypt advapi32 iphlpapi ole32 shell32 user32)
# Statisches OpenSSL braucht zusaetzlich diese System-Libs.
if(USE_OPENSSL_BACKEND)
    target_link_libraries(ncssh PUBLIC OpenSSL::Crypto)
endif()

qt_add_executable(sshit-commander WIN32
    src/main.cpp
    resources/resources.qrc)
target_link_libraries(sshit-commander PRIVATE ncssh)

if(MSVC)
    set(NCSSH_FLAGS /utf-8 /EHsc /W3 /Zc:__cplusplus /permissive-)
    set(NCSSH_DEFS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ncssh PUBLIC ${NCSSH_FLAGS})
    target_compile_definitions(ncssh PUBLIC ${NCSSH_DEFS})
endif()

# --- Tests (CTest) ---------------------------------------------------------
# Bauen mit:  cmake --build --preset default --target sshit-tests
# Ausfuehren: ctest --test-dir build --output-on-failure
enable_testing()
file(GLOB NCSSH_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.hpp)
if(NCSSH_TEST_SOURCES)
    # resources.qrc muss mit hinein: die i18n-Kataloge und Assets liegen dort,
    # sonst laufen die Uebersetzungs- und GUI-Tests gegen einen leeren Katalog.
    add_executable(sshit-tests ${NCSSH_TEST_SOURCES} resources/resources.qrc)
    target_link_libraries(sshit-tests PRIVATE ncssh)
    # Quellbaum-Pfad fuer Tests, die Dateien des Repos brauchen (i18n-Gate).
    target_compile_definitions(sshit-tests PRIVATE
        NCSSH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME sshit-tests COMMAND sshit-tests)
    # Ohne Bildschirm laufen lassen (CI/headless).
    set_tests_properties(sshit-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # windeployqt liefert nur qwindows mit — das Offscreen-Plugin daneben legen,
    # sonst bleibt Qt beim Start mit einer Fehler-MessageBox stehen.
    set(NCSSH_OFFSCREEN "${CMAKE_PREFIX_PATH}/plugins/platforms/qoffscreen.dll")
    if(EXISTS "${NCSSH_OFFSCREEN}")
        add_custom_command(TARGET sshit-tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NCSSH_OFFSCREEN}"
                    "$<TARGET_FILE_DIR:sshit-tests>/platforms/qoffscreen.dll"
            COMMENT "Offscreen-Plattform-Plugin fuer die Tests bereitstellen")
    endif()
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
