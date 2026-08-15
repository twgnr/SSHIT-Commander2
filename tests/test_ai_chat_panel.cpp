// Offscreen-Test fuer das KI-Chat-Panel und den KI-Tab der Einstellungen.
//
// Ohne laufenden Ollama-Server: das Panel wird mit vorbereiteten Nachrichten
// aufgebaut, und die Fehlerbehandlung wird gegen einen geschlossenen Port
// geprueft. Die Streaming-Abbildungen selbst deckt test_ai.cpp ab.
#include "tests/harness.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/ai_chat_panel.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/settings_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>

using namespace ncssh;

namespace {

template <typename Predicate>
bool pumpUntil(Predicate ready, int timeoutMs = 8000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!ready()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// Lenkt die Einstellungen auf ein frisches Verzeichnis um.
class ConfigDirGuard
{
public:
    ConfigDirGuard() : m_appdata(qgetenv("APPDATA")), m_xdg(qgetenv("XDG_CONFIG_HOME"))
    {
        qputenv("APPDATA", m_dir.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.path().toLocal8Bit());
    }
    ~ConfigDirGuard()
    {
        qputenv("APPDATA", m_appdata);
        qputenv("XDG_CONFIG_HOME", m_xdg);
    }
    bool isValid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_appdata;
    QByteArray m_xdg;
};

} // namespace

TEST(ai_chat_panel, opens_with_prepared_messages)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());
    // Auf einen geschlossenen Port zeigen — der Test darf nie eine echte
    // Verbindung aufbauen.
    core::setSetting(QString::fromLatin1(core::OLLAMA_URL),
                     QStringLiteral("http://127.0.0.1:1"));
    core::setSetting(QString::fromLatin1(core::AI_MODEL), QStringLiteral("test-model"));

    gui::AsyncBridge bridge;
    const QJsonArray messages =
        core::buildFileMessages(QStringLiteral("httpd.conf"), QStringLiteral("Listen 80"));
    gui::AiChatPanel panel(&bridge, messages, QStringLiteral("KI — httpd.conf"));

    CHECK_EQ(panel.windowTitle(), QStringLiteral("KI — httpd.conf"));
    auto *view = panel.findChild<QTextBrowser *>();
    CHECK(view != nullptr);
    if (!view)
        return;
    // Die Ausgangsfrage steht im Verlauf, bevor eine Antwort eintrifft.
    CHECK(pumpUntil([&] { return view->toHtml().contains(QStringLiteral("httpd.conf")); },
                    3000));
}

TEST(ai_chat_panel, unreachable_server_reports_error)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());
    core::setSetting(QString::fromLatin1(core::OLLAMA_URL),
                     QStringLiteral("http://127.0.0.1:1"));
    core::setSetting(QString::fromLatin1(core::AI_MODEL), QStringLiteral("test-model"));

    gui::AsyncBridge bridge;
    gui::AiChatPanel panel(&bridge, core::buildTerminalMessages(QStringLiteral("boom")),
                           QStringLiteral("KI — Terminal"));
    auto *status = panel.findChild<QLabel *>();
    CHECK(status != nullptr);
    if (!status)
        return;
    // Der Verbindungsfehler muss in der Statuszeile ankommen — still bleiben
    // waere hier das schlechteste Verhalten.
    CHECK(pumpUntil([&] { return !status->text().trimmed().isEmpty(); }));
}

TEST(ai_chat_panel, settings_ai_tab_writes_keys)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());

    gui::SettingsDialog dlg(nullptr, nullptr);
    // KI-Schalter, URL und Modell setzen …
    QCheckBox *aiEnabled = nullptr;
    for (QCheckBox *box : dlg.findChildren<QCheckBox *>()) {
        if (box->text().contains(QStringLiteral("KI-Assistent")))
            aiEnabled = box;
    }
    CHECK(aiEnabled != nullptr);
    if (!aiEnabled)
        return;
    aiEnabled->setChecked(true);

    QLineEdit *urlEdit = nullptr;
    for (QLineEdit *edit : dlg.findChildren<QLineEdit *>()) {
        if (edit->text().startsWith(QStringLiteral("http")))
            urlEdit = edit;
    }
    CHECK(urlEdit != nullptr);
    if (!urlEdit)
        return;
    // Bewusst NICHT die Standardadresse: sonst liefert ollamaUrl() auch dann
    // den erwarteten Wert, wenn gar nichts gespeichert wurde — der Test wuerde
    // den Schluessel-Fehler nicht bemerken.
    urlEdit->setText(QStringLiteral("http://192.168.7.5:11434"));

    QComboBox *modelCombo = nullptr;
    for (QComboBox *box : dlg.findChildren<QComboBox *>()) {
        if (box->isEditable() && box->count() == 0)
            modelCombo = box;
    }
    CHECK(modelCombo != nullptr);
    if (!modelCombo)
        return;
    modelCombo->setCurrentText(QStringLiteral("qwen2.5-coder"));

    // Ueber den echten Speichern-Knopf gehen — nur so wird save() ausgefuehrt.
    auto *buttons = dlg.findChild<QDialogButtonBox *>();
    CHECK(buttons != nullptr);
    if (!buttons)
        return;
    QPushButton *saveButton = buttons->button(QDialogButtonBox::Save);
    CHECK(saveButton != nullptr);
    if (!saveButton)
        return;
    saveButton->click();

    // Regressionstest: der Dialog schrieb die Adresse frueher nach "ai_url",
    // waehrend core::ollamaUrl() "ollama_url" liest — die Einstellung war
    // dadurch wirkungslos. Beide Seiten muessen dieselben Schluessel nutzen.
    CHECK_EQ(core::aiEnabled(), true);
    CHECK_EQ(core::ollamaUrl(), QStringLiteral("http://192.168.7.5:11434"));
    CHECK_EQ(core::aiModel(), QStringLiteral("qwen2.5-coder"));
}

TEST(ai_chat_panel, settings_pull_combo_has_suggestions)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());
    gui::SettingsDialog dlg(nullptr, nullptr);

    // Die Download-Auswahl bringt Vorschlaege mit, bleibt aber editierbar und
    // ohne Vorauswahl.
    QComboBox *pull = nullptr;
    for (QComboBox *box : dlg.findChildren<QComboBox *>()) {
        if (box->findText(QStringLiteral("llama3.2")) >= 0)
            pull = box;
    }
    CHECK(pull != nullptr);
    if (!pull)
        return;
    CHECK(pull->findText(QStringLiteral("qwen2.5-coder:7b")) >= 0);
    CHECK(pull->isEditable());
    CHECK(pull->currentText().isEmpty());
}
