# Stellt ein statisches OpenSSL bereit und setzt OPENSSL_ROOT_DIR, BEVOR libssh2
# via FetchContent konfiguriert wird (dessen find_package(OpenSSL) laeuft zur
# Konfigurationszeit). Nur aktiv bei -DUSE_OPENSSL_BACKEND=ON.
#
# Zwei Wege:
#   1. OPENSSL_ROOT_DIR ist gesetzt und enthaelt ein OpenSSL -> direkt nutzen.
#   2. sonst: OpenSSL aus Quellcode bauen (perl + nmake, no-asm). Einmalig pro
#      Build-Verzeichnis; das Ergebnis wird gecacht (kein Neubau bei Re-Configure).
#
# Voraussetzungen fuer Weg 2 (Windows/MSVC):
#   - Aufruf aus einer MSVC-Umgebung (vcvars), damit nmake/cl verfuegbar sind.
#   - ein vollstaendiges perl (Strawberry/ActiveState/Oracle) mit
#     Locale::Maketext::Simple — Git-Bash-perl reicht NICHT. Ueber -DOPENSSL_PERL
#     laesst sich ein bestimmtes perl erzwingen.

set(OPENSSL_SOURCE_VERSION "3.3.2" CACHE STRING "OpenSSL-Version fuer den Quellcode-Bau")

# Sucht ein perl, das die von OpenSSLs Configure benoetigten Module hat.
function(_find_working_perl out_var)
    set(_candidates "")
    if(OPENSSL_PERL)
        list(APPEND _candidates "${OPENSSL_PERL}")
    endif()
    # Oracle-DB und typische Windows-perl-Installationen bevorzugen.
    file(GLOB _oracle_perls "C:/app/*/product/*/dbhome*/perl/bin/perl.exe")
    list(APPEND _candidates
        ${_oracle_perls}
        "C:/Strawberry/perl/bin/perl.exe"
        "C:/Perl64/bin/perl.exe"
        "perl")
    foreach(_p IN LISTS _candidates)
        execute_process(
            COMMAND "${_p}" -MLocale::Maketext::Simple -e "1"
            RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(${out_var} "${_p}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(provide_openssl)
    # Weg 1: bereits vorhandenes OpenSSL.
    if(OPENSSL_ROOT_DIR AND EXISTS "${OPENSSL_ROOT_DIR}/include/openssl/opensslv.h")
        message(STATUS "OpenSSL-Backend: nutze vorhandenes OpenSSL in ${OPENSSL_ROOT_DIR}")
        return()
    endif()

    # Weg 2: aus Quellcode bauen (gecacht pro Build-Verzeichnis).
    set(_root "${CMAKE_BINARY_DIR}/openssl")
    set(_src "${_root}/src")
    set(_install "${_root}/install")
    if(EXISTS "${_install}/lib/libcrypto.lib")
        message(STATUS "OpenSSL-Backend: bereits gebaut in ${_install}")
        set(OPENSSL_ROOT_DIR "${_install}" CACHE PATH "OpenSSL-Wurzel" FORCE)
        return()
    endif()

    _find_working_perl(_perl)
    if(NOT _perl)
        message(FATAL_ERROR
            "USE_OPENSSL_BACKEND: kein geeignetes perl gefunden. OpenSSLs Configure "
            "braucht ein vollstaendiges perl (Strawberry/ActiveState/Oracle) mit "
            "Locale::Maketext::Simple. Setze -DOPENSSL_PERL=<pfad/perl.exe> oder "
            "-DOPENSSL_ROOT_DIR=<vorgebautes OpenSSL>.")
    endif()
    message(STATUS "OpenSSL-Backend: baue OpenSSL ${OPENSSL_SOURCE_VERSION} mit ${_perl}")

    # Quellcode holen (tag-gepinnt, flach).
    if(NOT EXISTS "${_src}/Configure")
        file(MAKE_DIRECTORY "${_root}")
        execute_process(
            COMMAND git clone --depth 1 --branch "openssl-${OPENSSL_SOURCE_VERSION}"
                    https://github.com/openssl/openssl.git "${_src}"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "OpenSSL-Quellcode konnte nicht geklont werden.")
        endif()
    endif()

    # Configure (statisch, ohne ASM/Tests/Docs/Apps) — no-asm ersetzt nasm.
    execute_process(
        COMMAND "${_perl}" Configure VC-WIN64A no-asm no-shared no-tests no-docs no-apps
                "--prefix=${_install}" "--openssldir=${_install}/ssl"
        WORKING_DIRECTORY "${_src}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "OpenSSL Configure fehlgeschlagen (perl/MSVC-Umgebung?).")
    endif()

    # Bauen + Header/Libs installieren (nmake aus der MSVC-Umgebung).
    execute_process(COMMAND nmake WORKING_DIRECTORY "${_src}" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "OpenSSL-Build (nmake) fehlgeschlagen — MSVC-Umgebung aktiv?")
    endif()
    execute_process(COMMAND nmake install_dev WORKING_DIRECTORY "${_src}" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "OpenSSL install_dev fehlgeschlagen.")
    endif()

    set(OPENSSL_ROOT_DIR "${_install}" CACHE PATH "OpenSSL-Wurzel" FORCE)
    message(STATUS "OpenSSL-Backend: fertig, OPENSSL_ROOT_DIR=${_install}")
endfunction()
