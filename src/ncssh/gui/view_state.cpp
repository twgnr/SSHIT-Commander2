#include "ncssh/gui/view_state.hpp"

#include "ncssh/core/settings.hpp"
#include "ncssh/gui/file_panel.hpp"

#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QTableView>
#include <QTreeView>
#include <QWidget>

namespace ncssh::gui {

namespace {

const QString kSettingKey = QStringLiteral("view_states");

// Nur eigene Fenster (Klassen aus ncssh::gui) — keine Qt-Standarddialoge,
// Menues oder Tooltips — und nicht das Hauptfenster.
bool isTracked(const QObject *obj)
{
    const auto *w = qobject_cast<const QWidget *>(obj);
    if (!w || !w->isWindow() || qobject_cast<const QMainWindow *>(w))
        return false;
    // windowType() vergleichen, nicht Flags maskieren: Qt::Popup usw. enthalten
    // das Window-Bit und traefen sonst jedes Fenster.
    const Qt::WindowType type = w->windowType();
    if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen)
        return false;
    return QLatin1String(w->metaObject()->className()).startsWith(QLatin1String("ncssh::gui::"));
}

QString windowKey(const QWidget *w)
{
    QString key = QString::fromLatin1(w->metaObject()->className()).mid(12);   // ohne ncssh::gui::
    if (!w->objectName().isEmpty())
        key += QLatin1Char('/') + w->objectName();
    return key;
}

// Kopfzeilen der Tabellen/Baumlisten eines Fensters in fester (Aufbau-)
// Reihenfolge. Datei-Panes speichern ihre Spalten selbst und bleiben aussen vor.
QList<QHeaderView *> headersOf(QWidget *window)
{
    QList<QHeaderView *> out;
    for (QHeaderView *h : window->findChildren<QHeaderView *>()) {
        if (h->orientation() != Qt::Horizontal)
            continue;
        QWidget *view = h->parentWidget();
        if (!qobject_cast<QTableView *>(view) && !qobject_cast<QTreeView *>(view))
            continue;
        bool inPane = false;
        for (QWidget *p = view; p && p != window; p = p->parentWidget())
            if (qobject_cast<FilePanel *>(p))
                inPane = true;
        if (!inPane)
            out << h;
    }
    return out;
}

QString headerKey(QHeaderView *h, int index)
{
    const QString name = h->parentWidget() ? h->parentWidget()->objectName() : QString();
    return name.isEmpty() ? QString::number(index) : name;
}

QJsonObject allStates()
{
    return QJsonObject::fromVariantMap(core::getSetting(kSettingKey).toMap());
}

} // namespace

ViewStateKeeper::ViewStateKeeper(QObject *parent) : QObject(parent) {}

void ViewStateKeeper::install()
{
    static ViewStateKeeper *keeper = nullptr;
    if (keeper || !qApp)
        return;
    keeper = new ViewStateKeeper(qApp);
    qApp->installEventFilter(keeper);
}

void ViewStateKeeper::save(QWidget *window)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("w"), window->width());
    entry.insert(QStringLiteral("h"), window->height());
    QJsonObject headers;
    const QList<QHeaderView *> list = headersOf(window);
    for (int i = 0; i < list.size(); ++i) {
        QHeaderView *h = list.at(i);
        if (h->count() == 0)
            continue;
        QJsonObject state;
        state.insert(QStringLiteral("n"), h->count());
        state.insert(QStringLiteral("s"), QString::fromLatin1(h->saveState().toBase64()));
        headers.insert(headerKey(h, i), state);
    }
    entry.insert(QStringLiteral("headers"), headers);
    QJsonObject all = allStates();
    all.insert(windowKey(window), entry);
    core::setSetting(kSettingKey, all);
}

void ViewStateKeeper::restore(QWidget *window)
{
    const QJsonObject entry = allStates().value(windowKey(window)).toObject();
    if (entry.isEmpty())
        return;

    // Groesse (nicht Position — Bildschirme wechseln). Auf den Bildschirm begrenzen.
    const int w = entry.value(QStringLiteral("w")).toInt();
    const int h = entry.value(QStringLiteral("h")).toInt();
    if (w > 0 && h > 0) {
        QSize size(w, h);
        if (const QScreen *screen = window->screen())
            size = size.boundedTo(screen->availableGeometry().size());
        window->resize(size);
    }

    const QJsonObject headers = entry.value(QStringLiteral("headers")).toObject();
    const QList<QHeaderView *> list = headersOf(window);
    for (int i = 0; i < list.size(); ++i) {
        QHeaderView *header = list.at(i);
        const QJsonObject state = headers.value(headerKey(header, i)).toObject();
        if (state.isEmpty())
            continue;
        const int count = state.value(QStringLiteral("n")).toInt();
        const QByteArray data =
            QByteArray::fromBase64(state.value(QStringLiteral("s")).toString().toLatin1());
        if (header->count() == count) {
            header->restoreState(data);
            continue;
        }
        // Spalten entstehen erst spaeter (Daten werden nachgeladen): anwenden,
        // sobald die Spaltenzahl passt — einmalig.
        auto conn = std::make_shared<QMetaObject::Connection>();
        QPointer<QHeaderView> guard(header);
        *conn = QObject::connect(header, &QHeaderView::sectionCountChanged, header,
                                 [guard, count, data, conn](int, int now) {
                                     if (!guard || now != count)
                                         return;
                                     QObject::disconnect(*conn);
                                     guard->restoreState(data);
                                 });
    }
}

bool ViewStateKeeper::eventFilter(QObject *obj, QEvent *event)
{
    // Schnell raus: nur Zeigen/Verbergen von Fenstern ist interessant.
    const QEvent::Type type = event->type();
    if (type != QEvent::Show && type != QEvent::Hide)
        return false;
    if (!isTracked(obj))
        return false;
    auto *window = static_cast<QWidget *>(obj);
    if (type == QEvent::Show)
        restore(window);
    else
        save(window);
    return false;
}

} // namespace ncssh::gui
