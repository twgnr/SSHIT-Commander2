#include "ncssh/gui/editor_dialog.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/ai_chat_panel.hpp"
#include "ncssh/gui/encoding_converter_dialog.hpp"
#include "ncssh/gui/highlighter.hpp"
#include "ncssh/gui/minimap.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QTextBlock>
#include <QToolBar>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

// Grossdatei-Schutz: darueber nur lesend oeffnen.
static constexpr qint64 kLargeFileLimit = 5 * 1024 * 1024;

// --- Zeilennummern-Rand -----------------------------------------------------

namespace {
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }
private:
    CodeEditor *m_editor;
};
} // namespace

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    m_lineNumberArea = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &CodeEditor::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(QColor(255, 255, 255, 14));
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        selections.append(sel);
    }
    setExtraSelections(selections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(QStringLiteral("#1e212b")));
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = int(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + int(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(QColor(QStringLiteral("#8b90a0")));
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + int(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// --- EditorDialog -----------------------------------------------------------

EditorDialog::EditorDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                           const QString &path, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_provider(provider), m_path(path)
{
    resize(940, 700);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *toolbar = new QToolBar(this);
    toolbar->addAction(_t("Speichern"), this, [this] { save(false); });
    toolbar->addAction(_t("Speichern unter …"), this, [this] { save(true); });
    toolbar->addSeparator();
    toolbar->addAction(_t("Gehe zu Zeile"), this, &EditorDialog::gotoLine);
    toolbar->addSeparator();
    // KI: Datei/Config untersuchen oder befragen (nur bei aktivierter KI).
    QAction *aiExplain = toolbar->addAction(_t("KI erklären"), this,
                                            &EditorDialog::explainWithAi);
    aiExplain->setToolTip(_t("Datei (oder Auswahl) vom lokalen Modell erklären lassen"));
    QAction *aiCheck = toolbar->addAction(_t("KI Fehleranalyse"), this,
                                          &EditorDialog::codecheckWithAi);
    aiCheck->setToolTip(_t("Quellcode vom lokalen Modell auf Fehler prüfen lassen"));
    toolbar->addSeparator();
    QAction *convert = toolbar->addAction(_t("Encoding konvertieren …"), this,
                                          &EditorDialog::openEncodingConverter);
    convert->setToolTip(_t("Datei in einen anderen Zeichensatz umwandeln (inkl. EBCDIC)"));
    layout->addWidget(toolbar);

    m_editor = new CodeEditor(this);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(core::getSettingInt(QStringLiteral("editor_font_size"), 11));
    m_editor->setFont(mono);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_dirty) {
            m_dirty = true;
            updateTitle();
        }
    });
    // Editor + Minimap nebeneinander
    auto *editorRow = new QHBoxLayout();
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(2);
    editorRow->addWidget(m_editor, 1);
    m_minimap = new Minimap(m_editor, this);
    editorRow->addWidget(m_minimap);
    layout->addLayout(editorRow, 1);

    // Suchen/Ersetzen-Leiste
    auto *findRow = new QHBoxLayout();
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(_t("Suchen …"));
    connect(m_find, &QLineEdit::returnPressed, this, [this] { findNext(); });
    // Treffer laufend in der Minimap markieren.
    connect(m_find, &QLineEdit::textChanged, this, &EditorDialog::updateMatches);
    m_replace = new QLineEdit(this);
    m_replace->setPlaceholderText(_t("Ersetzen durch …"));
    m_caseSensitive = new QCheckBox(_t("Aa"), this);
    m_caseSensitive->setToolTip(_t("Groß-/Kleinschreibung beachten"));
    auto *nextBtn = new QPushButton(QStringLiteral("▼"), this);
    auto *prevBtn = new QPushButton(QStringLiteral("▲"), this);
    auto *repBtn = new QPushButton(_t("Ersetzen"), this);
    auto *repAllBtn = new QPushButton(_t("Alle"), this);
    nextBtn->setFixedWidth(34);
    prevBtn->setFixedWidth(34);
    connect(nextBtn, &QPushButton::clicked, this, [this] { findNext(); });
    connect(prevBtn, &QPushButton::clicked, this, [this] { findNext(true); });
    connect(repBtn, &QPushButton::clicked, this, [this] { replaceCurrent(false); });
    connect(repAllBtn, &QPushButton::clicked, this, [this] { replaceCurrent(true); });
    findRow->addWidget(m_find, 2);
    findRow->addWidget(prevBtn);
    findRow->addWidget(nextBtn);
    findRow->addWidget(m_replace, 2);
    findRow->addWidget(m_caseSensitive);
    findRow->addWidget(repBtn);
    findRow->addWidget(repAllBtn);
    layout->addLayout(findRow);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    // Tastenkuerzel
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this), &QShortcut::activated,
            this, [this] { save(false); });
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")), this),
            &QShortcut::activated, this, [this] { save(true); });
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this), &QShortcut::activated,
            this, [this] { m_find->setFocus(); m_find->selectAll(); });
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")), this), &QShortcut::activated,
            this, [this] { gotoLine(); });

    // Syntax-Highlighting nach Dateiendung
    const QString language = SyntaxHighlighter::languageForFile(provider->basename(path));
    if (!language.isEmpty())
        m_highlighter = new SyntaxHighlighter(m_editor->document(), language);

    updateTitle();
    load();
}

void EditorDialog::updateTitle()
{
    const QString name = m_provider->basename(m_path);
    QString title = _t("Bearbeiten") + QStringLiteral(" — ") + name;
    if (m_readOnly)
        title += _t("  (schreibgeschützt)");
    else if (m_dirty)
        title += _t(" · geändert");
    setWindowTitle(title);
}

void EditorDialog::load()
{
    core::FileSystemProvider *provider = m_provider;
    const QString path = m_path;
    m_status->setText(_t("Lade …"));
    m_bridge->run<QString>(
        [provider, path] { return provider->readText(path, kLargeFileLimit + 1); },
        [this](const QString &text) {
            // Grossdatei-Schutz: nur lesend oeffnen.
            if (text.size() > kLargeFileLimit) {
                m_readOnly = true;
                m_editor->setReadOnly(true);
                m_status->setText(
                    _t("⚠ Datei zu groß — schreibgeschützt geöffnet (nur der Anfang wird "
                       "gezeigt)."));
            } else {
                m_status->setText(_t("%1 Zeichen  ·  %2 Zeilen")
                                      .arg(text.size())
                                      .arg(text.count(QLatin1Char('\n')) + 1));
            }
            m_editor->setPlainText(text);
            m_dirty = false;
            updateTitle();
        },
        [this](const QString &err) {
            m_status->setText(err);
            QMessageBox::warning(this, _t("Fehler"), err);
        });
}

void EditorDialog::save(bool saveAs)
{
    if (m_readOnly) {
        QMessageBox::information(this, _t("Schreibgeschützt"),
                                 _t("Die Datei wurde nur lesend geöffnet."));
        return;
    }
    QString target = m_path;
    if (saveAs) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, _t("Speichern unter"), _t("Neuer Dateiname:"), QLineEdit::Normal,
            m_provider->basename(m_path), &ok);
        if (!ok || name.isEmpty())
            return;
        target = m_provider->join(m_provider->parent(m_path), name);
    }
    core::FileSystemProvider *provider = m_provider;
    const QString content = m_editor->toPlainText();
    m_bridge->run(
        [provider, target, content] { provider->writeText(target, content); },
        [this, target] {
            m_path = target;
            m_dirty = false;
            updateTitle();
            m_status->setText(_t("Gespeichert."));
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void EditorDialog::updateMatches()
{
    const QString needle = m_find->text();
    std::vector<int> lines;
    if (!needle.isEmpty()) {
        const auto sensitivity = m_caseSensitive->isChecked() ? Qt::CaseSensitive
                                                              : Qt::CaseInsensitive;
        QTextBlock block = m_editor->document()->firstBlock();
        int lineNo = 0;
        while (block.isValid()) {
            if (block.text().contains(needle, sensitivity))
                lines.push_back(lineNo);
            block = block.next();
            ++lineNo;
        }
    }
    m_minimap->setMatches(lines);
}

void EditorDialog::findNext(bool backwards)
{
    const QString needle = m_find->text();
    if (needle.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (backwards)
        flags |= QTextDocument::FindBackward;
    if (m_caseSensitive->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (!m_editor->find(needle, flags)) {
        // Von vorn (bzw. hinten) beginnen.
        QTextCursor cur = m_editor->textCursor();
        cur.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        m_editor->setTextCursor(cur);
        if (!m_editor->find(needle, flags))
            m_status->setText(_t("Nicht gefunden."));
        else
            m_status->setText(_t("Von vorn gesucht."));
    } else {
        m_status->clear();
    }
}

void EditorDialog::replaceCurrent(bool all)
{
    if (m_readOnly || m_find->text().isEmpty())
        return;
    if (all) {
        const QString text = m_editor->toPlainText();
        QString result = text;
        result.replace(m_find->text(), m_replace->text(),
                       m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive);
        if (result != text) {
            m_editor->setPlainText(result);
            m_status->setText(_t("Alle Vorkommen ersetzt."));
        }
        return;
    }
    QTextCursor cur = m_editor->textCursor();
    if (cur.hasSelection()) {
        const bool matches = m_caseSensitive->isChecked()
                                 ? cur.selectedText() == m_find->text()
                                 : cur.selectedText().compare(m_find->text(),
                                                              Qt::CaseInsensitive) == 0;
        if (matches)
            cur.insertText(m_replace->text());
    }
    findNext();
}

void EditorDialog::gotoLine()
{
    bool ok = false;
    const int line = QInputDialog::getInt(this, _t("Gehe zu Zeile"), _t("Zeile:"),
                                          m_editor->textCursor().blockNumber() + 1,
                                          1, m_editor->blockCount(), 1, &ok);
    if (!ok)
        return;
    QTextCursor cur(m_editor->document()->findBlockByNumber(line - 1));
    m_editor->setTextCursor(cur);
    m_editor->centerCursor();
}

void EditorDialog::explainWithAi()
{
    if (!core::aiEnabled()) {
        QMessageBox::information(this, _t("KI"),
                                 _t("Die KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    bool ok = false;
    const QString question = QInputDialog::getText(
        this, _t("KI erklären"),
        _t("Frage zur Datei (leer = allgemein erklären):"), QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    // Markierter Abschnitt hat Vorrang — sonst die ganze Datei.
    const QString selected = m_editor->textCursor().selectedText();
    const QString source = selected.isEmpty()
                               ? m_editor->toPlainText()
                               : QString(selected).replace(QChar(0x2029), QLatin1Char('\n'));
    // Kontext deckeln — bei Dateien zaehlt der Anfang (Header/Direktiven).
    const auto [content, truncated] = core::truncateFile(source);
    Q_UNUSED(truncated);
    auto *panel = new AiChatPanel(
        m_bridge, core::buildFileMessages(m_provider->basename(m_path), content, question),
        _t("KI: %1").arg(m_path), this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->show();
}

void EditorDialog::codecheckWithAi()
{
    if (!core::aiEnabled()) {
        QMessageBox::information(this, _t("KI"),
                                 _t("Die KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    const QString name = m_provider->basename(m_path);
    const auto [content, truncated] = core::truncateFile(m_editor->toPlainText());
    Q_UNUSED(truncated);
    auto *panel = new AiChatPanel(
        m_bridge, core::buildCodecheckMessages(name, content, core::sourceLanguage(name)),
        _t("KI-Fehleranalyse: %1").arg(m_path), this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->show();
}

void EditorDialog::openEncodingConverter()
{
    // Ungespeicherte Aenderungen wuerden der Konverter nicht sehen.
    if (m_dirty) {
        QMessageBox::information(this, _t("Encoding konvertieren"),
                                 _t("Bitte zuerst speichern — der Konverter liest die Datei "
                                    "von der Platte."));
        return;
    }
    EncodingConverterDialog dlg(m_bridge, m_provider, m_path, this);
    if (dlg.exec() == QDialog::Accepted)
        load();   // ggf. neu geschriebene Datei wieder einlesen
}

void EditorDialog::closeEvent(QCloseEvent *event)
{
    if (m_dirty && !m_readOnly) {
        const auto answer = QMessageBox::question(
            this, _t("Ungespeicherte Änderungen"),
            _t("'%1' wurde geändert. Vor dem Schließen speichern?")
                .arg(m_provider->basename(m_path)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save) {
            save(false);
            // Speichern laeuft asynchron — Dialog trotzdem schliessen.
        }
    }
    event->accept();
}

} // namespace ncssh::gui
