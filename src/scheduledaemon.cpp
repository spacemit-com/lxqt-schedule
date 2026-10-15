#include "scheduledaemon.h"

#include <QDateTime>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QHash>
#include <QTimeZone>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>

namespace {
const char *kService = "org.lxqt.Schedule";
const char *kPath = "/org/lxqt/Schedule";
const char *kInterface = "org.lxqt.Schedule";
const char *kNotifyService = "org.freedesktop.Notifications";
const char *kNotifyPath = "/org/freedesktop/Notifications";
const char *kNotifyInterface = "org.freedesktop.Notifications";

// Upper bound to avoid pathological loops when advancing a repeating
// schedule whose period keeps it in the past.
const int kMaxAdvanceSteps = 1000;

bool isValidRepeat(const QString &repeat)
{
    return repeat.isEmpty() || repeat == QLatin1String("daily")
        || repeat == QLatin1String("weekly")
        || repeat == QLatin1String("monthly")
        || repeat == QLatin1String("yearly");
}

// 相对日期词（"明天""今天"等）跨天后会失效，落盘/加载时应剥离，
// 避免昨天创建的"明天开会"今天仍显示"明天开会"。
QString normalizeTitle(const QString &raw)
{
    QString title = raw.trimmed();
    const QStringList prefixes = {
        QStringLiteral("大后天"),
        QStringLiteral("明后天"),
        QStringLiteral("后天"),
        QStringLiteral("明天"),
        QStringLiteral("明日"),
        QStringLiteral("今天"),
        QStringLiteral("今日"),
    };
    for (const QString &prefix : prefixes) {
        if (title.startsWith(prefix)) {
            title = title.mid(prefix.size());
            if (title.startsWith(QStringLiteral("的")))
                title = title.mid(1);
            break;
        }
    }
    return title.trimmed();
}
} // namespace

ScheduleDaemon::ScheduleDaemon(QObject *parent)
    : QObject(parent)
{
    load();
    recomputeConflicts();

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ScheduleDaemon::checkDue);
    reschedule();
}

QString ScheduleDaemon::storagePath() const
{
    return storageFilePath();
}

QString ScheduleDaemon::storageFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                        + QLatin1String("/lxqt");
    return dir + QLatin1String("/schedule.json");
}

void ScheduleDaemon::load()
{
    m_items.clear();
    m_nextId = 1;

    QFile file(storageFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    m_nextId = qMax<qlonglong>(1, static_cast<qlonglong>(root.value(QLatin1String("next_id")).toDouble(1)));

    const QJsonArray arr = root.value(QLatin1String("schedules")).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        ScheduleItem item;
        item.id = static_cast<qlonglong>(o.value(QLatin1String("id")).toDouble(-1));
        item.title = normalizeTitle(o.value(QLatin1String("title")).toString());
        item.dueEpoch = static_cast<qlonglong>(o.value(QLatin1String("due_epoch")).toDouble(0));
        item.repeat = o.value(QLatin1String("repeat")).toString();
        item.createdEpoch = static_cast<qlonglong>(o.value(QLatin1String("created_epoch")).toDouble(0));
        item.conflict = o.value(QLatin1String("conflict")).toBool(false);
        if (item.id < 0 || item.title.isEmpty() || item.dueEpoch <= 0)
            continue;
        if (!isValidRepeat(item.repeat))
            continue;
        m_items.append(item);
        m_nextId = qMax(m_nextId, item.id + 1);
    }
}

void ScheduleDaemon::save()
{
    recomputeConflicts();

    const QString path = storageFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray arr;
    for (const ScheduleItem &item : m_items) {
        QJsonObject o;
        o.insert(QLatin1String("id"), item.id);
        o.insert(QLatin1String("title"), item.title);
        o.insert(QLatin1String("due_epoch"), item.dueEpoch);
        o.insert(QLatin1String("repeat"), item.repeat);
        o.insert(QLatin1String("created_epoch"), item.createdEpoch);
        o.insert(QLatin1String("conflict"), item.conflict);
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("next_id"), m_nextId);
    root.insert(QLatin1String("schedules"), arr);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ScheduleDaemon::recomputeConflicts()
{
    QHash<qlonglong, int> dueCounts;
    dueCounts.reserve(m_items.size());
    for (const ScheduleItem &item : m_items)
        ++dueCounts[item.dueEpoch];

    for (ScheduleItem &item : m_items)
        item.conflict = dueCounts.value(item.dueEpoch, 0) > 1;
}

void ScheduleDaemon::reschedule()
{
    qlonglong next = -1;
    for (const ScheduleItem &item : m_items) {
        if (next < 0 || item.dueEpoch < next)
            next = item.dueEpoch;
    }

    if (next < 0) {
        m_timer.stop();
        return;
    }

    const qlonglong now = QDateTime::currentSecsSinceEpoch();
    qlonglong delaySec = qMax<qlonglong>(0, next - now);
    // Poll at least once per second: fine for a lightweight desktop daemon,
    // and keeps the timer robust against suspend/resume clock jumps.
    m_timer.start(static_cast<int>(qMin<qlonglong>(qMax<qlonglong>(1000, delaySec * 1000), 60000)));
}

qlonglong ScheduleDaemon::advanceDue(qlonglong dueEpoch, const QString &repeat) const
{
    QDateTime dt = QDateTime::fromSecsSinceEpoch(dueEpoch, QTimeZone::UTC);
    if (repeat == QLatin1String("daily"))
        dt = dt.addDays(1);
    else if (repeat == QLatin1String("weekly"))
        dt = dt.addDays(7);
    else if (repeat == QLatin1String("monthly"))
        dt = dt.addMonths(1);
    else if (repeat == QLatin1String("yearly"))
        dt = dt.addYears(1);
    else
        return dueEpoch;
    return dt.toSecsSinceEpoch();
}

void ScheduleDaemon::sendNotification(const QString &title)
{
    QDBusInterface iface(QString::fromLatin1(kNotifyService),
                         QString::fromLatin1(kNotifyPath),
                         QString::fromLatin1(kNotifyInterface));

    // Notify(app_name, replaces_id, app_icon, summary, body, actions, hints, timeout)
    iface.call(QStringLiteral("Notify"),
               QStringLiteral("lxqt-schedule"),
               quint32(0),
               QStringLiteral("appointment-soon"),
               QStringLiteral("日程提醒"),
               title,
               QStringList(),
               QVariantMap(),
               -1);
}

void ScheduleDaemon::checkDue()
{
    const qlonglong now = QDateTime::currentSecsSinceEpoch();
    bool changed = false;

    QVector<ScheduleItem> kept;
    kept.reserve(m_items.size());

    for (ScheduleItem item : m_items) {
        if (item.dueEpoch > now) {
            kept.append(item);
            continue;
        }

        emit ScheduleTriggered(item.id, item.title);
        sendNotification(item.title);

        if (item.repeat.isEmpty()) {
            changed = true;
            continue; // drop one-shot schedule
        }

        // Advance a repeating schedule until it lands in the future.
        qlonglong advanced = item.dueEpoch;
        int steps = 0;
        while (advanced <= now && steps < kMaxAdvanceSteps) {
            advanced = advanceDue(advanced, item.repeat);
            ++steps;
        }
        item.dueEpoch = advanced;
        kept.append(item);
        changed = true;
    }

    m_items = kept;
    if (changed) {
        save();
        emit SchedulesChanged();
    }
    reschedule();
}

qlonglong ScheduleDaemon::AddSchedule(const QString &title, qlonglong dueEpoch, const QString &repeat)
{
    const QString cleanTitle = normalizeTitle(title);
    if (cleanTitle.isEmpty() || dueEpoch <= 0)
        return -1;
    if (!isValidRepeat(repeat))
        return -1;

    ScheduleItem item;
    item.id = m_nextId++;
    item.title = cleanTitle;
    item.dueEpoch = dueEpoch;
    item.repeat = repeat;
    item.createdEpoch = QDateTime::currentSecsSinceEpoch();

    m_items.append(item);
    save();
    reschedule();
    emit SchedulesChanged();
    return item.id;
}

bool ScheduleDaemon::UpdateSchedule(qlonglong id, const QString &title, qlonglong dueEpoch, const QString &repeat)
{
    if (dueEpoch <= 0)
        return false;
    if (!isValidRepeat(repeat))
        return false;

    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            ScheduleItem item = m_items.at(i);
            const QString newTitle = normalizeTitle(title);
            if (!newTitle.isEmpty())
                item.title = newTitle;
            item.dueEpoch = dueEpoch;
            item.repeat = repeat;
            m_items[i] = item;
            save();
            reschedule();
            emit SchedulesChanged();
            return true;
        }
    }
    return false;
}

bool ScheduleDaemon::RemoveSchedule(qlonglong id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            m_items.removeAt(i);
            save();
            reschedule();
            emit SchedulesChanged();
            return true;
        }
    }
    return false;
}

void ScheduleDaemon::RemoveAll()
{
    if (m_items.isEmpty())
        return;
    m_items.clear();
    save();
    reschedule();
    emit SchedulesChanged();
}

QString ScheduleDaemon::ListSchedules() const
{
    QJsonArray arr;
    for (const ScheduleItem &item : m_items) {
        QJsonObject o;
        o.insert(QLatin1String("id"), item.id);
        o.insert(QLatin1String("title"), item.title);
        o.insert(QLatin1String("due_epoch"), item.dueEpoch);
        o.insert(QLatin1String("repeat"), item.repeat);
        o.insert(QLatin1String("created_epoch"), item.createdEpoch);
        o.insert(QLatin1String("conflict"), item.conflict);
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QLatin1String("schedules"), arr);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
