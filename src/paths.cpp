#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace f1::paths {

namespace {

void copyTree(const QString& from, const QString& to) {
    QDir().mkpath(to);
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString dst = QDir(to).filePath(QDir(from).relativeFilePath(src));
        if (it.fileInfo().isDir()) QDir().mkpath(dst);
        else QFile::copy(src, dst);
    }
}

QString base(const char* env, const char* fallback) {
    QString root = qEnvironmentVariable(env);
    if (root.isEmpty()) root = QDir::home().filePath(QLatin1StringView(fallback));
    const QString fresh = QDir(root).filePath(QLatin1StringView(APP_NAME));
    const QString old = QDir(root).filePath(QStringLiteral("NoctaliaF1"));
    // Renamed from "Noctalia F1": keep the user's settings and cache (as the Python app does).
    if (!QFileInfo::exists(fresh) && QFileInfo(old).isDir()) copyTree(old, fresh);
    return fresh;
}

QString ensure(const QString& p) {
    QDir().mkpath(p);
    return QDir::toNativeSeparators(p);
}

}  // namespace

QString configDir() {
    const QString o = qEnvironmentVariable("NOCTALIA_F1_HOME");
    return ensure(o.isEmpty() ? base("APPDATA", "AppData/Roaming") : QDir(o).filePath(QStringLiteral("config")));
}

QString cacheDir() {
    const QString o = qEnvironmentVariable("NOCTALIA_F1_HOME");
    return ensure(o.isEmpty() ? base("LOCALAPPDATA", "AppData/Local") : QDir(o).filePath(QStringLiteral("cache")));
}

QString settingsFile() { return QDir(configDir()).filePath(QStringLiteral("settings.json")); }

QString resourceDir() { return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets")); }

QString builtinLangDir() { return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lang")); }

QString userLangDir() { return QDir(configDir()).filePath(QStringLiteral("lang")); }

}  // namespace f1::paths
