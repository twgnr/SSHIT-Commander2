// QFileDialog-Wrapper, die den nicht-nativen Qt-Dialog erzwingen.
//
// Der OS-native Datei-/Ordnerdialog ignoriert das App-Stylesheet. Der Qt-eigene
// Dialog uebernimmt Theme-Palette und QSS — gleiches Farbschema wie der Rest.
#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace ncssh::gui {

QString getOpenFileName(QWidget *parent, const QString &caption,
                        const QString &dir = {}, const QString &filter = {});

QStringList getOpenFileNames(QWidget *parent, const QString &caption,
                             const QString &dir = {}, const QString &filter = {});

QString getSaveFileName(QWidget *parent, const QString &caption,
                        const QString &dir = {}, const QString &filter = {});

QString getExistingDirectory(QWidget *parent, const QString &caption,
                             const QString &dir = {});

} // namespace ncssh::gui
