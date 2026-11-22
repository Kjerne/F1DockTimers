// Port of reference-python/tests/test_updater.py (the pure rules), plus the check and the
// download/verify/launch flow against a local HTTP server, and Windows' own signature check.
#include "common.h"
#include "core/update_rules.h"
#include "services/release_sig.h"
#include "services/updater.h"
#include "services/wintrust.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

using namespace f1;
using namespace f1::update;

namespace {

const QString SHA = QString(64, u'a');

QJsonObject asset(const QString& name, const QString& digest, const QString& url, qint64 size = 1) {
    QJsonObject a{{"name", name}, {"size", size}, {"browser_download_url", url}};
    if (!digest.isEmpty()) a["digest"_L1] = digest;
    return a;
}

QJsonObject release(const QString& tag = QStringLiteral("v1.0.3"), std::optional<QJsonArray> assets = std::nullopt) {
    const QString base = QStringLiteral("https://github.com/Kjerne/F1DockTimers/releases/download/v1.0.3/");
    return {{"tag_name", tag}, {"name", "F1DockTimers " + tag}, {"body", "Fixes"}, {"draft", false}, {"prerelease", false},
            {"html_url", "https://github.com/Kjerne/F1DockTimers/releases/tag/" + tag},
            {"assets", assets ? *assets
                              : QJsonArray{asset(QStringLiteral("F1DockTimers-1.0.3-setup.exe"), "sha256:" + SHA,
                                                 base + "F1DockTimers-1.0.3-setup.exe", 40'000'000),
                                           asset(QStringLiteral("F1DockTimers-1.0.3-portable.exe"), "sha256:" + QString(64, u'b'),
                                                 base + "F1DockTimers-1.0.3-portable.exe", 44'000'000)}}};
}

// A one-request-per-connection HTTP server: path -> (status, body).
class TinyHttp : public QObject {
public:
    QTcpServer server;
    QHash<QString, std::pair<int, QByteArray>> routes;
    QStringList hits;
    TinyHttp() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray req = s->readAll();
                    const QString path = QString::fromLatin1(req.split(' ').value(1));
                    hits << path;
                    const auto [status, body] = routes.value(path, {404, QByteArray("{}")});
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: " + QByteArray::number(body.size()) +
                             "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
            }
        });
    }
    QString url(const QString& path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path); }
};

// Sends https://github.com/... requests to the local server instead, so the real asset rules
// (github.com URLs only) stay in force in the test.
class RedirectingNam : public QNetworkAccessManager {
public:
    quint16 port = 0;
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& original, QIODevice* data) override {
        QNetworkRequest req(original);
        QUrl u = req.url();
        if (u.host() == u"github.com") {
            u.setScheme(QStringLiteral("http"));
            u.setHost(QStringLiteral("127.0.0.1"));
            u.setPort(port);
            req.setUrl(u);
        }
        return QNetworkAccessManager::createRequest(op, req, data);
    }
};


// A P-256 key made with Windows' BCrypt for the tests (the real release key is never involved).
struct TestKey {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    QByteArray pub;   // X||Y
    TestKey() {
        BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
        BCryptGenerateKeyPair(alg, &key, 256, 0);
        BCryptFinalizeKeyPair(key, 0);
        ULONG n = 0;
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &n, 0);
        QByteArray blob(static_cast<int>(n), '\0');
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(blob.data()), n, &n, 0);
        pub = blob.mid(sizeof(BCRYPT_ECCKEY_BLOB));
    }
    ~TestKey() {
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    QByteArray sign(const QByteArray& data) const {
        QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        QByteArray sig(64, '\0');
        ULONG n = 0;
        BCryptSignHash(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()), 32, reinterpret_cast<PUCHAR>(sig.data()), 64, &n, 0);
        return sig;
    }
};

QByteArray readFixture(const char* name) {
    QFile f(QStringLiteral(F1_FIXTURES "release_sig/") + QString::fromLatin1(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A release v9.0.0 with a setup, SHA256SUMS.txt and (optionally) its signature, served locally.
struct Flow {
    TinyHttp http;
    RedirectingNam nam;
    QTemporaryDir tmp;
    Settings s;
    TestKey key;
    Updater u{&s, &nam, [] {}};
    const QString base = QStringLiteral("/Kjerne/F1DockTimers/releases/download/v9.0.0/");
    const QString setupPath = base + QStringLiteral("F1DockTimers-9.0.0-setup.exe");
    QByteArray payload = QByteArray("MZ fake setup ").repeated(5000);
    QString sha = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    QString digest = sha;
    QByteArray sums = (sha + QStringLiteral("  F1DockTimers-9.0.0-setup.exe\r\n") + QString(64, u'1') +
                       QStringLiteral("  F1DockTimers-9.0.0-portable.zip\r\n")).toLatin1();
    QString launched;
    Flow() {
        nam.port = http.server.serverPort();
        u.kind = QStringLiteral("installed");
        u.currentVersion = QStringLiteral("1.1.0");
        u.apiUrl = http.url(QStringLiteral("/latest"));
        u.downloadDir = tmp.path();
        u.downloadHosts = {QStringLiteral("127.0.0.1")};
        u.releaseKeys = {key.pub};
        u.now = [] { return qint64(1'800'000'000); };
        u.authenticode = [](const QString&) { return Signature{QStringLiteral("NotSigned"), {}}; };
        u.launch = [this](const QString& file) {
            launched = file;
            return true;
        };
    }
    QByteArray signedBy(const TestKey& k) const { return k.sign(sums); }
    void serve(const QByteArray& sig, bool withSig = true) {
        const QString gh = QStringLiteral("https://github.com");
        QJsonArray assets{asset(QStringLiteral("F1DockTimers-9.0.0-setup.exe"), "sha256:" + digest, gh + setupPath, payload.size()),
                          asset(QStringLiteral("SHA256SUMS.txt"), {}, gh + base + QStringLiteral("SHA256SUMS.txt"), sums.size())};
        if (withSig) assets.append(asset(QStringLiteral("SHA256SUMS.txt.sig"), {}, gh + base + QStringLiteral("SHA256SUMS.txt.sig"), 64));
        http.routes[QStringLiteral("/latest")] = {200, QJsonDocument(release(QStringLiteral("v9.0.0"), assets)).toJson()};
        http.routes[base + QStringLiteral("SHA256SUMS.txt")] = {200, sums};
        if (withSig) http.routes[base + QStringLiteral("SHA256SUMS.txt.sig")] = {200, sig};
        http.routes[setupPath] = {200, payload};
    }
    bool check() {
        QSignalSpy available(&u, &Updater::available);
        u.check(false);
        return available.wait(10000) && u.latest && u.latest->signatureChecked;
    }
};
}  // namespace

class TestUpdater : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void versions() {
        QVERIFY(parseVersion(QStringLiteral("v1.0.3")) == (Version{1, 0, 3}));
        QVERIFY(parseVersion(QStringLiteral("1.10.0")) == (Version{1, 10, 0}));
        QVERIFY(!parseVersion(QStringLiteral("v1.0.3-beta")) && !parseVersion(QString()));
        QVERIFY(isNewer(QStringLiteral("v1.0.10"), QStringLiteral("1.0.9")));
        QVERIFY(!isNewer(QStringLiteral("v1.0.2"), QStringLiteral("1.0.2")));
        QVERIFY(!isNewer(QStringLiteral("v1.0.1"), QStringLiteral("1.0.2")));
        QVERIFY(!isNewer(QStringLiteral("garbage"), QStringLiteral("1.0.2")));
        QVERIFY(!isNewer(QStringLiteral("v1.0.8"), QStringLiteral("1.1.0")));   // Python releases never "update" the C++ build
    }
    void releaseParsingAndAssets() {
        const auto rel = parseRelease(release());
        QVERIFY(rel);
        QCOMPARE(rel->version, QStringLiteral("1.0.3"));
        const auto setup = pickAsset(rel->raw, QStringLiteral("installed"));
        const auto port = pickAsset(rel->raw, QStringLiteral("portable"));
        QVERIFY(setup && setup->name.endsWith(QStringLiteral("-setup.exe")) && setup->sha256 == SHA);
        QVERIFY(port && port->name.endsWith(QStringLiteral("-portable.exe")));
        QJsonObject pre = release();
        pre["prerelease"_L1] = true;
        QVERIFY(!parseRelease(pre));
        QJsonObject draft = release();
        draft["draft"_L1] = true;
        QVERIFY(!parseRelease(draft));
        QVERIFY(!parseRelease(release(QStringLiteral("nightly"))));
        QVERIFY(!parseRelease(QJsonObject{{"html_url", "x"}}));
        QJsonObject elsewhere = release();
        elsewhere["html_url"_L1] = "https://evil.example/x";
        QCOMPARE(parseRelease(elsewhere)->url, QString::fromLatin1(RELEASES_PAGE));
    }
    void assetsWithoutChecksumOrFromElsewhereAreRefused() {
        const QString name = QStringLiteral("F1DockTimers-1.0.3-setup.exe");
        QVERIFY(!pickAsset(release({}, QJsonArray{asset(name, {}, QStringLiteral("https://github.com/x/y/z.exe"))}), QStringLiteral("installed")));
        QVERIFY(!pickAsset(release({}, QJsonArray{asset(name, "sha256:" + SHA, QStringLiteral("https://evil.example/") + name)}),
                           QStringLiteral("installed")));
        QVERIFY(!pickAsset(release({}, QJsonArray{asset(name, QStringLiteral("md5:abc"), QStringLiteral("https://github.com/x"))}),
                           QStringLiteral("installed")));
    }
    void signatureRules() {
        const Signature signedBy{QStringLiteral("Valid"), QStringLiteral("CN=Example Publisher")};
        const Signature unsignedFile{QStringLiteral("NotSigned"), {}};
        QVERIFY(!signatureProblem(unsignedFile, unsignedFile));   // unsigned -> unsigned ok (checksum still checked)
        QVERIFY(!signatureProblem(unsignedFile, signedBy));       // a later signed release
        QVERIFY(!signatureProblem(signedBy, signedBy));
        QVERIFY(signatureProblem(signedBy, unsignedFile));        // never downgrade to unsigned
        QVERIFY(signatureProblem(signedBy, {QStringLiteral("Valid"), QStringLiteral("CN=Someone Else")}));
        QVERIFY(signatureProblem(unsignedFile, {QStringLiteral("HashMismatch"), QStringLiteral("CN=X")}));
    }
    void installKindRules() {
        QTemporaryDir tmp;
        const QString inst = tmp.filePath(QStringLiteral("elsewhere/F1DockTimers"));
        QDir().mkpath(inst);
        QFile(inst + QStringLiteral("/unins000.exe")).open(QIODevice::WriteOnly);
        QCOMPARE(installKind(inst + QStringLiteral("/F1DockTimers.exe"), {}), QStringLiteral("installed"));
        QCOMPARE(installKind(tmp.filePath(QStringLiteral("Programs/F1DockTimers/F1DockTimers.exe")), tmp.path()), QStringLiteral("installed"));
        QCOMPARE(installKind(tmp.filePath(QStringLiteral("build/Release/F1DockTimers.exe")), tmp.path()), QStringLiteral("other"));
    }
    void releaseWithMismatchedFileVersionsIsNotAutoInstalled() {
        // e.g. tag v1.0.2 whose files still say 1.0.3: open the page instead of looping
        QVERIFY(!pickAsset(release(QStringLiteral("v1.0.2")), QStringLiteral("installed")));
        QVERIFY(!pickAsset(release(QStringLiteral("v1.0.2")), QStringLiteral("portable")));
    }
    void downloadHosts() {
        QVERIFY(hostAllowed(QStringLiteral("github.com")));
        QVERIFY(hostAllowed(QStringLiteral("release-assets.githubusercontent.com")));
        QVERIFY(!hostAllowed(QStringLiteral("evilgithub.com")));
        QVERIFY(!hostAllowed(QStringLiteral("github.com.evil.example")));
    }
    void authenticodeOnRealFiles() {
        // Windows' own verdict: a Microsoft-signed system file and an unsigned one.
        const auto sys = wintrust::authenticode(QStringLiteral("C:/Windows/explorer.exe"));
        QVERIFY2(sys.status == u"Valid" || sys.status == u"NotSigned", qPrintable(sys.status));   // catalog-signed files report NotSigned
        QTemporaryDir tmp;
        QFile f(tmp.filePath(QStringLiteral("x.exe")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("MZ not a real program");
        f.close();
        QCOMPARE(wintrust::authenticode(f.fileName()).status, QStringLiteral("NotSigned"));
        QVERIFY(!wintrust::isPackaged());
    }

    void checkFindsANewerRelease() {
        TinyHttp http;
        http.routes[QStringLiteral("/latest")] = {200, QJsonDocument(release(QStringLiteral("v9.0.0"))).toJson()};
        QNetworkAccessManager nam;
        Settings s;
        int saves = 0;
        Updater u(&s, &nam, [&] { ++saves; });
        u.kind = QStringLiteral("other");
        u.apiUrl = http.url(QStringLiteral("/latest"));
        u.currentVersion = QStringLiteral("1.1.0");
        u.now = [] { return qint64(1'800'000'000); };
        QSignalSpy available(&u, &Updater::available), checked(&u, &Updater::checked);
        u.check(true);
        QVERIFY(available.wait(5000));
        QCOMPARE(available.first().first().toString(), QStringLiteral("9.0.0"));
        QCOMPARE(checked.size(), 1);
        QVERIFY(checked.first().first().toBool());
        QCOMPARE(s.update_checked_at, 1'800'000'000.0);
        QCOMPARE(saves, 1);
        QVERIFY(u.latest && !u.canInstall());   // not an installed copy: release page instead

        http.routes[QStringLiteral("/latest")] = {200, QJsonDocument(release(QStringLiteral("v1.0.8"))).toJson()};
        u.check(true);
        QVERIFY(checked.wait(5000));
        QVERIFY(!checked.last().first().toBool());
        QCOMPARE(checked.last().last().toString(), QStringLiteral("You're up to date (version 1.1.0)."));
        QVERIFY(!u.latest);

        http.routes.remove(QStringLiteral("/latest"));
        u.check(true);
        QVERIFY(checked.wait(5000));
        QCOMPARE(checked.last().last().toString(), QStringLiteral("No release published yet."));
        QCOMPARE(available.size(), 1);
    }
    void dailyCheckOnlyWhenADayHasPassed() {
        TinyHttp http;
        QNetworkAccessManager nam;
        Settings s;
        s.update_checked_at = 1'800'000'000.0 - 3600;
        Updater u(&s, &nam, [] {});
        u.kind = QStringLiteral("other");
        u.apiUrl = http.url(QStringLiteral("/latest"));
        u.now = [] { return qint64(1'800'000'000); };
        QMetaObject::invokeMethod(&u, [&u] { u.start(); });
        QTest::qWait(200);
        QVERIFY(http.hits.isEmpty());
        u.kind = QStringLiteral("store");
        QSignalSpy checked(&u, &Updater::checked);
        u.check(true);   // the Store copy never asks GitHub
        QCOMPARE(checked.size(), 1);
        QVERIFY(http.hits.isEmpty());
    }

    // ── release-key signatures ───────────────────────────────────────────────────────────────

    void scriptsSignatureVerifiesInTheApp() {
        // tests/fixtures/release_sig was made by tools/new-release-key.ps1 and tools/sign-release.ps1
        // with a throwaway key (tools/test-release-key.ps1 -WriteFixture): the app must accept it.
        const QByteArray sums = readFixture("SHA256SUMS.txt"), sig = readFixture("SHA256SUMS.txt.sig");
        const QByteArray pub = QByteArray::fromHex(readFixture("public.hex").trimmed());
        QCOMPARE(sig.size(), 64);
        QCOMPARE(pub.size(), 64);
        QVERIFY(releasesig::verify(sums, sig, pub));
        QByteArray tampered = sums;
        tampered[0] = tampered[0] == 'a' ? 'b' : 'a';
        QVERIFY(!releasesig::verify(tampered, sig, pub));
        QVERIFY(!releasesig::verify(sums, sig, TestKey().pub));   // another key
        QVERIFY(!releasesig::verify(sums, sig.left(63), pub));
        QVERIFY(!releasesig::verify(sums, QByteArray(64, '\0'), pub));
        QVERIFY(releasesig::verifyAny(sums, sig, {TestKey().pub, pub}));
        const auto parsed = parseSums(sums);
        QCOMPARE(parsed.value(QStringLiteral("F1DockTimers-9.0.0-setup.exe")), QStringLiteral("cd").repeated(32));
    }
    void sumsParsing() {
        const QByteArray text = QString(64, u'A').toLatin1() + " *F1DockTimers-1.2.3-setup.exe\r\n" + QString(64, u'b').toLatin1() +
                                "  dup.zip\n" + QString(64, u'c').toLatin1() + "  dup.zip\nnot a line\n";
        const auto sums = parseSums(text);
        QCOMPARE(sums.value(QStringLiteral("F1DockTimers-1.2.3-setup.exe")), QString(64, u'a'));   // lower case, '*' marker
        QVERIFY(!sums.contains(QStringLiteral("dup.zip")));   // listed twice: never trusted
        QCOMPARE(sums.size(), 1);
    }
    void builtInKeysAreWellFormed() {
        for (const QByteArray& k : releasesig::builtInKeys()) QCOMPARE(k.size(), 64);
    }

    void signedReleaseInstalls() {
        Flow f;
        f.serve(f.signedBy(f.key));
        QVERIFY(f.check());
        QVERIFY(f.u.canInstall());
        QSignalSpy ready(&f.u, &Updater::readyToQuit), failed(&f.u, &Updater::failed);
        f.u.install();
        QVERIFY(ready.wait(10000));
        QVERIFY(failed.isEmpty());
        QCOMPARE(QFileInfo(f.launched).fileName(), QStringLiteral("F1DockTimers-9.0.0-setup.exe"));
        QFile got(f.launched);
        QVERIFY(got.open(QIODevice::ReadOnly));
        QCOMPARE(got.readAll(), f.payload);
    }
    void missingSignatureNeverInstalls() {
        Flow f;
        f.serve(QByteArray(), /*withSig*/ false);
        QVERIFY(f.check());   // the update is still shown
        QVERIFY(!f.u.canInstall());
        QSignalSpy failed(&f.u, &Updater::failed);
        f.u.install();
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("not signed")));
        QVERIFY(f.launched.isEmpty());
        QVERIFY(!f.http.hits.contains(f.setupPath));   // nothing downloaded
    }
    void signatureByAnotherKeyNeverInstalls() {
        Flow f;
        TestKey other;
        f.serve(f.signedBy(other));
        QVERIFY(f.check());
        QVERIFY(!f.u.canInstall());
    }
    void tamperedSumsNeverInstall() {
        Flow f;
        const QByteArray sig = f.signedBy(f.key);
        f.sums.replace(f.sha.toLatin1(), QString(64, u'0').toLatin1());   // swapped after signing
        f.serve(sig);
        QVERIFY(f.check());
        QVERIFY(!f.u.canInstall());
    }
    void signedListMustAgreeWithGithub() {
        Flow f;
        f.digest = QString(64, u'e');   // GitHub reports another file than the one signed
        f.serve(f.signedBy(f.key));
        QVERIFY(f.check());
        QVERIFY(!f.u.canInstall());
    }
    void noBuiltInKeyNeverInstalls() {
        Flow f;
        f.u.releaseKeys.clear();
        f.serve(f.signedBy(f.key));
        QVERIFY(f.check());
        QVERIFY(!f.u.canInstall());
    }
    void signedButBrokenDownloadsAreRefused() {
        // A wrong file, an unsigned update for a signed app, a foreign host: all refused.
        Flow f;
        f.serve(f.signedBy(f.key));
        QVERIFY(f.check());
        QSignalSpy failed(&f.u, &Updater::failed);
        f.http.routes[f.setupPath] = {200, QByteArray("something else")};
        f.u.install();
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.last().first().toString().contains(QStringLiteral("checksum")));
        QVERIFY(!QFile::exists(QDir(f.tmp.path()).filePath(QStringLiteral("F1DockTimers-9.0.0-setup.exe"))));
        f.http.routes[f.setupPath] = {200, f.payload};
        f.u.authenticode = [](const QString& p) {
            return p.endsWith(QStringLiteral("setup.exe")) ? Signature{QStringLiteral("NotSigned"), {}}
                                                           : Signature{QStringLiteral("Valid"), QStringLiteral("CN=Kjerne")};
        };
        f.u.install();
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.last().first().toString().startsWith(QStringLiteral("Not installing: the update is not validly signed")));
        f.u.authenticode = [](const QString&) { return Signature{QStringLiteral("NotSigned"), {}}; };
        f.u.downloadHosts = DOWNLOAD_HOSTS;
        f.u.install();
        QVERIFY(failed.wait(10000));
        QVERIFY(f.launched.isEmpty());
    }
    void installWithoutAVerifiableAssetFails() {
        QNetworkAccessManager nam;
        Settings s;
        Updater u(&s, &nam, [] {});
        u.kind = QStringLiteral("other");
        u.latest = parseRelease(release());
        QVERIFY(!u.canInstall());
        QSignalSpy failed(&u, &Updater::failed);
        u.install();
        QCOMPARE(failed.size(), 1);
    }
};

QTEST_MAIN(TestUpdater)
#include "tst_updater.moc"
