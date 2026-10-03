#include "ncssh/gui/line_completer.hpp"

#include "ncssh/core/filesystem.hpp"

#include <QDir>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>

#include <algorithm>

namespace ncssh::gui {

LineCompleter::LineCompleter(AsyncBridge *bridge, QLineEdit *edit, bool handleTab)
    : QObject(edit), m_bridge(bridge), m_edit(edit)
{
    if (handleTab)
        edit->installEventFilter(this);
}

bool LineCompleter::eventFilter(QObject *obj, QEvent *event)
{
    // Tab vervollstaendigt (statt den Fokus zu wechseln); Shift+Tab bleibt
    // der normale Rueckwaerts-Fokuswechsel.
    if (obj == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab && !(ke->modifiers() & Qt::ShiftModifier)) {
            complete();
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void LineCompleter::complete()
{
    if (!m_provider)
        return;
    // Erneutes Tab direkt nach einer Vervollstaendigung mit mehreren Treffern:
    // zum naechsten Kandidaten weiterschalten (nach dem letzten wieder der erste).
    if (m_tabCycle.size() > 1 && m_edit->text() == m_tabCycleText
        && m_edit->cursorPosition() == m_tabCyclePos) {
        m_tabCycleIndex = (m_tabCycleIndex + 1) % m_tabCycle.size();
        const QString newBefore = m_tabCycle.at(m_tabCycleIndex);
        m_edit->setText(newBefore + m_tabCycleAfter);
        m_edit->setCursorPosition(newBefore.size());
        m_tabCycleText = m_edit->text();
        m_tabCyclePos = m_edit->cursorPosition();
        return;
    }
    m_tabCycle.clear();
    const int pos = m_edit->cursorPosition();
    const QString text = m_edit->text();
    const QString before = text.left(pos);
    const QString after = text.mid(pos);
    // Token-Anfang: in offenen Anfuehrungszeichen ab dem Quote (Leerzeichen
    // gehoeren dann zum Pfad), sonst ab dem letzten Leerzeichen.
    const bool inQuotes = (before.count(QLatin1Char('"')) % 2) == 1;
    const int start = inQuotes ? before.lastIndexOf(QLatin1Char('"')) + 1
                               : before.lastIndexOf(QLatin1Char(' ')) + 1;
    const QString token = before.mid(start);
    const int cut = qMax(token.lastIndexOf(QLatin1Char('/')),
                         token.lastIndexOf(QLatin1Char('\\'))) + 1;
    const QString dirPart = token.left(cut);
    const QString prefix = token.mid(cut);
    const QString sep = m_provider->isRemote ? QStringLiteral("/")
                                                       : QString(QDir::separator());
    const QString cwd = m_cwd.isEmpty() ? QStringLiteral(".") : m_cwd;
    QString base;
    if (dirPart.isEmpty())
        base = cwd;
    else if (dirPart.startsWith(QLatin1Char('/')) || QDir::isAbsolutePath(dirPart))
        base = dirPart;
    else
        base = m_provider->join(cwd, dirPart);

    core::FileSystemProvider *provider = m_provider;
    m_bridge->run<std::vector<core::FileEntry>>(
        [provider, base] { return provider->listDir(base); },
        [this, text, pos, before, after, start, dirPart, prefix, inQuotes, sep](
            const std::vector<core::FileEntry> &entries) {
            // Inzwischen weitergetippt/abgeschickt? Dann nichts ueberschreiben.
            if (m_edit->text() != text || m_edit->cursorPosition() != pos)
                return;
            std::vector<const core::FileEntry *> names;
            for (const auto &e : entries) {
                if (e.type == core::EntryType::Parent)
                    continue;
                if (e.name.startsWith(prefix))
                    names.push_back(&e);
            }
            if (names.empty() && !prefix.isEmpty()) {  // Gross/Kleinschreibung egal
                const QString low = prefix.toLower();
                for (const auto &e : entries) {
                    if (e.type == core::EntryType::Parent)
                        continue;
                    if (e.name.toLower().startsWith(low))
                        names.push_back(&e);
                }
            }
            if (names.empty())
                return;

            // cd & Co. erwarten ein Verzeichnis: Dateien dann nicht anbieten.
            const QString command = before.left(start).trimmed().section(QLatin1Char(' '), 0, 0);
            static const QStringList dirCommands = {QStringLiteral("cd"), QStringLiteral("pushd"),
                                                    QStringLiteral("chdir"), QStringLiteral("rmdir")};
            if (dirCommands.contains(command, Qt::CaseInsensitive)) {
                std::vector<const core::FileEntry *> dirs;
                for (const core::FileEntry *e : names)
                    if (e->type == core::EntryType::Dir)
                        dirs.push_back(e);
                if (!dirs.empty())
                    names = std::move(dirs);
            }
            // Verzeichnisse zuerst, dann alphabetisch — in dieser Reihenfolge
            // schaltet wiederholtes Tab durch die Treffer.
            std::stable_sort(names.begin(), names.end(),
                             [](const core::FileEntry *a, const core::FileEntry *b) {
                                 const bool da = a->type == core::EntryType::Dir;
                                 const bool db = b->type == core::EntryType::Dir;
                                 if (da != db)
                                     return da;
                                 return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
                             });

            // Zeile vor dem Cursor fuer einen Kandidaten (mit Quotes bei Leerzeichen;
            // Dateien schliessen das Wort ab, Verzeichnisse enden mit Trenner).
            auto lineFor = [&](const core::FileEntry *e) {
                const bool isDir = e->type == core::EntryType::Dir;
                const QString completed = isDir ? e->name + sep : e->name;
                const bool quote = inQuotes || (dirPart + completed).contains(QLatin1Char(' '));
                const QString lead = (quote && !inQuotes) ? QStringLiteral("\"") : QString();
                const QString tail = isDir ? QString()
                                           : (quote ? QStringLiteral("\" ") : QStringLiteral(" "));
                return before.left(start) + lead + dirPart + completed + tail;
            };
            m_tabCycle.clear();
            for (const core::FileEntry *e : names)
                m_tabCycle << lineFor(e);
            m_tabCycleIndex = 0;
            m_tabCycleAfter = after;
            const QString newBefore = m_tabCycle.first();
            m_edit->setText(newBefore + after);
            m_edit->setCursorPosition(newBefore.size());
            m_tabCycleText = m_edit->text();
            m_tabCyclePos = m_edit->cursorPosition();
        },
        [](const QString &) {}, this);
}

} // namespace ncssh::gui
