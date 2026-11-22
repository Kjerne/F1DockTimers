#include "updater.h"

#include "../core/i18n.h"
#include "../core/timeutil.h"
#include "version.h"
#include "release_sig.h"
#include "wintrust.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QThread>

Q_LOGGING_CATEGORY(lcUpdate, "f1.update")

namespace f1 {

namespace {

QString sha256File(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

}  // namespace

Updater::Updater(Settings* settings, QNetworkAccessManager* nam, std::function<void()> save, QObject* parent)
    : QObject(parent), m_settings(settings), m_nam(nam), m_save(std::move(save)) {
    currentVersion = QStringLiteral(F1_VERSION);
    kind = wintrust::isPackaged() ? QStringLiteral("store")
                                  : update::installKind(QCoreApplication::applicationFilePath(), qEnvironmentVariable("LOCALAPPDATA"));
    downloadDir = QDir(QDir::tempPath()).filePath(QStringLiteral("F1DockTimers-update"));
    authenticode = &wintrust::authenticode;
    launch = [](const QString& setup) {
        // Silent upgrade in place; the installer starts the app again after a silent install.
        return QProcess::startDetached(setup, {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                                               QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS")});
    };
    now = [] { return nowSec(); };
    releaseKeys = releasesig::builtInKeys();
    m_timer.setInterval(3600 * 1000);   // wake hourly, check if a day has passed
    connect(&m_timer, &QTimer::timeout, this, &Updater::maybeCheck);
}

Updater::~Updater() {
    if (m_reply) m_reply->abort();
}

void Updater::start() {
    if (kind == u"store") return;   // the Microsoft Store updates this copy itself
    m_timer.start();
    QTimer::singleShot(update::FIRST_CHECK_MS, this, &Updater::maybeCheck);
}

void Updater::maybeCheck() {
    if (m_settings->check_updates && now() - m_settings->update_checked_at >= update::CHECK_EVERY_S - 60) check(false);
}

void Updater::check(bool manual) {
    if (kind == u"store") {
        if (manual) Q_EMIT checked(false, T(QStringLiteral("This copy is updated by the Microsoft Store.")));
        return;
    }
    QNetworkRequest req{QUrl(apiUrl)};
    req.setRawHeader("User-Agent", F1_USER_AGENT);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] { checkedReply(reply, manual); });
}

void Updater::checkedReply(QNetworkReply* reply, bool manual) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    if (status != 200) {
        const QString msg = status == 404 ? T(QStringLiteral("No release published yet."))
                                          : T(QStringLiteral("Couldn't reach GitHub ({status})."),
                                              {{"status", status ? QString::number(status) : QStringLiteral("offline")}});
        qCInfo(lcUpdate) << "update check failed:" << status;
        if (manual) Q_EMIT checked(false, msg);
        return;
    }
    m_settings->update_checked_at = static_cast<double>(now());
    m_save();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    const auto rel = err.error == QJsonParseError::NoError ? update::parseRelease(doc.object()) : std::nullopt;
    if (!rel || !update::isNewer(rel->version, currentVersion)) {
        latest.reset();
        if (manual) Q_EMIT checked(false, T(QStringLiteral("You're up to date (version {version})."), {{"version", currentVersion}}));
        return;
    }
    latest = rel;
    qCInfo(lcUpdate) << "update available:" << rel->version;
    // Fetch the checksum list and its signature first, so the prompt knows whether it may install.
    const auto sums = update::namedAsset(rel->raw, QString::fromLatin1(update::SUMS_NAME));
    const auto sig = update::namedAsset(rel->raw, QString::fromLatin1(update::SIG_NAME));
    if (!sums || !sig || releaseKeys.isEmpty()) {
        latest->signatureChecked = true;
        qCInfo(lcUpdate) << "release" << rel->version << "is not signed with a release key";
        announce(manual);
        return;
    }
    const QString version = rel->version;
    fetchSmall(*sums, [this, sig = *sig, version, manual](std::optional<QByteArray> list) {
        fetchSmall(sig, [this, list, version, manual](std::optional<QByteArray> signature) {
            if (!latest || latest->version != version) return;   // a newer check replaced it
            latest->signatureChecked = true;
            if (list && signature && releasesig::verifyAny(*list, *signature, releaseKeys)) {
                latest->signedSums = update::parseSums(*list);
                qCInfo(lcUpdate) << "release" << version << "signature valid";
            } else {
                qCWarning(lcUpdate) << "release" << version << "has no valid release-key signature";
            }
            announce(manual);
        });
    });
}

void Updater::announce(bool manual) {
    if (!latest) return;
    if (manual)
        Q_EMIT checked(true, T(QStringLiteral("Version {version} is available (you have {current})."),
                               {{"version", latest->version}, {"current", currentVersion}}));
    Q_EMIT available(latest->version);
}

void Updater::guardRedirects(QNetworkReply* reply) {
    // Every hop must stay on GitHub's hosts, and never go from HTTPS to plain HTTP.
    connect(reply, &QNetworkReply::redirected, reply, [this, reply](const QUrl& to) {
        const bool downgrade = reply->url().scheme() == u"https" && to.scheme() != u"https";
        if (!downgrade && update::hostAllowed(to.host(), downloadHosts)) Q_EMIT reply->redirectAllowed();
        else reply->abort();
    });
}

void Updater::fetchSmall(const update::Asset& asset, std::function<void(std::optional<QByteArray>)> done) {
    QNetworkRequest req{QUrl(asset.url)};
    req.setRawHeader("User-Agent", F1_USER_AGENT);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_nam->get(req);
    guardRedirects(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const QByteArray body = reply->read(update::MAX_SUMS + 1);
        const bool ok = reply->error() == QNetworkReply::NoError && body.size() <= update::MAX_SUMS &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                        update::hostAllowed(reply->url().host(), downloadHosts);
        done(ok ? std::optional<QByteArray>(body) : std::nullopt);
    });
}

std::optional<update::Asset> Updater::trustedSetup() const {
    if (kind != u"installed" || !latest) return std::nullopt;
    const auto asset = update::pickAsset(latest->raw, kind);
    if (!asset || latest->signedSums.value(asset->name) != asset->sha256) return std::nullopt;
    return asset;
}

bool Updater::canInstall() const { return trustedSetup().has_value(); }

void Updater::install() {
    const auto asset = trustedSetup();
    if (!asset) {
        Q_EMIT failed(kind == u"installed" && latest && update::pickAsset(latest->raw, kind)
                          ? T(QStringLiteral("This release is not signed with the F1DockTimers release key, so it is not installed automatically."))
                          : T(QStringLiteral("This release has no verifiable download for your copy. Opening the release page.")));
        return;
    }
    if (asset->size > update::MAX_DOWNLOAD) {
        Q_EMIT failed(T(QStringLiteral("The download is unexpectedly large; not installing.")));
        return;
    }
    QDir().mkpath(downloadDir);
    m_asset = *asset;
    m_tooLarge = false;
    m_file.setFileName(QDir(downloadDir).filePath(asset->name));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT failed(T(QStringLiteral("The download failed or was cancelled.")));
        return;
    }
    QNetworkRequest req{QUrl(asset->url)};
    req.setRawHeader("User-Agent", F1_USER_AGENT);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setTransferTimeout(60000);
    m_reply = m_nam->get(req);
    guardRedirects(m_reply);
    connect(m_reply, &QNetworkReply::readyRead, this, &Updater::writeChunk);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 a, qint64 b) {
        Q_EMIT progress(a, b > 0 ? b : m_asset.size);
    });
    connect(m_reply, &QNetworkReply::finished, this, &Updater::downloaded);
}

void Updater::cancel() {
    if (m_reply) m_reply->abort();
}

void Updater::writeChunk() {
    if (!m_reply) return;
    const QByteArray data = m_reply->readAll();
    if (m_file.pos() + data.size() > update::MAX_DOWNLOAD) {
        m_tooLarge = true;
        m_reply->abort();
        return;
    }
    m_file.write(data);
}

void Updater::downloaded() {
    QNetworkReply* reply = m_reply;
    if (!reply) return;
    if (!m_tooLarge) writeChunk();   // the rest
    m_reply = nullptr;
    m_file.close();
    const bool ok = reply->error() == QNetworkReply::NoError && !m_tooLarge;
    const QString host = reply->url().host();
    reply->deleteLater();
    if (!ok) {
        discard(T(QStringLiteral("The download failed or was cancelled.")));
        return;
    }
    if (!update::hostAllowed(host, downloadHosts)) {
        discard(T(QStringLiteral("The download came from an unexpected server; not installing.")));
        return;
    }
    // Hashing ~45 MB and asking Windows about signatures takes a moment: do it on a worker
    // thread so the UI (and, in embedded mode, Explorer's taskbar) never waits for it.
    const QString target = m_file.fileName(), sha = m_asset.sha256;
    const auto sig = authenticode;
    QPointer<Updater> self(this);
    QThread* worker = QThread::create([self, target, sha, sig] {
        QString problem;
        // `sha` is both GitHub's digest and the signed list's entry (trustedSetup checked they agree).
        if (sha256File(target) != sha) {
            problem = T(QStringLiteral("The download doesn't match GitHub's published checksum; not installing."));
        } else if (const auto why = update::signatureProblem(sig(QCoreApplication::applicationFilePath()), sig(target))) {
            problem = T(QStringLiteral("Not installing: {why}."), {{"why", *why}});
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, problem] {
            if (self) self->afterVerify(problem);
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void Updater::afterVerify(const QString& problem) {
    if (!problem.isEmpty()) {
        discard(problem);
        return;
    }
    if (!launch(m_file.fileName())) {
        Q_EMIT failed(T(QStringLiteral("Couldn't start the update ({name})."), {{"name", QStringLiteral("OSError")}}));
        return;
    }
    qCInfo(lcUpdate) << "update" << m_asset.name << "launched; exiting";
    Q_EMIT readyToQuit();
}

void Updater::discard(const QString& message) {
    QFile::remove(m_file.fileName());
    qCWarning(lcUpdate) << "update not installed:" << message;
    Q_EMIT failed(message);
}

}  // namespace f1
