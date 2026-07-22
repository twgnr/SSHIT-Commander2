#include "ncssh/gui/ai_chat_panel.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/markdown.hpp"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

AiChatPanel::AiChatPanel(AsyncBridge *bridge, const QJsonArray &messages,
                         const QString &title, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_messages(messages)
{
    setWindowTitle(title);
    resize(820, 640);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        _t("Das Modell läuft lokal — Inhalte verlassen den Rechner nicht. "
           "Der Assistent ist rein beratend und führt nichts aus."), this);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_view = new QTextBrowser(this);
    m_view->setOpenExternalLinks(true);
    layout->addWidget(m_view, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *inputRow = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(_t("Frage eingeben …  (Strg+Enter = senden)"));
    connect(m_input, &QLineEdit::returnPressed, this, &AiChatPanel::ask);
    auto *askBtn = new QPushButton(_t("Senden"), this);
    askBtn->setDefault(true);
    connect(askBtn, &QPushButton::clicked, this, &AiChatPanel::ask);
    m_stopBtn = new QPushButton(_t("■ Stop"), this);
    connect(m_stopBtn, &QPushButton::clicked, this, [this] {
        if (m_task) {
            m_bridge->cancel(m_task);
            m_task = nullptr;
            m_status->setText(_t("Abgebrochen."));
        }
    });
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(askBtn);
    inputRow->addWidget(m_stopBtn);
    inputRow->addWidget(closeBtn);
    layout->addLayout(inputRow);

    // Die letzte Nutzerfrage anzeigen und gleich beantworten lassen.
    for (const QJsonValue &v : m_messages) {
        const QJsonObject msg = v.toObject();
        if (msg.value(QStringLiteral("role")).toString() == QLatin1String("user"))
            appendMarkdown(_t("Frage"), msg.value(QStringLiteral("content")).toString());
    }
    streamAnswer();
}

void AiChatPanel::appendMarkdown(const QString &role, const QString &text)
{
    m_view->append(QStringLiteral("<b>%1</b>").arg(role.toHtmlEscaped()));
    m_view->append(core::mdToHtml(text));
    m_view->append(QString());
    m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
}

void AiChatPanel::ask()
{
    const QString question = m_input->text().trimmed();
    if (question.isEmpty() || m_task)
        return;
    m_input->clear();
    m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                  {QStringLiteral("content"), question}});
    appendMarkdown(_t("Frage"), question);
    streamAnswer();
}

void AiChatPanel::streamAnswer()
{
    if (!core::aiEnabled()) {
        m_status->setText(_t("KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    const QString baseUrl = core::ollamaUrl();
    const QString model = core::aiModel();
    if (model.isEmpty()) {
        m_status->setText(_t("Kein Modell gewählt (Einstellungen → KI)."));
        return;
    }

    m_pending.clear();
    m_status->setText(_t("Denkt nach …"));
    const QJsonArray messages = m_messages;

    m_task = m_bridge->stream(
        [baseUrl, model, messages](const AsyncBridge::EmitLine &emitLine,
                                   const CancelTokenPtr &cancel) {
            core::chatStream(baseUrl, model, messages, QJsonObject{},
                             [&emitLine](const QString &chunk) { emitLine(chunk); }, cancel);
        },
        [this](const QString &chunk) { m_pending += chunk; },
        [this] {
            appendMarkdown(_t("Antwort"), m_pending);
            m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                          {QStringLiteral("content"), m_pending}});
            m_status->clear();
            m_task = nullptr;
        },
        [this](const QString &err) {
            m_status->setText(err);
            m_task = nullptr;
        });
}

} // namespace ncssh::gui
