#include "ncssh/gui/clipboard_manager.hpp"

#include "ncssh/core/i18n.hpp"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <algorithm>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

QString ClipEntry::preview() const
{
    if (kind == QLatin1String("file")) {
        if (files.size() == 1)
            return QStringLiteral("📄 %1").arg(files.first());
        return QStringLiteral("📄 %1 Dateien: %2")
            .arg(files.size())
            .arg(files.mid(0, 3).join(QStringLiteral(", ")))
            + (files.size() > 3 ? QStringLiteral(" …") : QString());
    }
    QString out = text;
    out.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
    if (out.size() > 200)
        out = out.left(200) + QStringLiteral(" …");
    return out;
}

ClipboardManager::ClipboardManager(QObject *parent) : QObject(parent)
{
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            &ClipboardManager::onClipboardChanged);
}

void ClipboardManager::onClipboardChanged()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return;

    ClipEntry entry;
    entry.timestamp = QDateTime::currentDateTime();

    // Kopierte Dateien (aus dem Explorer oder einer Pane) haben Vorrang.
    if (mime->hasUrls()) {
        QStringList files;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                files << url.toLocalFile();
        }
        if (files.isEmpty())
            return;
        const QString key = QStringLiteral("file:") + files.join(QLatin1Char('\n'));
        if (key == m_lastSeen)
            return;
        m_lastSeen = key;
        entry.kind = QStringLiteral("file");
        entry.files = files;
    } else {
        const QString text = QApplication::clipboard()->text();
        if (text.isEmpty() || text == m_lastSeen)
            return;
        m_lastSeen = text;
        entry.kind = QStringLiteral("text");
        entry.text = text;
    }

    entry.id = ++m_counter;
    // Duplikate nach oben ziehen statt doppelt zu fuehren.
    const QString newPreview = entry.preview();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&newPreview](const ClipEntry &e) {
                                       return e.preview() == newPreview;
                                   }),
                    m_entries.end());
    m_entries.insert(m_entries.begin(), entry);
    if (int(m_entries.size()) > kMaxEntries)
        m_entries.resize(kMaxEntries);
    emit changed();
}

void ClipboardManager::clear()
{
    m_entries.clear();
    emit changed();
}

void ClipboardManager::removeAt(int index)
{
    if (index < 0 || index >= int(m_entries.size()))
        return;
    m_entries.erase(m_entries.begin() + index);
    emit changed();
}

void ClipboardManager::activate(int index)
{
    if (index < 0 || index >= int(m_entries.size()))
        return;
    ClipEntry entry = m_entries[index];
    // Eigenes Setzen nicht als neuen Eintrag werten.
    if (entry.kind == QLatin1String("file")) {
        m_lastSeen = QStringLiteral("file:") + entry.files.join(QLatin1Char('\n'));
        auto *mime = new QMimeData();
        QList<QUrl> urls;
        for (const QString &f : entry.files)
            urls << QUrl::fromLocalFile(f);
        mime->setUrls(urls);
        mime->setText(entry.files.join(QLatin1Char('\n')));
        QApplication::clipboard()->setMimeData(mime);
    } else {
        m_lastSeen = entry.text;
        QApplication::clipboard()->setText(entry.text);
    }
    for (ClipEntry &e : m_entries)
        e.active = false;
    entry.active = true;
    m_entries.erase(m_entries.begin() + index);
    m_entries.insert(m_entries.begin(), entry);
    emit changed();
}

// ---------------------------------------------------------------------------

ClipboardDialog::ClipboardDialog(ClipboardManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("Clipboard-Manager"));
    resize(760, 480);

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(
        _t("Doppelklick setzt den Eintrag als aktiven Inhalt der Zwischenablage."), this);
    info->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(info);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("#"), _t("Typ"), _t("Zeit"),
                                        _t("Inhalt")});
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        m_manager->activate(row);
        const auto &entries = m_manager->entries();
        if (!entries.empty()) {
            const ClipEntry &e = entries.front();
            m_chosenText = (e.kind == QLatin1String("file"))
                               ? e.files.join(QLatin1Char('\n'))
                               : e.text;
        }
        accept();
    });
    layout->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *removeBtn = new QPushButton(_t("Eintrag löschen"), this);
    auto *clearBtn = new QPushButton(_t("Alle löschen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    auto *insertBtn = new QPushButton(_t("Einfügen"), this);
    insertBtn->setDefault(true);
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        m_manager->removeAt(m_table->currentRow());
    });
    connect(clearBtn, &QPushButton::clicked, this, [this] { m_manager->clear(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(insertBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        const auto &entries = m_manager->entries();
        if (row < 0 || row >= int(entries.size()))
            return;
        const ClipEntry &e = entries[row];
        m_chosenText = (e.kind == QLatin1String("file"))
                           ? e.files.join(QLatin1Char('\n'))
                           : e.text;
        accept();
    });
    buttons->addWidget(removeBtn);
    buttons->addWidget(clearBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    buttons->addWidget(insertBtn);
    layout->addLayout(buttons);

    connect(manager, &ClipboardManager::changed, this, &ClipboardDialog::reload);
    reload();
}

void ClipboardDialog::reload()
{
    m_table->setRowCount(0);
    const auto &entries = m_manager->entries();
    for (int i = 0; i < int(entries.size()); ++i) {
        const ClipEntry &e = entries[i];
        m_table->insertRow(i);
        auto *numItem = new QTableWidgetItem(
            e.active ? QStringLiteral("● %1").arg(i + 1) : QString::number(i + 1));
        if (e.active)
            numItem->setToolTip(_t("aktiver Eintrag"));
        m_table->setItem(i, 0, numItem);
        m_table->setItem(i, 1, new QTableWidgetItem(
                                   e.kind == QLatin1String("file") ? _t("Datei") : _t("Text")));
        m_table->setItem(i, 2, new QTableWidgetItem(
                                   e.timestamp.toString(QStringLiteral("HH:mm:ss"))));
        auto *item = new QTableWidgetItem(e.preview());
        item->setToolTip(e.kind == QLatin1String("file")
                             ? e.files.join(QLatin1Char('\n'))
                             : e.text.left(2000));
        m_table->setItem(i, 3, item);
    }
    m_status->setText(QStringLiteral("%1 Einträge").arg(entries.size()));
}

} // namespace ncssh::gui
