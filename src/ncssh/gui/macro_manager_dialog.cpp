#include "ncssh/gui/macro_manager_dialog.hpp"

#include "ncssh/core/appmonitor.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/global_hotkeys.hpp"
#include "ncssh/gui/macro_key_editor.hpp"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QScreen>
#include <QStandardPaths>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QContextMenuEvent>
#include <QMenu>
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
    // Keinen Tastaturfokus annehmen: sonst tippte "Text tippen" in die Taste
    // statt in das zuvor fokussierte Feld (Terminal, Editor …).
    setFocusPolicy(Qt::NoFocus);
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

void KeyTile::contextMenuEvent(QContextMenuEvent *event)
{
    // Rechtsklick auf eine Kachel: Bearbeiten/Ausfuehren/Leeren (siehe Dialog).
    m_holdTimer->stop();
    emit contextRequested(m_index, event->globalPos());
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
// ClipHost
// ---------------------------------------------------------------------------

namespace {

// Traegt das Tastenraster, ohne dessen Mindestgroesse nach aussen zu melden.
// Ist Platz da, fuellt das Raster ihn (die Tasten ruecken auseinander). Wird es
// enger, schrumpfen erst die Abstaende; danach bleibt das Raster in seiner
// Mindestgroesse oben links stehen und die unteren/rechten Tasten verschwinden
// am Rand — statt das Hauptfenster am Verkleinern zu hindern.
class ClipHost : public QWidget {
public:
    ClipHost(QWidget *inner, QWidget *parent) : QWidget(parent), m_inner(inner)
    {
        inner->setParent(this);
        inner->installEventFilter(this);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override { return m_inner->sizeHint(); }
    QSize minimumSizeHint() const override { return {0, 0}; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        place();
    }

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        // Raster neu aufgebaut (andere Groesse/Anzahl): Hinweis nach aussen
        // weitergeben und neu platzieren.
        if (obj == m_inner && event->type() == QEvent::LayoutRequest) {
            updateGeometry();
            place();
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    void place()
    {
        const QSize min = m_inner->minimumSizeHint();
        m_inner->setGeometry(0, 0, qMax(width(), min.width()), qMax(height(), min.height()));
    }

    QWidget *m_inner;
};

// Auswahl-Menue an der Mauszeiger-Position; liefert den gewaehlten Eintrag
// (leer = abgebrochen). Menuetexte werden gekuerzt, '&' nicht als Mnemonic.
QString pickFromMenu(QWidget *parent, const QStringList &entries)
{
    QMenu menu(parent);
    for (const QString &entry : entries) {
        QString label = entry.section(QLatin1Char('\n'), 0, 0);
        if (label.size() > 60 || entry.contains(QLatin1Char('\n')))
            label = label.left(57) + QStringLiteral(" …");
        label.replace(QLatin1Char('&'), QStringLiteral("&&"));
        QAction *action = menu.addAction(label);
        action->setData(entry);
    }
    QAction *chosen = menu.exec(QCursor::pos());
    return chosen ? chosen->data().toString() : QString();
}

// Zustaende einer Mehrzustands-Taste: JSON-Liste von Objekten
// {label, action_type, payload}; auch {"states": [...]} oder der Rohtext,
// falls der Editor ungueltiges JSON als Text gespeichert hat.
QJsonArray toggleStates(const QJsonValue &payload)
{
    if (payload.isArray())
        return payload.toArray();
    if (payload.isObject())
        return payload.toObject().value(QStringLiteral("states")).toArray();
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toString().toUtf8());
    if (doc.isArray())
        return doc.array();
    if (doc.isObject())
        return doc.object().value(QStringLiteral("states")).toArray();
    return {};
}

} // namespace

// Leitet Rueckrufe aus Worker-Threads in den GUI-Thread des Dialogs weiter.
// executeAction ruft sshSend/sshBroadcast im Worker; die Lambdas des
// Hauptfensters fassen aber Widgets an (nur im GUI-Thread erlaubt). Bewusst
// NICHT BlockingQueuedConnection: beim Beenden wartet der GUI-Thread in
// AsyncBridge::stop auf die Worker — ein blockierender Aufruf haette dort
// einen Deadlock. Posted Events zum selben Thread bleiben in Reihenfolge,
// d.h. der Konsolenbefehl kommt vor dem onDone des Jobs an.
// target wird im Dialog-Destruktor unter dem Mutex genullt; danach kann kein
// Worker mehr etwas an den sterbenden Dialog posten (bereits gepostete
// Events verwirft ~QObject).
struct MacroGuiGate {
    QMutex mutex;
    QObject *target = nullptr;

    void post(std::function<void()> fn)
    {
        QMutexLocker lock(&mutex);
        if (target)
            QMetaObject::invokeMethod(target, std::move(fn), Qt::QueuedConnection);
    }
};

// ---------------------------------------------------------------------------
// MacroManagerDialog
// ---------------------------------------------------------------------------

MacroManagerDialog::MacroManagerDialog(AsyncBridge *bridge,
                                       std::function<void(const QString &, bool)> sshSend,
                                       std::function<void(const QString &, bool)> sshBroadcast,
                                       QWidget *parent)
    : QDialog(parent), m_bridge(bridge),
      m_context(std::make_shared<ma::ExecContext>()),
      m_gate(std::make_shared<MacroGuiGate>())
{
    setWindowTitle(_t("Makro-Manager"));
    m_config = mc::load();
    m_gate->target = this;
    // Nur belegte Rueckrufe umhuellen — ein leerer bleibt leer, damit
    // executeAction weiterhin "Keine aktive Konsole" melden kann.
    if (sshSend) {
        m_context->sshSend = [gate = m_gate, fn = std::move(sshSend)](const QString &cmd,
                                                                      bool run) {
            gate->post([fn, cmd, run] { fn(cmd, run); });
        };
    }
    if (sshBroadcast) {
        m_context->sshBroadcast = [gate = m_gate, fn = std::move(sshBroadcast)](
                                      const QString &cmd, bool run) {
            gate->post([fn, cmd, run] { fn(cmd, run); });
        };
    }
    // Zwischenablage-Verlauf fuer die Aktion clipboard_history mitschreiben.
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        connect(clipboard, &QClipboard::dataChanged, this,
                &MacroManagerDialog::onClipboardChanged);
    m_currentLayer = m_config.layerNames().value(0, mc::kDefaultLayer);
    m_runMode = (m_config.mode == QLatin1String("run"));

    buildUi();
    refreshLayers();
    drawGrid();

    m_hotkeys = new GlobalHotkeys(this);
    connect(m_hotkeys, &GlobalHotkeys::activated, this, &MacroManagerDialog::onHotkey);
    updateHotkeys();

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

    // Zielfenster fuer Tastatur-Makros mitschreiben (nur fremde Fenster).
    m_targetTimer = new QTimer(this);
    m_targetTimer->setInterval(250);
    connect(m_targetTimer, &QTimer::timeout, this, [this] {
        const quintptr fg = core::foregroundWindowHandle();
        if (fg && !core::isOwnProcessWindow(fg))
            m_lastExternalWindow = fg;
    });
    m_targetTimer->start();
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive)
                    m_appActivatedAt.restart();
            });
}

MacroManagerDialog::~MacroManagerDialog()
{
    // Laufende Worker duerfen ab jetzt nichts mehr an diesen Dialog posten.
    {
        QMutexLocker lock(&m_gate->mutex);
        m_gate->target = nullptr;
    }
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

    // Raster-/Groessenfelder: Reihen und Spalten des aktuellen Layers sowie die
    // globale Tastengroesse — direkt anpassbar.
    auto *dims = new QFormLayout();
    dims->setContentsMargins(0, 8, 0, 0);
    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setRange(1, 12);
    connect(m_rowsSpin, &QSpinBox::valueChanged, this, [this](int) { onDimsChanged(); });
    dims->addRow(_t("Reihen:"), m_rowsSpin);
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 16);
    connect(m_colsSpin, &QSpinBox::valueChanged, this, [this](int) { onDimsChanged(); });
    dims->addRow(_t("Spalten:"), m_colsSpin);
    m_keySize = new QSpinBox(this);
    m_keySize->setRange(56, 200);
    m_keySize->setSingleStep(8);
    m_keySize->setValue(m_config.keySize);
    connect(m_keySize, &QSpinBox::valueChanged, this, [this](int value) {
        m_config.keySize = value;
        saveConfig();
        drawGrid();
    });
    dims->addRow(_t("Größe:"), m_keySize);
    left->addLayout(dims);

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
    // Fenster schwebend.
    m_dockRow = new QWidget(m_content);
    auto *dockLayout = new QHBoxLayout(m_dockRow);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->addWidget(new QLabel(_t("Andocken:"), this));
    m_dockCombo = new QComboBox(m_dockRow);
    m_dockCombo->setObjectName(QStringLiteral("MacroDockCombo"));
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
    m_rightLayout = right;
    auto *topRow = new QHBoxLayout();
    m_modeButton = new QPushButton(this);
    m_modeButton->setObjectName(QStringLiteral("MacroModeButton"));
    m_modeButton->setCheckable(true);
    m_modeButton->setChecked(m_runMode);
    connect(m_modeButton, &QPushButton::toggled, this, &MacroManagerDialog::toggleMode);
    topRow->addWidget(m_modeButton);
    topRow->addStretch(1);
    right->addLayout(topRow);

    m_gridHost = new QWidget(this);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setSpacing(6);
    // Ueber ClipHost: zu wenig Platz schneidet Tasten ab, statt das Fenster
    // (bzw. das Andock-Feld) auf die volle Rastergroesse festzunageln.
    right->addWidget(new ClipHost(m_gridHost, this), 1);

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
    updateHotkeys();   // Name/Groesse des Layers stecken in der Zuordnung
}

void MacroManagerDialog::deleteLayer()
{
    if (m_config.layerNames().size() <= 1) {
        QMessageBox::information(this, _t("Löschen"),
                                 _t("Der letzte Layer kann nicht gelöscht werden."));
        return;
    }
    if (QMessageBox::question(this, _t("Löschen"),
                              _t("Layer \"%1\" löschen?").arg(m_currentLayer))
        != QMessageBox::Yes)
        return;
    m_config.removeLayer(m_currentLayer);
    m_currentLayer = m_config.layerNames().value(0);
    mc::save(m_config);
    refreshLayers();
    drawGrid();
    updateHotkeys();
}

void MacroManagerDialog::onDimsChanged()
{
    mc::Layer *layer = currentLayer();
    if (!layer)
        return;
    layer->rows = m_rowsSpin->value();
    layer->cols = m_colsSpin->value();
    saveConfig();
    drawGrid();
    updateHotkeys();   // ausgeblendete Tasten loesen nicht mehr aus
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

    // Reihen/Spalten-Felder ohne Rueckkopplung auf den aktuellen Layer setzen.
    {
        QSignalBlocker rb(m_rowsSpin);
        QSignalBlocker cb(m_colsSpin);
        m_rowsSpin->setValue(layer->rows);
        m_colsSpin->setValue(layer->cols);
    }

    const int size = m_config.keySize;
    for (int index = 0; index < layer->capacity(); ++index) {
        auto *tile = new KeyTile(index, m_gridHost);
        const auto key = layer->key(index);
        tile->setConfig(key.value_or(QJsonObject()), size);
        // Mehrzustands-Taste: Beschriftung des aktuellen Zustands behalten,
        // auch nach Layerwechsel/Neuaufbau.
        if (key && key->value(QStringLiteral("action_type")).toString()
                       == QLatin1String("toggle_state")) {
            const QString label = toggleStateLabel(
                key->value(QStringLiteral("payload")),
                QStringLiteral("%1:%2").arg(m_currentLayer).arg(index));
            if (!label.isEmpty())
                tile->setDynamicText(label);
        }
        connect(tile, &KeyTile::clickedTile, this, &MacroManagerDialog::onTileClicked);
        connect(tile, &KeyTile::heldTile, this, &MacroManagerDialog::onTileHeld);
        connect(tile, &KeyTile::contextRequested, this, &MacroManagerDialog::onTileContextMenu);
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
    if (m_runMode) {
        if (const auto key = layer->key(index))
            runKey(*key, index, m_currentLayer);
        return;
    }
    // Bearbeiten-Modus: Editor oeffnen.
    editKey(index);
}

void MacroManagerDialog::onTileHeld(int index)
{
    // Langes Halten oeffnet den Editor auch im Ausfuehren-Modus.
    if (!m_runMode)
        return;
    editKey(index);
}

void MacroManagerDialog::editKey(int index)
{
    // Layer per Name festhalten: waehrend des modalen Editors kann der
    // kontextabhaengige Wechsel den aktuellen Layer umstellen.
    const QString layerName = m_currentLayer;
    mc::Layer *layer = m_config.get(layerName);
    if (!layer)
        return;
    // Globale Kuerzel aussetzen, solange der Editor offen ist: sonst faengt
    // Windows die Kombination ab, bevor das Kuerzel-Feld sie aufnehmen kann.
    m_hotkeysSuspended = true;
    updateHotkeys();
    MacroKeyEditor editor(layer->key(index).value_or(mc::newKey()), m_config.layerNames(), this);
    const bool accepted = editor.exec() == QDialog::Accepted;
    m_hotkeysSuspended = false;
    layer = m_config.get(layerName);
    if (!accepted || !layer) {
        updateHotkeys();
        return;
    }
    const QJsonObject config = editor.cleared() ? QJsonObject() : editor.config();
    layer->setKey(index, config);
    mc::save(m_config);
    drawGrid();

    const QStringList failed = updateHotkeys();
    const QKeySequence seq(config.value(QStringLiteral("shortcut")).toString(),
                           QKeySequence::PortableText);
    const QString combo =
        seq.isEmpty() ? QString() : QKeySequence(seq[0]).toString(QKeySequence::NativeText);
    if (!combo.isEmpty() && failed.contains(combo)) {
        QMessageBox::warning(this, _t("Makro-Manager"),
                             _t("Das globale Kürzel %1 konnte nicht registriert werden. "
                                "Vermutlich nutzt es bereits ein anderes Programm.")
                                 .arg(combo));
    }
}

QStringList MacroManagerDialog::updateHotkeys()
{
    if (!m_hotkeys)
        return {};
    m_hotkeys->clear();
    m_hotkeyKeys.clear();
    if (m_hotkeysSuspended)
        return {};

    QHash<QString, int> idByCombo;   // Kombination -> ID (-1 = nicht registrierbar)
    QStringList failed;
    for (const QString &name : m_config.layerNames()) {
        const mc::Layer layer = m_config.layers.value(name);
        for (auto it = layer.keys.cbegin(); it != layer.keys.cend(); ++it) {
            // Nur sichtbare Tasten — ausgeblendete (Raster verkleinert) zaehlen nicht.
            if (it.key() < 0 || it.key() >= layer.capacity())
                continue;
            const QKeySequence seq(it.value().value(QStringLiteral("shortcut")).toString(),
                                   QKeySequence::PortableText);
            if (seq.isEmpty())
                continue;
            // Nur die erste Kombination zaehlt (Mehrfach-Folgen kann Windows nicht).
            const QKeySequence first(seq[0]);
            const QString combo = first.toString(QKeySequence::NativeText);
            int id = idByCombo.value(combo, -2);
            if (id == -2) {
                id = int(m_hotkeyKeys.size());
                if (m_hotkeys->add(id, first)) {
                    m_hotkeyKeys.emplace_back();
                } else {
                    id = -1;
                    failed << combo;
                }
                idByCombo.insert(combo, id);
            }
            if (id >= 0)
                m_hotkeyKeys[size_t(id)].emplace_back(name, it.key());
        }
    }
    if (!failed.isEmpty())
        m_status->setText(_t("Globale Kürzel nicht registrierbar (bereits belegt?): %1")
                              .arg(failed.join(QStringLiteral(", "))));
    return failed;
}

void MacroManagerDialog::onHotkey(int id)
{
    if (id < 0 || id >= int(m_hotkeyKeys.size()) || m_hotkeyKeys[size_t(id)].empty())
        return;
    // Mehrere Tasten mit derselben Kombination: die im aktuellen Layer zuerst.
    const auto &targets = m_hotkeyKeys[size_t(id)];
    auto target = targets.front();
    for (const auto &t : targets) {
        if (t.first == m_currentLayer) {
            target = t;
            break;
        }
    }

    fireHotkey(target.first, target.second, 100);
}

void MacroManagerDialog::fireHotkey(const QString &layerName, int index, int tries)
{
    // Erst ausfuehren, wenn Strg/Alt/… losgelassen sind — sonst wuerde getippter
    // Text oder ein simuliertes Kuerzel mit der noch gedrueckten Ausloese-
    // Kombination vermischt. Hoechstens ~1,5 s warten.
    if (tries > 0 && GlobalHotkeys::modifiersHeld()) {
        QTimer::singleShot(15, this, [this, layerName, index, tries] {
            fireHotkey(layerName, index, tries - 1);
        });
        return;
    }
    // Konfiguration erneut lesen: die Taste kann inzwischen geaendert sein.
    const mc::Layer *layer = m_config.get(layerName);
    if (const auto key = layer ? layer->key(index) : std::nullopt)
        runKey(*key, index, layerName, /*viaHotkey=*/true);
}

void MacroManagerDialog::onTileContextMenu(int index, const QPoint &globalPos)
{
    mc::Layer *layer = currentLayer();
    if (!layer)
        return;
    const bool has = layer->key(index).has_value();
    QMenu menu(this);
    QAction *edit = menu.addAction(_t("Bearbeiten …"));
    QAction *run = menu.addAction(_t("Ausführen"));
    run->setEnabled(has);
    menu.addSeparator();
    QAction *clear = menu.addAction(_t("Leeren"));
    clear->setEnabled(has);

    QAction *chosen = menu.exec(globalPos);
    if (chosen == edit) {
        editKey(index);
    } else if (chosen == run) {
        if (const auto key = layer->key(index))
            runKey(*key, index, m_currentLayer);
    } else if (chosen == clear) {
        layer->setKey(index, QJsonObject());   // leeres Objekt entfernt die Taste
        mc::save(m_config);
        drawGrid();
        updateHotkeys();
    }
}

void MacroManagerDialog::runKey(const QJsonObject &config, int index, const QString &layer,
                                bool viaHotkey)
{
    const QString type = config.value(QStringLiteral("action_type")).toString();
    const QJsonValue payload = config.value(QStringLiteral("payload"));
    const QString keyId = QStringLiteral("%1:%2").arg(layer).arg(index);

    // Kam der Klick aus einem anderen Programm (die App wurde gerade erst
    // durch diesen Klick aktiv), gilt das zuvor aktive Fenster als Ziel:
    // erst dorthin zurueck, dann ausfuehren — wie bei einem Stream Deck.
    // Wer schon in der App arbeitete, tippt weiter ins fokussierte Feld.
    // Ein globales Kuerzel wirkt immer im Fenster, das gerade vorn ist.
    const ma::ActionSpec &spec = ma::spec(type);
    const bool consoleAction = type == QLatin1String("ssh_command")
                               || type == QLatin1String("ssh_broadcast");
    const bool fromOutside = !viaHotkey && m_appActivatedAt.isValid()
                             && m_appActivatedAt.elapsed() < 1000
                             && QGuiApplication::applicationState() == Qt::ApplicationActive;
    if (fromOutside && !spec.navigation && !spec.gui && !consoleAction
        && core::bringWindowToFront(m_lastExternalWindow)) {
        m_appActivatedAt.invalidate();
        // Kurz warten, bis das Fenster wirklich vorn ist und Eingaben annimmt.
        QTimer::singleShot(120, this, [this, config, index, layer] {
            runKey(config, index, layer);
        });
        return;
    }

    // Sequenz/Mehrere Aktionen: Schritte der Reihe nach, jeder erst nach
    // Abschluss des vorherigen — sonst wuerden Verzoegerungen und Reihenfolge
    // wirkungslos. Ueber den Dialog statt komplett im Worker, damit auch
    // Navigations- und GUI-Schritte wirken (executeAction ueberspringt sie).
    if (type == QLatin1String("sequence") || type == QLatin1String("multi_action")) {
        std::vector<QJsonObject> steps;
        for (const QJsonValue &v : payload.toArray()) {
            if (v.isObject())
                steps.push_back(v.toObject());
        }
        // Sequenz: bei jedem Druck nur der naechste Schritt (danach von vorn).
        if (type == QLatin1String("sequence") && !steps.empty()) {
            const QJsonObject one =
                steps[ma::nextSequenceStep(m_context.get(), keyId, int(steps.size()))];
            steps = {one};
        }
        runSteps(std::move(steps), keyId, index);
        return;
    }
    runAction(type, payload, keyId, index, {});
}

void MacroManagerDialog::runAction(const QString &type, const QJsonValue &payload,
                                   const QString &keyId, int index,
                                   std::function<void(bool)> done)
{
    const ma::ActionSpec &spec = ma::spec(type);
    // Navigations-Aktionen behandelt das Fenster selbst.
    if (spec.navigation) {
        navigate(type, payload);
        if (done)
            done(true);
        return;
    }
    // GUI-Aktionen (Popups, Zustandstasten, Bildschirmfoto) gehoeren in den
    // GUI-Thread — executeAction gibt fuer sie nur still nullopt zurueck.
    if (spec.gui) {
        runGuiAction(type, payload, keyId, index, std::move(done));
        return;
    }

    // Alles Uebrige laeuft im Worker (Tastatur/Maus/HTTP koennen blockieren).
    // Den Kontext als shared_ptr mitnehmen: der Job darf den Dialog ueberleben.
    const std::shared_ptr<ma::ExecContext> ctx = m_context;
    m_bridge->run<QString>(
        [type, payload, ctx, keyId]() -> QString {
            const auto error = ma::executeAction(type, payload, ctx.get(), keyId);
            return error.value_or(QString());
        },
        [this, done](const QString &error) {
            if (!error.isEmpty())
                m_status->setText(error);
            if (done)
                done(error.isEmpty());
        },
        [this, done](const QString &err) {
            if (err != QLatin1String("cancelled"))
                m_status->setText(err);
            if (done)
                done(false);
        }, this);
}

void MacroManagerDialog::navigate(const QString &type, const QJsonValue &payload)
{
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
}

void MacroManagerDialog::runSteps(std::vector<QJsonObject> steps, const QString &keyId,
                                  int index)
{
    if (steps.empty())
        return;
    const QJsonObject step = steps.front();
    steps.erase(steps.begin());

    const QString type =
        step.value(QStringLiteral("action_type")).toString(QStringLiteral("none"));
    const QJsonValue payload = step.value(QStringLiteral("payload"));
    // Naechster Schritt erst, wenn dieser durch ist. Bei einem Fehler (oder
    // abgebrochener Auswahl) die Sequenz beenden, statt blind weiterzumachen —
    // die Folgeschritte bauen darauf auf. Die Fehlermeldung setzt runAction.
    runAction(type, payload, keyId, index, [this, steps, keyId, index](bool ok) {
        if (ok)
            runSteps(steps, keyId, index);
    });
}

void MacroManagerDialog::runGuiAction(const QString &type, const QJsonValue &payload,
                                      const QString &keyId, int index,
                                      std::function<void(bool)> done)
{
    const auto finish = [&done](bool ok) {
        if (done)
            done(ok);
    };
    // Menues laufen in einer eigenen Ereignisschleife — danach pruefen, ob es
    // den Dialog noch gibt.
    QPointer<MacroManagerDialog> self(this);

    if (type == QLatin1String("screenshot")) {
        finish(takeScreenshot(payload.toString()));
        return;
    }

    if (type == QLatin1String("toggle_state")) {
        // Jeder Druck fuehrt die Aktion des aktuellen Zustands aus und
        // wechselt zum naechsten.
        const QJsonArray states = toggleStates(payload);
        if (states.isEmpty()) {
            m_status->setText(_t("Mehrzustands-Taste: keine Zustände hinterlegt "
                                 "(JSON-Liste mit action_type/payload/label)."));
            finish(false);
            return;
        }
        int current = 0;
        {
            QMutexLocker lock(&m_context->stateMutex);
            current = m_context->cycleIndex.value(keyId, 0) % int(states.size());
            m_context->cycleIndex.insert(keyId, (current + 1) % int(states.size()));
        }
        // Beschriftung zeigt den Zustand, der beim naechsten Druck dran ist.
        const QString nextLabel = toggleStateLabel(payload, keyId);
        if (!nextLabel.isEmpty() && keyId.startsWith(m_currentLayer + QLatin1Char(':'))
            && index >= 0 && index < int(m_tiles.size()))
            m_tiles[size_t(index)]->setDynamicText(nextLabel);

        const QJsonObject state = states.at(current).toObject();
        const QString stateType =
            state.value(QStringLiteral("action_type")).toString(QStringLiteral("none"));
        // Keine verschachtelten Mehrzustands-Tasten (Endlosschleife).
        if (stateType == QLatin1String("toggle_state")) {
            finish(true);
            return;
        }
        runAction(stateType, state.value(QStringLiteral("payload")),
                  keyId + QStringLiteral("/") + QString::number(current), index,
                  std::move(done));
        return;
    }

    if (type == QLatin1String("command_cycle")) {
        // Eine Zeile pro Eintrag; der gewaehlte Text wird getippt.
        QStringList entries;
        for (const QString &line : payload.toString().split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty())
                entries << trimmed;
        }
        if (entries.isEmpty()) {
            finish(false);
            return;
        }
        const QString chosen = pickFromMenu(this, entries);
        if (!self)
            return;
        if (chosen.isEmpty()) {
            finish(false);   // abgebrochen -> Sequenz nicht fortsetzen
            return;
        }
        runAction(QStringLiteral("write"), chosen, keyId, index, std::move(done));
        return;
    }

    if (type == QLatin1String("clipboard_history")) {
        if (m_clipHistory.isEmpty()) {
            m_status->setText(_t("Zwischenablage-Verlauf ist leer."));
            finish(false);
            return;
        }
        const QString chosen = pickFromMenu(this, m_clipHistory);
        if (!self)
            return;
        if (chosen.isEmpty()) {
            finish(false);
            return;
        }
        QGuiApplication::clipboard()->setText(chosen);
        finish(true);
        return;
    }

    finish(true);
}

bool MacroManagerDialog::takeScreenshot(const QString &folderIn)
{
    // Bildschirm unter dem Mauszeiger (dort wurde die Taste geklickt).
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QPixmap shot = screen ? screen->grabWindow(0) : QPixmap();
    if (shot.isNull()) {
        m_status->setText(_t("Bildschirmfoto fehlgeschlagen."));
        return false;
    }
    QString folder = folderIn.trimmed();
    if (folder.isEmpty())
        folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (folder.isEmpty())
        folder = QDir::homePath();
    const QString file = QDir(folder).filePath(
        QStringLiteral("screenshot-%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    if (!QDir().mkpath(folder) || !shot.save(file, "PNG")) {
        m_status->setText(_t("Bildschirmfoto konnte nicht gespeichert werden: %1")
                              .arg(QDir::toNativeSeparators(file)));
        return false;
    }
    QGuiApplication::clipboard()->setPixmap(shot);
    m_status->setText(_t("Bildschirmfoto gespeichert: %1").arg(QDir::toNativeSeparators(file)));
    return true;
}

QString MacroManagerDialog::toggleStateLabel(const QJsonValue &payload, const QString &keyId)
{
    const QJsonArray states = toggleStates(payload);
    if (states.isEmpty())
        return {};
    int current = 0;
    {
        QMutexLocker lock(&m_context->stateMutex);
        if (!m_context->cycleIndex.contains(keyId))
            return {};   // noch nie gedrueckt -> normale Beschriftung
        current = m_context->cycleIndex.value(keyId) % int(states.size());
    }
    return states.at(current).toObject().value(QStringLiteral("label")).toString();
}

void MacroManagerDialog::onClipboardChanged()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.trimmed().isEmpty())
        return;
    // Neueste zuerst, ohne Dubletten, begrenzt.
    m_clipHistory.removeAll(text);
    m_clipHistory.prepend(text);
    while (m_clipHistory.size() > 20)
        m_clipHistory.removeLast();
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
    updateHotkeys();
    QMessageBox::information(this, _t("Makro-Manager"),
                             _t("%1 Layer importiert: %2")
                                 .arg(added.size()).arg(added.join(QStringLiteral(", "))));
}

void MacroManagerDialog::updateModeLabel()
{
    // Im Bearbeiten-Modus beschreibt der Knopf, was ein Klick tut: zurueck in
    // den Ausfuehren-Modus — mit gewaehltem Andock-Rand also an die App andocken.
    if (m_runMode)
        m_modeButton->setText(_t("▶ Ausführen (Klick löst aus)"));
    else if (m_config.dock != QLatin1String("float"))
        m_modeButton->setText(_t("An App andocken"));
    else
        m_modeButton->setText(_t("▶ Ausführen (schwebend)"));
    m_status->setText(m_runMode
                          ? _t("Ausführen-Modus — langes Halten öffnet den Editor.")
                          : _t("Bearbeiten-Modus — Klick auf eine Taste öffnet den Editor."));
}

void MacroManagerDialog::applyModeVisibility()
{
    const bool edit = !m_runMode;
    const bool docked = m_dockSide != QLatin1String("float");
    // Layer-Editor, Andock-Auswahl und Schliessen-Knopf nur im Bearbeiten-Modus.
    if (m_leftPanel)
        m_leftPanel->setVisible(edit);
    if (m_dockRow)
        m_dockRow->setVisible(edit);
    if (m_closeButton)
        m_closeButton->setVisible(edit && !docked);
    // Angedockt nur die Tasten zeigen: kein Modus-Knopf, keine Statuszeile
    // (zum Bearbeiten oeffnet man den Makro-Manager erneut).
    if (m_modeButton)
        m_modeButton->setVisible(!docked);
    if (m_status)
        m_status->setVisible(!docked);

    // Raender: angedockt so eng wie moeglich an den Fensterrand, sonst normal.
    const int margin = docked ? 0 : 9;
    if (m_content && m_content->layout())
        m_content->layout()->setContentsMargins(margin, margin, margin, margin);
    if (m_rightLayout) {
        m_rightLayout->setContentsMargins(0, 0, 0, 0);
        m_rightLayout->setSpacing(docked ? 0 : 6);
    }
    if (m_grid) {
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setSpacing(docked ? 2 : 6);
    }
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

void MacroManagerDialog::openManager()
{
    // Zum Bearbeiten immer die schwebende Oberflaeche zeigen. Ist die Leiste
    // angedockt, loest der Wechsel in den Bearbeiten-Modus sie ab (schwebend).
    if (!m_config.open) {
        m_config.open = true;
        saveConfig();
    }
    if (m_runMode)
        m_modeButton->setChecked(false);  // -> toggleMode(false): Bearbeiten + schwebend
    show();
    raise();
    activateWindow();
}

void MacroManagerDialog::rememberVisibility()
{
    const bool docked = m_dockSide != QLatin1String("float");
    const bool visible = docked ? (m_dock && m_dock->isVisible()) : isVisible();
    const bool open = m_runMode && visible;
    if (m_config.open != open) {
        m_config.open = open;
        saveConfig();
    }
}

bool MacroManagerDialog::shouldRestore()
{
    const mc::MacroConfig config = mc::load();
    return config.open && config.mode == QLatin1String("run");
}

bool MacroManagerDialog::hasGlobalShortcuts()
{
    const mc::MacroConfig config = mc::load();
    for (const mc::Layer &layer : config.layers) {
        for (const QJsonObject &key : layer.keys) {
            if (!key.value(QStringLiteral("shortcut")).toString().isEmpty())
                return true;
        }
    }
    return false;
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
    updateModeLabel();   // Knopftext haengt an der gewaehlten Seite
    if (m_runMode) {
        // Bereits im Ausfuehren-Modus: Seite direkt anwenden.
        setDock(side, /*persist=*/false);
    } else if (side != QLatin1String("float")) {
        // Bearbeiten-Modus ist immer schwebend. Damit die Auswahl "Rechts" o.ae.
        // sofort sichtbar wird, schalten wir in den Ausfuehren-Modus — das
        // Umschalten wendet die gemerkte Seite an (toggleMode -> setDock).
        m_modeButton->setChecked(true);
    }
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
