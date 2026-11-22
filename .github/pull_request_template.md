## What this changes

<!-- A short description, and the issue it fixes if there is one (e.g. "Fixes #12"). -->

## How you tested it

<!-- Which Windows version, and what you checked in the running app (tray panel, desktop widget,
taskbar countdown, settings). Screenshots help for anything visual. -->

## Checklist

- [ ] `cmake --workflow --preset release` builds and all tests pass
- [ ] No made-up or placeholder Formula 1 data: everything shown comes from Jolpica, OpenF1 or the F1 live timing feed
- [ ] No paywall workarounds: OpenF1 live data stays on the Sponsor plan, and request limits are respected
- [ ] Passwords and tokens are only stored in Windows Credential Manager and never written to logs or settings
- [ ] Any new network address is listed in `PRIVACY.md`
- [ ] No binaries or build output included (release files are built by GitHub Actions)
- [ ] Changes to `.github/`, `packaging/`, `tools/`, the CMake files or `src/core/release_key.h` are explained above
- [ ] No `Co-authored-by` lines crediting AI tools (Claude, Copilot, ChatGPT ...) in the commit messages
