#include "update_rules.h"

#include "pyjson.h"
#include "sanitize.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace f1::update {

QString Version::text() const { return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch); }

std::optional<Version> parseVersion(const QString& text) {
    static const QRegularExpression re(QStringLiteral("^v?(\\d{1,4})\\.(\\d{1,4})\\.(\\d{1,4})$"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch()) return std::nullopt;
    return Version{m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()};
}

bool isNewer(const QString& remote, const QString& local) {
    const auto r = parseVersion(remote), l = parseVersion(local);
    return r && l && *r > *l;
}

QString installKind(const QString& exePath, const QString& localAppData) {
    const QFileInfo exe(exePath);
    if (QFileInfo::exists(exe.dir().filePath(QStringLiteral("unins000.exe")))) return QStringLiteral("installed");
    if (!localAppData.isEmpty()) {
        const QString programs =
            QDir::cleanPath(QDir(localAppData).filePath(QStringLiteral("Programs/F1DockTimers"))).toLower() + u'/';
        if (QDir::cleanPath(exe.absoluteFilePath()).toLower().startsWith(programs)) return QStringLiteral("installed");
    }
    return QStringLiteral("other");
}

std::optional<Asset> pickAsset(const QJsonObject& release, const QString& kind) {
    const QString want = kind == u"installed" ? QStringLiteral("-setup.exe") : QStringLiteral("-portable.exe");
    const auto ver = parseVersion(py::str(release.value(QStringLiteral("tag_name"))));
    if (!ver) return std::nullopt;
    // The file must carry the release's own version, otherwise the "new" app would report an
    // older version and the update would be offered forever.
    const QString expected = QStringLiteral("F1DockTimers-") + ver->text() + want;
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
    for (const QJsonValue& v : py::arr(release.value(QStringLiteral("assets")))) {
        if (!v.isObject()) continue;
        const QJsonObject a = v.toObject();
        if (py::str(a.value(QStringLiteral("name"))) != expected) continue;
        const QString digest = py::truthy(a.value(QStringLiteral("digest"))) ? py::str(a.value(QStringLiteral("digest"))) : QString();
        const QString sha = digest.startsWith(QStringLiteral("sha256:")) ? digest.mid(7).toLower() : QString();
        const QString url = py::truthy(a.value(QStringLiteral("browser_download_url")))
                                ? py::str(a.value(QStringLiteral("browser_download_url")))
                                : QString();
        if (!hex64.match(sha).hasMatch() || !url.startsWith(QStringLiteral("https://github.com/"))) return std::nullopt;
        return Asset{expected, url, sha, py::toInt(a.value(QStringLiteral("size"))).value_or(0)};
    }
    return std::nullopt;
}

QHash<QString, QString> parseSums(const QByteArray& text) {
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64}) [ *](.+?)\\r?$"));
    QHash<QString, QString> sums;
    QSet<QString> twice;
    for (const QByteArray& raw : text.split('\n')) {
        const auto m = line.match(QString::fromUtf8(raw));
        if (!m.hasMatch()) continue;
        const QString name = m.captured(2);
        if (sums.contains(name)) twice.insert(name);
        sums.insert(name, m.captured(1).toLower());
    }
    for (const QString& n : twice) sums.remove(n);
    return sums;
}

std::optional<Asset> namedAsset(const QJsonObject& release, const QString& name) {
    for (const QJsonValue& v : py::arr(release.value(QStringLiteral("assets")))) {
        const QJsonObject a = v.toObject();
        if (!v.isObject() || py::str(a.value(QStringLiteral("name"))) != name) continue;
        const QString url = py::truthy(a.value(QStringLiteral("browser_download_url")))
                                ? py::str(a.value(QStringLiteral("browser_download_url")))
                                : QString();
        if (!url.startsWith(QStringLiteral("https://github.com/"))) return std::nullopt;
        return Asset{name, url, QString(), py::toInt(a.value(QStringLiteral("size"))).value_or(0)};
    }
    return std::nullopt;
}

std::optional<Release> parseRelease(const QJsonValue& data) {
    if (!data.isObject()) return std::nullopt;
    const QJsonObject d = data.toObject();
    if (py::truthy(d.value(QStringLiteral("draft"))) || py::truthy(d.value(QStringLiteral("prerelease")))) return std::nullopt;
    const QString tag = py::truthy(d.value(QStringLiteral("tag_name"))) ? py::str(d.value(QStringLiteral("tag_name"))) : QString();
    const auto ver = parseVersion(tag);
    if (!ver) return std::nullopt;
    Release r;
    r.version = ver->text();
    r.tag = safe(tag, 20);
    r.name = safe(py::truthy(d.value(QStringLiteral("name"))) ? d.value(QStringLiteral("name")) : QJsonValue(tag), 80);
    const QString body = py::truthy(d.value(QStringLiteral("body"))) ? py::str(d.value(QStringLiteral("body"))) : QString();
    r.notes = safe(QString(body).remove(u'\r'), 1500).replace(u'\n', QStringLiteral(" \n"));
    const QString html = py::truthy(d.value(QStringLiteral("html_url"))) ? py::str(d.value(QStringLiteral("html_url"))) : QString();
    r.url = html.startsWith(QStringLiteral("https://github.com/") + QString::fromLatin1(REPO) + u'/') ? html
                                                                                                     : QString::fromLatin1(RELEASES_PAGE);
    r.raw = d;
    return r;
}

std::optional<QString> signatureProblem(const Signature& current, const Signature& update) {
    if (current.status == u"Valid") {
        if (update.status != u"Valid") return QStringLiteral("the update is not validly signed (%1)").arg(update.status);
        if (update.subject != current.subject) return QStringLiteral("the update is signed by a different publisher");
        return std::nullopt;
    }
    if (update.status != u"Valid" && update.status != u"NotSigned")
        return QStringLiteral("the update has a broken signature (%1)").arg(update.status);
    return std::nullopt;
}

bool hostAllowed(const QString& host, const QStringList& hosts) {
    for (const QString& h : hosts)
        if (host == h || host.endsWith(u'.' + h)) return true;
    return false;
}

}  // namespace f1::update
