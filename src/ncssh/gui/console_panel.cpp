#include "ncssh/gui/console_panel.hpp"

#include "ncssh/core/history.hpp"
#include "ncssh/core/i18n.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/ai_chat_panel.hpp"
#include "ncssh/gui/terminal_widget.hpp"

#include <QDir>
#include <QFont>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QTextDocument>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

ConsolePanel::ConsolePanel(AsyncBridge *bridge, const QString &title, QWidget *parent)
    : QWidget(parent), m_bridge(bridge)
{
    setObjectName(QStringLiteral("ConsolePanel"));
    // Ohne dieses Attribut zeichnet die QWidget-Unterklasse den aktiven Rahmen
    // (#ConsolePanel[active="true"]) nicht.
    setAttribute(Qt::WA_StyledBackground, true);
    setProperty("active", false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Kopfzeile mit Modus-Umschalter (Befehle <-> Terminal)
    auto *headerRow = new QHBoxLayout();
    m_header = new QLabel(title, this);
    m_header->setObjectName(QStringLiteral("ConsoleHeaderTitle"));
    m_modeButton = new QPushButton(_t("Terminal"), this);
    m_modeButton->setObjectName(QStringLiteral("Chip"));
    m_modeButton->setCheckable(true);
    connect(m_modeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on) switchToTerminal(); else switchToCommands();
    });
    // KI: die letzte Terminalausgabe erklaeren lassen (nur bei aktivierter KI).
    auto *aiButton = new QPushButton(_t("KI"), this);
    aiButton->setObjectName(QStringLiteral("Chip"));
    aiButton->setToolTip(_t("Ausgabe/Fehler mit KI erklären"));
    connect(aiButton, &QPushButton::clicked, this, &ConsolePanel::explainWithAi);
    // Abdocken: die Konsole wird zu einem eigenen Fenster; die Pane bekommt
    // dadurch den vollen Platz der Spalte.
    m_dockButton = new QPushButton(QStringLiteral("⤢"), this);
    m_dockButton->setObjectName(QStringLiteral("Chip"));
    m_dockButton->setToolTip(_t("⤢ Abdocken"));
    connect(m_dockButton, &QPushButton::clicked, this, [this] {
        if (m_docked)
            emit undockRequested();
        else
            emit dockRequested();
    });
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(aiButton);
    headerRow->addWidget(m_modeButton);
    headerRow->addWidget(m_dockButton);
    layout->addLayout(headerRow);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    // Seite 1: Befehle (Ausgabe + Eingabezeile)
    m_commandPage = new QWidget(m_stack);
    m_commandLayout = new QVBoxLayout(m_commandPage);
    m_commandLayout->setContentsMargins(0, 0, 0, 0);
    m_commandLayout->setSpacing(6);

    m_output = new QPlainTextEdit(m_commandPage);
    m_output->setObjectName(QStringLiteral("ConsoleOutput"));
    m_output->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(20000);
    m_commandLayout->addWidget(m_output, 1);

    auto *inputRow = new QHBoxLayout();
    m_prompt = new QLabel(QStringLiteral("$"), m_commandPage);
    m_prompt->setObjectName(QStringLiteral("Muted"));
    m_input = new QLineEdit(m_commandPage);
    m_input->setFont(mono);
    m_input->setPlaceholderText(
        _t("Befehl eingeben und Enter…   (↑/↓ = Historie, Strg+F = suchen, cd, clear)"));
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &ConsolePanel::submit);
    // Zustand des laufenden Befehls sichtbar machen und abbrechen koennen.
    m_status = new QLabel(m_commandPage);
    m_status->setObjectName(QStringLiteral("Muted"));
    m_stopButton = new QPushButton(QStringLiteral("■"), m_commandPage);
    m_stopButton->setObjectName(QStringLiteral("Chip"));
    m_stopButton->setToolTip(_t("Laufenden Befehl abbrechen (Esc)"));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ConsolePanel::cancelRunning);
    inputRow->addWidget(m_prompt);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_status);
    inputRow->addWidget(m_stopButton);
    m_commandLayout->addLayout(inputRow);
    m_stack->addWidget(m_commandPage);

    // Seite 2: interaktives Terminal (echtes PTY)
    m_terminal = new TerminalWidget(bridge, m_stack);
    m_stack->addWidget(m_terminal);

    m_historyStore.load();
    m_history = m_historyStore.history();
    m_historyPos = m_history.size();
}

void ConsolePanel::setRunner(core::CommandRunner *runner, const QString &cwd)
{
    m_runner = runner;
    setCwd(cwd);
}

void ConsolePanel::setCwd(const QString &cwd)
{
    const bool changed = (m_cwd != cwd);
    m_cwd = cwd;
    m_prompt->setText(cwd.isEmpty() ? QStringLiteral("$") : cwd + QStringLiteral(" $"));
    // Beim Verzeichniswechsel der Pane ein 'cd' ins laufende Terminal senden.
    if (changed && !cwd.isEmpty() && m_terminal->isRunning())
        m_terminal->sendText(QStringLiteral("cd \"%1\"\r").arg(cwd));
}

void ConsolePanel::setDocked(bool docked)
{
    m_docked = docked;
    m_dockButton->setText(docked ? QStringLiteral("⤢") : QStringLiteral("⤵"));
    m_dockButton->setToolTip(docked ? _t("⤢ Abdocken") : _t("⤵ Andocken"));
}

void ConsolePanel::setActive(bool active)
{
    if (property("active").toBool() == active)
        return;
    setProperty("active", active);
    // Dynamische Property wirkt erst nach erneutem Polish des Stylesheets.
    style()->unpolish(this);
    style()->polish(this);
}

void ConsolePanel::setSession(const net::SSHSessionPtr &session)
{
    m_session = session;
    // Laeuft bereits ein Terminal, mit der neuen Session neu starten.
    if (m_terminal->isRunning()) {
        m_terminal->stop();
        switchToTerminal();
    }
}

void ConsolePanel::shutdownShell()
{
    m_session.reset();
    m_terminal->stop();
}

void ConsolePanel::switchToTerminal()
{
    m_stack->setCurrentWidget(m_terminal);
    if (!m_terminal->isRunning()) {
        if (m_session)
            m_terminal->startRemote(m_session);
        else
            m_terminal->startLocal();
    }
    m_terminal->setFocus();
}

void ConsolePanel::switchToCommands()
{
    m_stack->setCurrentWidget(m_commandPage);
    m_input->setFocus();
}

void ConsolePanel::explainWithAi()
{
    if (!core::aiEnabled()) {
        QMessageBox::information(this, _t("KI"),
                                 _t("Die KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    const QString output = m_output->toPlainText();
    if (output.trimmed().isEmpty()) {
        QMessageBox::information(this, _t("KI"), _t("Es gibt noch keine Ausgabe."));
        return;
    }
    // Kontext deckeln — bei Terminalausgabe zaehlt das Ende (Fehler stehen unten).
    const auto [text, truncated] = core::truncateTerminal(output);
    auto *panel = new AiChatPanel(m_bridge, core::buildTerminalMessages(text),
                                  _t("KI — Terminalausgabe erklären"), this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->show();
}

void ConsolePanel::runCommand(const QString &command, bool execute)
{
    if (!execute) {
        m_input->setText(command);
        m_input->setFocus();
        return;
    }
    if (!m_runner || command.trimmed().isEmpty())
        return;
    if (m_running) {
        // Ein zweiter Befehl wuerde den ersten stumm ueberschreiben.
        appendOutput(_t("[Es läuft bereits ein Befehl — Stop/Esc bricht ihn ab]"));
        return;
    }

    appendOutput(QStringLiteral("%1 $ %2").arg(m_cwd, command));

    // 'cd' abfangen: Verzeichnis aufloesen und CWD synchronisieren.
    const QString trimmed = command.trimmed();
    if (trimmed == QLatin1String("cd") || trimmed.startsWith(QLatin1String("cd "))) {
        const QString target = trimmed == QLatin1String("cd")
                                   ? QStringLiteral("~")
                                   : trimmed.mid(3).trimmed();
        core::CommandRunner *runner = m_runner;
        const QString cwd = m_cwd;
        m_bridge->run<std::optional<QString>>(
            [runner, cwd, target] { return runner->resolveDir(cwd, target); },
            [this, target](const std::optional<QString> &resolved) {
                if (resolved) {
                    setCwd(*resolved);
                    emit cwdChanged(*resolved);
                } else {
                    appendOutput(_t("cd: kein Verzeichnis: %1").arg(target));
                }
            },
            [this](const QString &err) { appendOutput(_t("[Fehler] %1").arg(err)); });
        return;
    }

    core::CommandRunner *runner = m_runner;
    const QString cwd = m_cwd;
    setBusy(true);
    m_running = m_bridge->stream(
        [runner, command, cwd](const AsyncBridge::EmitLine &emit, const CancelTokenPtr &cancel) {
            runner->stream(command, cwd, [&emit](const QString &line) { emit(line); }, cancel);
        },
        [this](const QString &line) { appendOutput(line); },
        [this, runner] {
            m_running = nullptr;
            setBusy(false);
            // Exit-Code anzeigen, sofern der Runner ihn geliefert hat.
            const auto code = runner->lastExitStatus;
            if (code && *code == 0)
                m_status->setText(_t("✓ fertig (Exit 0)"));
            else if (code)
                m_status->setText(_t("✗ Exit %1").arg(*code));
            else
                m_status->setText(_t("✓ fertig"));
        },
        [this](const QString &err) {
            appendOutput(_t("[Fehler] %1").arg(err));
            m_running = nullptr;
            setBusy(false);
            m_status->setText(_t("✗ Fehler"));
        });
}

void ConsolePanel::setBusy(bool busy)
{
    m_status->setText(busy ? _t("läuft…") : QString());
    m_stopButton->setEnabled(busy);
}

void ConsolePanel::cancelRunning()
{
    if (!m_running)
        return;
    m_bridge->cancel(m_running);
    m_running = nullptr;
    appendOutput(_t("^C abgebrochen"));
    setBusy(false);
    m_status->setText(_t("■ abgebrochen"));
}

void ConsolePanel::submit()
{
    const QString command = m_input->text();
    if (command.trimmed().isEmpty())
        return;
    m_input->clear();
    m_history.append(command);
    m_historyPos = m_history.size();
    m_historyStore.add(command);
    m_historyStore.save();
    runCommand(command, true);
}

void ConsolePanel::appendOutput(const QString &text)
{
    m_output->appendPlainText(text);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void ConsolePanel::complete()
{
    if (!m_completionProvider)
        return;
    const int pos = m_input->cursorPosition();
    const QString text = m_input->text();
    const QString before = text.left(pos);
    const QString after = text.mid(pos);
    // Token-Anfang: in offenen Anfuehrungszeichen ab dem Quote (Leerzeichen
    // gehoeren dann zum Pfad), sonst ab dem letzten Leerzeichen.
    const bool inQuotes = (before.count(QLatin1Char('"')) % 2) == 1;
    const int start = inQuotes ? before.lastIndexOf(QLatin1Char('"')) + 1
                               : before.lastIndexOf(QLatin1Char(' ')) + 1;
    const QString token = before.mid(start);
    const int cut = qMax(token.lastIndexOf(QLatin1Char('/')),
                         token.lastIndexOf(QLatin1Char('\\'))) + 1;
    const QString dirPart = token.left(cut);
    const QString prefix = token.mid(cut);
    const QString sep = m_completionProvider->isRemote ? QStringLiteral("/")
                                                       : QString(QDir::separator());
    const QString cwd = m_cwd.isEmpty() ? QStringLiteral(".") : m_cwd;
    QString base;
    if (dirPart.isEmpty())
        base = cwd;
    else if (dirPart.startsWith(QLatin1Char('/')) || QDir::isAbsolutePath(dirPart))
        base = dirPart;
    else
        base = m_completionProvider->join(cwd, dirPart);

    core::FileSystemProvider *provider = m_completionProvider;
    m_bridge->run<std::vector<core::FileEntry>>(
        [provider, base] { return provider->listDir(base); },
        [this, text, pos, before, after, start, dirPart, prefix, inQuotes, sep](
            const std::vector<core::FileEntry> &entries) {
            // Inzwischen weitergetippt/abgeschickt? Dann nichts ueberschreiben.
            if (m_input->text() != text || m_input->cursorPosition() != pos)
                return;
            std::vector<const core::FileEntry *> names;
            for (const auto &e : entries) {
                if (e.type == core::EntryType::Parent)
                    continue;
                if (e.name.startsWith(prefix))
                    names.push_back(&e);
            }
            if (names.empty() && !prefix.isEmpty()) {  // Gross/Kleinschreibung egal
                const QString low = prefix.toLower();
                for (const auto &e : entries) {
                    if (e.type == core::EntryType::Parent)
                        continue;
                    if (e.name.toLower().startsWith(low))
                        names.push_back(&e);
                }
            }
            if (names.empty())
                return;

            auto apply = [&](const QString &completed, bool closeToken) {
                const bool quote = inQuotes || (dirPart + completed).contains(QLatin1Char(' '));
                const QString lead = (quote && !inQuotes) ? QStringLiteral("\"") : QString();
                const QString tail = closeToken ? (quote ? QStringLiteral("\" ")
                                                         : QStringLiteral(" "))
                                                : QString();
                const QString newBefore = before.left(start) + lead + dirPart + completed + tail;
                m_input->setText(newBefore + after);
                m_input->setCursorPosition(newBefore.size());
            };

            if (names.size() == 1) {
                const core::FileEntry *e = names.front();
                if (e->type == core::EntryType::Dir)
                    apply(e->name + sep, false);
                else
                    apply(e->name, true);
                return;
            }
            // Gemeinsamen Anfang aller Kandidaten bestimmen.
            QString common = names.front()->name;
            for (const core::FileEntry *e : names) {
                int i = 0;
                while (i < common.size() && i < e->name.size() && common[i] == e->name[i])
                    ++i;
                common.truncate(i);
            }
            if (common.size() <= prefix.size()) {
                // Nicht weiter eindeutig -> Kandidaten in der Ausgabe listen.
                QStringList shown;
                for (const core::FileEntry *e : names)
                    shown << e->name + (e->type == core::EntryType::Dir ? QStringLiteral("/")
                                                                        : QString());
                appendOutput(shown.join(QStringLiteral("  ")));
                return;
            }
            apply(common, false);
        },
        [](const QString &) {});
}

bool ConsolePanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Up) {
            // Beim ersten Hochblättern die aktuell getippte Zeile merken.
            if (m_historyPos >= m_history.size())
                m_historyDraft = m_input->text();
            if (m_historyPos > 0) {
                --m_historyPos;
                m_input->setText(m_history.value(m_historyPos));
            }
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            if (m_historyPos < m_history.size() - 1) {
                ++m_historyPos;
                m_input->setText(m_history.value(m_historyPos));
            } else {
                // Unten angekommen -> die gemerkte Entwurfszeile wiederherstellen.
                m_historyPos = m_history.size();
                m_input->setText(m_historyDraft);
            }
            return true;
        }
        if (ke->key() == Qt::Key_C && (ke->modifiers() & Qt::ControlModifier) && m_running) {
            cancelRunning();
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            // Erst die Suchleiste schliessen, sonst abbrechen.
            if (m_searchBar && m_searchBar->isVisible()) {
                hideSearch();
                return true;
            }
            if (m_running) {
                cancelRunning();
                return true;
            }
        }
        if (ke->key() == Qt::Key_F && (ke->modifiers() & Qt::ControlModifier)) {
            showSearch();
            return true;
        }
        // Tab vervollstaendigt den Pfad (wie in einer Shell), statt den Fokus zu
        // wechseln. Shift+Tab bleibt der normale Rueckwaerts-Fokuswechsel.
        if (ke->key() == Qt::Key_Tab && !(ke->modifiers() & Qt::ShiftModifier)) {
            complete();
            return true;
        }
    }
    // Suchleiste: Enter blaettert weiter, Shift+Enter zurueck, Esc schliesst.
    if (obj == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            hideSearch();
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            searchStep(!(ke->modifiers() & Qt::ShiftModifier));
            return true;
        }
    }
    if (event->type() == QEvent::FocusIn)
        emit activated();
    return QWidget::eventFilter(obj, event);
}

void ConsolePanel::showSearch()
{
    if (!m_searchBar) {
        m_searchBar = new QWidget(m_commandPage);
        auto *row = new QHBoxLayout(m_searchBar);
        row->setContentsMargins(4, 2, 4, 2);
        m_searchEdit = new QLineEdit(m_searchBar);
        m_searchEdit->setPlaceholderText(
            _t("In Ausgabe suchen…   (Enter = weiter, Shift+Enter = zurück, Esc = schließen)"));
        m_searchEdit->installEventFilter(this);
        connect(m_searchEdit, &QLineEdit::textChanged, this,
                [this] { searchStep(true); });
        auto *close = new QPushButton(QStringLiteral("✕"), m_searchBar);
        close->setFixedWidth(28);
        connect(close, &QPushButton::clicked, this, [this] { hideSearch(); });
        row->addWidget(m_searchEdit, 1);
        row->addWidget(close);
        // Direkt ueber der Eingabezeile einhaengen.
        m_commandLayout->insertWidget(m_commandLayout->count() - 1, m_searchBar);
    }
    m_searchBar->setVisible(true);
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void ConsolePanel::hideSearch()
{
    if (m_searchBar)
        m_searchBar->setVisible(false);
    // Hervorhebung entfernen.
    m_output->setExtraSelections({});
    m_input->setFocus();
}

void ConsolePanel::searchStep(bool forward)
{
    if (!m_searchEdit)
        return;
    const QString needle = m_searchEdit->text();
    if (needle.isEmpty()) {
        m_output->setExtraSelections({});
        return;
    }
    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;
    // Vom aktuellen Cursor aus suchen; am Ende zum Anfang umbrechen.
    if (!m_output->find(needle, flags)) {
        QTextCursor cursor = m_output->textCursor();
        cursor.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
        m_output->setTextCursor(cursor);
        m_output->find(needle, flags);
    }
}

} // namespace ncssh::gui
