#include "ncssh/gui/file_dialogs.hpp"

#include <QFileDialog>

namespace ncssh::gui {

namespace {
// Gemeinsame Vorgaben: nicht-nativer Dialog (damit das QSS greift) und eine
// breitere Standardgroesse.
void prepare(QFileDialog &dialog, const QString &caption, const QString &dir,
             const QString &filter)
{
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowTitle(caption);
    if (!dir.isEmpty())
        dialog.setDirectory(dir);
    if (!filter.isEmpty())
        dialog.setNameFilter(filter);
    dialog.resize(900, 560);
}
} // namespace

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
                        const QString &filter)
{
    QFileDialog dialog(parent);
    prepare(dialog, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedFiles().value(0);
}

QStringList getOpenFileNames(QWidget *parent, const QString &caption, const QString &dir,
                             const QString &filter)
{
    QFileDialog dialog(parent);
    prepare(dialog, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedFiles();
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
                        const QString &filter)
{
    QFileDialog dialog(parent);
    prepare(dialog, caption, dir, filter);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    if (!dir.isEmpty())
        dialog.selectFile(dir);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedFiles().value(0);
}

QString getExistingDirectory(QWidget *parent, const QString &caption, const QString &dir)
{
    QFileDialog dialog(parent);
    prepare(dialog, caption, dir, QString());
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedFiles().value(0);
}

} // namespace ncssh::gui
