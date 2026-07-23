// Prueft, dass der libssh2-Build die modernen Algorithmen anbietet, die viele
// Server erwarten. Ohne ECDSA/ECDH im WinCNG-Backend scheitert der Handshake
// mit "Unable to exchange encryption keys". Dieser Test ist der Waechter dafuer.
#include "tests/harness.hpp"

#include <libssh2.h>

#include <cstring>

namespace {

// Liefert true, wenn libssh2 fuer die Methode den Algorithmus anbietet.
bool offers(LIBSSH2_SESSION *s, int methodType, const char *name)
{
    const char **algs = nullptr;
    const int n = libssh2_session_supported_algs(s, methodType, &algs);
    bool found = false;
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(algs[i], name) == 0) {
            found = true;
            break;
        }
    }
    if (algs)
        libssh2_free(s, algs);
    return found;
}

} // namespace

TEST(ssh_algs, ecdh_and_ecdsa_are_offered)
{
    CHECK_EQ(libssh2_init(0), 0);
    LIBSSH2_SESSION *s = libssh2_session_init();
    CHECK(s != nullptr);
    if (!s) {
        libssh2_exit();
        return;
    }

    // KEX: moderne Server bieten haeufig nur ecdh/curve25519 an. ecdh-nistp256
    // muss vorhanden sein, sonst schlaegt der Schluesseltausch fehl.
    CHECK(offers(s, LIBSSH2_METHOD_KEX, "ecdh-sha2-nistp256"));
    // Hostkeys: ed25519-only-Server bleiben ein Problem, aber ecdsa und
    // rsa-sha2 muessen zumindest verfuegbar sein.
    CHECK(offers(s, LIBSSH2_METHOD_HOSTKEY, "ecdsa-sha2-nistp256"));
    CHECK(offers(s, LIBSSH2_METHOD_HOSTKEY, "rsa-sha2-512"));
    // Klassischer Fallback muss ebenfalls noch da sein.
    CHECK(offers(s, LIBSSH2_METHOD_KEX, "diffie-hellman-group14-sha256"));

    libssh2_session_free(s);
    libssh2_exit();
}
