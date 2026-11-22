#include "autostart.h"

#include "wintrust.h"

#include <QCoreApplication>
#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace f1::autostart {

namespace {

bool has(const wchar_t* name) {
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, name, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

}  // namespace

QString command(const QString& exePath) {
    return u'"' + QDir::toNativeSeparators(exePath) + QStringLiteral("\" --minimized");
}

bool isEnabled() {
    if (wintrust::isPackaged()) return false;   // StartupTask: not managed by this build yet
    return has(VALUE) || has(LEGACY_VALUE);
}

QString currentCommand() {
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, VALUE, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return QString::fromWCharArray(buf);
}

bool setEnabled(bool on) {
    if (wintrust::isPackaged()) return false;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    RegDeleteValueW(key, LEGACY_VALUE);   // old name, replaced below
    bool ok = true;
    if (on) {
        const std::wstring cmd = command(QCoreApplication::applicationFilePath()).toStdWString();
        ok = RegSetValueExW(key, VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LSTATUS rc = RegDeleteValueW(key, VALUE);
        ok = rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

}  // namespace f1::autostart
