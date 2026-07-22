#include "ncssh/gui/help_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/markdown.hpp"
#include "ncssh/core/shortcuts.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <vector>

namespace ncssh::gui {

using core::_t;

namespace {
struct Topic {
    QString title;
    QString body;   // Markdown
};

// Handbuch-Themen (feldgenaue Bedienungsdoku wie im Original).
const std::vector<Topic> &topics()
{
    static const std::vector<Topic> list = {
        {_t("Überblick"),
         _t("**SSHIT-Commander** ist ein Dual-Pane-Dateimanager mit integriertem "
            "SSH/SFTP-Terminal.\n\n"
            "- Links und rechts je eine **Pane** — lokal oder remote, gleiche Bedienung.\n"
            "- Unter jeder Pane eine **Konsole** mit zwei Modi: *Befehle* und *Terminal*.\n"
            "- Beliebig viele **Tabs**, jeder mit eigener Verbindung.")},
        {_t("Verbinden"),
         _t("`F9` bzw. *Aktionen → SSH verbinden* öffnet die **Server-Profile**.\n\n"
            "- **Auth-Methode**: `key` (Schlüsseldatei, auch PuTTY-PPK), `password` oder "
            "`agent` (Pageant/OpenSSH-Agent).\n"
            "- **Host-Key-Richtlinie**: `accept-new` (Trust-on-First-Use), `strict` "
            "(unbekannter Key = Abbruch) oder `ignore`.\n"
            "- Bei unbekanntem Key wird der Fingerprint angezeigt und auf Wunsch gespeichert.\n"
            "- **Import** übernimmt Sitzungen aus PuTTY, WinSCP und `~/.ssh/config`.")},
        {_t("Dateien & Panes"),
         _t("- **Navigieren**: Doppelklick, `Backspace` (hoch), Pfad direkt eintippen.\n"
            "- `F3` Ansehen · `F4` Bearbeiten · `F5` Übertragen · `F6` Umbenennen · "
            "`F7` Neuer Ordner · `F8` Löschen.\n"
            "- **Lesezeichen**: ☆ merkt den aktuellen Pfad, ▾ öffnet die Liste — "
            "getrennt je Verbindung.\n"
            "- **Eigenschaften** (Kontextmenü) zeigt Größe/Eigner/Datum und den "
            "**chmod-Editor** mit rwx-Checkboxen und Oktal-Anzeige.")},
        {_t("Konsole & Terminal"),
         _t("- Modus **Befehle**: Befehl → Ausgabe, mit `↑`/`↓` durch die Historie, "
            "`Strg+C` bricht ab. Ein `cd` synchronisiert die Pane.\n"
            "- Modus **Terminal**: echter PTY-Shell-Channel (lokal via ConPTY, remote "
            "via SSH) — `top`, `vim`, Farben, Tab-Completion funktionieren.\n"
            "- `Strg+Shift+C` / `Strg+Shift+V` kopiert/fügt im Terminal ein.\n"
            "- Der **KI**-Button erklärt die letzte Ausgabe (lokales Modell).")},
        {_t("Übertragungen"),
         _t("`F5` reiht eine Übertragung in die **Queue** ein; `Strg+T` öffnet sie.\n\n"
            "Die Queue zeigt Fortschritt, Tempo und ETA, verifiziert die Zielgröße (✓) "
            "und erlaubt **Abbrechen** und **Wiederholen**.")},
        {_t("Werkzeuge"),
         _t("- **Befehlspalette** (`Strg+P`): Katalog mit OS-Filter; der **Assistent** "
            "füllt Parameter mit Live-Vorschau (optional `sudo`).\n"
            "- **Suche**: `Strg+Shift+F` nach Namen, `Strg+Alt+F` nach Inhalten (grep).\n"
            "- **Massen-Umbenennen** (`Strg+Shift+R`): Suchen/Ersetzen, Nummerierung, "
            "Präfix/Suffix — mit Vorschau und Konfliktauflösung.\n"
            "- **Vergleich**: `Strg+D` Verzeichnisse, `Strg+Shift+D` zwei Dateien.\n"
            "- Außerdem: Encoding-Konverter, venv-Verwaltung, Netzwerk-Scanner, "
            "Sicherheits-Audit (CVE) und Plugins.")},
        {_t("SSH-Tunnel"),
         _t("`Strg+Shift+T` öffnet Port-Weiterleitungen:\n\n"
            "- **Lokal (-L)**: ein lokaler Port wird auf ein Ziel hinter dem Server geleitet.\n"
            "- **Remote (-R)**: ein Port auf dem Server zeigt auf ein lokales Ziel.\n"
            "- **Dynamisch (-D)**: SOCKS5-Proxy über die SSH-Verbindung.")},
        {_t("Einstellungen"),
         _t("`Strg+,` öffnet die Einstellungen:\n\n"
            "- **Allgemein**: Sprache, Theme, Schriftgrößen, Datumsformat, Startpfad.\n"
            "- **KI**: Ollama-Adresse testen und Modell wählen — das Modell läuft lokal.\n"
            "- **Tastenkürzel**: frei konfigurierbar, mit Dublettenprüfung.\n\n"
            "Sprache und Schriftgrößen greifen nach einem Neustart.")},
        {_t("Sicherheit"),
         _t("- Passwörter und Passphrasen liegen im **Windows Credential Manager**, "
            "nie im Klartext.\n"
            "- Der **Host-Key wird vor der Authentifizierung geprüft** — bei "
            "Abweichung wird kein Passwort gesendet.\n"
            "- Der KI-Assistent ist **rein beratend** und führt nichts aus; die Inhalte "
            "verlassen den Rechner nicht.")},
    };
    return list;
}
} // namespace

HelpDialog::HelpDialog(int startTab, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(_t("Hilfe"));
    resize(900, 640);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildShortcutsTab(), _t("Tastenkürzel"));
    tabs->addTab(buildGuideTab(), _t("Handbuch"));
    tabs->setCurrentIndex(startTab);
    layout->addWidget(tabs, 1);

    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);
}

QWidget *HelpDialog::buildShortcutsTab()
{
    m_shortcuts = new QTreeWidget(this);
    m_shortcuts->setHeaderLabels({_t("Aktion"), _t("Kürzel")});
    m_shortcuts->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcuts->setAlternatingRowColors(true);

    const QHash<QString, QString> current = core::getShortcuts();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const QString &group : core::groupOrder()) {
        auto *item = new QTreeWidgetItem(m_shortcuts, {group});
        item->setExpanded(true);
        groups.insert(group, item);
    }
    for (const core::ShortcutDef &def : core::shortcutDefs()) {
        QTreeWidgetItem *parent = groups.value(def.group, nullptr);
        if (!parent) {
            parent = new QTreeWidgetItem(m_shortcuts, {def.group});
            parent->setExpanded(true);
            groups.insert(def.group, parent);
        }
        new QTreeWidgetItem(parent, {def.label, current.value(def.id)});
    }
    return m_shortcuts;
}

QWidget *HelpDialog::buildGuideTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    m_search = new QLineEdit(page);
    m_search->setPlaceholderText(_t("Themen durchsuchen …"));
    connect(m_search, &QLineEdit::textChanged, this, &HelpDialog::filterTopics);
    layout->addWidget(m_search);

    auto *splitter = new QSplitter(Qt::Horizontal, page);
    m_topics = new QListWidget(splitter);
    connect(m_topics, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        showTopic(m_topics->item(row)->data(Qt::UserRole).toInt());
    });
    m_guide = new QTextBrowser(splitter);
    m_guide->setOpenExternalLinks(true);
    splitter->addWidget(m_topics);
    splitter->addWidget(m_guide);
    splitter->setSizes({230, 640});
    layout->addWidget(splitter, 1);

    filterTopics(QString());
    if (m_topics->count() > 0)
        m_topics->setCurrentRow(0);
    return page;
}

void HelpDialog::filterTopics(const QString &needle)
{
    m_topics->clear();
    const QString lower = needle.trimmed().toLower();
    const auto &all = topics();
    for (int i = 0; i < int(all.size()); ++i) {
        if (!lower.isEmpty()
            && !all[i].title.toLower().contains(lower)
            && !all[i].body.toLower().contains(lower))
            continue;
        auto *item = new QListWidgetItem(all[i].title, m_topics);
        item->setData(Qt::UserRole, i);
    }
    if (m_topics->count() > 0)
        m_topics->setCurrentRow(0);
    else
        m_guide->setHtml(QStringLiteral("<i>%1</i>").arg(_t("Kein Treffer.")));
}

void HelpDialog::showTopic(int index)
{
    const auto &all = topics();
    if (index < 0 || index >= int(all.size()))
        return;
    m_guide->setHtml(QStringLiteral("<h2>%1</h2>%2")
                         .arg(all[index].title.toHtmlEscaped(),
                              core::mdToHtml(all[index].body)));
}

} // namespace ncssh::gui
