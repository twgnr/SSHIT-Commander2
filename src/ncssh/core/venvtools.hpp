// Hilfen rund um Python-virtuelle-Umgebungen (venv/pipenv).
// (Port von core/venvtools.py)
//
// Erzeugt die Shell-Befehle zum Anlegen/Aktivieren einer venv und findet
// bekannte Umgebungen auf der Platte. Keine GUI, kein Qt-Widgets.
#pragma once

#include <QString>
#include <optional>
#include <utility>
#include <vector>

namespace ncssh::core {

// Eine gefundene virtuelle Umgebung (Ergebnis von discover()).
struct VenvInfo {
    QString name;
    QString path;
    QString version;
    QString kind;      // "venv" | "pipenv"
    QString project;
    QString info;
};

// Installations-Befehl (nach Aktivierung) passend zum Projekt; "" wenn nichts passt.
QString detectInstall(const QString &projectDir);

// Haengt bei aktivem skip die passende "Dependencies ignorieren"-Option an.
QString applySkip(const QString &install, bool skip);

// Findet installierte Python-Interpreter. Liefert [(label, befehl)].
std::vector<std::pair<QString, QString>> discoverPythons();

// "pipenv" (im pipenv/virtualenvs-Ordner oder mit .project) sonst "venv".
QString envKind(const QString &venvDir);

// Python-Version einer venv aus pyvenv.cfg (oder "").
QString pyvenvVersion(const QString &venvDir);

// Notizen/Projektbezug pro Umgebung (in den Einstellungen).
QString envInfo(const QString &path);
void setEnvInfo(const QString &path, const QString &info);
void setEnvProject(const QString &path, const QString &project);
QString envProject(const QString &venvDir);
void deleteEnvMeta(const QString &path);

// Befehl, der eine vorhandene venv in der aktuellen Shell aktiviert.
QString activateCommand(const QString &osType, const QString &venvDir);

// Befehlsfolge: ins Projekt wechseln, venv anlegen, aktivieren, Pakete installieren.
// install == nullopt -> automatisch ermitteln; "" -> keine Installation.
std::vector<QString> setupCommands(const QString &osType, const QString &projectDir,
                                   const QString &venvDir, const QString &python = QString(),
                                   const std::optional<QString> &install = std::nullopt);

// True, wenn path wie eine venv aussieht (pyvenv.cfg bzw. activate-Skript).
bool isVenv(const QString &path);

// Findet bekannte venv/pipenv-Umgebungen (dedupliziert).
std::vector<VenvInfo> discover(const std::vector<QString> &extraDirs = {});

} // namespace ncssh::core
