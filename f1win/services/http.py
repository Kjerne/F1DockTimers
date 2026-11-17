"""Async JSON over QNetworkAccessManager: timeouts, size cap, per-host pacing.

Callbacks run on the GUI thread but the transfer itself never blocks it.
"""
from __future__ import annotations

import json
import logging
import time
from collections import deque
from urllib.parse import urlsplit

from PySide6.QtCore import QByteArray, QObject, QTimer, QUrl
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from ..core.sanitize import MAX_BODY_BYTES
from ..version import USER_AGENT

log = logging.getLogger(__name__)

# (minimum gap between request starts in seconds, max requests per rolling minute)
# Jolpica: 4/s burst, 500/h. OpenF1: Free 3/s and 30/min, Sponsor 6/s and 60/min;
# set_openf1_plan() switches. A small margin keeps clock jitter from tripping the limit.
LIMITS = {
    "api.jolpi.ca": (0.35, 100),
    "api.openf1.org": (0.36, 28),
}
OPENF1_PLANS = {"free": (0.36, 28), "sponsor": (0.18, 57)}
DEFAULT_LIMIT = (0.2, 120)


class RateLimiter:
    """Per-host pacing: a minimum gap, a rolling one-minute cap and Retry-After blocks. Pure."""

    def __init__(self, limits: dict | None = None) -> None:
        self.limits = dict(limits or LIMITS)
        self.starts: dict[str, deque] = {}
        self.blocked_until: dict[str, float] = {}

    def wait(self, host: str, now: float) -> float:
        gap, per_min = self.limits.get(host, DEFAULT_LIMIT)
        q = self.starts.setdefault(host, deque())
        while q and now - q[0] >= 60.0:
            q.popleft()
        w = max(0.0, self.blocked_until.get(host, 0.0) - now)
        if q:
            w = max(w, q[-1] + gap - now)
        if len(q) >= per_min:
            w = max(w, q[0] + 60.0 - now + 0.05)
        return w

    def note(self, host: str, now: float) -> None:
        self.starts.setdefault(host, deque()).append(now)

    def block(self, host: str, until: float) -> None:
        self.blocked_until[host] = max(self.blocked_until.get(host, 0.0), until)


class HttpClient(QObject):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.nam = QNetworkAccessManager(self)
        self.nam.setAutoDeleteReplies(False)
        self._queue: deque = deque()
        self.limiter = RateLimiter()
        self._pump = QTimer(self)
        self._pump.setSingleShot(True)
        self._pump.timeout.connect(self._drain)
        self.offline = False
        self.inflight = 0

    # ── public ──────────────────────────────────────────────────────────────

    def get_json(self, url: str, on_ok, on_fail, headers: dict | None = None, timeout_ms: int = 15000) -> None:
        self._queue.append(("GET", url, None, headers or {}, timeout_ms, on_ok, on_fail))
        self._drain()

    def post_form(self, url: str, body: bytes, on_ok, on_fail, timeout_ms: int = 15000) -> None:
        hdr = {"Content-Type": "application/x-www-form-urlencoded"}
        self._queue.append(("POST", url, body, hdr, timeout_ms, on_ok, on_fail))
        self._drain()

    def set_openf1_plan(self, plan: str) -> None:
        self.limiter.limits["api.openf1.org"] = OPENF1_PLANS.get(plan, OPENF1_PLANS["free"])

    def pending(self, host: str) -> int:
        return sum(1 for item in self._queue if urlsplit(item[1]).hostname == host)

    # ── internals ───────────────────────────────────────────────────────────

    def _drain(self) -> None:
        """Start every queued request whose host is allowed now; a throttled host
        never holds up requests to another one."""
        now = time.monotonic()
        soonest = None
        kept: deque = deque()
        while self._queue:
            item = self._queue.popleft()
            host = urlsplit(item[1]).hostname or ""
            wait = self.limiter.wait(host, now)
            if wait > 0 or any(urlsplit(k[1]).hostname == host for k in kept):
                kept.append(item)  # keep order per host
                if wait > 0:
                    soonest = wait if soonest is None else min(soonest, wait)
                continue
            self.limiter.note(host, now)
            method, url, body, headers, timeout_ms, on_ok, on_fail = item
            self._start(method, url, body, headers, timeout_ms, on_ok, on_fail)
        self._queue = kept
        if self._queue:
            delay = soonest if soonest is not None else 0.05
            if not self._pump.isActive() or self._pump.remainingTime() > delay * 1000:
                self._pump.start(int(delay * 1000) + 5)

    def _start(self, method, url, body, headers, timeout_ms, on_ok, on_fail) -> None:
        req = QNetworkRequest(QUrl(url))
        req.setRawHeader(b"User-Agent", USER_AGENT.encode())
        req.setRawHeader(b"Accept", b"application/json")
        for k, v in headers.items():
            req.setRawHeader(k.encode(), str(v).encode())
        req.setTransferTimeout(timeout_ms)
        req.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute,
                         QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
        if method == "POST":
            reply = self.nam.post(req, QByteArray(body or b""))
        else:
            reply = self.nam.get(req)
        self.inflight += 1

        def progress(received: int, _total: int) -> None:
            if received > MAX_BODY_BYTES:
                reply.abort()

        reply.downloadProgress.connect(progress)
        reply.finished.connect(lambda: self._done(reply, url, on_ok, on_fail))

    def _done(self, reply: QNetworkReply, url: str, on_ok, on_fail) -> None:
        self.inflight -= 1
        status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute) or 0
        err = reply.error()
        data = bytes(reply.readAll())
        reply.deleteLater()
        safe_url = url.split("?")[0]
        if err != QNetworkReply.NetworkError.NoError and not status:
            reason = "timeout" if err == QNetworkReply.NetworkError.OperationCanceledError else "net"
            self.offline = err in (QNetworkReply.NetworkError.HostNotFoundError,
                                   QNetworkReply.NetworkError.UnknownNetworkError,
                                   QNetworkReply.NetworkError.TemporaryNetworkFailureError,
                                   QNetworkReply.NetworkError.NetworkSessionFailedError)
            log.info("request failed (%s): %s", reason, safe_url)
            on_fail(reason, 0)
            return
        self.offline = False
        if status == 429:
            try:
                retry = float(bytes(reply.rawHeader(b"Retry-After")).decode() or 0)
            except ValueError:
                retry = 0.0
            host = QUrl(url).host()
            self.limiter.block(host, time.monotonic() + max(1.0, min(retry or 10.0, 600.0)))
        if status != 200:
            log.info("request failed (http %s): %s", status, safe_url)
            on_fail(f"http {status}", int(status))
            return
        if not data or len(data) > MAX_BODY_BYTES:
            on_fail("size", int(status))
            return
        if data.startswith(b"\xef\xbb\xbf"):
            data = data[3:]
        try:
            decoded = json.loads(data.decode("utf-8", errors="replace"))
        except ValueError:
            on_fail("parse", int(status))
            return
        if not isinstance(decoded, (dict, list)):
            on_fail("parse", int(status))
            return
        try:
            on_ok(decoded)
        except Exception:  # a parser bug must not kill the poller
            log.exception("handler failed for %s", safe_url)
            on_fail("parse", int(status))
