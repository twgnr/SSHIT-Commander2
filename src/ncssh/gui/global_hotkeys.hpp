// Systemweite Tastenkuerzel (Windows: RegisterHotKey).
//
// Die Kuerzel gelten auch dann, wenn ein anderes Programm vorn ist — genau
// dafuer sind die "globalen Kuerzel" der Makro-Tasten da. Gemeldet wird ueber
// ein eigenes Nur-Nachrichten-Fenster: WM_HOTKEY kommt damit auch waehrend
// nativer Modal-Schleifen (Fenster ziehen, Systemdialoge) an, die an Qts
// Ereignisverteiler vorbeilaufen. Andere Plattformen: add() liefert false.
#pragma once

#include <QKeySequence>
#include <QObject>
#include <QVector>
#include <optional>
#include <utility>

namespace ncssh::gui {

class GlobalHotkeys : public QObject {
    Q_OBJECT
public:
    explicit GlobalHotkeys(QObject *parent = nullptr);
    ~GlobalHotkeys() override;

    // Registriert die erste Tastenkombination von seq unter id. false, wenn
    // sie sich nicht abbilden laesst oder schon belegt ist (auch von uns).
    bool add(int id, const QKeySequence &seq);
    void clear();   // alle Registrierungen aufheben

    // (Modifikatoren MOD_*, virtueller Tastencode) — nullopt, wenn die
    // Kombination keine Windows-Taste ergibt. Ohne MOD_NOREPEAT.
    static std::optional<std::pair<unsigned, unsigned>> toNative(const QKeySequence &seq);
    // Haelt der Nutzer gerade Strg/Umschalt/Alt/Win? Ein Makro, das noch bei
    // gedrueckter Ausloese-Kombination tippt, liefe sonst als Strg+<Taste>.
    static bool modifiersHeld();

    quintptr nativeHandle() const { return m_hwnd; }   // fuer Tests

signals:
    void activated(int id);

private:
    quintptr m_hwnd = 0;
    QVector<int> m_ids;
};

} // namespace ncssh::gui
