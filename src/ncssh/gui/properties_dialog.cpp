#include "ncssh/gui/properties_dialog.hpp"

#include "ncssh/core/dateformat.hpp"
#include "ncssh/core/fileops.hpp"
#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

static QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    double v = bytes;
    const char *units[] = {"KB", "MB", "GB", "TB"};
    int i = -1;
    do { v /= 1024.0; ++i; } while (v >= 1024.0 && i < 3);
    return QStringLiteral("%1 %2 (%3 B)").arg(v, 0, 'f', 1)
        .arg(QString::fromLatin1(units[i])).arg(bytes);
}

PropertiesDialog::PropertiesDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                                   const QString &path, const core::FileEntry &entry,
                                   QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_provider(provider), m_path(path)
{
    setWindowTitle(_t("Eigenschaften — %1").arg(entry.name));
    resize(460, 460);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    form->addRow(_t("Name"), new QLabel(entry.name, this));
    form->addRow(_t("Pfad"), new QLabel(path, this));
    form->addRow(_t("Typ"), new QLabel(entry.isDir() ? _t("Ordner") : _t("Datei"), this));

    m_sizeLabel = new QLabel(entry.isDir() ? _t("wird berechnet …") : humanSize(entry.size), this);
    form->addRow(_t("Größe"), m_sizeLabel);

    if (entry.modified.isValid())
        form->addRow(_t("Geändert"), new QLabel(core::formatDt(entry.modified, QString()), this));
    if (entry.created.isValid())
        form->addRow(_t("Erstellt"), new QLabel(core::formatDt(entry.created, QString()), this));
    if (!entry.owner.isEmpty())
        form->addRow(_t("Eigentümer"), new QLabel(entry.owner + QStringLiteral(" / ") + entry.group, this));
    if (!entry.linkTarget.isEmpty())
        form->addRow(_t("Symlink-Ziel"), new QLabel(entry.linkTarget, this));
    layout->addLayout(form);

    // Ordnergroesse rekursiv im Worker berechnen (nur lokal sinnvoll/schnell).
    if (entry.isDir() && !provider->isRemote) {
        bridge->run<QPair<qint64, bool>>(
            [path] {
                const auto [size, truncated] = core::dirSize(path);
                return QPair<qint64, bool>(size, truncated);
            },
            [this](const QPair<qint64, bool> &result) {
                m_sizeLabel->setText(humanSize(result.first)
                                     + (result.second ? QStringLiteral(" (gekürzt)") : QString()));
            },
            [this](const QString &) { m_sizeLabel->setText(_t("nicht ermittelbar")); });
    }

    // --- chmod-Editor ---
    auto *permBox = new QGroupBox(_t("Rechte"), this);
    auto *grid = new QGridLayout(permBox);
    const QStringList whoLabels = {_t("Eigner"), _t("Gruppe"), _t("Andere")};
    const QStringList bitLabels = {QStringLiteral("r"), QStringLiteral("w"), QStringLiteral("x")};
    grid->addWidget(new QLabel(QString(), permBox), 0, 0);
    const QStringList bitTips = {_t("Lesen"), _t("Schreiben"), _t("Ausführen")};
    for (int b = 0; b < 3; ++b) {
        auto *head = new QLabel(bitLabels[b], permBox);
        head->setToolTip(bitTips[b]);
        grid->addWidget(head, 0, b + 1);
    }
    for (int who = 0; who < 3; ++who) {
        grid->addWidget(new QLabel(whoLabels[who], permBox), who + 1, 0);
        for (int b = 0; b < 3; ++b) {
            auto *check = new QCheckBox(permBox);
            const int idx = who * 3 + b;
            m_bits[idx] = check;
            // Bit-Wert: owner r=0400 ... other x=0001
            const quint32 bitValue = 1u << (8 - idx);
            check->setChecked(entry.permissions & bitValue);
            connect(check, &QCheckBox::toggled, this, &PropertiesDialog::syncFromChecks);
            grid->addWidget(check, who + 1, b + 1);
        }
    }
    m_octal = new QLineEdit(permBox);
    m_octal->setMaxLength(4);
    m_octal->setFixedWidth(70);
    connect(m_octal, &QLineEdit::textEdited, this, &PropertiesDialog::syncFromOctal);
    grid->addWidget(new QLabel(_t("Oktal"), permBox), 4, 0);
    grid->addWidget(m_octal, 4, 1, 1, 3);
    layout->addWidget(permBox);
    syncFromChecks();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    box->button(QDialogButtonBox::Apply)->setText(_t("Rechte anwenden"));
    connect(box->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &PropertiesDialog::applyChmod);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

quint32 PropertiesDialog::currentMode() const
{
    quint32 mode = 0;
    for (int i = 0; i < 9; ++i) {
        if (m_bits[i] && m_bits[i]->isChecked())
            mode |= 1u << (8 - i);
    }
    return mode;
}

void PropertiesDialog::syncFromChecks()
{
    if (m_updating)
        return;
    m_updating = true;
    m_octal->setText(QString::number(currentMode(), 8).rightJustified(3, QLatin1Char('0')));
    m_updating = false;
}

void PropertiesDialog::syncFromOctal()
{
    if (m_updating)
        return;
    bool ok = false;
    const uint mode = m_octal->text().toUInt(&ok, 8);
    if (!ok)
        return;
    m_updating = true;
    for (int i = 0; i < 9; ++i) {
        if (m_bits[i])
            m_bits[i]->setChecked(mode & (1u << (8 - i)));
    }
    m_updating = false;
}

void PropertiesDialog::applyChmod()
{
    core::FileSystemProvider *provider = m_provider;
    const QString path = m_path;
    const quint32 mode = currentMode();
    m_bridge->run(
        [provider, path, mode] { provider->chmod(path, mode); },
        [this] { accept(); },
        [this](const QString &err) {
            QMessageBox::warning(this, _t("Rechte ändern fehlgeschlagen"), err);
        });
}

} // namespace ncssh::gui
