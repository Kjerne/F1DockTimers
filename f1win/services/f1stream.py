"""Experimental F1 live-timing stream (SignalR Core over server-sent events).

Replaces f1-live.sh + curl + pkill from the original with a native Qt client.
The cookie the load balancer hands out is kept by QNetworkAccessManager's
own cookie jar, which is why the original needed curl.

UNOFFICIAL AND UNDOCUMENTED. Off by default; the user must opt in under
Settings > Formula 1 > Live data source. It may stop working or be
restricted by F1 at any time, and F1's terms may not allow automated use.
"""
from __future__ import annotations

import json
import logging
import time

from PySide6.QtCore import QByteArray, QObject, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from ..core.f1feed import RS, FeedState
from ..version import USER_AGENT

log = logging.getLogger(__name__)

BASE = "https://livetiming.formula1.com/signalrcore"
FEEDS = ["SessionInfo", "SessionData", "TimingData", "DriverList", "TrackStatus", "LapCount", "ExtrapolatedClock"]
MAX_BUFFER = 4 * 1024 * 1024


class F1Stream(QObject):
    updated = Signal()
    failed = Signal(str)

    def __init__(self, nam: QNetworkAccessManager, parent=None) -> None:
        super().__init__(parent)
        self.nam = nam
        self.state = FeedState()
        self.reply: QNetworkReply | None = None
        self.running = False
        self.retry_at = 0.0
        self.buffer = b""
        self.token = ""
        self.gen = 0
        self._emit = QTimer(self)
        self._emit.setSingleShot(True)
        self._emit.setInterval(500)  # coalesce bursts of updates
        self._emit.timeout.connect(self.updated)

    def _request(self, url: str) -> QNetworkRequest:
        req = QNetworkRequest(QUrl(url))
        req.setRawHeader(b"User-Agent", USER_AGENT.encode())
        return req

    def ensure_running(self) -> None:
        if self.running or time.monotonic() < self.retry_at:
            return
        self.running = True
        self.gen += 1
        gen = self.gen
        req = self._request(BASE + "/negotiate?negotiateVersion=1")
        req.setTransferTimeout(15000)
        reply = self.nam.post(req, QByteArray())
        reply.finished.connect(lambda: self._negotiated(reply, gen))

    def _negotiated(self, reply: QNetworkReply, gen: int) -> None:
        data = bytes(reply.readAll())
        ok = reply.error() == QNetworkReply.NetworkError.NoError
        reply.deleteLater()
        if gen != self.gen:
            return
        try:
            token = json.loads(data.decode("utf-8")).get("connectionToken", "") if ok else ""
        except (ValueError, AttributeError):
            token = ""
        if not token or not all(c.isalnum() or c in "-_" for c in token):
            self._closed(60)
            return
        self.token = token
        req = self._request(f"{BASE}?id={token}")
        req.setRawHeader(b"Accept", b"text/event-stream")
        req.setTransferTimeout(90000)
        self.buffer = b""
        self.reply = self.nam.get(req)
        self.reply.readyRead.connect(lambda: self._read(gen))
        self.reply.finished.connect(lambda: self._stream_finished(gen))
        QTimer.singleShot(1000, lambda: self._send(gen, '{"protocol":"json","version":1}'))
        sub = json.dumps({"arguments": [FEEDS], "invocationId": "1", "target": "Subscribe", "type": 1})
        QTimer.singleShot(2000, lambda: self._send(gen, sub))

    def _send(self, gen: int, text: str) -> None:
        if gen != self.gen or not self.token:
            return
        req = self._request(f"{BASE}?id={self.token}")
        req.setHeader(QNetworkRequest.KnownHeaders.ContentTypeHeader, "text/plain;charset=UTF-8")
        req.setTransferTimeout(15000)
        r = self.nam.post(req, QByteArray((text + RS).encode()))
        r.finished.connect(r.deleteLater)

    def _read(self, gen: int) -> None:
        if gen != self.gen or self.reply is None:
            return
        self.buffer += bytes(self.reply.readAll())
        if len(self.buffer) > MAX_BUFFER:
            self.stop()
            self._closed(60)
            return
        changed = False
        while b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            if self.state.handle_line(line.decode("utf-8", errors="replace").rstrip("\r")):
                changed = True
        if changed and not self._emit.isActive():
            self._emit.start()

    def _stream_finished(self, gen: int) -> None:
        if gen != self.gen:
            return
        if self.reply is not None:
            self.reply.deleteLater()
            self.reply = None
        self._closed(15)

    def _closed(self, retry_s: int) -> None:
        self.running = False
        self.retry_at = time.monotonic() + retry_s
        if retry_s >= 60:
            self.failed.emit("net")

    def stop(self) -> None:
        if not self.running and self.reply is None:
            return
        self.gen += 1
        self.running = False
        self.retry_at = 0
        if self.reply is not None:
            r, self.reply = self.reply, None
            r.abort()
            r.deleteLater()
        self.state.reset()
