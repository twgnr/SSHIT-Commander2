#include "ncssh/gui/console_panel.hpp"

#include "ncssh/core/history.hpp"
#include "ncssh/core/i18n.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/gui/ai_chat_panel.hpp"
#include "ncssh/gui/terminal_widget.hpp"

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
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

ConsolePanel::ConsolePanel(AsyncBridge *bridge, const QString &title, QWidget *parent)
    : QWidget(parent), m_bridge(bridge)
{
    setObjectName(QStringLiteral("ConsolePanel"));
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
    aiButton->setToolTip(_t("Ausgabe/Fehler von der KI erklären lassen"));
    connect(aiButton, &QPushButton::clicked, this, &ConsolePanel::explainWithAi);
    // Abdocken: die Konsole wird zu einem eigenen Fenster; die Pane bekommt
    // dadurch den vollen Platz der Spalte.
    m_dockButton = new QPushButton(QStringLiteral("⤢"), this);
    m_dockButton->setObjectName(QStringLiteral("Chip"));
    m_dockButton->setToolTip(_t("Abdocken"));
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
    auto *cmdLayout = new QVBoxLayout(m_commandPage);
    cmdLayout->setContentsMargins(0, 0, 0, 0);
    cmdLayout->setSpacing(6);

    m_output = new QPlainTextEdit(m_commandPage);
    m_output->setObjectName(QStringLiteral("ConsoleOutput"));
    m_output->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(20000);
    cmdLayout->addWidget(m_output, 1);

    auto *inputRow = new QHBoxLayout();
    m_prompt = new QLabel(QStringLiteral("$"), m_commandPage);
    m_prompt->setObjectName(QStringLiteral("Muted"));
    m_input = new QLineEdit(m_commandPage);
    m_input->setFont(mono);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &ConsolePanel::submit);
    inputRow->addWidget(m_prompt);
    inputRow->addWidget(m_input, 1);
    cmdLayout->addLayout(inputRow);
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
    m_dockButton->setToolTip(docked ? _t("Abdocken") : _t("Andocken"));
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
            [this](const std::optional<QString> &resolved) {
                if (resolved) {
                    setCwd(*resolved);
                    emit cwdChanged(*resolved);
                } else {
                    appendOutput(_t("Verzeichnis nicht gefunden."));
                }
            },
            [this](const QString &err) { appendOutput(err); });
        return;
    }

    core::CommandRunner *runner = m_runner;
    const QString cwd = m_cwd;
    m_running = m_bridge->stream(
        [runner, command, cwd](const AsyncBridge::EmitLine &emit, const CancelTokenPtr &cancel) {
            runner->stream(command, cwd, [&emit](const QString &line) { emit(line); }, cancel);
        },
        [this](const QString &line) { appendOutput(line); },
        [this] { m_running = nullptr; },
        [this](const QString &err) { appendOutput(err); m_running = nullptr; });
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

bool ConsolePanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Up) {
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
                m_historyPos = m_history.size();
                m_input->clear();
            }
            return true;
        }
        if (ke->key() == Qt::Key_C && (ke->modifiers() & Qt::ControlModifier) && m_running) {
            m_bridge->cancel(m_running);
            appendOutput(QStringLiteral("^C"));
            return true;
        }
    }
    if (event->type() == QEvent::FocusIn)
        emit activated();
    return QWidget::eventFilter(obj, event);
}

} // namespace ncssh::gui
