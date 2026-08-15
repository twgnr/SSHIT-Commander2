// Host-Key-Bestaetigung (Trust-on-First-Use): zeigt Host, Algorithmus und
// Fingerprint und fragt, ob dem Server dauerhaft vertraut werden soll.
#pragma once

#include <QDialog>

namespace ncssh::gui {

class HostKeyDialog : public QDialog {
    Q_OBJECT
public:
    HostKeyDialog(const QString &host, int port, const QString &algorithm,
                  const QString &fingerprint, QWidget *parent = nullptr);

    // true = Fingerprint speichern (dauerhaft vertrauen).
    bool trustPermanently() const { return m_trust; }

    // One-Liner: zeigt den Dialog, true wenn gespeichert werden soll.
    static bool ask(const QString &host, int port, const QString &algorithm,
                    const QString &fingerprint, QWidget *parent = nullptr);

    // Warnung bei GEAENDERTEM Host-Key: stellt erwarteten und erhaltenen
    // Fingerprint gegenueber. true = der Nutzer will trotzdem vertrauen (der
    // alte Pin wird dann ersetzt). Vorgabe ist Abbrechen.
    static bool askChanged(const QString &host, int port, const QString &algorithm,
                           const QString &expected, const QString &received,
                           QWidget *parent = nullptr);

private:
    bool m_trust = false;
};

} // namespace ncssh::gui
