// Host-Key-Bestaetigung (Trust-on-First-Use): zeigt Host, Algorithmus und
// Fingerprint eines unbekannten Servers — VOR der Anmeldung — und fragt, ob
// ihm dauerhaft, nur diesmal oder gar nicht vertraut werden soll.
#pragma once

#include <QDialog>

namespace ncssh::gui {

class HostKeyDialog : public QDialog {
    Q_OBJECT
public:
    enum class Decision { Cancel, Once, Trust };

    HostKeyDialog(const QString &host, int port, const QString &algorithm,
                  const QString &fingerprint, QWidget *parent = nullptr);

    Decision decision() const { return m_decision; }

    // One-Liner: zeigt den Dialog und liefert die Entscheidung.
    static Decision askUnknown(const QString &host, int port, const QString &algorithm,
                               const QString &fingerprint, QWidget *parent = nullptr);

    // Warnung bei GEAENDERTEM Host-Key: stellt erwarteten und erhaltenen
    // Fingerprint gegenueber. true = der Nutzer will trotzdem vertrauen (der
    // alte Pin wird dann ersetzt). Vorgabe ist Abbrechen.
    static bool askChanged(const QString &host, int port, const QString &algorithm,
                           const QString &expected, const QString &received,
                           QWidget *parent = nullptr);

private:
    Decision m_decision = Decision::Cancel;
};

} // namespace ncssh::gui
