// Dialog zum Erzeugen von SSH-Schluesseln und zum Konvertieren zwischen
// OpenSSH und PuTTY (PPK).  (Port von gui/key_dialog.py)
#pragma once

#include "ncssh/gui/bridge.hpp"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;

namespace ncssh::gui {

class KeyDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyDialog(AsyncBridge *bridge, QWidget *parent = nullptr);

    // Pfad des zuletzt gespeicherten Privatschluessels (fuer das Server-Profil).
    QString savedKeyPath() const { return m_savedKeyPath; }

private:
    void generate();
    void saveKeys();
    void convertToPpk();
    void convertToOpenssh();

    AsyncBridge *m_bridge;
    QByteArray m_privateKey;
    QByteArray m_publicKey;
    QString m_savedKeyPath;

    QComboBox *m_keyType = nullptr;
    QLineEdit *m_comment = nullptr;
    QPlainTextEdit *m_publicView = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
