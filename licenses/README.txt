Third-party licences
====================

SSHIT-Commander itself is licensed under the GNU General Public License v3
(see the LICENSE file in the program folder).

The following components are distributed alongside it and remain under their
own licences:

Qt 6 (Qt6*.dll and the plugin folders next to the executable)
    GNU Lesser General Public License v3  ->  Qt-LGPL-3.0.txt
    The LGPL v3 is an addendum to the GPL v3; the GPL text is in LICENSE.
    Qt is linked dynamically, so you may replace the shipped Qt libraries with
    your own compatible build.
    Source code: https://www.qt.io/download-open-source  and  https://code.qt.io

libssh2 (compiled into the executable)
    BSD 3-Clause  ->  libssh2-BSD-3-Clause.txt
    Source code: https://www.libssh2.org

OpenSSL 3 (only in builds made with -DUSE_OPENSSL_BACKEND=ON)
    Apache License 2.0
    Source code: https://www.openssl.org
    Standard builds use Windows CNG instead and contain no OpenSSL.
