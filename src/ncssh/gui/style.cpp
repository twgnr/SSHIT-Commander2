#include "ncssh/gui/style.hpp"

#include "ncssh/core/settings.hpp"

#include <QApplication>
#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
#include <QVariantMap>

namespace ncssh::gui {

using core::getSetting;
using core::setSetting;

const QString &defaultTheme()
{
    static const QString name = QStringLiteral("Dunkel");
    return name;
}

const std::vector<ColorKey> &colorKeys()
{
    static const std::vector<ColorKey> keys = {
        {QStringLiteral("bg"), QStringLiteral("Hintergrund (Fenster)")},
        {QStringLiteral("surface"), QStringLiteral("Flächen / Panes")},
        {QStringLiteral("surface2"), QStringLiteral("Flächen 2 / Kopfzeilen")},
        {QStringLiteral("base"), QStringLiteral("Eingabefelder / Konsole")},
        {QStringLiteral("alt"), QStringLiteral("Wechselzeilen")},
        {QStringLiteral("border"), QStringLiteral("Rahmen")},
        {QStringLiteral("text"), QStringLiteral("Text")},
        {QStringLiteral("muted"), QStringLiteral("Text gedämpft")},
        {QStringLiteral("disabled"), QStringLiteral("Text deaktiviert")},
        {QStringLiteral("accent"), QStringLiteral("Akzent")},
        {QStringLiteral("accent2"), QStringLiteral("Akzent 2 / Auswahl")},
        {QStringLiteral("on_accent"), QStringLiteral("Text auf Akzent")},
        {QStringLiteral("hover"), QStringLiteral("Hover")},
        {QStringLiteral("scroll"), QStringLiteral("Scrollbalken")},
        {QStringLiteral("term_bg"), QStringLiteral("Terminal-Hintergrund")},
        {QStringLiteral("term_fg"), QStringLiteral("Terminal-Text")},
    };
    return keys;
}

static ThemeColors makeTheme(std::initializer_list<std::pair<const char *, const char *>> vals)
{
    ThemeColors t;
    for (const auto &[k, v] : vals)
        t.insert(QString::fromLatin1(k), QString::fromLatin1(v));
    return t;
}

const QHash<QString, ThemeColors> &builtinThemes()
{
    static const QHash<QString, ThemeColors> themes = {
        {QStringLiteral("Dunkel"), makeTheme({
            {"bg", "#16181f"}, {"surface", "#1e212b"}, {"surface2", "#262a36"},
            {"border", "#2e3340"}, {"text", "#e6e8ef"}, {"muted", "#8b90a0"},
            {"accent", "#4f8cff"}, {"accent2", "#3a6fd6"}, {"base", "#12141a"},
            {"hover", "#303542"}, {"alt", "#232633"}, {"scroll", "#3a3f4d"},
            {"on_accent", "#ffffff"}, {"term_bg", "#12141a"}, {"term_fg", "#d3d7cf"},
            {"disabled", "#565b6b"}})},
        {QStringLiteral("Mitternacht"), makeTheme({
            {"bg", "#0d1117"}, {"surface", "#161b22"}, {"surface2", "#1f2630"},
            {"border", "#30363d"}, {"text", "#e6edf3"}, {"muted", "#8b949e"},
            {"accent", "#2ea043"}, {"accent2", "#238636"}, {"base", "#0b0e13"},
            {"hover", "#21262d"}, {"alt", "#161b22"}, {"scroll", "#30363d"},
            {"on_accent", "#ffffff"}, {"term_bg", "#0b0e13"}, {"term_fg", "#c9d1d9"},
            {"disabled", "#4a525c"}})},
        {QStringLiteral("Hell"), makeTheme({
            {"bg", "#f3f4f6"}, {"surface", "#ffffff"}, {"surface2", "#e9ecf1"},
            {"border", "#d0d4dc"}, {"text", "#1b1f27"}, {"muted", "#6b7280"},
            {"accent", "#2563eb"}, {"accent2", "#1d4ed8"}, {"base", "#ffffff"},
            {"hover", "#dfe3ea"}, {"alt", "#f0f2f6"}, {"scroll", "#c2c8d2"},
            {"on_accent", "#ffffff"}, {"term_bg", "#fbfbfd"}, {"term_fg", "#1b1f27"},
            {"disabled", "#aab0bb"}})},
        {QStringLiteral("Hoher Kontrast"), makeTheme({
            {"bg", "#000000"}, {"surface", "#000000"}, {"surface2", "#101010"},
            {"border", "#ffffff"}, {"text", "#ffffff"}, {"muted", "#cfcfcf"},
            {"accent", "#ffd400"}, {"accent2", "#00e5ff"}, {"base", "#000000"},
            {"hover", "#1f1f1f"}, {"alt", "#0a0a0a"}, {"scroll", "#bdbdbd"},
            {"on_accent", "#000000"}, {"term_bg", "#000000"}, {"term_fg", "#ffffff"},
            {"disabled", "#7a7a7a"}})},
    };
    return themes;
}

static ThemeColors g_currentTerm = makeTheme({{"bg", "#12141a"}, {"fg", "#d3d7cf"}});

std::pair<QString, QString> terminalColors()
{
    return {g_currentTerm.value(QStringLiteral("bg")), g_currentTerm.value(QStringLiteral("fg"))};
}

QHash<QString, ThemeColors> customThemes()
{
    QHash<QString, ThemeColors> out;
    const QVariantMap raw = getSetting(QStringLiteral("custom_themes"), QVariantMap{}).toMap();
    for (auto it = raw.begin(); it != raw.end(); ++it) {
        const QVariantMap colors = it.value().toMap();
        ThemeColors tc;
        for (auto c = colors.begin(); c != colors.end(); ++c)
            tc.insert(c.key(), c.value().toString());
        out.insert(it.key(), tc);
    }
    return out;
}

QHash<QString, ThemeColors> allThemes()
{
    QHash<QString, ThemeColors> merged = builtinThemes();
    const auto custom = customThemes();
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        if (!builtinThemes().contains(it.key()))
            merged.insert(it.key(), it.value());
    }
    return merged;
}

bool isBuiltin(const QString &name)
{
    return builtinThemes().contains(name);
}

// Erlaubte Farbwerte: Hex oder einfacher Farbname (verhindert QSS-Injektion).
static QString safeColor(const QString &value, const QString &fallback)
{
    static const QRegularExpression re(
        QStringLiteral("^#(?:[0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$|^[a-zA-Z]{1,24}$"));
    const QString v = value.trimmed();
    return re.match(v).hasMatch() ? v : fallback;
}

static ThemeColors complete(const ThemeColors &colors)
{
    ThemeColors base = builtinThemes().value(defaultTheme());
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        if (base.contains(it.key()) && !it.value().isEmpty())
            base.insert(it.key(), safeColor(it.value(), base.value(it.key())));
    }
    return base;
}

ThemeColors themeColors(const QString &name)
{
    const auto themes = allThemes();
    return complete(themes.value(name, builtinThemes().value(defaultTheme())));
}

void saveCustomTheme(const QString &name, const ThemeColors &colors)
{
    QVariantMap data = getSetting(QStringLiteral("custom_themes"), QVariantMap{}).toMap();
    QJsonObject entry;
    for (const auto &ck : colorKeys())
        entry.insert(ck.key, colors.value(ck.key));
    QJsonObject root = QJsonObject::fromVariantMap(data);
    root.insert(name, entry);
    setSetting(QStringLiteral("custom_themes"), root);
}

void deleteCustomTheme(const QString &name)
{
    QVariantMap data = getSetting(QStringLiteral("custom_themes"), QVariantMap{}).toMap();
    if (data.contains(name)) {
        data.remove(name);
        setSetting(QStringLiteral("custom_themes"), QJsonObject::fromVariantMap(data));
    }
}

QStringList themeNames()
{
    QStringList names;
    for (const auto &n : {QStringLiteral("Dunkel"), QStringLiteral("Mitternacht"),
                          QStringLiteral("Hell"), QStringLiteral("Hoher Kontrast")})
        names << n;
    const auto custom = customThemes();
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        if (!names.contains(it.key()))
            names << it.key();
    }
    return names;
}

static QString buildStylesheet(const ThemeColors &c)
{
    const auto v = [&](const char *k) { return c.value(QString::fromLatin1(k)); };
    return QStringLiteral(R"QSS(
* { font-family: "Segoe UI", "Inter", sans-serif; font-size: 13px; color: %text%; }
QMainWindow, QDialog, QWidget { background: %bg%; }
QToolTip { background: %surface2%; color: %text%; border: 1px solid %border%; padding: 4px 7px; border-radius: 4px; }

#Pane { background: %surface%; border: 1px solid %border%; border-radius: 10px; }
#Pane[active="true"] { border: 1px solid %accent%; }
#PaneHeader { background: %surface2%; color: %text%; font-weight: 600; padding: 6px 10px; border-radius: 6px; }

#ConsolePanel { background: %surface%; border: 1px solid %border%; border-radius: 10px; }
#ConsolePanel[active="true"] { border: 1px solid %accent%; }
#ConsolePanel[venv="true"] { border: 1px solid #3fb950; }
#FloatingConsole[venv="true"] { border: 2px solid #3fb950; }
#VenvBadge { color: #3fb950; font-weight: 600; padding: 1px 6px; border: 1px solid #3fb950; border-radius: 6px; }
#FloatingConsole { border: 2px solid transparent; }
#FloatingConsole[active="true"] { border: 2px solid %accent%; }
#ConsoleHeader { background: %surface2%; border-radius: 6px; }
#ConsoleHeaderTitle { font-weight: 600; padding: 4px 8px; }
QPlainTextEdit#ConsoleOutput { background: %base%; border: 1px solid %border%; border-radius: 8px; padding: 6px; selection-background-color: %accent2%; }

QLineEdit, QPlainTextEdit, QComboBox, QSpinBox { background: %base%; border: 1px solid %border%; border-radius: 8px; padding: 6px 8px; selection-background-color: %accent2%; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border: 1px solid %accent%; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 22px; }
QComboBox::down-arrow { width: 0; height: 0; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid %muted%; margin-right: 8px; }
QComboBox::down-arrow:disabled { border-top: 6px solid %disabled%; }
QComboBox QAbstractItemView { background: %surface2%; border: 1px solid %border%; selection-background-color: %accent2%; outline: none; }

QPushButton { background: %surface2%; border: 1px solid %border%; border-radius: 8px; padding: 6px 14px; }
QPushButton:hover { background: %hover%; border-color: %accent2%; }
QPushButton:pressed { background: %accent2%; }
QPushButton:default { background: %accent%; border-color: %accent%; color: %on_accent%; }
QPushButton:default:hover { background: %accent2%; }
QPushButton:disabled { background: %surface%; border-color: %border%; color: %disabled%; }
QPushButton#Chip { padding: 3px 10px; border-radius: 12px; font-size: 12px; }
QPushButton#Chip:checked { background: %accent2%; border-color: %accent%; color: %on_accent%; }
QPushButton#Chip::menu-indicator { image: none; width: 0; }
QToolButton#TabPlus { font-size: 18px; font-weight: bold; color: %muted%; border: none; border-radius: 5px; }
QToolButton#TabPlus:hover { background: %hover%; color: %accent%; }

QTableWidget, QListWidget { background: %surface%; border: 1px solid %border%; border-radius: 8px; gridline-color: %border%; outline: none; alternate-background-color: %alt%; }
QTableWidget::item, QListWidget::item { padding: 4px 6px; }
QTableWidget::item:selected, QListWidget::item:selected { background: %accent2%; color: %on_accent%; }
QHeaderView::section { background: %surface2%; color: %muted%; border: none; border-bottom: 1px solid %border%; padding: 6px 8px; font-weight: 600; }

QToolBar { background: %surface%; border: none; border-bottom: 1px solid %border%; spacing: 4px; padding: 4px; }
QToolBar QToolButton { background: transparent; border-radius: 8px; padding: 6px 12px; color: %text%; }
QToolBar QToolButton:hover { background: %surface2%; }
QToolBar QToolButton:pressed { background: %accent2%; }
QToolBar QToolButton:disabled { color: %disabled%; background: transparent; }
QMenuBar { background: %bg%; }
QMenuBar::item:selected { background: %surface2%; }
QMenuBar::item:disabled { color: %disabled%; }
QMenu { background: %surface2%; border: 1px solid %border%; }
QMenu::item:selected { background: %accent2%; }
QMenu::item:disabled { color: %disabled%; }
QStatusBar { background: %surface%; color: %muted%; border-top: 1px solid %border%; }

QTabBar::tab { background: %surface%; color: %muted%; border: 1px solid %border%; padding: 6px 14px; border-top-left-radius: 8px; border-top-right-radius: 8px; }
QTabBar::tab:selected { background: %surface2%; color: %text%; border-bottom-color: %accent%; }
QTabWidget::pane { border: 1px solid %border%; border-radius: 8px; }

QSplitter::handle { background: %border%; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:hover { background: %accent2%; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: %scroll%; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: %accent2%; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal { background: %scroll%; border-radius: 5px; min-width: 30px; }
QScrollBar::handle:horizontal:hover { background: %accent2%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QLabel#Muted { color: %muted%; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %border%; background: %base%; }
QCheckBox::indicator:checked { background: %accent%; border-color: %accent%; }
)QSS")
        .replace(QStringLiteral("%text%"), v("text"))
        .replace(QStringLiteral("%bg%"), v("bg"))
        .replace(QStringLiteral("%surface2%"), v("surface2"))
        .replace(QStringLiteral("%surface%"), v("surface"))
        .replace(QStringLiteral("%border%"), v("border"))
        .replace(QStringLiteral("%base%"), v("base"))
        .replace(QStringLiteral("%accent2%"), v("accent2"))
        .replace(QStringLiteral("%accent%"), v("accent"))
        .replace(QStringLiteral("%muted%"), v("muted"))
        .replace(QStringLiteral("%disabled%"), v("disabled"))
        .replace(QStringLiteral("%hover%"), v("hover"))
        .replace(QStringLiteral("%on_accent%"), v("on_accent"))
        .replace(QStringLiteral("%alt%"), v("alt"))
        .replace(QStringLiteral("%scroll%"), v("scroll"));
}

QString stylesheetFor(const ThemeColors &colors)
{
    return buildStylesheet(complete(colors));
}

void applyTheme(QApplication *app, const QString &name)
{
    const ThemeColors c = complete(allThemes().value(name, builtinThemes().value(defaultTheme())));
    g_currentTerm.insert(QStringLiteral("bg"), c.value(QStringLiteral("term_bg")));
    g_currentTerm.insert(QStringLiteral("fg"), c.value(QStringLiteral("term_fg")));
    app->setStyle(QStringLiteral("Fusion"));
    QPalette pal;
    const auto col = [&](const char *k) { return QColor(c.value(QString::fromLatin1(k))); };
    pal.setColor(QPalette::Window, col("bg"));
    pal.setColor(QPalette::WindowText, col("text"));
    pal.setColor(QPalette::Base, col("base"));
    pal.setColor(QPalette::AlternateBase, col("alt"));
    pal.setColor(QPalette::Text, col("text"));
    pal.setColor(QPalette::Button, col("surface2"));
    pal.setColor(QPalette::ButtonText, col("text"));
    pal.setColor(QPalette::Highlight, col("accent2"));
    pal.setColor(QPalette::HighlightedText, col("on_accent"));
    pal.setColor(QPalette::ToolTipBase, col("surface2"));
    pal.setColor(QPalette::ToolTipText, col("text"));
    pal.setColor(QPalette::PlaceholderText, col("muted"));
    const QColor dis = col("disabled");
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, dis);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, dis);
    pal.setColor(QPalette::Disabled, QPalette::Text, dis);
    app->setPalette(pal);
    app->setStyleSheet(buildStylesheet(c));
}

} // namespace ncssh::gui
