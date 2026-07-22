#include "ncssh/core/ppk.hpp"

#include <QByteArrayList>
#include <QCryptographicHash>
#include <QList>
#include <QMessageAuthenticationCode>
#include <stdexcept>

namespace ncssh::core {

static const QByteArray kAuthMagic = QByteArrayLiteral("openssh-key-v1\x00");

static void fail(const QString &msg)
{
    throw std::runtime_error(msg.toStdString());
}

bool isPpk(const QByteArray &data)
{
    QByteArray trimmed = data;
    while (!trimmed.isEmpty() && (trimmed.at(0) == ' ' || trimmed.at(0) == '\r'
                                  || trimmed.at(0) == '\n' || trimmed.at(0) == '\t'))
        trimmed.remove(0, 1);
    return trimmed.left(19) == QByteArrayLiteral("PuTTY-User-Key-File");
}

// --- SSH-Wire-Helfer -------------------------------------------------------

static quint32 be32(const QByteArray &buf, int off)
{
    return (quint8(buf[off]) << 24) | (quint8(buf[off + 1]) << 16)
           | (quint8(buf[off + 2]) << 8) | quint8(buf[off + 3]);
}

// Liest ein laengenpraefigiertes Feld ab off; setzt off hinter das Feld.
static QByteArray rd(const QByteArray &buf, int &off)
{
    if (off + 4 > buf.size())
        fail("PPK/Key beschaedigt (Laengenfeld).");
    const quint32 n = be32(buf, off);
    off += 4;
    if (off + int(n) > buf.size())
        fail("PPK/Key beschaedigt (Feldlaenge).");
    const QByteArray out = buf.mid(off, n);
    off += n;
    return out;
}

// Schreibt ein laengenpraefigiertes Feld.
static QByteArray ws(const QByteArray &s)
{
    QByteArray out;
    const quint32 n = s.size();
    out.append(char((n >> 24) & 0xFF));
    out.append(char((n >> 16) & 0xFF));
    out.append(char((n >> 8) & 0xFF));
    out.append(char(n & 0xFF));
    out.append(s);
    return out;
}

static QByteArray packU32(quint32 n)
{
    QByteArray out;
    out.append(char((n >> 24) & 0xFF));
    out.append(char((n >> 16) & 0xFF));
    out.append(char((n >> 8) & 0xFF));
    out.append(char(n & 0xFF));
    return out;
}

static QByteArray lstripZero(const QByteArray &b)
{
    int i = 0;
    while (i < b.size() && b.at(i) == '\x00')
        ++i;
    return b.mid(i);
}

static QByteArray rjust(const QByteArray &b, int width)
{
    if (b.size() >= width)
        return b;
    return QByteArray(width - b.size(), '\x00') + b;
}

// --- PPK parsen ------------------------------------------------------------

struct PpkInfo {
    QByteArray algo;
    QByteArray pub;
    QByteArray priv;
    QByteArray encryption = QByteArrayLiteral("none");
    QByteArray comment;
};

static PpkInfo parsePpk(const QByteArray &data)
{
    QByteArray norm = data;
    norm.replace("\r\n", "\n");
    const QList<QByteArray> lines = norm.split('\n');
    PpkInfo out;
    int i = 0;
    while (i < lines.size()) {
        const QByteArray &line = lines[i];
        const int colon = line.indexOf(':');
        if (colon < 0) {
            ++i;
            continue;
        }
        const QByteArray key = line.left(colon).trimmed();
        const QByteArray val = line.mid(colon + 1).trimmed();
        if (key.startsWith("PuTTY-User-Key-File")) {
            out.algo = val;
        } else if (key == "Public-Lines") {
            const int n = val.toInt();
            QByteArray joined;
            for (int k = i + 1; k <= i + n && k < lines.size(); ++k)
                joined += lines[k];
            out.pub = QByteArray::fromBase64(joined);
            i += n;
        } else if (key == "Private-Lines") {
            const int n = val.toInt();
            QByteArray joined;
            for (int k = i + 1; k <= i + n && k < lines.size(); ++k)
                joined += lines[k];
            out.priv = QByteArray::fromBase64(joined);
            i += n;
        } else if (key == "Encryption") {
            out.encryption = val;
        } else if (key == "Comment") {
            out.comment = val;
        }
        ++i;
    }
    return out;
}

static QByteArray perKey(const QByteArray &algo, const QByteArray &pub,
                         const QByteArray &priv, const QByteArray &comment)
{
    if (algo == "ssh-ed25519") {
        int off = 0;
        rd(pub, off);              // key type
        const QByteArray a = rd(pub, off);
        int po = 0;
        const QByteArray seedRaw = rd(priv, po);
        const QByteArray seed = rjust(lstripZero(seedRaw), 32);  // mpint -> 32 Byte
        return ws("ssh-ed25519") + ws(a) + ws(seed + a) + ws(comment);
    }
    if (algo == "ssh-rsa") {
        int off = 0;
        rd(pub, off);              // key type
        const QByteArray e = rd(pub, off);
        const QByteArray n = rd(pub, off);
        int po = 0;
        const QByteArray d = rd(priv, po);
        const QByteArray p = rd(priv, po);
        const QByteArray q = rd(priv, po);
        const QByteArray iqmp = rd(priv, po);
        return ws("ssh-rsa") + ws(n) + ws(e) + ws(d) + ws(iqmp) + ws(p) + ws(q) + ws(comment);
    }
    if (algo.startsWith("ecdsa-sha2-")) {
        int off = 0;
        rd(pub, off);              // key type
        const QByteArray curve = rd(pub, off);
        const QByteArray point = rd(pub, off);
        int po = 0;
        const QByteArray d = rd(priv, po);
        return ws(algo) + ws(curve) + ws(point) + ws(d) + ws(comment);
    }
    if (algo == "ssh-dss") {
        int off = 0;
        rd(pub, off);              // key type
        const QByteArray p = rd(pub, off);
        const QByteArray q = rd(pub, off);
        const QByteArray g = rd(pub, off);
        const QByteArray y = rd(pub, off);
        int po = 0;
        const QByteArray x = rd(priv, po);
        return ws("ssh-dss") + ws(p) + ws(q) + ws(g) + ws(y) + ws(x) + ws(comment);
    }
    fail(QStringLiteral("PPK-Key-Typ nicht unterstützt: %1").arg(QString::fromLatin1(algo)));
    return {};
}

QByteArray ppkToOpenssh(const QByteArray &data, const QString & /*passphrase*/)
{
    const PpkInfo info = parsePpk(data);
    if (info.encryption != "none")
        fail("Verschlüsselte PPK können nicht direkt gelesen werden. Bitte in "
             "PuTTYgen über 'Conversions → Export OpenSSH key' konvertieren.");
    const QByteArray pk = perKey(info.algo, info.pub, info.priv, info.comment);

    const QByteArray check = QByteArray(4, '\x00');
    QByteArray block = check + check + pk;
    int pad = 1;
    while (block.size() % 8 != 0)
        block.append(char(pad++));
    const QByteArray body = kAuthMagic + ws("none") + ws("none") + ws(QByteArray())
                            + packU32(1) + ws(info.pub) + ws(block);

    const QByteArray b64 = body.toBase64();
    QByteArray out = "-----BEGIN OPENSSH PRIVATE KEY-----\n";
    for (int i = 0; i < b64.size(); i += 70)
        out += b64.mid(i, 70) + "\n";
    out += "-----END OPENSSH PRIVATE KEY-----\n";
    return out;
}

// --- OpenSSH -> PPK (v2, unverschluesselt) ---------------------------------

static QByteArray mpint(const QByteArray &bIn)
{
    QByteArray b = lstripZero(bIn);
    if (b.isEmpty())
        b = QByteArray(1, '\x00');
    if (quint8(b.at(0)) & 0x80)
        b.prepend('\x00');
    return ws(b);
}

static void parseOpenssh(const QByteArray &data, QByteArray &pubOut, QByteArray &bodyOut)
{
    QByteArray norm = data;
    norm.replace("\r\n", "\n");
    norm = norm.trimmed();
    const QList<QByteArray> lines = norm.split('\n');
    if (lines.isEmpty() || !lines.first().startsWith("-----BEGIN OPENSSH PRIVATE KEY-----"))
        fail("Kein OpenSSH-Privatschlüssel (openssh-key-v1).");
    QByteArray joined;
    for (int i = 1; i < lines.size() - 1; ++i)
        joined += lines[i];
    const QByteArray blob = QByteArray::fromBase64(joined);
    if (!blob.startsWith(kAuthMagic))
        fail("Ungültiger OpenSSH-Schlüssel.");
    int off = kAuthMagic.size();
    const QByteArray cipher = rd(blob, off);
    rd(blob, off);  // kdf
    rd(blob, off);  // kdfopts
    if (cipher != "none")
        fail("Verschlüsselte Schlüssel bitte zuerst entschlüsseln.");
    off += 4;       // nkeys
    pubOut = rd(blob, off);
    const QByteArray privSection = rd(blob, off);
    bodyOut = privSection.mid(8);  // 2x 4-Byte Check-Int ueberspringen
}

static QByteArray puttyPrivate(const QByteArray &algo, const QByteArray &body)
{
    if (algo == "ssh-ed25519") {
        int o = 0;
        rd(body, o);               // key type
        rd(body, o);               // a
        const QByteArray keypair = rd(body, o);  // seed(32) + pub(32)
        return mpint(keypair.left(32));
    }
    if (algo == "ssh-rsa") {
        int o = 0;
        rd(body, o);               // key type
        rd(body, o);               // n
        rd(body, o);               // e
        const QByteArray d = rd(body, o);
        const QByteArray iqmp = rd(body, o);
        const QByteArray p = rd(body, o);
        const QByteArray q = rd(body, o);
        return ws(d) + ws(p) + ws(q) + ws(iqmp);
    }
    if (algo.startsWith("ecdsa-sha2-")) {
        int o = 0;
        rd(body, o);               // key type
        rd(body, o);               // curve
        rd(body, o);               // point
        const QByteArray d = rd(body, o);
        return ws(d);
    }
    if (algo == "ssh-dss") {
        int o = 0;
        rd(body, o);               // key type
        rd(body, o);               // p
        rd(body, o);               // q
        rd(body, o);               // g
        rd(body, o);               // y
        const QByteArray x = rd(body, o);
        return ws(x);
    }
    fail(QStringLiteral("Key-Typ nicht unterstützt: %1").arg(QString::fromLatin1(algo)));
    return {};
}

static QByteArray b64Lines(const QByteArray &blob, int &numLines)
{
    const QByteArray b64 = blob.toBase64();
    QByteArrayList lines;
    for (int i = 0; i < b64.size(); i += 64)
        lines.append(b64.mid(i, 64));
    numLines = lines.size();
    return lines.join('\n');
}

QByteArray opensshToPpk(const QByteArray &data, const QString &comment)
{
    QByteArray pub, body;
    parseOpenssh(data, pub, body);
    int ao = 0;
    const QByteArray algo = rd(pub, ao);
    const QByteArray privblob = puttyPrivate(algo, body);
    const QByteArray cbytes = comment.toUtf8();

    const QByteArray macData = ws(algo) + ws("none") + ws(cbytes) + ws(pub) + ws(privblob);
    const QByteArray macKey =
        QCryptographicHash::hash(QByteArrayLiteral("putty-private-key-file-mac-key"),
                                 QCryptographicHash::Sha1);
    const QByteArray mac =
        QMessageAuthenticationCode::hash(macData, macKey, QCryptographicHash::Sha1).toHex();

    int pubN = 0, prvN = 0;
    const QByteArray pubB64 = b64Lines(pub, pubN);
    const QByteArray prvB64 = b64Lines(privblob, prvN);
    QByteArrayList parts = {
        "PuTTY-User-Key-File-2: " + algo,
        "Encryption: none",
        "Comment: " + cbytes,
        "Public-Lines: " + QByteArray::number(pubN), pubB64,
        "Private-Lines: " + QByteArray::number(prvN), prvB64,
        "Private-MAC: " + mac,
    };
    return parts.join("\r\n") + "\r\n";
}

} // namespace ncssh::core
