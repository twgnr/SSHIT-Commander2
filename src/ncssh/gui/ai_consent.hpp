// Einmalige Rueckfrage, bevor Inhalte an einen Cloud-KI-Anbieter gehen.
// Lokales Ollama fragt nie. Die Zustimmung gilt je Anbieter dauerhaft und
// laesst sich in den Einstellungen (KI) wieder zuruecknehmen.
#pragma once

#include <QString>

class QWidget;

namespace ncssh::gui {

// true = senden erlaubt (lokal, bereits zugestimmt oder jetzt zugestimmt).
bool confirmCloudAi(const QString &provider, QWidget *parent);

// Hinweistext fuer KI-Fenster: lokal vs. Cloud-Anbieter.
QString aiPrivacyHint(const QString &provider);

} // namespace ncssh::gui
