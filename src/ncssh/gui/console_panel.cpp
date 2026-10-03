#include "ncssh/gui/console_panel.hpp"

#include "ncssh/core/history.hpp"
#include "ncssh/core/i18n.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/command_params.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/ai_chat_panel.hpp"
#include "ncssh/gui/icons.hpp"
#include "ncssh/gui/line_completer.hpp"
#include "ncssh/gui/param_dialog.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/shell_backends.hpp"
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
#include <QAction>
#include <QPointer>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
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
    setProperty("sudo", false);
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
    // Verbindungsaufbau abbrechen — nur sichtbar, solange einer laeuft. In der
    // Kopfzeile, damit er im Befehls- wie im Terminal-Modus erreichbar ist.
    m_connectCancelButton = new QPushButton(_t("Verbindung abbrechen"), this);
    m_connectCancelButton->setObjectName(QStringLiteral("Chip"));
    m_connectCancelButton->setVisible(false);
    connect(m_connectCancelButton, &QPushButton::clicked, this,
            &ConsolePanel::connectCancelRequested);
    // Befehlspalette und Verlauf direkt an der Konsole (wie in der
    // Python-Version): gewaehlter Befehl landet hier — auch im Terminal.
    // Als Symbole (Platz im Kopf); die Beschriftung steht im Tooltip.
    auto *paletteButton = new QPushButton(this);
    m_paletteButton = paletteButton;
    paletteButton->setObjectName(QStringLiteral("Chip"));
    paletteButton->setToolTip(_t("Befehlspalette — Befehl auswählen und hier einfügen"));
    paletteButton->setAccessibleName(_t("Befehle"));
    connect(paletteButton, &QPushButton::clicked, this, [this] {
        emit activated();   // diese Seite wird Ziel
        emit paletteRequested();
    });
    auto *historyButton = new QPushButton(this);
    m_historyButton = historyButton;
    historyButton->setObjectName(QStringLiteral("Chip"));
    historyButton->setAccessibleName(_t("Verlauf"));
    historyButton->setToolTip(_t("Befehlsverlauf & Favoriten — Befehl hier einfügen"));
    connect(historyButton, &QPushButton::clicked, this, [this] {
        emit activated();
        emit historyRequested();
    });
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(m_connectCancelButton);
    // Parameter des getippten Befehls — nur sichtbar, wenn er bekannt ist.
    m_paramButton = new QPushButton(this);
    m_paramButton->setObjectName(QStringLiteral("Chip"));
    m_paramButton->setAccessibleName(_t("Parameter"));
    m_paramButton->setVisible(false);
    connect(m_paramButton, &QPushButton::clicked, this, &ConsolePanel::openParamDialog);
    m_paramTimer = new QTimer(this);
    m_paramTimer->setSingleShot(true);
    m_paramTimer->setInterval(250);
    connect(m_paramTimer, &QTimer::timeout, this, &ConsolePanel::updateParamButton);
    refreshIcons();
    headerRow->addWidget(m_paramButton);
    headerRow->addWidget(paletteButton);
    headerRow->addWidget(historyButton);
    // Weiteres Terminal als Reiter — nur im Terminal-Modus sichtbar.
    m_addTerminalButton = new QPushButton(QStringLiteral("+"), this);
    m_addTerminalButton->setObjectName(QStringLiteral("Chip"));
    m_addTerminalButton->setToolTip(_t("Weiteres Terminal öffnen"));
    m_addTerminalButton->setVisible(false);
    connect(m_addTerminalButton, &QPushButton::clicked, this, &ConsolePanel::addTerminal);
    headerRow->addWidget(aiButton);
    headerRow->addWidget(m_addTerminalButton);
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
    // Harte Obergrenze, falls die Stylesheet-Schrift breiter ist als beim
    // Kuerzen gemessen — die Spaltenbreite darf nie am Pfad haengen.
    m_prompt->setMaximumWidth(300);
    m_input = new QLineEdit(m_commandPage);
    m_input->setFont(mono);
    m_input->setPlaceholderText(
        _t("Befehl eingeben und Enter…   (↑/↓ = Historie, Strg+F = suchen, cd, clear)"));
    m_input->installEventFilter(this);
    m_completer = new LineCompleter(bridge, m_input, /*handleTab=*/false);
    connect(m_input, &QLineEdit::returnPressed, this, &ConsolePanel::submit);
    connect(m_input, &QLineEdit::textChanged, m_paramTimer, qOverload<>(&QTimer::start));
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

    // Seite 2: interaktive Terminals (echtes PTY) als Reiter. Die Reiterleiste
    // erscheint erst ab dem zweiten Terminal.
    m_terminalPage = new QWidget(m_stack);
    auto *termLayout = new QVBoxLayout(m_terminalPage);
    termLayout->setContentsMargins(0, 0, 0, 0);
    m_termTabs = new QTabWidget(m_terminalPage);
    m_termTabs->setDocumentMode(true);
    m_termTabs->setTabsClosable(true);
    m_termTabs->setTabBarAutoHide(true);
    m_termTabs->setMovable(true);
    termLayout->addWidget(m_termTabs);
    m_terminal = createTerminal();
    m_termTabs->addTab(m_terminal, _t("Terminal %1").arg(++m_terminalSeq));
    connect(m_termTabs, &QTabWidget::tabCloseRequested, this, &ConsolePanel::closeTerminal);
    connect(m_termTabs, &QTabWidget::tabBarDoubleClicked, this, &ConsolePanel::renameTerminal);
    connect(m_termTabs, &QTabWidget::currentChanged, this, [this](int index) {
        auto *terminal = qobject_cast<TerminalWidget *>(m_termTabs->widget(index));
        if (!terminal)
            return;
        m_terminal = terminal;
        m_paramTimer->start();
        if (terminalMode()) {
            startTerminal(terminal);
            terminal->setFocus();
        }
    });
    m_stack->addWidget(m_terminalPage);

    m_historyStore.load();
    m_history = m_historyStore.history();
    m_historyPos = m_history.size();
}

void ConsolePanel::setRunner(core::CommandRunner *runner, const QString &cwd)
{
    m_runner = runner;
    setCwd(cwd);
}

void ConsolePanel::setCompletionProvider(core::FileSystemProvider *provider)
{
    m_completionProvider = provider;
    m_completer->setProvider(provider);
}

void ConsolePanel::setCwd(const QString &cwd)
{
    const bool changed = (m_cwd != cwd);
    m_cwd = cwd;
    m_completer->setCwd(cwd);
    // Langen Pfad vorne kuerzen (Ende bleibt lesbar), voller Pfad im Tooltip.
    // Ungekuerzt wurde die Textbreite zur Mindestbreite der Konsole — und damit
    // der ganzen Spalte: ein Ordner wie ~/.cache/electron/<sha256> zog die
    // Pane beim Betreten breiter.
    constexpr int kPromptMaxWidth = 260;
    const QString full = cwd.isEmpty() ? QStringLiteral("$") : cwd + QStringLiteral(" $");
    m_prompt->setText(
        m_prompt->fontMetrics().elidedText(full, Qt::ElideLeft, kPromptMaxWidth));
    m_prompt->setToolTip(cwd);
    // Beim Verzeichniswechsel der Pane ein 'cd' ins laufende Terminal senden.
    if (changed && !cwd.isEmpty() && m_terminal->isRunning())
        m_terminal->sendText(cdCommand(m_terminal->shellKind(), cwd) + QStringLiteral("\r"));
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

void ConsolePanel::setSudo(bool sudo)
{
    if (property("sudo").toBool() == sudo)
        return;
    setProperty("sudo", sudo);
    style()->unpolish(this);
    style()->polish(this);
}

void ConsolePanel::setSession(const net::SSHSessionPtr &session)
{
    m_session = session;
    m_helpCache.clear();   // Hilfe gilt je Server
    if (m_paramDialog)
        m_paramDialog->close();
    // Neues System: zusaetzliche Terminals gehoeren zur alten Verbindung und
    // werden geschlossen. Lief eines, startet das erste mit der neuen Session.
    bool wasRunning = false;
    for (TerminalWidget *terminal : terminals())
        wasRunning = wasRunning || terminal->isRunning();
    while (m_termTabs->count() > 1)
        closeTerminal(m_termTabs->count() - 1);
    m_terminalSeq = 1;
    // Vom Nutzer vergebene Namen (tabData = true) bleiben stehen.
    if (!m_termTabs->tabBar()->tabData(0).toBool())
        m_termTabs->setTabText(0, _t("Terminal %1").arg(m_terminalSeq));
    m_terminal->stop();
    if (wasRunning)
        switchToTerminal();
}

void ConsolePanel::setHeaderTitle(const QString &title)
{
    if (m_header)
        m_header->setText(title);
}

void ConsolePanel::shutdownShell()
{
    m_session.reset();
    for (TerminalWidget *terminal : terminals())
        terminal->stop();
}

bool ConsolePanel::terminalMode() const
{
    return m_stack->currentWidget() == m_terminalPage;
}

QList<TerminalWidget *> ConsolePanel::terminals() const
{
    QList<TerminalWidget *> list;
    for (int i = 0; i < m_termTabs->count(); ++i)
        if (auto *terminal = qobject_cast<TerminalWidget *>(m_termTabs->widget(i)))
            list << terminal;
    return list;
}

TerminalWidget *ConsolePanel::createTerminal()
{
    auto *terminal = new TerminalWidget(m_bridge, m_termTabs);
    connect(terminal, &QPlainTextEdit::textChanged, m_paramTimer, qOverload<>(&QTimer::start));
    // Beendete Shell: Enter im Terminal startet eine neue (lokal bzw. ueber
    // die aktuelle Session) — startTerminal startet nur, wenn keine laeuft.
    connect(terminal, &TerminalWidget::restartRequested, this, [this, terminal] {
        if (terminalMode() && m_terminal == terminal) {
            startTerminal(terminal);
            terminal->setFocus();
        }
    });
    return terminal;
}

void ConsolePanel::startTerminal(TerminalWidget *terminal)
{
    if (terminal->isRunning())
        return;
    if (m_session)
        terminal->startRemote(m_session);
    else
        terminal->startLocal();
}

void ConsolePanel::addTerminal()
{
    // Eigene Shell (remote: eigener Kanal auf derselben SSH-Session), die im
    // aktuellen Verzeichnis der Pane beginnt.
    TerminalWidget *terminal = createTerminal();
    const int index = m_termTabs->addTab(terminal, _t("Terminal %1").arg(++m_terminalSeq));
    if (!terminalMode())
        m_modeButton->setChecked(true);   // -> switchToTerminal
    m_termTabs->setCurrentIndex(index);   // -> currentChanged startet die Shell
    startTerminal(terminal);
    if (terminal->isRunning() && !m_cwd.isEmpty())
        terminal->sendText(cdCommand(terminal->shellKind(), m_cwd) + QStringLiteral("\r"));
    terminal->setFocus();
}

void ConsolePanel::closeTerminal(int index)
{
    // Das letzte Terminal bleibt (die Reiterleiste ist dann ohnehin verborgen).
    if (m_termTabs->count() <= 1)
        return;
    auto *terminal = qobject_cast<TerminalWidget *>(m_termTabs->widget(index));
    if (!terminal)
        return;
    terminal->stop();
    m_termTabs->removeTab(index);   // currentChanged fuehrt m_terminal nach
    terminal->deleteLater();
}

void ConsolePanel::renameTerminal(int index)
{
    if (index < 0 || index >= m_termTabs->count())
        return;
    // Eingabefeld direkt ueber dem Reiter: Enter/Fokusverlust uebernimmt,
    // Esc verwirft. Ueber den Seitenzeiger, falls Reiter inzwischen wandern.
    QTabBar *bar = m_termTabs->tabBar();
    auto *editor = new QLineEdit(bar);
    editor->setObjectName(QStringLiteral("TerminalTabEditor"));
    editor->setText(bar->tabText(index));
    editor->selectAll();
    editor->setGeometry(bar->tabRect(index));
    QPointer<QWidget> page = m_termTabs->widget(index);
    auto finish = [this, editor, page](bool accept) {
        if (editor->property("done").toBool())
            return;
        editor->setProperty("done", true);
        const int i = page ? m_termTabs->indexOf(page) : -1;
        const QString name = editor->text().trimmed();
        if (accept && i >= 0 && !name.isEmpty()) {
            m_termTabs->setTabText(i, name);
            m_termTabs->tabBar()->setTabData(i, true);
        }
        editor->deleteLater();
        if (page)
            page->setFocus();
    };
    connect(editor, &QLineEdit::editingFinished, this, [finish] { finish(true); });
    auto *cancel = new QAction(editor);
    cancel->setShortcut(QKeySequence(Qt::Key_Escape));
    cancel->setShortcutContext(Qt::WidgetShortcut);
    editor->addAction(cancel);
    connect(cancel, &QAction::triggered, this, [finish] { finish(false); });
    editor->show();
    editor->setFocus();
}

bool ConsolePanel::event(QEvent *event)
{
    if (event->type() == themeChangedEventType())
        refreshIcons();
    return QWidget::event(event);
}

void ConsolePanel::refreshIcons()
{
    m_paletteButton->setIcon(themedIcon(QStringLiteral("palette"), 16));
    m_historyButton->setIcon(themedIcon(QStringLiteral("history"), 16));
    m_paramButton->setIcon(themedIcon(QStringLiteral("params"), 16));
}

QString ConsolePanel::osType() const
{
    if (m_session)
        return m_session->osType == QLatin1String("windows") ? QStringLiteral("windows")
                                                             : QStringLiteral("posix");
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#else
    return QStringLiteral("posix");
#endif
}

QString ConsolePanel::currentCommandLine() const
{
    if (terminalMode())
        return m_terminal->isRunning() ? m_terminal->currentInputLine() : QString();
    return m_input->text();
}

void ConsolePanel::updateParamButton()
{
    const QString name = core::commandName(currentCommandLine(), osType());
    const bool known = core::isKnownCommand(name, osType());
    m_paramButton->setVisible(known);
    if (known)
        m_paramButton->setToolTip(_t("Parameter für „%1“ anzeigen").arg(name));
}

void ConsolePanel::appendToCommand(const QString &text)
{
    if (text.isEmpty())
        return;
    // Terminal: an die Eingabe der Shell tippen (sie steht am Zeilenende).
    if (terminalMode() && m_terminal->isRunning()) {
        const QString line = m_terminal->currentInputLine();
        const bool space = !line.isEmpty() && !line.endsWith(QLatin1Char(' '));
        m_terminal->sendText((space ? QStringLiteral(" ") : QString()) + text);
        return;
    }
    QString line = m_input->text();
    if (!line.isEmpty() && !line.endsWith(QLatin1Char(' ')))
        line += QLatin1Char(' ');
    m_input->setText(line + text);
    m_input->setCursorPosition(m_input->text().size());
}

void ConsolePanel::openParamDialog()
{
    const QString line = currentCommandLine();
    const QString os = osType();
    if (!core::isKnownCommand(core::commandName(line, os), os))
        return;
    if (m_paramDialog)
        m_paramDialog->close();
    auto *dlg = new ParamDialog(
        m_bridge, line, os, m_completionProvider, m_cwd,
        [this](const QString &text) { appendToCommand(text); },
        [this] { return currentCommandLine(); }, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_paramDialog = dlg;
    // Nach dem Schliessen zurueck an die Eingabe (Terminal-Cursor bzw.
    // Befehlszeile): ein Enter fuehrt den zusammengestellten Befehl dann aus.
    // Verzoegert, weil das Fenstersystem den Fokus erst nach dem Ausblenden
    // des Dialogs wieder vergibt.
    connect(dlg, &QDialog::finished, this, [this] {
        QTimer::singleShot(0, this, [this] {
            window()->activateWindow();
            if (terminalMode())
                m_terminal->setFocus();
            else
                m_input->setFocus();
        });
    });
    dlg->show();

    // Weitere Optionen aus der Hilfe des Befehls (nur Unix: Server oder lokal).
    const QString helpCmd = os == QLatin1String("posix") ? core::helpCommandFor(line) : QString();
    if (helpCmd.isEmpty() || !m_runner) {
        dlg->setHelpStatus(os == QLatin1String("posix")
                               ? QString()
                               : _t("Optionen aus dem Befehlskatalog."));
        return;
    }
    if (m_helpCache.contains(helpCmd)) {
        dlg->addHelpOptions(core::parseHelpOptions(m_helpCache.value(helpCmd)));
        return;
    }
    dlg->setHelpStatus(_t("Lade Hilfe vom Server …"));
    QPointer<ParamDialog> target = dlg;
    auto lines = std::make_shared<QStringList>();
    core::CommandRunner *runner = m_runner;
    const QString cwd = m_cwd;
    m_bridge->stream(
        [runner, helpCmd, cwd](const AsyncBridge::EmitLine &emitLine, const CancelTokenPtr &cancel) {
            runner->stream(helpCmd, cwd, [&emitLine](const QString &l) { emitLine(l); }, cancel);
        },
        [lines](const QString &l) { lines->append(l); },
        [this, target, lines, helpCmd] {
            const QString text = lines->join(QLatin1Char('\n'));
            m_helpCache.insert(helpCmd, text);
            if (target)
                target->addHelpOptions(core::parseHelpOptions(text));
        },
        [target](const QString &err) {
            if (target)
                target->setHelpStatus(_t("Hilfe nicht verfügbar: %1").arg(err));
        },
        this);
}

void ConsolePanel::switchToTerminal()
{
    m_stack->setCurrentWidget(m_terminalPage);
    m_addTerminalButton->setVisible(true);
    m_paramTimer->start();
    startTerminal(m_terminal);
    m_terminal->setFocus();
}

void ConsolePanel::switchToCommands()
{
    m_stack->setCurrentWidget(m_commandPage);
    m_addTerminalButton->setVisible(false);
    m_paramTimer->start();
    m_input->setFocus();
}

void ConsolePanel::explainWithAi()
{
    if (!core::aiEnabled()) {
        QMessageBox::information(this, _t("KI"),
                                 _t("Die KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    // Die sichtbare Seite zaehlt: im Terminal-Modus steht die Ausgabe im
    // Terminal-Widget, nicht im Befehlsfenster (das dann leer ist und zur
    // Meldung "Es gibt noch keine Ausgabe" trotz vollem Bildschirm fuehrte).
    const QString output = terminalMode()
                               ? m_terminal->toPlainText()
                               : m_output->toPlainText();
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
    // Terminal-Modus: in die laufende Shell tippen. Vorher landete der Befehl
    // in der (unsichtbaren) Eingabezeile des Befehlsmodus.
    if (terminalMode() && m_terminal->isRunning()) {
        if (command.trimmed().isEmpty())
            return;
        m_terminal->sendText(execute ? command + QStringLiteral("\r") : command);
        if (execute)
            m_historyStore.add(command);  // liest neu ein und speichert selbst
        m_terminal->setFocus();
        return;
    }
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
            [this](const QString &err) { appendOutput(_t("[Fehler] %1").arg(err)); }, this);
        return;
    }

    core::CommandRunner *runner = m_runner;
    const QString cwd = m_cwd;
    setBusy(true);
    // Laufnummer: Rueckrufe eines abgebrochenen, aelteren Befehls kommen erst
    // spaeter an und duerfen m_running/den Status des NEUEN Befehls nicht
    // zuruecksetzen (sonst war er nicht mehr stoppbar, und ein dritter Befehl
    // durfte parallel starten).
    const quint64 seq = ++m_runSeq;
    m_running = m_bridge->stream(
        [runner, command, cwd](const AsyncBridge::EmitLine &emitLine, const CancelTokenPtr &cancel) {
            // NICHT "emit" nennen: das ist Qts (leeres) Makro — aus emit(line)
            // wurde "(line)", und die Ausgabe ging jahrelang verloren.
            runner->stream(command, cwd, [&emitLine](const QString &line) { emitLine(line); }, cancel);
        },
        [this, seq](const QString &line) {
            if (seq == m_runSeq)   // Nachzuegler eines abgebrochenen Befehls verwerfen
                appendOutput(line);
        },
        [this, runner, seq] {
            if (seq != m_runSeq)
                return;
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
        [this, seq](const QString &err) {
            if (seq != m_runSeq)
                return;
            m_running = nullptr;
            setBusy(false);
            // Vom Nutzer gestoppt ist kein Fehler — die Meldung aus
            // cancelRunning() bleibt stehen.
            if (err == QLatin1String("cancelled")) {
                m_status->setText(_t("■ abgebrochen"));
                return;
            }
            appendOutput(_t("[Fehler] %1").arg(err));
            m_status->setText(_t("✗ Fehler"));
        }, this);
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
    // add() liest die Datei neu ein und speichert selbst — ein eigenes save()
    // schriebe sonst womoeglich einen veralteten Stand zurueck.
    m_historyStore.add(command);
    runCommand(command, true);
}

void ConsolePanel::printInfo(const QString &text, bool error)
{
    appendOutput(error ? QStringLiteral("✖ ") + text : QStringLiteral("» ") + text);
    if (terminalMode())
        m_terminal->printLocal(text, error);
}

void ConsolePanel::setConnectCancelVisible(bool visible)
{
    m_connectCancelButton->setVisible(visible);
}

void ConsolePanel::appendOutput(const QString &text)
{
    m_output->appendPlainText(text);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
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
            m_completer->complete();
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
