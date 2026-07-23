#include "ncssh/gui/macro_manager_dialog.hpp"

#include "ncssh/core/appmonitor.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/macro_key_editor.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QHash>
#include <QJsonArray>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
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

    // Zuletzt gemerkte Andock-Seite anwenden: nur im Ausfuehren-Modus und nur,
    // wenn der Dialog ein QMainWindow als Eltern hat (Bearbeiten bleibt schwebend).
    if (m_runMode && m_config.dock != QLatin1String("float")
        && qobject_cast<QMainWindow *>(parentWidget()))
        setDock(m_config.dock, /*persist=*/false);

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
    saveConfig();
}

void MacroManagerDialog::saveConfig()
{
    m_config.mode = m_runMode ? QStringLiteral("run") : QStringLiteral("edit");
    try {
        mc::save(m_config);
    } catch (...) {
    }
}

void MacroManagerDialog::buildUi()
{
    resize(980, 640);
    // Gesamte Oberflaeche in ein eigenes Widget legen, damit sie zwischen dem
    // schwebenden Dialog und einem angedockten QDockWidget umziehen kann.
    m_dialogLayout = new QVBoxLayout(this);
    m_dialogLayout->setContentsMargins(0, 0, 0, 0);
    m_content = new QWidget(this);
    m_dialogLayout->addWidget(m_content);
    auto *root = new QHBoxLayout(m_content);

    // --- Linke Spalte: Layer (nur im Bearbeiten-Modus sichtbar) ---
    m_leftPanel = new QWidget(m_content);
    auto *left = new QVBoxLayout(m_leftPanel);
    left->setContentsMargins(0, 0, 0, 0);
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

    // Import/Export gehoeren zur Editier-Oberflaeche (nur Bearbeiten-Modus).
    auto *ieRow = new QHBoxLayout();
    auto *exportBtn = new QPushButton(_t("Exportieren …"), this);
    auto *importBtn = new QPushButton(_t("Importieren …"), this);
    connect(exportBtn, &QPushButton::clicked, this, &MacroManagerDialog::exportLayers);
    // Import mit Auswahl — vorher wurde alles aus der Datei uebernommen.
    connect(importBtn, &QPushButton::clicked, this, &MacroManagerDialog::importLayers);
    ieRow->addWidget(exportBtn);
    ieRow->addWidget(importBtn);
    left->addLayout(ieRow);

    // Andocken: den Makro-Manager an einen Rand des Hauptfensters heften.
    // Die Auswahl gilt im Ausfuehren-Modus; im Bearbeiten-Modus bleibt das
    // Fenster schwebend (wie im Original).
    m_dockRow = new QWidget(m_content);
    auto *dockLayout = new QHBoxLayout(m_dockRow);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->addWidget(new QLabel(_t("Andocken:"), this));
    m_dockCombo = new QComboBox(m_dockRow);
    m_dockCombo->addItem(_t("Freischwebend"), QStringLiteral("float"));
    m_dockCombo->addItem(_t("Links"), QStringLiteral("left"));
    m_dockCombo->addItem(_t("Rechts"), QStringLiteral("right"));
    m_dockCombo->addItem(_t("Oben"), QStringLiteral("top"));
    m_dockCombo->addItem(_t("Unten"), QStringLiteral("bottom"));
    connect(m_dockCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { onDockCombo(); });
    dockLayout->addWidget(m_dockCombo, 1);
    left->addWidget(m_dockRow);
    left->addStretch(0);
    root->addWidget(m_leftPanel, 1);

    // --- Rechte Spalte: Raster ---
    auto *right = new QVBoxLayout();
    auto *topRow = new QHBoxLayout();
    m_modeButton = new QPushButton(this);
    m_modeButton->setCheckable(true);
    m_modeButton->setChecked(m_runMode);
    connect(m_modeButton, &QPushButton::toggled, this, &MacroManagerDialog::toggleMode);
    topRow->addWidget(m_modeButton);
    topRow->addStretch(1);
    right->addLayout(topRow);

    m_gridHost = new QWidget(this);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setSpacing(6);
    right->addWidget(m_gridHost, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    right->addWidget(m_status);

    m_closeButton = new QPushButton(_t("Schließen"), this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    right->addWidget(m_closeButton);
    root->addLayout(right, 3);

    updateModeLabel();
    applyModeVisibility();
    syncDockCombo();
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

    const QString keyId = QStringLiteral("%1:%2").arg(m_currentLayer).arg(index);

    // Sequenz: Schritte der Reihe nach, jeder erst nach Abschluss des
    // vorherigen — sonst wuerden Verzoegerungen und Reihenfolge wirkungslos.
    if (type == QLatin1String("sequence")) {
        std::vector<QJsonObject> steps;
        for (const QJsonValue &v : payload.toArray()) {
            if (v.isObject())
                steps.push_back(v.toObject());
        }
        runSteps(std::move(steps), keyId);
        return;
    }

    // Alles Uebrige laeuft im Worker (Tastatur/Maus/HTTP koennen blockieren).
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

void MacroManagerDialog::runSteps(std::vector<QJsonObject> steps, const QString &keyId)
{
    if (steps.empty())
        return;
    const QJsonObject step = steps.front();
    steps.erase(steps.begin());

    const QString type = step.value(QStringLiteral("action_type")).toString();
    const QJsonValue payload = step.value(QStringLiteral("payload"));
    ma::ExecContext *ctx = &m_context;
    m_bridge->run<QString>(
        [type, payload, ctx, keyId]() -> QString {
            const auto error = ma::executeAction(type, payload, ctx, keyId);
            return error.value_or(QString());
        },
        // Naechster Schritt erst, wenn dieser durch ist.
        [this, steps = std::move(steps), keyId](const QString &error) mutable {
            if (!error.isEmpty()) {
                // Bei einem Fehler die Sequenz abbrechen, statt blind
                // weiterzumachen — die Folgeschritte bauen darauf auf.
                m_status->setText(error);
                return;
            }
            runSteps(std::move(steps), keyId);
        },
        [this](const QString &err) { m_status->setText(err); });
}

void MacroManagerDialog::exportLayers()
{
    const QString path = getSaveFileName(this, _t("Makros exportieren"),
                                         QStringLiteral("makros-export.json"),
                                         _t("JSON-Dateien (*.json)"));
    if (path.isEmpty())
        return;
    try {
        mc::writeExport(m_config, path);
        QMessageBox::information(this, _t("Makro-Manager"),
                                 _t("%1 Layer exportiert.").arg(m_config.layers.size()));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Makro-Manager"),
                             _t("Export fehlgeschlagen: %1").arg(QString::fromUtf8(exc.what())));
    }
}

void MacroManagerDialog::importLayers()
{
    const QString path = getOpenFileName(this, _t("Makros importieren"), QString(),
                                         _t("JSON-Dateien (*.json)"));
    if (path.isEmpty())
        return;
    QMap<QString, mc::Layer> incoming;
    try {
        incoming = mc::readImport(path);
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Makro-Manager"),
                             _t("Import fehlgeschlagen: %1").arg(QString::fromUtf8(exc.what())));
        return;
    }
    if (incoming.isEmpty()) {
        QMessageBox::information(this, _t("Makro-Manager"), _t("Keine Layer in der Datei."));
        return;
    }

    // Auswahl, was uebernommen werden soll.
    QDialog dlg(this);
    dlg.setWindowTitle(_t("Layer importieren"));
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(_t("Zu importierende Layer auswählen:"), &dlg));
    auto *list = new QListWidget(&dlg);
    for (auto it = incoming.begin(); it != incoming.end(); ++it) {
        auto *item = new QListWidgetItem(it.key(), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(list, 1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QStringList added;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() != Qt::Checked)
            continue;
        const QString key = list->item(i)->text();
        mc::Layer layer = incoming.value(key);
        // Namenskonflikte aufloesen, statt vorhandene Layer zu ersetzen.
        const QString name = m_config.uniqueName(layer.name);
        layer.name = name;
        m_config.layers.insert(name, layer);
        m_config.order.append(name);
        added << name;
    }
    if (added.isEmpty())
        return;
    mc::save(m_config);
    refreshLayers();
    QMessageBox::information(this, _t("Makro-Manager"),
                             _t("%1 Layer importiert: %2")
                                 .arg(added.size()).arg(added.join(QStringLiteral(", "))));
}

void MacroManagerDialog::updateModeLabel()
{
    m_modeButton->setText(m_runMode ? _t("▶ Ausführen (Klick löst aus)")
                                    : _t("✎ Bearbeiten (Klick öffnet Editor)"));
    m_status->setText(m_runMode
                          ? _t("Ausführen-Modus — langes Halten öffnet den Editor.")
                          : _t("Bearbeiten-Modus — Klick auf eine Taste öffnet den Editor."));
}

void MacroManagerDialog::applyModeVisibility()
{
    // Layer-Editor, Andock-Auswahl und Schliessen-Knopf nur im Bearbeiten-Modus.
    const bool edit = !m_runMode;
    if (m_leftPanel)
        m_leftPanel->setVisible(edit);
    if (m_dockRow)
        m_dockRow->setVisible(edit);
    if (m_closeButton)
        m_closeButton->setVisible(edit && m_dockSide == QLatin1String("float"));
}

void MacroManagerDialog::toggleMode(bool runMode)
{
    m_runMode = runMode;
    m_config.mode = runMode ? QStringLiteral("run") : QStringLiteral("edit");
    updateModeLabel();
    applyModeVisibility();
    if (!m_runMode) {
        // Bearbeiten-Modus ist immer abgedockt (schwebend).
        if (m_dockSide != QLatin1String("float")) {
            setDock(QStringLiteral("float"), /*persist=*/false);
            return;
        }
    } else {
        // Ausfuehren-Modus: gemerkte Andock-Seite anwenden.
        if (m_config.dock != QLatin1String("float") && m_dockSide != m_config.dock
            && qobject_cast<QMainWindow *>(parentWidget())) {
            setDock(m_config.dock, /*persist=*/false);
            return;
        }
    }
    saveConfig();
}

// --- Andocken ---------------------------------------------------------------

void MacroManagerDialog::present()
{
    // Als "geoeffnet" merken, damit die Leiste beim naechsten Start zurueckkommt.
    if (!m_config.open) {
        m_config.open = true;
        saveConfig();
    }
    if (m_dockSide == QLatin1String("float") || !m_dock) {
        show();
        raise();
        activateWindow();
    } else {
        m_dock->show();
        m_dock->raise();
    }
}

void MacroManagerDialog::closeEvent(QCloseEvent *event)
{
    // Nur ein echtes, vom Nutzer ausgeloestes Schliessen (Fenster-X) merkt sich
    // "geschlossen". Programmatisches Schliessen (App-Beenden) laesst den
    // geoeffnet-Zustand bestehen, damit die Leiste beim Start zurueckkehrt.
    if (event->spontaneous()) {
        m_config.open = false;
        saveConfig();
    }
    QDialog::closeEvent(event);
}

void MacroManagerDialog::onDockCombo()
{
    const QString side = m_dockCombo->currentData().toString();
    m_config.dock = side;
    saveConfig();
    // Im Bearbeiten-Modus nur merken (Fenster bleibt schwebend); im Ausfuehren-
    // Modus sofort anwenden.
    if (m_runMode)
        setDock(side, /*persist=*/false);
}

void MacroManagerDialog::syncDockCombo()
{
    // Zeigt die gemerkte Andock-Seite (config.dock), nicht den momentanen
    // Zustand — im Bearbeiten-Modus ist das Fenster trotz Auswahl schwebend.
    const int i = m_dockCombo->findData(m_config.dock);
    if (i >= 0 && i != m_dockCombo->currentIndex()) {
        QSignalBlocker blocker(m_dockCombo);
        m_dockCombo->setCurrentIndex(i);
    }
}

void MacroManagerDialog::setDock(const QString &side, bool persist)
{
    if (side == m_dockSide)
        return;
    auto *main = qobject_cast<QMainWindow *>(parentWidget());
    if (side != QLatin1String("float") && !main) {
        QMessageBox::information(this, _t("Makro-Manager"),
                                 _t("Andocken ist nur im Hauptfenster möglich."));
        syncDockCombo();
        return;
    }

    static const QHash<QString, Qt::DockWidgetArea> areas = {
        {QStringLiteral("left"), Qt::LeftDockWidgetArea},
        {QStringLiteral("right"), Qt::RightDockWidgetArea},
        {QStringLiteral("top"), Qt::TopDockWidgetArea},
        {QStringLiteral("bottom"), Qt::BottomDockWidgetArea},
    };

    if (side == QLatin1String("float")) {
        // Inhalt zurueck in den schwebenden Dialog holen.
        if (m_dock) {
            m_dock->setWidget(nullptr);
            if (main)
                main->removeDockWidget(m_dock);
            m_dock->deleteLater();
            m_dock = nullptr;
        }
        m_content->setParent(nullptr);
        m_dialogLayout->addWidget(m_content);
        m_content->show();
        m_dockSide = QStringLiteral("float");
        show();
        raise();
        activateWindow();
    } else {
        // QDockWidget (ohne Titelleiste) erzeugen bzw. verschieben.
        if (!m_dock) {
            m_dock = new QDockWidget(_t("Makro-Manager"), main);
            m_dock->setObjectName(QStringLiteral("MacroManagerDock"));
            m_dock->setTitleBarWidget(new QWidget(m_dock));   // keine Titelleiste
            m_dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
            m_dock->setWidget(m_content);
        }
        main->addDockWidget(areas.value(side), m_dock);
        m_dock->show();
        m_dockSide = side;
        hide();  // schwebende Huelle ausblenden
    }

    if (persist) {
        m_config.dock = m_dockSide;
    }
    saveConfig();
    syncDockCombo();
    applyModeVisibility();  // Schliessen-Knopf nur schwebend zeigen
    drawGrid();             // Anordnung an neuen Zustand anpassen
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
