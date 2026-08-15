// Eigenschaften & Rechte: Groesse/Owner/Datum + chmod-Editor mit rwx-Checkboxen
// und Oktal-Anzeige.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QLabel;

namespace ncssh::gui {

class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    PropertiesDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                     const QString &path, const core::FileEntry &entry,
                     QWidget *parent = nullptr);

private:
    void syncFromChecks();   // Checkboxen -> Oktal
    void syncFromOctal();    // Oktal -> Checkboxen
    quint32 currentMode() const;
    void applyChmod();

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider;
    QString m_path;

    QCheckBox *m_bits[9] = {};   // owner rwx, group rwx, other rwx
    QLineEdit *m_octal = nullptr;
    QLabel *m_sizeLabel = nullptr;
    bool m_updating = false;
};

} // namespace ncssh::gui
