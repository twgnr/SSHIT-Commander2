#include "ncssh/gui/macro_manager_dialog.hpp"

#include "ncssh/core/appmonitor.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/macro_key_editor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
namespace ma = core::macroactions;
namespace mc = core::macros;

// ---------------------------------------------------------------------------
// KeyTile
// ---------------------------------------------------------------------------

KeyTile::KeyTile(int index, QWidget *parent)
    : QPushButton(parent), m_index(index), m_holdTimer(new QTimer(this))
{
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(kHoldMs);
    connect(m_holdTimer, &QTimer::timeout, this, [this] {
        m_held = true;
        emit heldTile(m_index);
    });
}

void KeyTile::setConfig(const QJsonObject &config, int size)
{
    m_config = config;
    m_hasDynamicText = false;
    m_dynamicText.clear();
    setFixedSize(size, size);
    if (!config.isEmpty()) {
        QStringList tip;
        const QString label = config.value(QStringLiteral("label")).toString();
        const QString shortcut = config.value(QStringLiteral("shortcut")).toString();
        if (!label.isEmpty())
            tip << label;
        if (!shortcut.isEmpty())
            tip << QStringLiteral("[%1]").arg(shortcut);
        setToolTip(tip.join(QStringLiteral("  ")));
    } else {
        setToolTip(QString());
    }
    update();
}

void KeyTile::setDynamicText(const QString &text)
{
    if (!m_hasDynamicText || text != m_dynamicText) {
        m_dynamicText = text;
        m_hasDynamicText = true;
        update();
    }
}

void KeyTile::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_held = false;
        m_holdTimer->start();
    }
    QPushButton::mousePressEvent(event);
}

void KeyTile::mouseReleaseEvent(QMouseEvent *event)
{
    const bool left = (event->button() == Qt::LeftButton);
    m_holdTimer->stop();
    QPushButton::mouseReleaseEvent(event);
    if (left && !m_held && rect().contains(event->position().toPoint()))
        emit clickedTile(m_index);
}

void KeyTile::paintEvent(QPaintEvent *event)
{
    QPushButton::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect textRect = rect().adjusted(4, 4, -4, -4);

    const QString label = m_hasDynamicText
                              ? m_dynamicText
                              : m_config.value(QStringLiteral("label")).toString();
    const QString iconPath = m_config.value(QStringLiteral("icon")).toString();
    if (!iconPath.isEmpty()) {
        QPixmap pm(iconPath);
        if (!pm.isNull()) {
            // Icon ueber die gesamte Tastengroesse strecken (Label liegt darueber).
            p.drawPixmap(rect(), pm.scaled(rect().size(), Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation));
        }
    }

    if (!label.isEmpty()) {
        const QString family = m_config.value(QStringLiteral("font_family")).toString();
        if (!family.isEmpty()) {
            QFont f = p.font();
            f.setFamily(family);
            p.setFont(f);
        }
        const QString color =
            m_config.value(QStringLiteral("font_color")).toString(QStringLiteral("#ffffff"));
        p.setPen(QColor(color.isEmpty() ? QStringLiteral("#ffffff") : color));
        const QString pos =
            m_config.value(QStringLiteral("label_pos")).toString(QStringLiteral("bottom"));
        int flags = Qt::AlignHCenter | Qt::TextWordWrap;
        if (pos == QLatin1String("top"))
            flags |= Qt::AlignTop;
        else if (pos == QLatin1String("middle"))
            flags |= Qt::AlignVCenter;
        else
            flags |= Qt::AlignBottom;
        p.drawText(textRect, flags, label);
    }
}

// ---------------------------------------------------------------------------
// MacroManagerDialog
// ---------------------------------------------------------------------------

MacroManagerDialog::MacroManagerDialog(AsyncBridge *bridge,
                                       std::function<void(const QString &, bool)> sshSend,
                                       std::function<void(const QString &, bool)> sshBroadcast,
                                       QWidget *parent)
    : QDialog(parent), m_bridge(bridge)
{
    setWindowTitle(_t("Makro-Manager"));
    m_config = mc::load();
    m_context.sshSend = std::move(sshSend);
    m_context.sshBroadcast = std::move(sshBroadcast);
    m_currentLayer = m_config.layerNames().value(0, mc::kDefaultLayer);
    m_runMode = (m_config.mode == QLatin1String("run"));

    buildUi();
    refreshLayers();
    drawGrid();

    // Kontextabhaengiger Layerwechsel: Vordergrund-Programm beobachten.
    m_foregroundTimer = new QTimer(this);
    m_foregroundTimer->setInterval(1200);
    connect(m_foregroundTimer, &QTimer::timeout, this, &MacroManagerDialog::pollForeground);
    if (m_config.contextAware)
        m_foregroundTimer->start();
}

MacroManagerDialog::~MacroManagerDialog()
{
    // Zustand sichern (Modus, Kontext-Schalter, Tastengroesse).
    m_config.mode = m_runMode ? QStringLiteral("run") : QStringLiteral("edit");
    try {
        mc::save(m_config);
    } catch (...) {
    }
}

void MacroManagerDialog::buildUi()
{
    resize(980, 640);
    auto *root = new QHBoxLayout(this);

    // --- Linke Spalte: Layer ---
    auto *left = new QVBoxLayout();
    left->addWidget(new QLabel(_t("Layer"), this));
    m_layerList = new QListWidget(this);
    connect(m_layerList, &QListWidget::currentRowChanged, this,
            [this](int) { onLayerSelected(); });
    left->addWidget(m_layerList, 1);

    auto *layerButtons = new QHBoxLayout();
    auto *addBtn = new QPushButton(_t("Neu"), this);
    auto *editBtn = new QPushButton(_t("Bearbeiten"), this);
    auto *delBtn = new QPushButton(_t("Löschen"), this);
    connect(addBtn, &QPushButton::clicked, this, &MacroManagerDialog::addLayer);
    connect(editBtn, &QPushButton::clicked, this, &MacroManagerDialog::editLayer);
    connect(delBtn, &QPushButton::clicked, this, &MacroManagerDialog::deleteLayer);
    layerButtons->addWidget(addBtn);
    layerButtons->addWidget(editBtn);
    layerButtons->addWidget(delBtn);
    left->addLayout(layerButtons);

    m_contextAware = new QCheckBox(_t("Layer automatisch zum Programm wechseln"), this);
    m_contextAware->setChecked(m_config.contextAware);
    connect(m_contextAware, &QCheckBox::toggled, this, [this](bool on) {
        m_config.contextAware = on;
        if (on)
            m_foregroundTimer->start();
        else
            m_foregroundTimer->stop();
    });
    left->addWidget(m_contextAware);

    auto *sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(_t("Tastengröße"), this));
    m_keySize = new QSpinBox(this);
    m_keySize->setRange(48, 200);
    m_keySize->setValue(m_config.keySize);
    connect(m_keySize, &QSpinBox::valueChanged, this, [this](int value) {
        m_config.keySize = value;
        drawGrid();
    });
    sizeRow->addWidget(m_keySize);
    left->addLayout(sizeRow);
    root->addLayout(left, 1);

    // --- Rechte Spalte: Raster ---
    auto *right = new QVBoxLayout();
    auto *topRow = new QHBoxLayout();
    m_modeButton = new QPushButton(this);
    m_modeButton->setCheckable(true);
    m_modeButton->setChecked(m_runMode);
    connect(m_modeButton, &QPushButton::toggled, this, &MacroManagerDialog::toggleMode);
    topRow->addWidget(m_modeButton);
    topRow->addStretch(1);
    auto *exportBtn = new QPushButton(_t("Exportieren …"), this);
    auto *importBtn = new QPushButton(_t("Importieren …"), this);
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString path = getSaveFileName(
            this, _t("Layer exportieren"), QStringLiteral("macros.json"),
            QStringLiteral("JSON (*.json)"));
        if (path.isEmpty())
            return;
        try {
            mc::writeExport(m_config, path);
            m_status->setText(_t("Exportiert."));
        } catch (const std::exception &exc) {
            QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
        }
    });
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QString path = getOpenFileName(
            this, _t("Layer importieren"), QString(), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty())
            return;
        try {
            const auto imported = mc::readImport(path);
            for (auto it = imported.begin(); it != imported.end(); ++it) {
                mc::Layer layer = it.value();
                const QString name = m_config.uniqueName(it.key());
                layer.name = name;
                m_config.layers.insert(name, layer);
                m_config.order.append(name);
            }
            mc::save(m_config);
            refreshLayers();
            m_status->setText(QStringLiteral("%1 Layer importiert.").arg(imported.size()));
        } catch (const std::exception &exc) {
            QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
        }
    });
    topRow->addWidget(exportBtn);
    topRow->addWidget(importBtn);
    right->addLayout(topRow);

    m_gridHost = new QWidget(this);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setSpacing(6);
    right->addWidget(m_gridHost, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    right->addWidget(m_status);

    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    right->addWidget(closeBtn);
    root->addLayout(right, 3);

    toggleMode(m_runMode);
}

void MacroManagerDialog::refreshLayers()
{
    m_layerList->blockSignals(true);
    m_layerList->clear();
    for (const QString &name : m_config.layerNames()) {
        const mc::Layer layer = m_config.layers.value(name);
        auto *item = new QListWidgetItem(
            layer.app.isEmpty() ? name : QStringLiteral("%1  →  %2").arg(name, layer.app),
            m_layerList);
        item->setData(Qt::UserRole, name);
    }
    m_layerList->blockSignals(false);
    // Aktuellen Layer auswaehlen.
    for (int i = 0; i < m_layerList->count(); ++i) {
        if (m_layerList->item(i)->data(Qt::UserRole).toString() == m_currentLayer) {
            m_layerList->setCurrentRow(i);
            return;
        }
    }
    if (m_layerList->count() > 0)
        m_layerList->setCurrentRow(0);
}

void MacroManagerDialog::onLayerSelected()
{
    auto *item = m_layerList->currentItem();
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();
    if (name == m_currentLayer)
        return;
    m_layerHistory.append(m_currentLayer);
    m_currentLayer = name;
    drawGrid();
}

mc::Layer *MacroManagerDialog::currentLayer()
{
    return m_config.get(m_currentLayer);
}

void MacroManagerDialog::addLayer()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, _t("Neuer Layer"), _t("Name:"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    mc::Layer &layer = m_config.addLayer(name.trimmed());
    m_currentLayer = layer.name;
    mc::save(m_config);
    refreshLayers();
    drawGrid();
}

void MacroManagerDialog::editLayer()
{
    mc::Layer *layer = currentLayer();
    if (!layer)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(_t("Layer bearbeiten"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    auto *name = new QLineEdit(layer->name, &dlg);
    auto *app = new QLineEdit(layer->app, &dlg);
    app->setPlaceholderText(_t("z. B. code.exe — leer = kein automatischer Wechsel"));
    auto *grabBtn = new QPushButton(_t("Aktives Programm übernehmen"), &dlg);
    connect(grabBtn, &QPushButton::clicked, &dlg, [app] {
        const auto [pid, exe] = core::foregroundProcess();
        if (!exe.isEmpty())
            app->setText(exe);
    });
    auto *rows = new QSpinBox(&dlg);
    rows->setRange(1, 16);
    rows->setValue(layer->rows);
    auto *cols = new QSpinBox(&dlg);
    cols->setRange(1, 16);
    cols->setValue(layer->cols);
    form->addRow(_t("Name"), name);
    form->addRow(_t("Programm"), app);
    form->addRow(QString(), grabBtn);
    form->addRow(_t("Zeilen"), rows);
    form->addRow(_t("Spalten"), cols);
    layout->addLayout(form);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);
    if (dlg.exec() != QDialog::Accepted)
        return;

    layer->app = app->text().trimmed();
    layer->rows = rows->value();
    layer->cols = cols->value();
    const QString newName = name->text().trimmed();
    if (!newName.isEmpty() && newName != m_currentLayer)
        m_currentLayer = m_config.renameLayer(m_currentLayer, newName);
    mc::save(m_config);
    refreshLayers();
    drawGrid();
}

void MacroManagerDialog::deleteLayer()
{
    if (m_config.layerNames().size() <= 1) {
        QMessageBox::information(this, _t("Löschen"),
                                 _t("Der letzte Layer kann nicht gelöscht werden."));
        return;
    }
    if (QMessageBox::question(this, _t("Löschen"),
                              QStringLiteral("Layer \"%1\" löschen?").arg(m_currentLayer))
        != QMessageBox::Yes)
        return;
    m_config.removeLayer(m_currentLayer);
    m_currentLayer = m_config.layerNames().value(0);
    mc::save(m_config);
    refreshLayers();
    drawGrid();
}

void MacroManagerDialog::drawGrid()
{
    // Altes Raster abbauen.
    for (KeyTile *tile : m_tiles)
        tile->deleteLater();
    m_tiles.clear();
    while (QLayoutItem *item = m_grid->takeAt(0))
        delete item;

    mc::Layer *layer = currentLayer();
    if (!layer)
        return;
    const int size = m_config.keySize;
    for (int index = 0; index < layer->capacity(); ++index) {
        auto *tile = new KeyTile(index, m_gridHost);
        const auto key = layer->key(index);
        tile->setConfig(key.value_or(QJsonObject()), size);
        connect(tile, &KeyTile::clickedTile, this, &MacroManagerDialog::onTileClicked);
        connect(tile, &KeyTile::heldTile, this, &MacroManagerDialog::onTileHeld);
        m_grid->addWidget(tile, index / layer->cols, index % layer->cols);
        m_tiles.push_back(tile);
    }
    m_status->setText(QStringLiteral("%1 — %2 × %3 Tasten")
                          .arg(m_currentLayer).arg(layer->rows).arg(layer->cols));
}

void MacroManagerDialog::onTileClicked(int index)
{
    mc::Layer *layer = currentLayer();
    if (!layer)
        return;
    const auto key = layer->key(index);
    if (m_runMode) {
        if (key)
            runKey(*key, index);
        return;
    }
    // Bearbeiten-Modus: Editor oeffnen.
    MacroKeyEditor editor(key.value_or(mc::newKey()), m_config.layerNames(), this);
    if (editor.exec() != QDialog::Accepted)
        return;
    layer->setKey(index, editor.cleared() ? QJsonObject() : editor.config());
    mc::save(m_config);
    drawGrid();
}

void MacroManagerDialog::onTileHeld(int index)
{
    // Langes Halten oeffnet den Editor auch im Ausfuehren-Modus.
    if (!m_runMode)
        return;
    mc::Layer *layer = currentLayer();
    if (!layer)
        return;
    const auto key = layer->key(index);
    MacroKeyEditor editor(key.value_or(mc::newKey()), m_config.layerNames(), this);
    if (editor.exec() != QDialog::Accepted)
        return;
    layer->setKey(index, editor.cleared() ? QJsonObject() : editor.config());
    mc::save(m_config);
    drawGrid();
}

void MacroManagerDialog::runKey(const QJsonObject &config, int index)
{
    const QString type = config.value(QStringLiteral("action_type")).toString();
    const QJsonValue payload = config.value(QStringLiteral("payload"));
    const ma::ActionSpec &spec = ma::spec(type);

    // Navigations-Aktionen behandelt das Fenster selbst.
    if (spec.navigation) {
        if (type == QLatin1String("layer") || type == QLatin1String("jump_to_layer")) {
            const QString target = payload.toString();
            if (m_config.layers.contains(target)) {
                m_layerHistory.append(m_currentLayer);
                m_currentLayer = target;
                refreshLayers();
                drawGrid();
            }
        } else if (type == QLatin1String("back")) {
            if (!m_layerHistory.isEmpty()) {
                m_currentLayer = m_layerHistory.takeLast();
                refreshLayers();
                drawGrid();
            }
        } else if (type == QLatin1String("back_to_main")) {
            m_currentLayer = mc::kDefaultLayer;
            refreshLayers();
            drawGrid();
        }
        return;
    }

    // Alles Uebrige laeuft im Worker (Tastatur/Maus/HTTP koennen blockieren).
    const QString keyId = QStringLiteral("%1:%2").arg(m_currentLayer).arg(index);
    ma::ExecContext *ctx = &m_context;
    m_bridge->run<QString>(
        [type, payload, ctx, keyId]() -> QString {
            const auto error = ma::executeAction(type, payload, ctx, keyId);
            return error.value_or(QString());
        },
        [this](const QString &error) {
            if (!error.isEmpty())
                m_status->setText(error);
        },
        [this](const QString &err) { m_status->setText(err); });
}

void MacroManagerDialog::toggleMode(bool runMode)
{
    m_runMode = runMode;
    m_modeButton->setText(runMode ? _t("▶ Ausführen (Klick löst aus)")
                                  : _t("✎ Bearbeiten (Klick öffnet Editor)"));
    m_status->setText(runMode
                          ? _t("Ausführen-Modus — langes Halten öffnet den Editor.")
                          : _t("Bearbeiten-Modus — Klick auf eine Taste öffnet den Editor."));
}

void MacroManagerDialog::pollForeground()
{
    const auto [pid, exe] = core::foregroundProcess();
    if (exe.isEmpty() || exe == m_lastForegroundApp)
        return;
    m_lastForegroundApp = exe;
    // Layer suchen, dessen "app" zum Vordergrund-Programm passt.
    for (const QString &name : m_config.layerNames()) {
        const mc::Layer layer = m_config.layers.value(name);
        if (layer.app.isEmpty())
            continue;
        if (exe.contains(layer.app.toLower()) || layer.app.toLower().contains(exe)) {
            if (name != m_currentLayer) {
                m_currentLayer = name;
                refreshLayers();
                drawGrid();
            }
            return;
        }
    }
}

} // namespace ncssh::gui
