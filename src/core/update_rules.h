#pragma once
// The pure rules of the GitHub Releases updater (port of the tested helpers in
// services/updater.py): version compare, which release and file to trust, signature policy.

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace f1::update {

inline constexpr auto REPO = "Kjerne/F1DockTimers";
inline constexpr auto API_LATEST = "https://api.github.com/repos/Kjerne/F1DockTimers/releases/latest";
inline constexpr auto RELEASES_PAGE = "https://github.com/Kjerne/F1DockTimers/releases/latest";
inline constexpr qint64 CHECK_EVERY_S = 24 * 3600;
inline constexpr int FIRST_CHECK_MS = 30'000;
inline constexpr qint64 MAX_DOWNLOAD = 300LL * 1024 * 1024;
// Release files may only come from GitHub's own hosts.
inline const QStringList DOWNLOAD_HOSTS{QStringLiteral("github.com"), QStringLiteral("objects.githubusercontent.com"),
                                        QStringLiteral("release-assets.githubusercontent.com")};

struct Version {
    int major = 0, minor = 0, patch = 0;
    auto operator<=>(const Version&) const = default;
    QString text() const;
};
// "v1.0.3" / "1.0.3" -> 1.0.3; nullopt for anything else (e.g. pre-release tags).
std::optional<Version> parseVersion(const QString& text);
bool isNewer(const QString& remote, const QString& local);

// installed | other. The Python build also has "portable" (a one-file exe that replaces itself);
// the C++ build is a folder, so a copy that was not installed opens the release page instead.
QString installKind(const QString& exePath, const QString& localAppData);

struct Asset {
    QString name, url, sha256;
    qint64 size = 0;
};

// The checksum list CI publishes with each release, and the maintainer's signature of it.
inline constexpr auto SUMS_NAME = "SHA256SUMS.txt";
inline constexpr auto SIG_NAME = "SHA256SUMS.txt.sig";
inline constexpr qint64 MAX_SUMS = 64 * 1024;

// "<sha256> <space or *><file name>" lines -> {file name: sha256 in lower case}. A name listed
// more than once is left out, so it can never be installed.
QHash<QString, QString> parseSums(const QByteArray& text);

// A release file by its exact name, downloaded from github.com (no digest required).
std::optional<Asset> namedAsset(const QJsonObject& release, const QString& name);
// The setup ("installed") or portable asset of a release: named with the release's own version,
// a sha256 digest published by GitHub, downloaded from github.com.
std::optional<Asset> pickAsset(const QJsonObject& release, const QString& kind);

struct Release {
    QString version, tag, name, notes, url;
    QJsonObject raw;
    // Filled once SHA256SUMS.txt and its signature were fetched and checked: the signed list
    // (empty when the signature is missing or not by a release key).
    bool signatureChecked = false;
    QHash<QString, QString> signedSums;
};
// A published, non-draft, non-prerelease release with a plain version tag.
std::optional<Release> parseRelease(const QJsonValue& data);

// Signed app -> the update must be validly signed by the same publisher. Unsigned app -> accept
// unsigned (the checksum is still required) or validly signed. Returns the reason when refused.
struct Signature {
    QString status, subject;   // Valid | NotSigned | HashMismatch | ... ; signer subject
};
std::optional<QString> signatureProblem(const Signature& current, const Signature& update);

bool hostAllowed(const QString& host, const QStringList& hosts = DOWNLOAD_HOSTS);

}  // namespace f1::update
