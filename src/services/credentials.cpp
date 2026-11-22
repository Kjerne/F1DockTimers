#include "credentials.h"

#include <windows.h>
#include <wincred.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCred, "f1.credentials")

namespace f1::credentials {

namespace {

struct Stored {
    QString user, password;
};

std::optional<Stored> readCredential(const QString& target) {
    PCREDENTIALW cred = nullptr;
    const std::wstring t = target.toStdWString();
    if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &cred)) return std::nullopt;
    Stored s;
    s.user = cred->UserName ? QString::fromWCharArray(cred->UserName) : QString();
    s.password = QString::fromUtf16(reinterpret_cast<const char16_t*>(cred->CredentialBlob), cred->CredentialBlobSize / 2);
    SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
    CredFree(cred);
    return s;
}

bool writeCredential(const QString& target, const QString& user, const QString& password) {
    std::wstring t = target.toStdWString(), u = user.toStdWString();
    std::wstring comment = L"Stored using python-keyring";   // same entry the Python app writes
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = t.data();
    c.UserName = u.data();
    c.Comment = comment.data();
    c.CredentialBlobSize = static_cast<DWORD>(password.size() * 2);
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<QChar*>(password.constData()));
    c.Persist = CRED_PERSIST_ENTERPRISE;
    return CredWriteW(&c, 0) != FALSE;
}

void deleteCredential(const QString& target) {
    const std::wstring t = target.toStdWString();
    CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0);
}

// keyring.get_password(service, username)
std::optional<QString> keyringGet(const QString& service, const QString& username) {
    auto res = readCredential(service);
    if (!res || (!username.isEmpty() && res->user != username)) res = readCredential(compoundTarget(username, service));
    if (!res) return std::nullopt;
    return res->password;
}

// keyring.set_password(service, username, password)
bool keyringSet(const QString& service, const QString& username, const QString& password) {
    if (const auto existing = readCredential(service))   // keep another user's entry under its compound name
        writeCredential(compoundTarget(existing->user, service), existing->user, existing->password);
    return writeCredential(service, username, password);
}

// keyring.delete_password(service, username)
void keyringDelete(const QString& service, const QString& username) {
    for (const QString& target : {service, compoundTarget(username, service)}) {
        const auto existing = readCredential(target);
        if (existing && existing->user == username) deleteCredential(target);
    }
}

}  // namespace

QString compoundTarget(const QString& username, const QString& service) { return username + u'@' + service; }

std::optional<QString> getPassword(const QString& username) {
    if (username.isEmpty()) return std::nullopt;
    const QString service = QString::fromLatin1(SERVICE), legacy = QString::fromLatin1(LEGACY_SERVICE);
    if (auto pw = keyringGet(service, username)) return pw;
    if (auto old = keyringGet(legacy, username)) {   // move it to the new name once
        if (keyringSet(service, username, *old)) keyringDelete(legacy, username);
        return old;
    }
    return std::nullopt;
}

bool setPassword(const QString& username, const QString& password) {
    if (username.isEmpty()) return false;
    const bool ok = keyringSet(QString::fromLatin1(SERVICE), username, password);
    if (!ok) qCWarning(lcCred) << "could not store credential";
    return ok;
}

void deletePassword(const QString& username) {
    if (username.isEmpty()) return;
    keyringDelete(QString::fromLatin1(SERVICE), username);
    keyringDelete(QString::fromLatin1(LEGACY_SERVICE), username);
}

std::optional<Pair> resolve(const QString& settingsUsername) {
    const QString user = qEnvironmentVariable("OPENF1_USERNAME"), pw = qEnvironmentVariable("OPENF1_PASSWORD");
    if (!user.isEmpty() && !pw.isEmpty()) return Pair{user, pw};
    if (!settingsUsername.isEmpty())
        if (auto stored = getPassword(settingsUsername); stored && !stored->isEmpty()) return Pair{settingsUsername, *stored};
    return std::nullopt;
}

}  // namespace f1::credentials
