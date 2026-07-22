#include "ncssh/gui/console_panel.hpp"

#include "ncssh/core/history.hpp"
#include "ncssh/core/i18n.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
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

    m_header = new QLabel(title, this);
    m_header->setObjectName(QStringLiteral("ConsoleHeaderTitle"));
    layout->addWidget(m_header);

    m_output = new QPlainTextEdit(this);
    m_output->setObjectName(QStringLiteral("ConsoleOutput"));
    m_output->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(20000);
    layout->addWidget(m_output, 1);

    auto *inputRow = new QHBoxLayout();
    m_prompt = new QLabel(QStringLiteral("$"), this);
    m_prompt->setObjectName(QStringLiteral("Muted"));
    m_input = new QLineEdit(this);
    m_input->setFont(mono);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &ConsolePanel::submit);
    inputRow->addWidget(m_prompt);
    inputRow->addWidget(m_input, 1);
    layout->addLayout(inputRow);

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
    m_cwd = cwd;
    m_prompt->setText(cwd.isEmpty() ? QStringLiteral("$") : cwd + QStringLiteral(" $"));
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
