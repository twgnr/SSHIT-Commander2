// Merkt sich die Ansicht aller eigenen Fenster (Dialoge, Editor, KI-Fenster …):
// Fenstergroesse und je Tabelle/Baumliste den Kopfzeilen-Zustand (Spalten-
// breiten, -reihenfolge, ausgeblendete Spalten, Sortierung). Beim naechsten
// Oeffnen sieht das Fenster genau so aus wie beim letzten Schliessen.
//
// Zentral per Event-Filter auf der QApplication — einzelne Dialoge muessen
// dafuer nichts tun. Ausgenommen: das Hauptfenster (eigene Sitzungs-
// speicherung) und die Datei-Panes (merken ihre Spalten selbst).
#pragma once

#include <QObject>

class QWidget;

namespace ncssh::gui {

class ViewStateKeeper : public QObject {
    Q_OBJECT
public:
    explicit ViewStateKeeper(QObject *parent = nullptr);

    // Filter an der Anwendung anmelden (einmal beim Start).
    static void install();

    // Einzeln nutzbar (und testbar): Zustand eines Fensters sichern/anwenden.
    static void save(QWidget *window);
    static void restore(QWidget *window);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

} // namespace ncssh::gui
