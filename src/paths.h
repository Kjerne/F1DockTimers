#pragma once
// Filesystem locations, same as f1win/paths.py: settings roam (%APPDATA%\F1DockTimers), the cache
// stays local (%LOCALAPPDATA%\F1DockTimers). NOCTALIA_F1_HOME=<dir> moves both to <dir>\config and
// <dir>\cache (tests, side-by-side runs, and the MSIX-sandboxed dev environment).

#include <QString>

namespace f1::paths {

inline constexpr const char* APP_NAME = "F1DockTimers";

QString configDir();
QString cacheDir();
QString settingsFile();
// Bundled assets (fonts, tracks.txt): <exe dir>/assets
QString resourceDir();
// Built-in languages: <exe dir>/lang
QString builtinLangDir();
QString userLangDir();

}  // namespace f1::paths
