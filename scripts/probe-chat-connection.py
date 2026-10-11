"""Read-only CYD connection experiment. No device writes or chat actions.

Connects to the installed desktop's undocumented IPC router. The router may
change in future app versions. Only metadata is printed; message bodies are
never saved. This is NOT the Micro slot-assignment feed.
"""
import argparse
import ctypes
import json
import msvcrt
from pathlib import Path
import sqlite3
import struct
import time
import uuid


class DesktopListener:
    MAX_FRAME = 64 * 1024 * 1024

    def __init__(self):
        self.pipe = open(r"\\.\pipe\codex-ipc", "r+b", buffering=0)
        self.buffer = bytearray()
        self.client_id = None
        self.following = []
        self.send({"type": "request", "method": "initialize", "version": 0,
                   "requestId": str(uuid.uuid4()),
                   "params": {"clientType": "cyd-readonly-probe"}})

    def send(self, message):
        data = json.dumps(message).encode("utf-8")
        self.pipe.write(struct.pack("<I", len(data)) + data)

    def follow(self, thread_id, following=True):
        if not self.client_id:
            raise RuntimeError("Desktop connection not initialized")
        self.send({"type": "broadcast", "method": "thread-stream-following-changed",
                   "version": 1, "sourceClientId": self.client_id,
                   "params": {"conversationId": thread_id, "hostId": "local",
                              "following": following}})
        if following:
            self.following.append(thread_id)

    def poll(self):
        available = ctypes.c_ulong()
        handle = ctypes.c_void_p(msvcrt.get_osfhandle(self.pipe.fileno()))
        if not ctypes.windll.kernel32.PeekNamedPipe(
                handle, None, 0, None, ctypes.byref(available), None):
            raise ConnectionError("Desktop pipe disconnected")
        if available.value:
            self.buffer.extend(self.pipe.read(min(available.value, 1024 * 1024)))
        messages = []
        while len(self.buffer) >= 4:
            size = struct.unpack_from("<I", self.buffer)[0]
            if not 0 < size <= self.MAX_FRAME:
                raise ValueError("Unexpected desktop frame length")
            if len(self.buffer) < size + 4:
                break
            message = json.loads(self.buffer[4:size + 4])
            del self.buffer[:size + 4]
            if message.get("type") == "client-discovery-request":
                self.send({"type": "client-discovery-response",
                           "requestId": message["requestId"],
                           "response": {"canHandle": False}})
            elif message.get("method") == "initialize":
                if message.get("resultType") != "success":
                    raise ConnectionError("Desktop rejected initialization")
                self.client_id = message["result"]["clientId"]
            else:
                messages.append(message)
        return messages

    def close(self, notify=True):
        try:
            for thread_id in self.following if notify else ():
                self.follow(thread_id, False)
        except (OSError, ConnectionError):
            pass
        finally:
            self.pipe.close()


def recent_local_chats(database, limit=6):
    """Local candidates only; deliberately excludes internal review/subagent chats."""
    connection = sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True, timeout=0.25)
    connection.row_factory = sqlite3.Row
    try:
        rows = connection.execute("""
            SELECT t.id, COALESCE(NULLIF(t.name, ''), t.title) AS title,
                   t.cwd, p.name AS project, t.project_id, t.updated_at
            FROM threads t LEFT JOIN projects p ON p.id = t.project_id
            WHERE t.archived = 0 AND t.source IN ('vscode', 'cli', 'appServer')
              AND (t.thread_source IS NULL OR t.thread_source IN
                   ('user', 'agent_created_thread', 'chatgpt_handoff', 'voice_chat'))
            ORDER BY t.updated_at DESC, t.id DESC LIMIT ?
        """, (limit,)).fetchall()
        return [dict(row) for row in rows]
    finally:
        connection.close()


def metadata(state):
    history = state.get("turnHistory") or {}
    canonical = history.get("history") or {}
    turns = state.get("turns") or canonical.get("turns") or []
    latest = turns[-1] if isinstance(turns, list) and turns else {}
    return {
        "title": state.get("title"), "cwd": state.get("cwd"),
        "runtimeStatus": state.get("threadRuntimeStatus"),
        "unread": state.get("hasUnreadTurn"),
        "requestMethods": [r.get("method") for r in state.get("requests", [])],
        "latestTurnStatus": latest.get("status"),
        "historyFields": list(canonical),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path,
                        default=Path.home() / ".codex" / "state_5.sqlite")
    parser.add_argument("--seconds", type=int, default=12)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error("--seconds must be 1..60")
    chats = recent_local_chats(args.database)
    print(json.dumps({"source": "local candidates, not Micro assignments", "chats": chats}))
    listener = DesktopListener()
    subscribed = False
    received = set()
    patches = 0
    try:
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            for message in listener.poll():
                if message.get("method") != "thread-stream-state-changed":
                    continue
                params = message.get("params", {})
                if params.get("hostId") != "local":
                    continue
                change = params.get("change", {})
                if change.get("type") == "snapshot":
                    received.add(params["conversationId"])
                    print(json.dumps({"threadId": params["conversationId"],
                                      "snapshot": metadata(change["conversationState"])}))
                elif change.get("type") == "patches":
                    patches += 1
            if listener.client_id and not subscribed:
                for chat in chats:
                    listener.follow(chat["id"])
                subscribed = True
            time.sleep(.02)
    finally:
        listener.close()
    print(json.dumps({"connected": bool(listener.client_id),
                      "snapshots": len(received), "patchEvents": patches,
                      "withoutLiveSnapshot": [c["id"] for c in chats if c["id"] not in received]}))


if __name__ == "__main__":
    main()
