#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {

const char *kService = "org.lxqt.Schedule";
const char *kPath = "/org/lxqt/Schedule";
const char *kInterface = "org.lxqt.Schedule";

void usage(QTextStream &out)
{
    out << "Usage: lxqt-schedule-ctl <command> [args]\n"
        << "\n"
        << "Commands:\n"
        << "  add <title> <when> [--repeat daily|weekly|monthly|yearly]\n"
        << "      <when> is Unix epoch seconds or ISO \"YYYY-MM-DD HH:MM[:SS]\"\n"
        << "  remove <id>\n"
        << "  update <id> [--title <title>] [--when <when>] [--repeat daily|weekly|monthly|yearly] [--clear-repeat]\n"
        << "  list [--json]\n"
        << "  clear\n"
        << "  ping\n";
}

qlonglong parseWhen(const QString &text, bool *ok)
{
    *ok = false;

    // Pure integer -> epoch seconds.
    bool isInt = false;
    const qlonglong epoch = text.toLongLong(&isInt);
    if (isInt && epoch > 0) {
        *ok = true;
        return epoch;
    }

    const QString normalized = QString(text).replace(QLatin1Char('T'), QLatin1Char(' '));
    const QDateTime dt = QDateTime::fromString(normalized, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (dt.isValid()) {
        *ok = true;
        return dt.toSecsSinceEpoch();
    }
    const QDateTime dtMin = QDateTime::fromString(normalized, QStringLiteral("yyyy-MM-dd HH:mm"));
    if (dtMin.isValid()) {
        *ok = true;
        return dtMin.toSecsSinceEpoch();
    }

    return 0;
}

QString formatListJson(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return QString::fromUtf8(payload);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = app.arguments();
    if (args.size() < 2) {
        usage(out);
        return 1;
    }

    const QString command = args.at(1);
    QDBusInterface iface(QString::fromLatin1(kService),
                         QString::fromLatin1(kPath),
                         QString::fromLatin1(kInterface),
                         QDBusConnection::sessionBus());

    if (command == QLatin1String("ping")) {
        QDBusReply<QString> reply = iface.call(QStringLiteral("ListSchedules"));
        if (reply.isValid()) {
            out << "ok\n";
            return 0;
        }
        err << "no daemon: " << reply.error().message() << "\n";
        return 1;
    }

    if (command == QLatin1String("add")) {
        if (args.size() < 4) {
            usage(err);
            return 1;
        }
        const QString title = args.at(2);
        const QString when = args.at(3);
        QString repeat;
        for (int i = 4; i < args.size(); ++i) {
            if (args.at(i) == QLatin1String("--repeat") && i + 1 < args.size())
                repeat = args.at(++i);
        }

        bool ok = false;
        const qlonglong dueEpoch = parseWhen(when, &ok);
        if (!ok) {
            err << "invalid <when>: " << when << "\n";
            return 1;
        }

        QDBusReply<qlonglong> reply = iface.call(QStringLiteral("AddSchedule"),
                                                 title, dueEpoch, repeat);
        if (!reply.isValid()) {
            err << "add failed: " << reply.error().message() << "\n";
            return 1;
        }
        if (reply.value() < 0) {
            err << "add failed: invalid arguments\n";
            return 1;
        }
        out << reply.value() << "\n";
        return 0;
    }

    if (command == QLatin1String("update")) {
        if (args.size() < 3) {
            usage(err);
            return 1;
        }
        bool ok = false;
        const qlonglong id = args.at(2).toLongLong(&ok);
        if (!ok || id <= 0) {
            err << "invalid id: " << args.at(2) << "\n";
            return 1;
        }

        QString title;
        QString when;
        QString repeat;
        bool clearRepeat = false;
        for (int i = 3; i < args.size(); ++i) {
            const QString arg = args.at(i);
            if (arg == QLatin1String("--title") && i + 1 < args.size()) {
                title = args.at(++i);
            } else if (arg == QLatin1String("--when") && i + 1 < args.size()) {
                when = args.at(++i);
            } else if (arg == QLatin1String("--repeat") && i + 1 < args.size()) {
                repeat = args.at(++i);
            } else if (arg == QLatin1String("--clear-repeat")) {
                clearRepeat = true;
            } else {
                usage(err);
                return 1;
            }
        }

        if (!repeat.isEmpty() && clearRepeat) {
            err << "--repeat and --clear-repeat are mutually exclusive\n";
            return 1;
        }
        if (!repeat.isEmpty() && repeat != QLatin1String("daily")
            && repeat != QLatin1String("weekly")
            && repeat != QLatin1String("monthly")
            && repeat != QLatin1String("yearly")) {
            err << "invalid --repeat: " << repeat << "\n";
            return 1;
        }

        // At least one mutable field is required.
        if (title.trimmed().isEmpty() && when.isEmpty() && repeat.isEmpty() && !clearRepeat) {
            err << "update requires --title, --when, --repeat, or --clear-repeat\n";
            return 1;
        }

        // Fetch the current record so unspecified fields are preserved.
        QDBusReply<QString> listReply = iface.call(QStringLiteral("ListSchedules"));
        if (!listReply.isValid()) {
            err << "update failed: " << listReply.error().message() << "\n";
            return 1;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(listReply.value().toUtf8());
        const QJsonArray arr = doc.object().value(QLatin1String("schedules")).toArray();
        qlonglong currentDue = 0;
        QString currentTitle;
        QString currentRepeat;
        bool found = false;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            if (static_cast<qlonglong>(o.value(QLatin1String("id")).toDouble(-1)) != id)
                continue;
            currentDue = static_cast<qlonglong>(o.value(QLatin1String("due_epoch")).toDouble(0));
            currentTitle = o.value(QLatin1String("title")).toString();
            currentRepeat = o.value(QLatin1String("repeat")).toString();
            found = true;
            break;
        }
        if (!found) {
            err << "not found\n";
            return 1;
        }

        const QString newTitle = title.trimmed().isEmpty() ? currentTitle : title.trimmed();
        qlonglong newDue = currentDue;
        if (!when.isEmpty()) {
            bool whenOk = false;
            newDue = parseWhen(when, &whenOk);
            if (!whenOk) {
                err << "invalid <when>: " << when << "\n";
                return 1;
            }
        }
        QString newRepeat = currentRepeat;
        if (clearRepeat)
            newRepeat.clear();
        else if (!repeat.isEmpty())
            newRepeat = repeat;

        QDBusReply<bool> reply = iface.call(QStringLiteral("UpdateSchedule"),
                                            id, newTitle, newDue, newRepeat);
        if (!reply.isValid()) {
            err << "update failed: " << reply.error().message() << "\n";
            return 1;
        }
        out << (reply.value() ? "ok\n" : "not found\n");
        return reply.value() ? 0 : 1;
    }

    if (command == QLatin1String("remove")) {
        if (args.size() < 3) {
            usage(err);
            return 1;
        }
        bool ok = false;
        const qlonglong id = args.at(2).toLongLong(&ok);
        if (!ok) {
            err << "invalid id: " << args.at(2) << "\n";
            return 1;
        }
        QDBusReply<bool> reply = iface.call(QStringLiteral("RemoveSchedule"), id);
        if (!reply.isValid()) {
            err << "remove failed: " << reply.error().message() << "\n";
            return 1;
        }
        out << (reply.value() ? "ok\n" : "not found\n");
        return reply.value() ? 0 : 1;
    }

    if (command == QLatin1String("clear")) {
        QDBusReply<void> reply = iface.call(QStringLiteral("RemoveAll"));
        if (!reply.isValid()) {
            err << "clear failed: " << reply.error().message() << "\n";
            return 1;
        }
        out << "ok\n";
        return 0;
    }

    if (command == QLatin1String("list")) {
        const bool json = args.contains(QStringLiteral("--json"));
        QDBusReply<QString> reply = iface.call(QStringLiteral("ListSchedules"));
        if (!reply.isValid()) {
            err << "list failed: " << reply.error().message() << "\n";
            return 1;
        }
        if (json) {
            out << formatListJson(reply.value().toUtf8()) << "\n";
            return 0;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        const QJsonArray arr = doc.object().value(QLatin1String("schedules")).toArray();
        if (arr.isEmpty()) {
            out << "no schedules\n";
            return 0;
        }
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QDateTime due = QDateTime::fromSecsSinceEpoch(
                static_cast<qlonglong>(o.value(QLatin1String("due_epoch")).toDouble()));
            const QString title = (o.value(QLatin1String("conflict")).toBool(false)
                                       ? QStringLiteral("[冲突] ")
                                       : QString())
                                  + o.value(QLatin1String("title")).toString();
            out << o.value(QLatin1String("id")).toVariant().toLongLong() << "\t"
                << due.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "\t"
                << (o.value(QLatin1String("repeat")).toString().isEmpty()
                        ? QStringLiteral("-")
                        : o.value(QLatin1String("repeat")).toString())
                << "\t" << title << "\n";
        }
        return 0;
    }

    usage(err);
    return 1;
}
