"""Write winget manifests for a published GitHub release.

    python tools/make_winget.py 1.0.6

Reads the release's setup file and its SHA-256 from the GitHub API (no download needed) and
writes manifests/k/Kjerne/F1DockTimers/<version>/ in the layout of microsoft/winget-pkgs. Submit
them with a pull request to https://github.com/microsoft/winget-pkgs (see docs/WINGET.md).
"""
from __future__ import annotations

import json
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = "Kjerne/F1DockTimers"
PKG = "Kjerne.F1DockTimers"
SCHEMA = "1.6.0"


def release_asset(version: str) -> tuple[str, str]:
    req = urllib.request.Request(f"https://api.github.com/repos/{REPO}/releases/tags/v{version}",
                                 headers={"Accept": "application/vnd.github+json", "User-Agent": "f1docktimers-winget"})
    rel = json.load(urllib.request.urlopen(req, timeout=20))
    name = f"F1DockTimers-{version}-setup.exe"
    for a in rel.get("assets", []):
        if a.get("name") == name:
            digest = str(a.get("digest") or "")
            if not digest.startswith("sha256:"):
                raise SystemExit(f"{name} has no SHA-256 digest on GitHub")
            return a["browser_download_url"], digest.split(":", 1)[1].upper()
    raise SystemExit(f"release v{version} has no {name}")


def manifests(version: str, url: str, sha: str) -> dict[str, str]:
    head = f"# yaml-language-server: $schema=https://aka.ms/winget-manifest.{{kind}}.{SCHEMA}.schema.json\n\n"
    common = f"PackageIdentifier: {PKG}\nPackageVersion: {version}\n"
    return {
        f"{PKG}.yaml": head.format(kind="version") + common +
        f"DefaultLocale: en-US\nManifestType: version\nManifestVersion: {SCHEMA}\n",
        f"{PKG}.installer.yaml": head.format(kind="installer") + common +
        "InstallerType: inno\nScope: user\nInstallModes:\n- interactive\n- silent\n- silentWithProgress\n"
        "UpgradeBehavior: install\nInstallers:\n"
        f"- Architecture: x64\n  InstallerUrl: {url}\n  InstallerSha256: {sha}\n"
        f"ManifestType: installer\nManifestVersion: {SCHEMA}\n",
        f"{PKG}.locale.en-US.yaml": head.format(kind="defaultLocale") + common +
        "PackageLocale: en-US\nPublisher: Kjerne\n"
        f"PublisherUrl: https://github.com/Kjerne\nPublisherSupportUrl: https://github.com/{REPO}/issues\n"
        f"PrivacyUrl: https://github.com/{REPO}/blob/main/PRIVACY.md\n"
        "PackageName: F1DockTimers\n"
        f"PackageUrl: https://github.com/{REPO}\nLicense: MIT\nLicenseUrl: https://github.com/{REPO}/blob/main/LICENSE\n"
        "ShortDescription: Formula 1 schedule, standings, live timing and a session countdown in your Windows taskbar.\n"
        "Description: |-\n  F1DockTimers shows the Formula 1 calendar, standings, results and live timing in a tray panel,\n"
        "  a desktop widget and a countdown next to the taskbar clock. Free, open source, no ads, no tracking.\n"
        "Tags:\n- formula1\n- f1\n- motorsport\n- countdown\n- taskbar\n- widget\n"
        f"ReleaseNotesUrl: https://github.com/{REPO}/releases/tag/v{version}\n"
        f"ManifestType: defaultLocale\nManifestVersion: {SCHEMA}\n",
    }


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    version = sys.argv[1].lstrip("v")
    url, sha = release_asset(version)
    out = ROOT / "manifests" / "k" / "Kjerne" / "F1DockTimers" / version
    out.mkdir(parents=True, exist_ok=True)
    for name, text in manifests(version, url, sha).items():
        (out / name).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
