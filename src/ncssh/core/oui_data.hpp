// MAC-Hersteller-Zuordnung (OUI) - kuratierte, hochsichere Teilliste.
//
// Die ersten drei MAC-Oktette (OUI) bestimmen den Hersteller. Hinterlegt sind
// v.a. Virtualisierungs-Vendoren und gaengige Geraete. Optional wird zusaetzlich
// eine vollstaendige Liste aus assets/oui.csv geladen (Format "AABBCC,Hersteller"
// oder "AA:BB:CC,Hersteller"), falls vorhanden.
#pragma once

#include <QString>

namespace ncssh::core {

// Hersteller zur MAC; "(lokal/zufaellig)" bei lokal verwalteter MAC,
// sonst "" wenn unbekannt.
QString ouiVendor(const QString &mac);

} // namespace ncssh::core
