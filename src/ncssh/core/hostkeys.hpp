// Bekannte Host-Keys (Trust-on-First-Use).  (Port von core/hostkeys.py)
//
// Speichert Fingerprints pro "host:port|algorithmus". Das Pinning PRO KEY-TYP
// verhindert Fehlalarme, wenn ein Server mehrere Host-Keys anbietet (z.B.
// ed25519 + rsa) und ueber Verbindungen hinweg ein anderer Typ ausgehandelt
// wird — ein solcher Wechsel gilt als UNBEKANNT (erneute Nachfrage), nicht
// als GEAENDERT (MITM-Warnung).
//
// Altbestand im Format "host:port" (ohne Algorithmus) wird weiter gelesen:
// stimmt ein Fingerprint damit ueberein, gilt der Host als bekannt.
#pragma once

#include <QHash>
#include <QString>
#include <optional>

namespace ncssh::core {

class HostKeyStore {
public:
    HostKeyStore();

    void load();
    void save() const;

    // Gespeicherter Fingerprint fuer (host, port[, algo]). Exakte Suche.
    std::optional<QString> get(const QString &host, int port, const QString &algo = {}) const;

    // Fingerprint aus einem Alt-Eintrag ohne Algorithmus (Migration).
    std::optional<QString> getLegacy(const QString &host, int port) const;

    void add(const QString &host, int port, const QString &fingerprint,
             const QString &algo = {});
    void remove(const QString &host, int port, const QString &algo = {});

    // Entfernt einen Eintrag anhand seines exakten Speicher-Schluessels.
    void removeKey(const QString &rawKey);

private:
    static QString key(const QString &host, int port, const QString &algo = {});

    QHash<QString, QString> m_data;
};

} // namespace ncssh::core
