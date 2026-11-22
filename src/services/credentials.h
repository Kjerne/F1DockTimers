#pragma once
// OpenF1 credentials in Windows Credential Manager (port of services/credentials.py).
//
// Stored exactly where Python's `keyring` (WinVaultKeyring) puts them, so the C++ and Python
// builds share one entry: a generic credential with target "F1DockTimers/OpenF1" (or
// "<user>@F1DockTimers/OpenF1" when another user already holds the plain target), the password
// as UTF-16LE in the blob. The password is never written to settings, the cache or the log.
// OPENF1_USERNAME / OPENF1_PASSWORD in the environment are honoured too, as in the original.

#include <QString>
#include <optional>
#include <utility>

namespace f1::credentials {

inline constexpr const char* SERVICE = "F1DockTimers/OpenF1";
inline constexpr const char* LEGACY_SERVICE = "NoctaliaF1/OpenF1";

using Pair = std::pair<QString, QString>;

std::optional<QString> getPassword(const QString& username);
bool setPassword(const QString& username, const QString& password);
void deletePassword(const QString& username);

// (username, password) from the environment first, then Credential Manager.
std::optional<Pair> resolve(const QString& settingsUsername);

// keyring's compound target name, for tests.
QString compoundTarget(const QString& username, const QString& service);

}  // namespace f1::credentials
