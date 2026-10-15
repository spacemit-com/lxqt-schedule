#ifndef LXQT_SCHEDULE_DAEMON_H
#define LXQT_SCHEDULE_DAEMON_H

#include <QObject>
#include <QTimer>
#include <QVector>

struct ScheduleItem
{
    qlonglong id = -1;
    QString title;
    qlonglong dueEpoch = 0;    // Unix epoch seconds (UTC)
    QString repeat;            // "", daily, weekly, monthly, yearly
    qlonglong createdEpoch = 0;
    bool conflict = false;     // true when another item shares dueEpoch
};

class ScheduleDaemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.lxqt.Schedule")

public:
    explicit ScheduleDaemon(QObject *parent = nullptr);

    QString storagePath() const;

public Q_SLOTS:
    qlonglong AddSchedule(const QString &title, qlonglong dueEpoch, const QString &repeat);
    bool UpdateSchedule(qlonglong id, const QString &title, qlonglong dueEpoch, const QString &repeat);
    bool RemoveSchedule(qlonglong id);
    void RemoveAll();
    QString ListSchedules() const;

Q_SIGNALS:
    void SchedulesChanged();
    void ScheduleTriggered(qlonglong id, const QString &title);

private Q_SLOTS:
    void checkDue();

private:
    QString storageFilePath() const;
    void load();
    void save();
    void reschedule();
    void recomputeConflicts();
    void sendNotification(const QString &title);
    qlonglong advanceDue(qlonglong dueEpoch, const QString &repeat) const;

    QVector<ScheduleItem> m_items;
    qlonglong m_nextId = 1;
    QTimer m_timer;
};

#endif // LXQT_SCHEDULE_DAEMON_H
