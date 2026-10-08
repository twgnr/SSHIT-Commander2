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

QR Code generator library by Project Nayuki (compiled into the executable)
    MIT License  ->  qrcodegen-MIT.txt
    Used to show the QR code when setting up two-factor authentication.
    Source code: https://www.nayuki.io/page/qr-code-generator-library

zlib (compiled into the executable; standard builds)
    zlib License  ->  zlib-License.txt
    Used for SSH compression.
    Source code: https://zlib.net
    Builds made with -DUSE_ZLIB_COMPRESSION=OFF contain no zlib.

OpenSSL 3 (compiled into the executable; standard builds)
    Apache License 2.0  ->  OpenSSL-Apache-2.0.txt
    Source code: https://www.openssl.org
    Builds made with -DUSE_OPENSSL_BACKEND=OFF use Windows CNG instead and
    contain no OpenSSL.
