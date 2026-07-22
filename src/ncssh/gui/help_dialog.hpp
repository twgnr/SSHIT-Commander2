// Hilfe: Tastenkuerzel-Uebersicht (aus den konfigurierten Kuerzeln) und
// Bedienungs-Handbuch mit Themenliste und Volltextsuche.
// (Port von gui/help_dialog.py + user_guide_dialog.py)
#pragma once

#include <QDialog>

class QTreeWidget;
class QTextBrowser;
class QLineEdit;
class QListWidget;

namespace ncssh::gui {

class HelpDialog : public QDialog {
    Q_OBJECT
public:
    // startTab: 0 = Tastenkuerzel, 1 = Handbuch
    explicit HelpDialog(int startTab = 0, QWidget *parent = nullptr);

private:
    QWidget *buildShortcutsTab();
    QWidget *buildGuideTab();
    void showTopic(int index);
    void filterTopics(const QString &needle);

    QTreeWidget *m_shortcuts = nullptr;
    QListWidget *m_topics = nullptr;
    QTextBrowser *m_guide = nullptr;
    QLineEdit *m_search = nullptr;
};

} // namespace ncssh::gui
