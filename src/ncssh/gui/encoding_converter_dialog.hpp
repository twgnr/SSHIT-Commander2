// Datei-Encoding-Konverter: wandelt Textdateien zwischen Zeichensaetzen um
// (UTF-8, Windows-1252, EBCDIC u.v.m.), mit Auto-Erkennung, Vorschau,
// Fehlerstrategie und Ausgabe in eine neue Datei oder als Ueberschreiben.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>

class QComboBox;
class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;

namespace ncssh::gui {

class EncodingConverterDialog : public QDialog {
    Q_OBJECT
public:
    EncodingConverterDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                            const QString &path, QWidget *parent = nullptr);

private:
    void loadSource();
    void updatePreview();
    void convert();
    // Beschaedigten Text vom lokalen Modell rekonstruieren lassen.
    void repairWithAi();

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider;
    QString m_path;
    QByteArray m_raw;
    // Quelle war groesser als das Lese-Limit: dann darf NICHT geschrieben
    // werden — sonst kuerzt das "Konvertieren" die Datei stillschweigend.
    bool m_truncated = false;

    QComboBox *m_srcCodec = nullptr;
    QComboBox *m_dstCodec = nullptr;
    QComboBox *m_errorMode = nullptr;
    QCheckBox *m_overwrite = nullptr;
    QLineEdit *m_targetName = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPlainTextEdit *m_repairPreview = nullptr;
    QLabel *m_repairLabel = nullptr;
    QPushButton *m_repairButton = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
