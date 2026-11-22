# Contributing

Thanks for helping! Bug reports, ideas and pull requests are all welcome.

## Reporting a bug

Open an issue with your Windows version, what you expected and what happened instead. Screenshots
help for anything visual. The log is at `%LOCALAPPDATA%\F1DockTimers\f1.log` (it contains no
passwords or tokens).

## Pull requests

1. Build the app from source: Visual Studio 2022 (C++ workload) and Qt 6.8.3 for MSVC 2022 64-bit,
   then `cmake --workflow --preset release` in the repository folder. That builds and runs every
   test; the app is `build\msvc\Release\F1DockTimers.exe`. Details in [docs/BUILD.md](docs/BUILD.md).
2. Use `NOCTALIA_F1_HOME` for a separate settings and cache folder while testing, so your own
   settings stay untouched.
3. Keep to the rules the app follows:
   - Only real data from Jolpica, OpenF1 and the F1 live timing feed; nothing made up.
   - No paywall workarounds; respect the services' request limits.
   - Passwords and tokens only in Windows Credential Manager, never in logs or settings files.
   - A new web address the app talks to must be added to `PRIVACY.md`.
   - No binaries or build output; release files are built by GitHub Actions.

Every pull request is scanned by the "PR safety check" for things that need a careful look (running
other programs, new web addresses, registry changes, credentials near logs, build changes, ...).
A flag is not a rejection: just explain the flagged lines in the pull request.

## Translations

English is built in. To add a language, copy `lang/_template.txt` to `lang/<code>.txt` (for example
`da.txt`), fill in `@code` and `@name`, and translate the lines you want; empty lines stay English.
Keep `{placeholders}` as they are. You can try it without building anything: put the file in
`%APPDATA%\F1DockTimers\lang` (Settings > General > Language > Open language folder), pick it and
restart the app. New texts in the code go through `T(QStringLiteral("..."))` (or `F1_TR_NOOP("...")`
for texts kept in an array); run `python tools/make_lang_template.py` to update the template (a test
fails while it is out of date).

## AI tools

Using AI tools is fine, but please remove `Co-authored-by` lines that credit them (for example
`Co-authored-by: Claude ...`, `Co-authored-by: Copilot ...` or `Co-authored-by: ChatGPT ...`) from your commit messages, so the AI
isn't listed as a contributor. The PR safety check fails while such lines are present.
