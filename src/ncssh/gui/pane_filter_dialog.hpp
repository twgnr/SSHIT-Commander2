// Filter-Dialog einer Pane: was angezeigt wird (Typ, Name, Regex, Endungen,
// Datum, Groesse) und mehrstufige Sortierung (z. B. erst Name, dann Datum).
#pragma once

#include "ncssh/core/panefilter.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <vector>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace ncssh::gui {

class PaneFilterDialog : public QDialog {
    Q_OBJECT
public:
    // columns: sortierbare Spalten als (Kennung, Beschriftung).
    PaneFilterDialog(const core::PaneFilter &filter, const QList<core::SortKey> &sortKeys,
                     bool dirsFirst, const QList<QPair<QString, QString>> &columns,
                     QWidget *parent = nullptr);

    core::PaneFilter filter() const;
    QList<core::SortKey> sortKeys() const;
    bool dirsFirst() const;

signals:
    void applied();   // "Anwenden": Einstellungen sofort in der Pane zeigen

private:
    void load(const core::PaneFilter &filter, const QList<core::SortKey> &sortKeys,
              bool dirsFirst);
    void updateState();   // Datumsfelder umschalten, Fehler anzeigen

    QComboBox *m_kind = nullptr;
    QLineEdit *m_names = nullptr;
    QLineEdit *m_starts = nullptr;
    QLineEdit *m_ends = nullptr;
    QLineEdit *m_regex = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QLineEdit *m_extensions = nullptr;
    QComboBox *m_dateField = nullptr;
    QComboBox *m_dateMode = nullptr;
    QStackedWidget *m_dateStack = nullptr;
    QDateTimeEdit *m_from = nullptr;
    QDateTimeEdit *m_to = nullptr;
    QSpinBox *m_amount = nullptr;
    QComboBox *m_unit = nullptr;
    QCheckBox *m_minOn = nullptr;
    QDoubleSpinBox *m_min = nullptr;
    QComboBox *m_minUnit = nullptr;
    QCheckBox *m_maxOn = nullptr;
    QDoubleSpinBox *m_max = nullptr;
    QComboBox *m_maxUnit = nullptr;
    QCheckBox *m_applyToDirs = nullptr;

    struct SortRow {
        QComboBox *column = nullptr;
        QComboBox *direction = nullptr;
    };
    std::vector<SortRow> m_sortRows;
    QCheckBox *m_dirsFirst = nullptr;

    QLabel *m_error = nullptr;
    QPushButton *m_ok = nullptr;
    QPushButton *m_apply = nullptr;
};

} // namespace ncssh::gui
