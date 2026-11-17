"""OpenF1 credentials in Windows Credential Manager (via ``keyring``).

The password is never written to settings, the cache or the log. Environment
variables OPENF1_USERNAME / OPENF1_PASSWORD are honoured too, as in the
original plugin.
"""
from __future__ import annotations

import logging
import os

log = logging.getLogger(__name__)
SERVICE = "F1DockTimers/OpenF1"
LEGACY_SERVICE = "NoctaliaF1/OpenF1"

try:
    import keyring
    from keyring.errors import KeyringError
except Exception:  # pragma: no cover - keyring missing
    keyring = None  # type: ignore

    class KeyringError(Exception):  # type: ignore
        pass


def available() -> bool:
    return keyring is not None


def get_password(username: str) -> str | None:
    if not username or keyring is None:
        return None
    try:
        pw = keyring.get_password(SERVICE, username)
        if pw is None:
            old = keyring.get_password(LEGACY_SERVICE, username)
            if old is not None:           # move it to the new name once
                keyring.set_password(SERVICE, username, old)
                try:
                    keyring.delete_password(LEGACY_SERVICE, username)
                except Exception:
                    pass
                pw = old
        return pw
    except (KeyringError, OSError, RuntimeError):
        log.warning("credential store unavailable")
        return None


def set_password(username: str, password: str) -> bool:
    if not username or keyring is None:
        return False
    try:
        keyring.set_password(SERVICE, username, password)
        return True
    except (KeyringError, OSError, RuntimeError):
        log.warning("could not store credential")
        return False


def delete_password(username: str) -> None:
    if not username or keyring is None:
        return
    for service in (SERVICE, LEGACY_SERVICE):
        try:
            keyring.delete_password(service, username)
        except Exception:
            pass


def resolve(settings_username: str) -> tuple[str, str] | None:
    """(username, password) from the environment first, then Credential Manager."""
    user = os.environ.get("OPENF1_USERNAME", "")
    pw = os.environ.get("OPENF1_PASSWORD", "")
    if user and pw:
        return user, pw
    if settings_username:
        stored = get_password(settings_username)
        if stored:
            return settings_username, stored
    return None
