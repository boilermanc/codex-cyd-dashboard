"""Bounded local transport diagnostics; never record chat or device identities."""
import json
import math
import os
from pathlib import Path
import re
import threading
import time


class Diagnostics:
    MAX_BYTES = 1024 * 1024
    BACKUPS = 2  # Current file plus two backups: three files total.
    EVENTS = frozenset({
        "start", "stop", "unexpected_exit", "usb_open", "usb_error",
        "usb_ack_timeout", "ipc_connected", "ipc_error", "database_error",
        "report_error", "open_chat_error", "loop_gap", "heartbeat", "transition",
    })
    TRANSITIONS = frozenset({
        "desktop_connected", "device_verified", "acknowledged", "serial_open",
    })
    NUMERIC_FIELDS = frozenset({
        "pid", "seconds", "duration_ms", "loop_gap_ms", "ack_age_ms", "send_age_ms",
        "ipc_age_ms", "attempt", "messages", "watched_chats", "chat_count",
        "page", "pages", "errno", "winerror", "exit_code",
    })
    BOOLEAN_FIELDS = frozenset({"dry_run", "requested", "connected"})

    def __init__(self, root):
        self.path = Path(root) / ".local" / "usb-helper-events.jsonl"
        self._states = {}
        self._lock = threading.RLock()

    def event(self, event, **fields):
        """Return success; failure to log must never interrupt the helper.

        Only known event names and numeric/boolean fields survive. Pass an
        exception as error=exc to retain its class and numeric OS codes only.
        Paths, payloads, identities, messages and tracebacks are discarded.
        """
        try:
            if not isinstance(event, str) or event not in self.EVENTS:
                return False
            entry = {"timestamp": time.time(), "event": event, "pid": os.getpid()}
            for key in self.NUMERIC_FIELDS:
                value = fields.get(key)
                if type(value) in (int, float) and math.isfinite(value):
                    entry[key] = value
            for key in self.BOOLEAN_FIELDS:
                if type(fields.get(key)) is bool:
                    entry[key] = fields[key]
            error = fields.get("error")
            if isinstance(error, BaseException):
                name = type(error).__name__
                entry["error_class"] = name if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]{0,79}", name) else "Exception"
                for key in ("errno", "winerror"):
                    value = getattr(error, key, None)
                    if type(value) is int:
                        entry[key] = value
            if event == "transition":
                name, value = fields.get("name"), fields.get("value")
                if name not in self.TRANSITIONS or type(value) is not bool:
                    return False
                entry.update(name=name, value=value)
            line = (json.dumps(entry, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
            with self._lock:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                if self.path.exists() and self.path.stat().st_size + len(line) > self.MAX_BYTES:
                    self._rotate()
                with self.path.open("ab") as stream:
                    stream.write(line)
            return True
        except Exception:
            return False

    def _rotate(self):
        for index in range(self.BACKUPS, 0, -1):
            source = self.path if index == 1 else Path(str(self.path) + f".{index - 1}")
            target = Path(str(self.path) + f".{index}")
            if source.exists():
                source.replace(target)

    def transition(self, name, value):
        """Log initial state and changes only; retry changes after failed writes."""
        try:
            if name not in self.TRANSITIONS or type(value) is not bool:
                return False
            with self._lock:
                if name in self._states and self._states[name] == value:
                    return True
                if not self.event("transition", name=name, value=value):
                    return False
                self._states[name] = value
            return True
        except Exception:
            return False
