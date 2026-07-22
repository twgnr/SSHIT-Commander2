#include "ncssh/gui/encoding_converter_dialog.hpp"

#include "ncssh/core/encodings.hpp"
#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

EncodingConverterDialog::EncodingConverterDialog(AsyncBridge *bridge,
                                                 core::FileSystemProvider *provider,
                                                 const QString &path, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_provider(provider), m_path(path)
{
    setWindowTitle(_t("Datei-Encoding konvertieren") + QStringLiteral(" — ")
                   + provider->basename(path));
    resize(720, 620);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_srcCodec = new QComboBox(this);
    m_dstCodec = new QComboBox(this);
    for (const auto &[label, codec] : core::encodingsList()) {
        m_srcCodec->addItem(label, codec);
        m_dstCodec->addItem(label, codec);
    }
    m_dstCodec->setCurrentIndex(m_dstCodec->findData(QStringLiteral("utf-8")));
    connect(m_srcCodec, &QComboBox::currentIndexChanged, this,
            &EncodingConverterDialog::updatePreview);
    form->addRow(_t("Quell-Encoding"), m_srcCodec);
    form->addRow(_t("Ziel-Encoding"), m_dstCodec);

    m_errorMode = new QComboBox(this);
    for (const auto &[label, mode] : core::errorModes())
        m_errorMode->addItem(label, mode);
    m_errorMode->setCurrentIndex(m_errorMode->findData(QStringLiteral("replace")));
    form->addRow(_t("Bei Fehlern"), m_errorMode);

    m_overwrite = new QCheckBox(_t("Originaldatei überschreiben"), this);
    connect(m_overwrite, &QCheckBox::toggled, this,
            [this](bool on) { m_targetName->setEnabled(!on); });
    form->addRow(QString(), m_overwrite);

    m_targetName = new QLineEdit(provider->basename(path) + QStringLiteral(".converted"), this);
    form->addRow(_t("Zieldatei"), m_targetName);
    layout->addLayout(form);

    layout->addWidget(new QLabel(_t("Vorschau (Quelle)"), this));
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    layout->addWidget(m_preview, 1);

    m_status = new QLabel(_t("Lade …"), this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *convertBtn = new QPushButton(_t("Konvertieren"), this);
    convertBtn->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(convertBtn, &QPushButton::clicked, this, &EncodingConverterDialog::convert);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(convertBtn);
    layout->addLayout(buttons);

    loadSource();
}

void EncodingConverterDialog::loadSource()
{
    core::FileSystemProvider *provider = m_provider;
    const QString path = m_path;
    m_bridge->run<QByteArray>(
        [provider, path] { return provider->readBytes(path, 5'000'000); },
        [this](const QByteArray &data) {
            m_raw = data;
            // Quell-Encoding automatisch erkennen und vorwaehlen.
            const QString detected = core::detectEncoding(data);
            const int idx = m_srcCodec->findData(detected);
            if (idx >= 0)
                m_srcCodec->setCurrentIndex(idx);
            m_status->setText(QStringLiteral("%1 Bytes · erkannt: %2")
                                  .arg(data.size()).arg(detected));
            updatePreview();
        },
        [this](const QString &err) { m_status->setText(err); });
}

void EncodingConverterDialog::updatePreview()
{
    if (m_raw.isEmpty())
        return;
    try {
        m_preview->setPlainText(
            core::decodePreview(m_raw, m_srcCodec->currentData().toString(), 4000));
    } catch (const std::exception &exc) {
        m_preview->setPlainText(QString::fromUtf8(exc.what()));
    }
}

void EncodingConverterDialog::convert()
{
    if (m_raw.isEmpty())
        return;
    QByteArray converted;
    try {
        converted = core::convert(m_raw, m_srcCodec->currentData().toString(),
                                  m_dstCodec->currentData().toString(),
                                  m_errorMode->currentData().toString());
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Konvertierung fehlgeschlagen"),
                             QString::fromUtf8(exc.what()));
        return;
    }
    const QString target = m_overwrite->isChecked()
                               ? m_path
                               : m_provider->join(m_provider->parent(m_path),
                                                  m_targetName->text().trimmed());
    core::FileSystemProvider *provider = m_provider;
    m_bridge->run(
        [provider, target, converted] { provider->writeBytes(target, converted); },
        [this, target] {
            QMessageBox::information(this, _t("Fertig"),
                                     QStringLiteral("Geschrieben: %1").arg(target));
            accept();
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

} // namespace ncssh::gui
