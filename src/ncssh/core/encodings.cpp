#include "ncssh/core/encodings.hpp"

#include <QHash>
#include <QStringDecoder>
#include <QStringEncoder>
#include <stdexcept>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace ncssh::core {

const std::vector<std::pair<QString, QString>> &encodingsList()
{
    static const std::vector<std::pair<QString, QString>> list = {
        // Unicode
        {QStringLiteral("UTF-8"), QStringLiteral("utf-8")},
        {QStringLiteral("UTF-8 (BOM)"), QStringLiteral("utf-8-sig")},
        {QStringLiteral("UTF-16"), QStringLiteral("utf-16")},
        {QStringLiteral("UTF-16 LE"), QStringLiteral("utf-16-le")},
        {QStringLiteral("UTF-16 BE"), QStringLiteral("utf-16-be")},
        {QStringLiteral("UTF-32"), QStringLiteral("utf-32")},
        // Westlich / Windows / ISO
        {QStringLiteral("Windows-1252 (Westlich)"), QStringLiteral("cp1252")},
        {QStringLiteral("Windows-1250 (Mitteleuropa)"), QStringLiteral("cp1250")},
        {QStringLiteral("ISO 8859-1 (Latin-1)"), QStringLiteral("latin-1")},
        {QStringLiteral("ISO 8859-15 (Latin-9)"), QStringLiteral("iso8859-15")},
        {QStringLiteral("ASCII"), QStringLiteral("ascii")},
        {QStringLiteral("Mac Roman"), QStringLiteral("mac-roman")},
        {QStringLiteral("DOS/OEM 850"), QStringLiteral("cp850")},
        {QStringLiteral("DOS/OEM 437"), QStringLiteral("cp437")},
        // Kyrillisch / weitere
        {QStringLiteral("KOI8-R"), QStringLiteral("koi8-r")},
        {QStringLiteral("Windows-1251 (Kyrillisch)"), QStringLiteral("cp1251")},
        // Ostasiatisch
        {QStringLiteral("Shift-JIS (Japanisch)"), QStringLiteral("shift_jis")},
        {QStringLiteral("GBK (Chinesisch)"), QStringLiteral("gbk")},
        {QStringLiteral("Big5 (Chinesisch)"), QStringLiteral("big5")},
        // EBCDIC (Grossrechner)
        {QStringLiteral("EBCDIC US/Kanada (cp037)"), QStringLiteral("cp037")},
        {QStringLiteral("EBCDIC International (cp500)"), QStringLiteral("cp500")},
        {QStringLiteral("EBCDIC Deutschland (cp273)"), QStringLiteral("cp273")},
        {QStringLiteral("EBCDIC US + Euro (cp1140)"), QStringLiteral("cp1140")},
        {QStringLiteral("EBCDIC Deutschland + Euro (cp1141)"), QStringLiteral("cp1141")},
        {QStringLiteral("EBCDIC Latin-1/Open (cp1047)"), QStringLiteral("cp1047")},
        {QStringLiteral("EBCDIC Griechisch (cp875)"), QStringLiteral("cp875")},
    };
    return list;
}

const std::vector<std::pair<QString, QString>> &errorModes()
{
    static const std::vector<std::pair<QString, QString>> list = {
        {QStringLiteral("Streng (Fehler melden)"), QStringLiteral("strict")},
        {QStringLiteral("Ersetzen (� / ?)"), QStringLiteral("replace")},
        {QStringLiteral("Ignorieren"), QStringLiteral("ignore")},
    };
    return list;
}

// Codec-Name -> Windows-Codepage (fuer alle Nicht-Unicode-Codecs).
static int codecToCodepage(const QString &codec)
{
    static const QHash<QString, int> map = {
        {QStringLiteral("cp1252"), 1252},  {QStringLiteral("cp1250"), 1250},
        {QStringLiteral("latin-1"), 28591}, {QStringLiteral("iso8859-15"), 28605},
        {QStringLiteral("ascii"), 20127},  {QStringLiteral("mac-roman"), 10000},
        {QStringLiteral("cp850"), 850},    {QStringLiteral("cp437"), 437},
        {QStringLiteral("koi8-r"), 20866}, {QStringLiteral("cp1251"), 1251},
        {QStringLiteral("shift_jis"), 932}, {QStringLiteral("gbk"), 936},
        {QStringLiteral("big5"), 950},
        {QStringLiteral("cp037"), 37},     {QStringLiteral("cp500"), 500},
        {QStringLiteral("cp273"), 20273},  {QStringLiteral("cp1140"), 1140},
        {QStringLiteral("cp1141"), 1141},  {QStringLiteral("cp1047"), 1047},
        {QStringLiteral("cp875"), 875},
    };
    return map.value(codec, -1);
}

static bool isUnicodeCodec(const QString &codec)
{
    return codec.startsWith(QLatin1String("utf-"));
}

static void throwCodec(const QString &msg)
{
    throw std::runtime_error(msg.toStdString());
}

// --- Unicode-Familie ueber QStringDecoder/-Encoder -------------------------

static QStringConverter::Encoding qtEncodingFor(const QString &codec)
{
    if (codec == QLatin1String("utf-8")) return QStringConverter::Utf8;
    if (codec == QLatin1String("utf-8-sig")) return QStringConverter::Utf8;
    if (codec == QLatin1String("utf-16")) return QStringConverter::Utf16;
    if (codec == QLatin1String("utf-16-le")) return QStringConverter::Utf16LE;
    if (codec == QLatin1String("utf-16-be")) return QStringConverter::Utf16BE;
    if (codec == QLatin1String("utf-32")) return QStringConverter::Utf32;
    throwCodec(QStringLiteral("Unbekannter Codec: %1").arg(codec));
    return QStringConverter::Utf8;  // unerreichbar
}

static QString decodeUnicode(const QByteArray &data, const QString &codec,
                             const QString &errors)
{
    QByteArray input = data;
    if (codec == QLatin1String("utf-8-sig") && input.startsWith("\xEF\xBB\xBF"))
        input.remove(0, 3);
    QStringDecoder dec(qtEncodingFor(codec));
    QString text = dec.decode(input);
    if (dec.hasError()) {
        if (errors == QLatin1String("strict"))
            throwCodec(QStringLiteral("Ungültige Bytes für Codec %1").arg(codec));
        if (errors == QLatin1String("ignore"))
            text.remove(QChar(0xFFFD));
    }
    return text;
}

static QByteArray encodeUnicode(const QString &text, const QString &codec,
                                const QString &errors)
{
    QStringEncoder enc(qtEncodingFor(codec),
                       codec == QLatin1String("utf-16") || codec == QLatin1String("utf-32")
                           ? QStringConverter::Flag::WriteBom
                           : QStringConverter::Flag::Default);
    QByteArray out = enc.encode(text);
    if (enc.hasError() && errors == QLatin1String("strict"))
        throwCodec(QStringLiteral("Text nicht darstellbar in Codec %1").arg(codec));
    if (codec == QLatin1String("utf-8-sig"))
        out.prepend("\xEF\xBB\xBF");
    return out;
}

// --- Codepage-Familie ueber WinAPI -----------------------------------------

#ifdef Q_OS_WIN

static QString decodeCodepage(const QByteArray &data, int cp, const QString &errors)
{
    if (data.isEmpty())
        return {};
    DWORD flags = (errors == QLatin1String("strict")) ? MB_ERR_INVALID_CHARS : 0;
    // Einige Codepages (z.B. EBCDIC) unterstuetzen MB_ERR_INVALID_CHARS nicht.
    int len = MultiByteToWideChar(cp, flags, data.constData(), data.size(), nullptr, 0);
    if (len <= 0 && flags) {
        if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
            throwCodec(QStringLiteral("Ungültige Bytes für Codepage %1").arg(cp));
        len = MultiByteToWideChar(cp, 0, data.constData(), data.size(), nullptr, 0);
        flags = 0;
    }
    if (len <= 0)
        throwCodec(QStringLiteral("Codepage %1 nicht verfügbar").arg(cp));
    QString out(len, QChar(0));
    MultiByteToWideChar(cp, flags, data.constData(), data.size(),
                        reinterpret_cast<wchar_t *>(out.data()), len);
    if (errors == QLatin1String("ignore"))
        out.remove(QChar(0xFFFD));
    return out;
}

static QByteArray encodeCodepage(const QString &text, int cp, const QString &errors)
{
    if (text.isEmpty())
        return {};
    const auto *wide = reinterpret_cast<const wchar_t *>(text.utf16());
    BOOL usedDefault = FALSE;
    // UTF-ähnliche Codepages erlauben keine defaultChar-Parameter.
    const bool noDefault = (cp == 65001 || cp == 42);
    // "ignore": unkodierbare Zeichen mit SUB (0x1A) ersetzen und danach entfernen.
    const char subChar = 0x1A;
    const char defChar = (errors == QLatin1String("ignore")) ? subChar : '?';
    int len = WideCharToMultiByte(cp, 0, wide, text.size(), nullptr, 0,
                                  noDefault ? nullptr : &defChar,
                                  noDefault ? nullptr : &usedDefault);
    if (len <= 0)
        throwCodec(QStringLiteral("Codepage %1 nicht verfügbar").arg(cp));
    QByteArray out(len, 0);
    usedDefault = FALSE;
    WideCharToMultiByte(cp, 0, wide, text.size(), out.data(), len,
                        noDefault ? nullptr : &defChar,
                        noDefault ? nullptr : &usedDefault);
    if (usedDefault) {
        if (errors == QLatin1String("strict"))
            throwCodec(QStringLiteral("Text nicht darstellbar in Codepage %1").arg(cp));
        if (errors == QLatin1String("ignore"))
            out.replace(subChar, QByteArray());
    }
    return out;
}

#endif // Q_OS_WIN

// --- Oeffentliche API -------------------------------------------------------

QString decodeBytes(const QByteArray &data, const QString &codec, const QString &errors)
{
    if (isUnicodeCodec(codec))
        return decodeUnicode(data, codec, errors);
#ifdef Q_OS_WIN
    const int cp = codecToCodepage(codec);
    if (cp < 0)
        throwCodec(QStringLiteral("Unbekannter Codec: %1").arg(codec));
    return decodeCodepage(data, cp, errors);
#else
    throwCodec(QStringLiteral("Codec %1 auf dieser Plattform nicht verfügbar").arg(codec));
    return {};
#endif
}

QByteArray encodeText(const QString &text, const QString &codec, const QString &errors)
{
    if (isUnicodeCodec(codec))
        return encodeUnicode(text, codec, errors);
#ifdef Q_OS_WIN
    const int cp = codecToCodepage(codec);
    if (cp < 0)
        throwCodec(QStringLiteral("Unbekannter Codec: %1").arg(codec));
    return encodeCodepage(text, cp, errors);
#else
    throwCodec(QStringLiteral("Codec %1 auf dieser Plattform nicht verfügbar").arg(codec));
    return {};
#endif
}

QByteArray convert(const QByteArray &data, const QString &srcCodec,
                   const QString &dstCodec, const QString &errors)
{
    return encodeText(decodeBytes(data, srcCodec, errors), dstCodec, errors);
}

QString decodePreview(const QByteArray &data, const QString &codec, int limit)
{
    return decodeBytes(data.left(limit), codec, QStringLiteral("replace"));
}

QString detectEncoding(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QStringLiteral("utf-8-sig");
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF"))
        return QStringLiteral("utf-16");
    QStringDecoder dec(QStringConverter::Utf8);
    dec.decode(data);
    if (!dec.hasError())
        return QStringLiteral("utf-8");
    return QStringLiteral("cp1252");
}

} // namespace ncssh::core
