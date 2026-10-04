// Zeichensatz-Konvertierung von Dateien — inkl. EBCDIC.
// Reine, testbare Logik: Quell-Bytes mit einem Codec dekodieren und mit einem
// anderen Codec wieder kodieren. Die GUI (Editor/Tools) nutzt convert().
#pragma once

#include <QByteArray>
#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core {

// (Anzeigename, Codec-Name). Gruppiert; EBCDIC-Varianten ausdruecklich dabei.
const std::vector<std::pair<QString, QString>> &encodingsList();

// Fehlerstrategien beim De-/Kodieren: strict | replace | ignore.
const std::vector<std::pair<QString, QString>> &errorModes();

// Dekodiert data mit srcCodec und kodiert es mit dstCodec.
// Bei errors="strict" wird bei ungueltigen Zeichen std::runtime_error geworfen;
// sonst gemaess errors ersetzt/ignoriert.
QByteArray convert(const QByteArray &data, const QString &srcCodec,
                   const QString &dstCodec, const QString &errors = QStringLiteral("strict"));

// Dekodiert (fehlertolerant) die ersten Bytes — fuer die Vorschau.
QString decodePreview(const QByteArray &data, const QString &codec, int limit = 2000);

// Grobe Quell-Encoding-Heuristik: BOM, UTF-16 ohne BOM (Null-Bytes an
// jeder zweiten Stelle, typisch fuer Windows-INI/-REG-Dateien), gueltiges
// UTF-8, sonst Windows-1252.
QString detectEncoding(const QByteArray &data);

// Text mit erkannter Kodierung dekodieren (fehlertolerant) — fuer Ansehen,
// Vorschau und readText() aller Dateisysteme.
QString decodeAuto(const QByteArray &data);

// Bausteine (auch einzeln nutzbar, z.B. im Editor):
QString decodeBytes(const QByteArray &data, const QString &codec,
                    const QString &errors = QStringLiteral("strict"));
QByteArray encodeText(const QString &text, const QString &codec,
                      const QString &errors = QStringLiteral("strict"));

} // namespace ncssh::core
