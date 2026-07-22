// Minimap fuer den Editor: stark verkleinerte Zeilen-Uebersicht des Dokuments
// mit sichtbarem Bereich und farbigen Suchtreffern; Klick springt hin.
// (Port von gui/minimap.py)
#pragma once

#include <QWidget>
#include <vector>

class QPlainTextEdit;

namespace ncssh::gui {

class Minimap : public QWidget {
    Q_OBJECT
public:
    explicit Minimap(QPlainTextEdit *editor, QWidget *parent = nullptr);

    // Zeilennummern (0-basiert), die als Treffer markiert werden.
    void setMatches(const std::vector<int> &lines);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void jumpTo(int y);

    QPlainTextEdit *m_editor;
    std::vector<int> m_matches;
    static constexpr int kLineHeight = 2;   // Pixel je Zeile
    static constexpr int kWidth = 90;
};

} // namespace ncssh::gui
