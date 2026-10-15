#include "scheduledaemon.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lxqt-schedule"));
    QCoreApplication::setOrganizationName(QStringLiteral("lxqt"));

    ScheduleDaemon daemon;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.lxqt.Schedule"))) {
        // A daemon is already running; exit quietly.
        return 0;
    }

    if (!bus.registerObject(QStringLiteral("/org/lxqt/Schedule"), &daemon,
                            QDBusConnection::ExportAllSlots
                                | QDBusConnection::ExportAllSignals)) {
        qWarning() << "lxqt-schedule: failed to register D-Bus object"
                   << bus.lastError().message();
        return 1;
    }

    return app.exec();
}
