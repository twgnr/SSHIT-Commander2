#include "ncssh/gui/preview_panel.hpp"

#include "ncssh/core/i18n.hpp"

#include <QFont>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

namespace {
// Grenze fuer die Vorschau — groessere Dateien werden abgeschnitten.
constexpr qint64 kPreviewLimit = 200'000;

bool isImageName(const QString &name)
{
    static const QStringList exts = {
        QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"),
        QStringLiteral(".gif"), QStringLiteral(".bmp"), QStringLiteral(".webp"),
        QStringLiteral(".svg"), QStringLiteral(".ico"),
    };
    const QString low = name.toLower();
    for (const QString &ext : exts) {
        if (low.endsWith(ext))
            return true;
    }
    return false;
}

// Heuristik wie bei grep: ein NUL-Byte im Anfang gilt als binaer.
bool looksBinary(const QByteArray &data)
{
    return data.left(8000).contains('\0');
}
} // namespace

PreviewPanel::PreviewPanel(AsyncBridge *bridge, QWidget *parent)
    : QWidget(parent), m_bridge(bridge)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_title = new QLabel(_t("Vorschau"), this);
    m_title->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_title);

    m_stack = new QStackedWidget(this);
    m_text = new QPlainTextEdit(m_stack);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);
    m_text->setFont(mono);
    m_stack->addWidget(m_text);

    m_image = new QLabel(m_stack);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setScaledContents(false);
    m_stack->addWidget(m_image);

    layout->addWidget(m_stack, 1);
}

void PreviewPanel::clearPreview()
{
    if (m_task) {
        m_bridge->cancel(m_task);
        m_task = nullptr;
    }
    m_currentPath.clear();
    m_title->setText(_t("Vorschau"));
    m_text->clear();
    m_image->clear();
    m_stack->setCurrentWidget(m_text);
}

void PreviewPanel::preview(core::FileSystemProvider *provider, const QString &path)
{
    if (!provider || path.isEmpty()) {
        clearPreview();
        return;
    }
    if (path == m_currentPath)
        return;
    m_currentPath = path;
    const QString name = provider->basename(path);
    m_title->setText(name);

    if (m_task) {
        m_bridge->cancel(m_task);
        m_task = nullptr;
    }

    if (isImageName(name)) {
        // Remote deutlich knapper deckeln: ein 25-MB-Bild ueber SSH belegt die
        // Session sonst minutenlang, nur fuer eine Vorschau.
        const qint64 limit = provider->isRemote ? 8'000'000 : 25'000'000;
        m_task = m_bridge->run<QByteArray>(
            [provider, path, limit] { return provider->readBytes(path, limit); },
            [this, limit](const QByteArray &data) {
                QPixmap pixmap;
                if (data.size() >= limit) {
                    // Abgeschnitten gelesen -> gar nicht erst dekodieren.
                    m_text->setPlainText(_t("(Datei zu groß für die Vorschau)"));
                    m_stack->setCurrentWidget(m_text);
                } else if (pixmap.loadFromData(data)) {
                    m_image->setPixmap(pixmap.scaled(m_image->size(), Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
                    m_stack->setCurrentWidget(m_image);
                } else {
                    m_text->setPlainText(_t("(Vorschau nicht verfügbar)"));
                    m_stack->setCurrentWidget(m_text);
                }
                m_task = nullptr;
            },
            [this](const QString &) {
                m_text->setPlainText(_t("(Vorschau nicht verfügbar)"));
                m_stack->setCurrentWidget(m_text);
                m_task = nullptr;
            });
        return;
    }

    // Binaerdateien nicht als Text ausgeben — das produziert nur Rauschen.
    m_task = m_bridge->run<QByteArray>(
        [provider, path] { return provider->readBytes(path, kPreviewLimit); },
        [this](const QByteArray &data) {
            m_text->setPlainText(looksBinary(data) ? _t("(Binärdatei — keine Vorschau)")
                                                   : QString::fromUtf8(data));
            m_stack->setCurrentWidget(m_text);
            m_task = nullptr;
        },
        [this](const QString &) {
            m_text->setPlainText(_t("(Vorschau nicht verfügbar)"));
            m_stack->setCurrentWidget(m_text);
            m_task = nullptr;
        });
}

} // namespace ncssh::gui
