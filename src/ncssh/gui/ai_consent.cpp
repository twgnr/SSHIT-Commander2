#include "ncssh/gui/ai_consent.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"

#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>

namespace ncssh::gui {

using core::_t;

bool confirmCloudAi(const QString &provider, QWidget *parent)
{
    if (!core::isCloudProvider(provider))
        return true;
    const QString key = QString::fromLatin1(core::AI_CLOUD_CONSENT);
    QJsonObject consent = QJsonObject::fromVariantMap(core::getSetting(key).toMap());
    if (consent.value(provider).toBool())
        return true;

    QMessageBox box(QMessageBox::Question, _t("KI-Anbieter: Daten werden gesendet"),
                    _t("Die Anfrage geht an %1. Dabei verlassen Terminalausgaben bzw. "
                       "Dateiinhalte deinen Rechner und werden beim Anbieter verarbeitet.\n\n"
                       "Keine Passwörter, Schlüssel oder vertraulichen Daten senden.")
                        .arg(core::aiProviderName(provider)),
                    QMessageBox::NoButton, parent);
    auto *always = box.addButton(_t("Senden — nicht mehr fragen"), QMessageBox::AcceptRole);
    box.addButton(_t("Abbrechen"), QMessageBox::RejectRole);
    box.setDefaultButton(always);
    box.exec();
    if (box.clickedButton() != always)
        return false;
    consent.insert(provider, true);
    core::setSetting(key, consent);
    return true;
}

QString aiPrivacyHint(const QString &provider)
{
    if (!core::isCloudProvider(provider))
        return _t("Das Modell läuft lokal — Inhalte verlassen den Rechner nicht. "
                  "Der Assistent ist rein beratend und führt nichts aus.");
    return _t("Anbieter: %1 — Inhalte werden an den Anbieter gesendet. "
              "Der Assistent ist rein beratend und führt nichts aus.")
        .arg(core::aiProviderName(provider));
}

} // namespace ncssh::gui
