// Tab-Pfadvervollstaendigung fuer ein QLineEdit — wie in einer Shell: das Wort
// vor dem Cursor wird gegen das Dateisystem (lokal oder SFTP) ergaenzt, bei
// mehreren Treffern schaltet jedes weitere Tab zum naechsten (nach dem
// letzten wieder der erste). Genutzt von der Konsolen-Eingabezeile und den
// Feldern des Parameter-Fensters.
#pragma once

#include "ncssh/gui/bridge.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

class QLineEdit;

namespace ncssh::core { class FileSystemProvider; }

namespace ncssh::gui {

class LineCompleter : public QObject {
    Q_OBJECT
public:
    // handleTab: Tab-Taste im Feld selbst abfangen (sonst ruft der Besitzer
    // complete() aus seinem eigenen Event-Filter).
    LineCompleter(AsyncBridge *bridge, QLineEdit *edit, bool handleTab);

    void setProvider(core::FileSystemProvider *provider) { m_provider = provider; }
    core::FileSystemProvider *provider() const { return m_provider; }
    // Basisverzeichnis fuer relative Pfade.
    void setCwd(const QString &cwd) { m_cwd = cwd; }

    void complete();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    AsyncBridge *m_bridge;
    QLineEdit *m_edit;
    core::FileSystemProvider *m_provider = nullptr;
    QString m_cwd;
    // Tab-Durchschalten: Zeilen (Text vor dem Cursor) je Kandidat, aktueller
    // Index und der Stand, den das letzte Tab hinterlassen hat — weicht die
    // Eingabe davon ab, beginnt das naechste Tab eine neue Vervollstaendigung.
    QStringList m_tabCycle;
    int m_tabCycleIndex = 0;
    QString m_tabCycleAfter;
    QString m_tabCycleText;
    int m_tabCyclePos = -1;
};

} // namespace ncssh::gui
