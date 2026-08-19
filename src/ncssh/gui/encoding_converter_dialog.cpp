#include "ncssh/gui/encoding_converter_dialog.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/encodings.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/file_dialogs.hpp"

#include <QCheckBox>
#include <QJsonArray>
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

    m_overwrite = new QCheckBox(_t("Quelldatei überschreiben"), this);
    connect(m_overwrite, &QCheckBox::toggled, this,
            [this](bool on) { m_targetName->setEnabled(!on); });
    form->addRow(QString(), m_overwrite);

    auto *targetRow = new QHBoxLayout();
    m_targetName = new QLineEdit(provider->basename(path) + QStringLiteral(".converted"), this);
    auto *browse = new QPushButton(_t("Durchsuchen …"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen = getSaveFileName(this, _t("Ziel wählen"), m_targetName->text());
        if (!chosen.isEmpty())
            m_targetName->setText(m_provider->basename(chosen));
    });
    targetRow->addWidget(m_targetName, 1);
    targetRow->addWidget(browse);
    form->addRow(_t("Neue Datei:"), targetRow);
    layout->addLayout(form);

    auto *previewLabel = new QLabel(_t("Vorschau (Quelle):"), this);
    layout->addWidget(previewLabel);
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    layout->addWidget(m_preview, 1);

    // KI-Reparatur: zeigt eine zweite Vorschau, die beim Konvertieren gespeichert
    // wird. Ohne aktivierte KI bleibt der Bereich verborgen.
    m_repairLabel = new QLabel(
        _t("Vorschau (KI-repariert) — wird beim Konvertieren gespeichert:"), this);
    m_repairLabel->setVisible(false);
    layout->addWidget(m_repairLabel);
    m_repairPreview = new QPlainTextEdit(this);
    m_repairPreview->setFont(mono);
    m_repairPreview->setVisible(false);
    layout->addWidget(m_repairPreview, 1);

    m_status = new QLabel(_t("Lade …"), this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    m_repairButton = new QPushButton(_t("Mit KI reparieren …"), this);
    m_repairButton->setToolTip(
        _t("Beschädigten Text vom lokalen Modell rekonstruieren lassen"));
    connect(m_repairButton, &QPushButton::clicked, this, &EncodingConverterDialog::repairWithAi);
    auto *convertBtn = new QPushButton(_t("Konvertieren"), this);
    convertBtn->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(convertBtn, &QPushButton::clicked, this, &EncodingConverterDialog::convert);
    buttons->addWidget(m_repairButton);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(convertBtn);
    layout->addLayout(buttons);

    loadSource();
}

void EncodingConverterDialog::repairWithAi()
{
    if (!core::aiEnabled()) {
        QMessageBox::information(this, _t("KI"),
                                 _t("Die KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    const QString source = m_preview->toPlainText();
    if (source.trimmed().isEmpty())
        return;
    const auto [text, truncated] = core::truncateFile(source);
    m_repairButton->setEnabled(false);
    m_status->setText(_t("KI repariert … (kann je nach Modell dauern)"));
    m_repairLabel->setVisible(true);
    m_repairPreview->setVisible(true);
    m_repairPreview->clear();

    const QString baseUrl = core::ollamaUrl();
    const QString model = core::aiModel();
    const QJsonArray messages = core::buildRepairMessages(text);
    m_bridge->stream(
        [baseUrl, model, messages](const AsyncBridge::EmitLine &emitLine,
                                   const CancelTokenPtr &cancel) {
            core::chatStream(baseUrl, model, messages, {}, emitLine, cancel);
        },
        [this](const QString &chunk) { m_repairPreview->insertPlainText(chunk); },
        [this, truncated] {
            m_repairButton->setEnabled(true);
            m_status->setText(_t("KI-Reparatur übernommen.")
                              + (truncated ? _t(" (Hinweis: Kontext war zu lang und wurde "
                                                "gekürzt)")
                                           : QString()));
        },
        [this](const QString &err) {
            m_repairButton->setEnabled(true);
            m_status->setText(err);
        });
}

void EncodingConverterDialog::loadSource()
{
    core::FileSystemProvider *provider = m_provider;
    const QString path = m_path;
    // Ein Byte mehr als das Limit lesen: kommt es zurueck, ist die Datei
    // groesser und darf nicht konvertiert (= gekuerzt) werden.
    constexpr qint64 kLimit = 5'000'000;
    m_bridge->run<QByteArray>(
        [provider, path] { return provider->readBytes(path, kLimit + 1); },
        [this](const QByteArray &data) {
            m_truncated = data.size() > kLimit;
            m_raw = m_truncated ? data.left(kLimit) : data;
            // Quell-Encoding automatisch erkennen und vorwaehlen.
            const QString detected = core::detectEncoding(m_raw);
            const int idx = m_srcCodec->findData(detected);
            if (idx >= 0)
                m_srcCodec->setCurrentIndex(idx);
            m_status->setText(QStringLiteral("%1 Bytes · erkannt: %2")
                                  .arg(m_raw.size()).arg(detected));
            if (m_truncated)
                m_status->setText(
                    _t("Datei größer als 5 MB — nur Vorschau möglich, kein Konvertieren."));
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
    // Nur ein Ausschnitt gelesen -> Schreiben wuerde den Rest der Datei
    // abschneiden. Lieber gar nicht schreiben als stumm Daten verlieren.
    if (m_truncated) {
        QMessageBox::warning(
            this, _t("Konvertierung"),
            _t("Die Datei ist größer als 5 MB. Sie kann hier nur angesehen, "
               "nicht konvertiert werden — sonst ginge der Rest verloren."));
        return;
    }
    if (!m_overwrite->isChecked() && m_targetName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, _t("Konvertierung"), _t("Bitte einen Zielpfad angeben."));
        return;
    }
    QByteArray converted;
    try {
        // Liegt eine KI-Reparatur vor, wird sie geschrieben — sonst die Quelle.
        // Die Reparatur-Vorschau umfasst nur den Anfang der Datei; sie darf
        // deshalb NUR in eine neue Datei geschrieben werden, niemals ueber die
        // Quelle (das wuerde den Rest verwerfen).
        const QString repaired = m_repairPreview->isVisible()
                                     ? m_repairPreview->toPlainText()
                                     : QString();
        if (!repaired.trimmed().isEmpty() && m_overwrite->isChecked()) {
            QMessageBox::warning(
                this, _t("Konvertierung"),
                _t("Die KI-Reparatur zeigt nur den Anfang der Datei. Bitte in eine "
                   "NEUE Datei schreiben (Häkchen „Quelldatei überschreiben“ entfernen)."));
            return;
        }
        if (!repaired.trimmed().isEmpty()) {
            converted = core::encodeText(repaired, m_dstCodec->currentData().toString(),
                                         m_errorMode->currentData().toString());
        } else {
            converted = core::convert(m_raw, m_srcCodec->currentData().toString(),
                                      m_dstCodec->currentData().toString(),
                                      m_errorMode->currentData().toString());
        }
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
            QMessageBox::information(this, _t("Konvertierung"),
                                     _t("Datei geschrieben: %1").arg(target));
            accept();
        },
        [this](const QString &err) {
            QMessageBox::warning(this, _t("Speichern fehlgeschlagen"), err);
        });
}

} // namespace ncssh::gui
