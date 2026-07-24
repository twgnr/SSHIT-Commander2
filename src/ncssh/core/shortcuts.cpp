#include "ncssh/core/shortcuts.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"

#include <QJsonObject>
#include <QVariantMap>

namespace ncssh::core {

const std::vector<ShortcutDef> &shortcutDefs()
{
    // Lazy aufgebaut, damit _t() die beim Start gesetzte Sprache nutzt.
    static const std::vector<ShortcutDef> defs = {
        {QStringLiteral("connect"), _t("Aktionen"), _t("SSH verbinden"), QStringLiteral("F9")},
        {QStringLiteral("palette"), _t("Aktionen"), _t("Befehlspalette"), QStringLiteral("Ctrl+P")},
        {QStringLiteral("history"), _t("Aktionen"), _t("Verlauf & Favoriten"), QStringLiteral("Ctrl+H")},
        {QStringLiteral("transfers"), _t("Aktionen"), _t("Übertragungen"), QStringLiteral("Ctrl+T")},
        {QStringLiteral("tunnels"), _t("Aktionen"), _t("SSH-Tunnel"), QStringLiteral("Ctrl+Shift+T")},
        {QStringLiteral("sftp_batch"), _t("Aktionen"), _t("SFTP-Batch / geplante Aufgaben"),
         QStringLiteral("Ctrl+Shift+B")},
        {QStringLiteral("reload"), _t("Aktionen"), _t("Neu laden"), QStringLiteral("Ctrl+R")},

        {QStringLiteral("view"), _t("Dateien"), _t("Ansehen"), QStringLiteral("F3")},
        {QStringLiteral("edit"), _t("Dateien"), _t("Bearbeiten"), QStringLiteral("F4")},
        {QStringLiteral("copy"), _t("Dateien"), _t("Kopieren"), QStringLiteral("F5")},
        {QStringLiteral("rename"), _t("Dateien"), _t("Umbenennen"), QStringLiteral("F6")},
        {QStringLiteral("mkdir"), _t("Dateien"), _t("Neuer Ordner"), QStringLiteral("F7")},
        {QStringLiteral("delete"), _t("Dateien"), _t("Löschen"), QStringLiteral("F8")},
        {QStringLiteral("bulk_rename"), _t("Dateien"), _t("Massen-Umbenennen"), QStringLiteral("Ctrl+Shift+R")},

        {QStringLiteral("search_name"), _t("Werkzeuge"), _t("Datei-Suche (Name)"), QStringLiteral("Ctrl+Shift+F")},
        {QStringLiteral("search_content"), _t("Werkzeuge"), _t("Inhalts-Suche (grep)"), QStringLiteral("Ctrl+Alt+F")},
        {QStringLiteral("dir_diff"), _t("Werkzeuge"), _t("Verzeichnis-Vergleich"), QStringLiteral("Ctrl+D")},
        {QStringLiteral("file_diff"), _t("Werkzeuge"), _t("Datei-Vergleich"), QStringLiteral("Ctrl+Shift+D")},
        {QStringLiteral("encoding_convert"), _t("Werkzeuge"), _t("Datei-Encoding konvertieren"), QString()},
        {QStringLiteral("venv_setup"), _t("Werkzeuge"), _t("venv verwalten"), QString()},
        {QStringLiteral("hidden"), _t("Werkzeuge"), _t("Versteckte Dateien"), QStringLiteral("Ctrl+.")},
        {QStringLiteral("sync_panes"), _t("Werkzeuge"), _t("Panes synchronisieren"), QStringLiteral("Ctrl+E")},
        {QStringLiteral("swap_panes"), _t("Werkzeuge"), _t("Panes tauschen"), QStringLiteral("Ctrl+U")},
        {QStringLiteral("pane_status"), _t("Werkzeuge"), _t("Status / Lesezeichen (Auswahl)"), QStringLiteral("Ctrl+F9")},

        {QStringLiteral("new_tab"), _t("Tabs & App"), _t("Neuer Tab"), QStringLiteral("Ctrl+Shift+N")},
        {QStringLiteral("rename_tab"), _t("Tabs & App"), _t("Tab umbenennen"), QStringLiteral("Ctrl+Shift+E")},
        {QStringLiteral("close_tab"), _t("Tabs & App"), _t("Tab schließen"), QStringLiteral("Ctrl+W")},
        {QStringLiteral("settings"), _t("Tabs & App"), _t("Einstellungen"), QStringLiteral("Ctrl+,")},
        {QStringLiteral("help"), _t("Tabs & App"), _t("Tastenkürzel-Hilfe"), QStringLiteral("F1")},
    };
    return defs;
}

QStringList groupOrder()
{
    return {_t("Aktionen"), _t("Dateien"), _t("Werkzeuge"), _t("Tabs & App")};
}

QHash<QString, QString> defaultShortcuts()
{
    QHash<QString, QString> out;
    for (const auto &d : shortcutDefs())
        out.insert(d.id, d.key);
    return out;
}

QHash<QString, QString> getShortcuts()
{
    QHash<QString, QString> merged = defaultShortcuts();
    const QVariantMap overrides =
        getSetting(QStringLiteral("shortcuts"), QVariantMap{}).toMap();
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (merged.contains(it.key()) && it.value().canConvert<QString>())
            merged.insert(it.key(), it.value().toString());
    }
    return merged;
}

void saveShortcuts(const QHash<QString, QString> &mapping)
{
    const QHash<QString, QString> defaults = defaultShortcuts();
    QJsonObject overrides;
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        if (defaults.contains(it.key()) && it.value() != defaults.value(it.key()))
            overrides.insert(it.key(), it.value());
    }
    setSetting(QStringLiteral("shortcuts"), overrides);
}

QString labelFor(const QString &sid)
{
    for (const auto &d : shortcutDefs()) {
        if (d.id == sid)
            return d.label;
    }
    return sid;
}

} // namespace ncssh::core
