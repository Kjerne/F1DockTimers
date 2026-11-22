#pragma once
// Start with Windows via HKCU\...\Run (per user, no admin rights). Port of services/autostart.py:
// the same value name ("F1DockTimers", the installer's too), and the pre-rename "NoctaliaF1"
// value is removed. The Microsoft Store copy starts through its package's StartupTask instead,
// which this build does not manage yet.

#include <QString>

namespace f1::autostart {

inline constexpr auto RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr auto VALUE = L"F1DockTimers";
inline constexpr auto LEGACY_VALUE = L"NoctaliaF1";

QString command(const QString& exePath);   // "<exe>" --minimized
bool isEnabled();
bool setEnabled(bool on);
QString currentCommand();                  // the registered command, or ""

}  // namespace f1::autostart
