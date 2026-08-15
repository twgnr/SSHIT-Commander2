// Vorschau-Panel: zeigt den Inhalt der markierten Datei schreibgeschuetzt an
// (Text oder Bild). Reine Anzeige — keine Bearbeitung.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QWidget>

class QPlainTextEdit;
class QLabel;
class QStackedWidget;

namespace ncssh::gui {

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(AsyncBridge *bridge, QWidget *parent = nullptr);

    // Zeigt die Datei an; leerer Pfad leert die Vorschau.
    void preview(core::FileSystemProvider *provider, const QString &path);
    void clearPreview();

private:
    AsyncBridge *m_bridge;
    BridgeTask *m_task = nullptr;
    QString m_currentPath;

    QLabel *m_title = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QLabel *m_image = nullptr;
};

} // namespace ncssh::gui
