// F1DockTimers, C++/Qt port: entry point.

#include "app.h"
#include "paths.h"
#include "version.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QMutex>
#include <QPalette>
#include "ui/widgets.h"
#include <QSystemTrayIcon>

namespace {

QFile* g_log = nullptr;
QMutex g_logLock;

void logHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (type == QtDebugMsg && !qEnvironmentVariableIsSet("F1DT_DEBUG")) return;
    static const char* names[] = {"DEBUG", "WARNING", "CRITICAL", "FATAL", "INFO"};
    const QByteArray line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss,zzz")).toUtf8() + ' ' +
                            names[type] + ' ' + (ctx.category ? ctx.category : "qt") + ": " + msg.toUtf8() + '\n';
    QMutexLocker lock(&g_logLock);
    if (g_log) {
        if (g_log->size() > 512'000) g_log->resize(0);
        g_log->write(line);
        g_log->flush();
    }
}


QString serverName() {
    // One instance per Windows user (and per test home), the same name as the Python build's, so
    // an old shortcut or a second start reaches whichever build is running.
    QString key;
    for (const char* env : {"LOGNAME", "USER", "LNAME", "USERNAME"})   // Python's getpass.getuser()
        if (key = qEnvironmentVariable(env); !key.isEmpty()) break;
    if (qEnvironmentVariableIsSet("NOCTALIA_F1_HOME")) key += u'|' + qEnvironmentVariable("NOCTALIA_F1_HOME");
    return QStringLiteral("F1DockTimers-") +
           QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication qapp(argc, argv);
    QApplication::setApplicationName(QStringLiteral(F1_APP_TITLE));
    QApplication::setApplicationVersion(QStringLiteral(F1_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    // f1.log, rotated like the Python app's (512 kB, two old copies).
    const QString logPath = QDir(f1::paths::cacheDir()).filePath(QStringLiteral("f1.log"));
    if (QFileInfo(logPath).size() > 512'000) {
        QFile::remove(logPath + QStringLiteral(".2"));
        QFile::rename(logPath + QStringLiteral(".1"), logPath + QStringLiteral(".2"));
        QFile::rename(logPath, logPath + QStringLiteral(".1"));
    }
    QFile log(logPath);
    if (log.open(QIODevice::Append | QIODevice::Text)) g_log = &log;
    qInstallMessageHandler(logHandler);

    // Single instance: a second start hands its message to the running one and exits.
    // "show" (as the Python build sends) or "quit" (`F1DockTimers.exe --quit`).
    const bool quitRequest = QApplication::arguments().contains(QStringLiteral("--quit"));
    const QString name = serverName();
    {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(300)) {
            probe.write(quitRequest ? "quit" : "show");
            probe.flush();
            probe.waitForBytesWritten(400);
            probe.disconnectFromServer();
            return 0;
        }
        if (quitRequest) return 0;   // nothing running
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name);
    server.listen(name);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral(F1_APP_TITLE), QStringLiteral("No system tray available."));
        return 1;
    }
    f1::ui::applyDarkPalette();
    f1::ui::wrapTooltips(&qapp);   // long tooltips wrap at ~70 characters
    f1::App app;
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, &app] {
        QLocalSocket* c = server.nextPendingConnection();
        QObject::connect(c, &QLocalSocket::readyRead, &app, [c, &app] {
            const QByteArray msg = c->readAll();
            if (msg.startsWith("quit")) app.quit();
            else if (msg.startsWith("show")) app.openPanel();
        });
        QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
    });
    app.start();
    const QStringList args = QApplication::arguments();
    if (const qsizetype i = args.indexOf(QStringLiteral("--screenshots")); i >= 0 && i + 1 < args.size()) {
        const qsizetype ti = args.indexOf(QStringLiteral("--tabs"));
        const QString tabs = ti >= 0 && ti + 1 < args.size() ? args[ti + 1] : QStringLiteral("schedule,weather");
        app.screenshots(args[i + 1], tabs.split(u','));
    }
    const int rc = QApplication::exec();
    qInstallMessageHandler(nullptr);
    g_log = nullptr;
    return rc;
}
