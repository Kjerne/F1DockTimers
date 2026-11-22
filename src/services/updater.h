#pragma once
// Update check and one-click update from GitHub Releases (port of services/updater.py).
//
// Check: GET the latest release from the public GitHub API (no account, no personal data). About
// 30 s after start, then daily, plus "Check for updates" on demand.
// Trust: a release is installed only if its SHA256SUMS.txt carries a valid signature by the
// maintainer's release key (core/release_key.h; the private key never leaves their PC), and the
// signed list gives the setup the same SHA-256 that GitHub reports. Without that the update is
// shown but only opens the release page, so a hijacked GitHub account cannot push an update.
// Install: download the setup, check its SHA-256 against the signed list and GitHub's digest and,
// when this app is code-signed, require a valid Authenticode signature from the same publisher;
// then run the setup silently (it replaces the app and starts it again). A copy that was not
// installed opens the release page instead.

#include "../core/update_rules.h"
#include "../settings.h"

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace f1 {

class Updater : public QObject {
    Q_OBJECT
public:
    Updater(Settings* settings, QNetworkAccessManager* nam, std::function<void()> save, QObject* parent = nullptr);
    ~Updater() override;
    void start();
    void check(bool manual = true);
    bool canInstall() const;
    void install();
    void cancel();

    QString kind;   // installed | other | store
    std::optional<update::Release> latest;

    // Seams for the tests (defaults are the real thing).
    QString apiUrl = QString::fromLatin1(update::API_LATEST);
    QString currentVersion;
    QStringList downloadHosts = update::DOWNLOAD_HOSTS;
    QString downloadDir;   // default: %TEMP%/F1DockTimers-update
    std::function<update::Signature(const QString& path)> authenticode;
    std::function<bool(const QString& setup)> launch;   // starts the setup; false = could not
    std::function<qint64()> now;
    QList<QByteArray> releaseKeys;   // default: the keys built into the app

Q_SIGNALS:
    void available(const QString& version);         // once per check that finds something newer
    void checked(bool newer, const QString& message);   // for "Check for updates"
    void progress(qint64 done, qint64 total);
    void failed(const QString& message);
    void readyToQuit();                             // the new version is starting; this one should exit

private:
    void maybeCheck();
    void checkedReply(QNetworkReply* reply, bool manual);
    void announce(bool manual);
    void fetchSmall(const update::Asset& asset, std::function<void(std::optional<QByteArray>)> done);
    void guardRedirects(QNetworkReply* reply);
    std::optional<update::Asset> trustedSetup() const;   // the setup, if the signed list vouches for it
    void writeChunk();
    void downloaded();
    void afterVerify(const QString& problem);
    void discard(const QString& message);

    Settings* m_settings;
    QNetworkAccessManager* m_nam;
    std::function<void()> m_save;
    QTimer m_timer;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    update::Asset m_asset;
    bool m_tooLarge = false;
};

}  // namespace f1
