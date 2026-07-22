// Editor mit Syntax-Highlighting, Zeilennummern, Suchen/Ersetzen, Gehe-zu-Zeile
// und Speichern (lokal & remote, Strg+S / Strg+Shift+S).
// (Port von gui/editor_dialog.py + code_editor.py)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QPlainTextEdit>

class QLineEdit;
class QLabel;
class QCheckBox;

namespace ncssh::gui {

class SyntaxHighlighter;
class Minimap;

// Editor-Flaeche mit Zeilennummern-Rand.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

    QWidget *m_lineNumberArea;
};

class EditorDialog : public QDialog {
    Q_OBJECT
public:
    EditorDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                 const QString &path, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void load();
    void save(bool saveAs);
    void findNext(bool backwards = false);
    void updateMatches();   // Suchtreffer in der Minimap markieren
    void replaceCurrent(bool all);
    void gotoLine();
    void explainWithAi();          // Datei/Config von der KI erklaeren lassen
    void codecheckWithAi();        // Quellcode auf Fehler pruefen lassen
    void openEncodingConverter();  // Zeichensatz der Datei umwandeln
    void updateTitle();

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider;
    QString m_path;
    bool m_dirty = false;
    bool m_readOnly = false;

    CodeEditor *m_editor = nullptr;
    SyntaxHighlighter *m_highlighter = nullptr;
    Minimap *m_minimap = nullptr;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
