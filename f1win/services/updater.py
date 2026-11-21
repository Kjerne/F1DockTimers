"""Update check and one-click update from GitHub Releases.

* Check: GET https://api.github.com/repos/<repo>/releases/latest (public, no account,
  no personal data; GitHub sees the IP and the user agent like any website). Once
  ~30 s after start and then daily, plus "Check for updates" on demand.
* Install: download the matching release file, verify its SHA-256 against the digest
  GitHub publishes for the asset, and (when the running app is code-signed) require a
  valid Authenticode signature from the same publisher. Then:
    - installed copy  -> run the new setup.exe silently, it replaces the app and restarts it
    - portable exe    -> save the new portable exe next to the old one, start it, it
                         deletes the old file after this process has exited
    - anything else (source / build folder) -> open the release page in the browser
"""
from __future__ import annotations

import hashlib
import json
import logging
import os
import re
import subprocess
import threading
import sys
import tempfile
import time
from pathlib import Path

from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from ..core.sanitize import safe
from ..version import USER_AGENT, VERSION
from ..i18n import T

log = logging.getLogger(__name__)

REPO = "Kjerne/F1DockTimers"
API_LATEST = f"https://api.github.com/repos/{REPO}/releases/latest"
RELEASES_PAGE = f"https://github.com/{REPO}/releases/latest"
CHECK_EVERY_S = 24 * 3600
FIRST_CHECK_MS = 30_000
MAX_DOWNLOAD = 300 * 1024 * 1024
# Release files may only come from GitHub's own hosts.
DOWNLOAD_HOSTS = ("github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com")


# ── pure helpers (tested) ────────────────────────────────────────────────────

def parse_version(text) -> tuple[int, ...] | None:
    """"v1.0.3" / "1.0.3" -> (1, 0, 3); None for anything else (e.g. pre-release tags)."""
    m = re.fullmatch(r"v?(\d{1,4})\.(\d{1,4})\.(\d{1,4})", str(text or "").strip())
    return tuple(int(g) for g in m.groups()) if m else None


def is_newer(remote, local: str = VERSION) -> bool:
    r, l = parse_version(remote), parse_version(local)
    return bool(r and l and r > l)


def install_kind(frozen: bool, exe: Path, meipass: str | None, localappdata: str | None) -> str:
    """installed | portable | other."""
    if not frozen:
        return "other"
    exe = Path(exe)
    if (exe.parent / "unins000.exe").exists():
        return "installed"
    if localappdata and Path(localappdata, "Programs", "F1DockTimers") in exe.parents:
        return "installed"
    if meipass and Path(meipass).resolve() != exe.parent.resolve():
        return "portable"           # one-file build unpacks to %TEMP%
    return "other"


def pick_asset(release: dict, kind: str) -> dict | None:
    """The setup or portable asset of a release, with name, url, size and sha256."""
    want = "-setup.exe" if kind == "installed" else "-portable.exe"
    ver = parse_version(release.get("tag_name"))
    if ver is None:
        return None
    # The file must carry the release's own version, otherwise the "new" app would
    # report an older version and the update would be offered forever.
    expected = "F1DockTimers-" + ".".join(str(n) for n in ver) + want
    for a in release.get("assets") or []:
        if not isinstance(a, dict):
            continue
        name = str(a.get("name") or "")
        if name == expected:
            digest = str(a.get("digest") or "")
            sha = digest[7:].lower() if digest.startswith("sha256:") else ""
            url = str(a.get("browser_download_url") or "")
            if not re.fullmatch(r"[0-9a-f]{64}", sha) or not url.startswith("https://github.com/"):
                return None
            return {"name": name, "url": url, "size": int(a.get("size") or 0), "sha256": sha}
    return None


def parse_release(data) -> dict | None:
    if not isinstance(data, dict) or data.get("draft") or data.get("prerelease"):
        return None
    tag = str(data.get("tag_name") or "")
    if parse_version(tag) is None:
        return None
    return {
        "version": ".".join(str(n) for n in parse_version(tag)),
        "tag": safe(tag, 20),
        "name": safe(data.get("name") or tag, 80),
        "notes": safe(str(data.get("body") or "").replace("\r", ""), 1500).replace("\n", " \n"),
        "url": str(data.get("html_url") or RELEASES_PAGE) if str(data.get("html_url") or "").startswith(
            f"https://github.com/{REPO}/") else RELEASES_PAGE,
        "raw": data,
    }


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def authenticode(path: Path) -> tuple[str, str]:
    """(status, signer subject) via Windows' own Get-AuthenticodeSignature."""
    if sys.platform != "win32":
        return "NotSupported", ""
    cmd = ("$s = Get-AuthenticodeSignature -LiteralPath $env:F1DT_FILE; "
           "[pscustomobject]@{s=[string]$s.Status; n=[string]$s.SignerCertificate.Subject} | ConvertTo-Json -Compress")
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", cmd],
                           env=dict(os.environ, F1DT_FILE=str(path)), capture_output=True, text=True, timeout=30,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        d = json.loads(r.stdout or "{}")
        return str(d.get("s") or "Unknown"), str(d.get("n") or "")
    except (OSError, ValueError, subprocess.SubprocessError):
        return "Unknown", ""


def signature_ok(current: tuple[str, str], new: tuple[str, str]) -> tuple[bool, str]:
    """Signed app -> update must be validly signed by the same publisher.
    Unsigned app -> accept unsigned (checksum still required) or validly signed."""
    cur_status, cur_subject = current
    new_status, new_subject = new
    if cur_status == "Valid":
        if new_status != "Valid":
            return False, f"the update is not validly signed ({new_status})"
        if new_subject != cur_subject:
            return False, "the update is signed by a different publisher"
        return True, ""
    if new_status not in ("Valid", "NotSigned"):
        return False, f"the update has a broken signature ({new_status})"
    return True, ""


# ── service ──────────────────────────────────────────────────────────────────

class Updater(QObject):
    available = Signal(dict)          # release info, once per check that finds something newer
    checked = Signal(bool, str)       # (newer found?, message) for "Check now"
    progress = Signal(int, int)       # bytes, total
    failed = Signal(str)
    ready_to_quit = Signal()          # the new version is starting; this one should exit
    _verified = Signal(str)           # worker thread -> UI thread: "" = ok, else the reason

    def __init__(self, settings, parent=None) -> None:
        super().__init__(parent)
        self.settings = settings
        self.nam = QNetworkAccessManager(self)
        self._verified.connect(self._after_verify)
        self.latest: dict | None = None
        self._reply: QNetworkReply | None = None
        self._timer = QTimer(self)
        self._timer.setInterval(3600 * 1000)   # wake hourly, check if a day has passed
        self._timer.timeout.connect(self._maybe_check)
        from .packaging import is_packaged
        self.kind = "store" if is_packaged() else install_kind(
            getattr(sys, "frozen", False), Path(sys.executable), getattr(sys, "_MEIPASS", None),
            os.environ.get("LOCALAPPDATA"))

    def start(self) -> None:
        if self.kind == "store":
            return                  # the Microsoft Store updates this copy itself
        self._timer.start()
        QTimer.singleShot(FIRST_CHECK_MS, self._maybe_check)

    def _maybe_check(self) -> None:
        if self.settings.check_updates and time.time() - self.settings.update_checked_at >= CHECK_EVERY_S - 60:
            self.check(manual=False)

    def check(self, manual: bool = True) -> None:
        if self.kind == "store":
            if manual:
                self.checked.emit(False, T("This copy is updated by the Microsoft Store."))
            return
        req = QNetworkRequest(QUrl(API_LATEST))
        req.setRawHeader(b"User-Agent", USER_AGENT.encode())
        req.setRawHeader(b"Accept", b"application/vnd.github+json")
        req.setRawHeader(b"X-GitHub-Api-Version", b"2022-11-28")
        req.setTransferTimeout(15000)
        reply = self.nam.get(req)
        reply.finished.connect(lambda: self._checked(reply, manual))

    def _checked(self, reply: QNetworkReply, manual: bool) -> None:
        status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute) or 0
        body = bytes(reply.readAll())
        reply.deleteLater()
        if status != 200:
            msg = T('No release published yet.') if status == 404 else T("Couldn't reach GitHub ({status}).", status=status or 'offline')
            log.info("update check failed: %s", status)
            if manual:
                self.checked.emit(False, msg)
            return
        self.settings.update_checked_at = time.time()
        self.settings.save()
        try:
            rel = parse_release(json.loads(body.decode("utf-8")))
        except ValueError:
            rel = None
        if rel is None or not is_newer(rel["version"]):
            self.latest = None
            if manual:
                self.checked.emit(False, T("You're up to date (version {version}).", version=VERSION))
            return
        self.latest = rel
        log.info("update available: %s", rel["version"])
        if manual:
            self.checked.emit(True, T('Version {version} is available (you have {current}).', version=rel['version'], current=VERSION))
        self.available.emit(rel)

    # ── install ─────────────────────────────────────────────────────────────

    def can_install(self) -> bool:
        return self.kind in ("installed", "portable") and self.latest is not None \
            and pick_asset(self.latest["raw"], self.kind) is not None

    def install(self) -> None:
        rel = self.latest
        asset = pick_asset(rel["raw"], self.kind) if rel else None
        if asset is None:
            self.failed.emit(T('This release has no verifiable download for your copy. Opening the release page.'))
            return
        if asset["size"] > MAX_DOWNLOAD:
            self.failed.emit(T('The download is unexpectedly large; not installing.'))
            return
        folder = Path(tempfile.gettempdir()) / "F1DockTimers-update"
        folder.mkdir(exist_ok=True)
        self._target = folder / asset["name"]
        self._asset = asset
        self._fh = open(self._target, "wb")
        req = QNetworkRequest(QUrl(asset["url"]))
        req.setRawHeader(b"User-Agent", USER_AGENT.encode())
        req.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute,
                         QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
        req.setTransferTimeout(60000)
        self._reply = self.nam.get(req)
        self._reply.readyRead.connect(self._write_chunk)
        self._reply.downloadProgress.connect(lambda a, b: self.progress.emit(int(a), int(b or asset["size"])))
        self._reply.finished.connect(self._downloaded)

    def cancel(self) -> None:
        if self._reply is not None:
            self._reply.abort()

    def _write_chunk(self) -> None:
        data = bytes(self._reply.readAll())
        if self._fh.tell() + len(data) > MAX_DOWNLOAD:
            self._reply.abort()
            return
        self._fh.write(data)

    def _downloaded(self) -> None:
        reply, self._reply = self._reply, None
        self._write_chunk_rest(reply)
        self._fh.close()
        ok = reply.error() == QNetworkReply.NetworkError.NoError
        host = reply.url().host()
        reply.deleteLater()
        if not ok:
            self._discard(T('The download failed or was cancelled.'))
            return
        if not any(host == h or host.endswith("." + h) for h in DOWNLOAD_HOSTS):
            self._discard(T('The download came from an unexpected server; not installing.'))
            return
        # Hashing ~45 MB and asking PowerShell about signatures takes a few seconds: do it on a
        # worker thread so the UI (and, in embedded mode, Explorer's taskbar) never waits for it.
        threading.Thread(target=self._verify, args=(self._target, self._asset["sha256"]), daemon=True).start()

    def _verify(self, target: Path, sha: str) -> None:
        try:
            if sha256_file(target) != sha:
                self._verified.emit(T("The download doesn't match GitHub's published checksum; not installing."))
                return
            ok, why = signature_ok(authenticode(Path(sys.executable)), authenticode(target))
            self._verified.emit("" if ok else T('Not installing: {why}.', why=why))
        except Exception as e:      # noqa: BLE001 - reported to the user, never silently installed
            self._verified.emit(T("Couldn't verify the download ({name}).", name=e.__class__.__name__))

    def _after_verify(self, problem: str) -> None:
        if problem:
            self._discard(problem)
        else:
            self._launch()

    def _write_chunk_rest(self, reply) -> None:
        rest = bytes(reply.readAll())
        if rest:
            self._fh.write(rest)

    def _discard(self, msg: str) -> None:
        try:
            self._target.unlink()
        except OSError:
            pass
        log.warning("update not installed: %s", msg)
        self.failed.emit(msg)

    def _launch(self) -> None:
        flags = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        pid = str(os.getpid())
        try:
            if self.kind == "installed":
                # Silent upgrade in place; installer.iss restarts the app after a silent install.
                subprocess.Popen([str(self._target), "/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                                  "/CLOSEAPPLICATIONS"], creationflags=flags, close_fds=True)
            else:
                old = Path(sys.executable)
                new = old.with_name(self._asset["name"])
                if new.resolve() != old.resolve():
                    import shutil
                    shutil.copy2(self._target, new)
                subprocess.Popen([str(new), "--show", "--wait-pid", pid, "--replaces", str(old)],
                                 creationflags=flags, close_fds=True)
        except OSError as e:
            self.failed.emit(T("Couldn't start the update ({name}).", name=e.__class__.__name__))
            return
        log.info("update %s launched; exiting", self._asset["name"])
        self.ready_to_quit.emit()


def wait_for_pid(pid: int, timeout_s: float = 20.0) -> None:
    """Block until the old version (that started us) has exited."""
    if sys.platform != "win32" or pid <= 0:
        return
    import ctypes
    SYNCHRONIZE = 0x00100000
    k32 = ctypes.windll.kernel32
    h = k32.OpenProcess(SYNCHRONIZE, False, pid)
    if h:
        k32.WaitForSingleObject(h, int(timeout_s * 1000))
        k32.CloseHandle(h)


def remove_replaced(path: str) -> None:
    """Delete the old portable exe after an update (only an F1DockTimers exe next to us)."""
    try:
        old = Path(path).resolve()
        me = Path(sys.executable).resolve()
        if old == me or old.parent != me.parent or not re.fullmatch(r"F1DockTimers[\w.\-]*\.exe", old.name):
            return
        for _ in range(20):
            try:
                old.unlink()
                return
            except PermissionError:
                time.sleep(0.5)
            except FileNotFoundError:
                return
    except OSError:
        pass
