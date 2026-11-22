#include "i18n.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <algorithm>

namespace f1 {

namespace {
QMap<QString, QString> g_table;
QString g_code = QStringLiteral("en");

QString readUtf8(const QString& file, bool* ok) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    *ok = true;
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);   // utf-8-sig
    return QString::fromUtf8(data);
}
}  // namespace

namespace i18n {

QString format(const QString& text, Args values, bool* ok) {
    *ok = true;
    QString out;
    out.reserve(text.size() + 16);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'{') {
            if (i + 1 < text.size() && text.at(i + 1) == u'{') {
                out += u'{';
                ++i;
                continue;
            }
            const qsizetype close = text.indexOf(u'}', i + 1);
            if (close < 0) {
                *ok = false;
                return text;
            }
            const QString name = text.mid(i + 1, close - i - 1);
            bool found = false;
            for (const auto& [k, v] : values) {
                if (name == QLatin1StringView(k)) {
                    out += v;
                    found = true;
                    break;
                }
            }
            if (!found) {
                *ok = false;
                return text;
            }
            i = close;
        } else if (c == u'}') {
            if (i + 1 < text.size() && text.at(i + 1) == u'}') {
                out += u'}';
                ++i;
                continue;
            }
            *ok = false;
            return text;
        } else {
            out += c;
        }
    }
    return out;
}

Parsed parse(const QString& text) {
    Parsed p;
    const QStringList lines = text.split(u'\n');
    for (QString line : lines) {
        while (line.endsWith(u'\r')) line.chop(1);
        if (line.trimmed().isEmpty()) continue;
        if (line.trimmed().startsWith(u'#')) continue;   // Python: lstrip().startswith("#")
        if (line.startsWith(u'@')) {
            const QString rest = line.mid(1);
            const qsizetype eq = rest.indexOf(u'=');
            const QString k = (eq < 0 ? rest : rest.left(eq)).trimmed();
            const QString v = eq < 0 ? QString() : rest.mid(eq + 1).trimmed();
            p.meta.insert(k, v);
            continue;
        }
        qsizetype sep = line.indexOf(QStringLiteral(" = "));
        qsizetype width = 3;
        if (sep < 0) {
            sep = line.indexOf(u'=');
            width = 1;
        }
        if (sep < 0) continue;
        QString k = line.left(sep).trimmed();
        QString v = line.mid(sep + width).trimmed();
        if (!k.isEmpty() && !v.isEmpty()) {
            k.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            v.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            p.table.insert(k, v);
        }
    }
    return p;
}

QList<Language> available(const QStringList& dirs) {
    QMap<QString, Language> found;
    for (const QString& folder : dirs) {
        QDir d(folder);
        QStringList files = d.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
        for (const QString& name : files) {
            if (name.startsWith(u'_')) continue;   // the template
            bool ok = false;
            const QString path = d.filePath(name);
            const QString text = readUtf8(path, &ok);
            if (!ok) continue;
            const Parsed p = parse(text);
            const QString stem = QFileInfo(name).completeBaseName();
            const QString code = p.meta.value(QStringLiteral("code")).isEmpty() ? stem : p.meta.value(QStringLiteral("code"));
            const QString lname = p.meta.value(QStringLiteral("name")).isEmpty() ? code : p.meta.value(QStringLiteral("name"));
            found.insert(code.toLower(), Language{code, lname, path});
        }
    }
    QList<Language> out = found.values();
    std::stable_sort(out.begin(), out.end(),
                     [](const Language& a, const Language& b) { return a.name.toLower() < b.name.toLower(); });
    return out;
}

QString windowsLanguage() {
    const QString name = QLocale::system().name();   // "da_DK"
    if (name.isEmpty()) return QStringLiteral("en");
    return name.section(u'_', 0, 0).section(u'-', 0, 0).toLower();
}

QString load(const QString& choice, const QStringList& dirs) {
    g_table.clear();
    g_code = QStringLiteral("en");
    const QString want = (choice.isEmpty() ? QStringLiteral("auto") : choice).toLower();
    const QList<Language> langs = available(dirs);
    const Language* pick = nullptr;
    if (want != u"auto") {
        for (const auto& l : langs)
            if (l.code.toLower() == want) { pick = &l; break; }
    } else {
        const QString win = windowsLanguage();
        for (const auto& l : langs)
            if (l.code.toLower() == win) { pick = &l; break; }
        if (!pick)
            for (const auto& l : langs)
                if (l.code.toLower().section(u'-', 0, 0) == win) { pick = &l; break; }
    }
    if (!pick || pick->code.toLower() == u"en") return g_code;
    bool ok = false;
    const QString text = readUtf8(pick->file, &ok);
    if (!ok) return g_code;
    g_table = parse(text).table;
    g_code = pick->code;
    return g_code;
}

void setTable(const QMap<QString, QString>& table, const QString& code) {
    g_table = table;
    g_code = table.isEmpty() ? QStringLiteral("en") : code;
}

QString current() { return g_code; }

}  // namespace i18n

QString T(const QString& english) {
    auto it = g_table.constFind(english);
    return (it != g_table.constEnd() && !it->isEmpty()) ? *it : english;
}

QString T(const QString& english, Args values) {
    const QString tr = T(english);
    if (values.size() == 0) return tr;
    bool ok = false;
    QString out = i18n::format(tr, values, &ok);
    if (!ok) out = i18n::format(english, values, &ok);   // a broken translation never breaks the UI
    return out;
}

}  // namespace f1
