# Code signing policy

> **Status: pending.** The application to SignPath Foundation is under review. Until it is
> approved, releases are **not signed** yet (Windows may show a SmartScreen warning).

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

## What is signed

Only the release files of this project, built from this repository by its GitHub Actions workflow
(`.github/workflows/windows-build.yml`):

* `F1DockTimers-<version>-setup.exe` (installer)
* `F1DockTimers-<version>-portable.exe` (single-file portable app)

Third-party components (Qt/PySide6, Python, keyring, tzdata) are bundled unmodified and keep their
own licences, listed in `licenses/` and `THIRD_PARTY_NOTICES.md`.

## Team and roles

| Role | Members |
| --- | --- |
| Committers and reviewers | [Kjerne](https://github.com/Kjerne) |
| Approvers | [Kjerne](https://github.com/Kjerne) |

* **Committers** may change the source code in this repository.
* **Reviewers** review every change from people who are not committers (pull requests) before it is merged.
* **Approvers** manually approve each signing request in SignPath before a release is signed.

Every team member uses multi-factor authentication for GitHub and SignPath.

## Privacy

See [PRIVACY.md](PRIVACY.md). In short: the app does not collect, store or send personal data. It
only downloads public Formula 1 data from the services listed there, and it sends nothing else
anywhere unless you enter an OpenF1 account yourself.
