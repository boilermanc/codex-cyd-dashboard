"""Local recent-chat dashboard over USB; never sends prompts or approvals."""
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import re
import time
import unicodedata
import uuid

spec = importlib.util.spec_from_file_location("cyd_probe", Path(__file__).with_name("probe-chat-connection.py"))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

ALIASES = {}
FIELDS = {"threadRuntimeStatus", "hasUnreadTurn", "requests"}


def project_key(chat):
    if chat.get("project_id"):
        return "project:" + chat["project_id"]
    path = re.sub(r"\\+", "/", chat.get("cwd", "")).lower().rstrip("/")
    path = path.removeprefix("/?/").removeprefix("//?/")
    return "folder:" + path if path else "chat:" + chat["id"]


def watch_candidates(chats):
    # Most recent chat for every project, plus recent activity across chats.
    chosen = {c["id"]: c for c in chats[:24]}
    projects = set()
    for chat in chats:
        key = project_key(chat)
        if key not in projects:
            chosen[chat["id"]] = chat
            projects.add(key)
    return list(chosen.values())


def short_name(chat):
    # Folder is a fallback, never inferred from a chat's prose.
    folder = re.split(r"[\\/]", chat.get("cwd", "").rstrip("\\/"))[-1]
    text = chat.get("project") or ALIASES.get(folder.lower()) or folder or chat["title"]
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    text = " ".join(text.split())
    return text[:48] or "Chat"


def short_chat_title(chat):
    text = unicodedata.normalize("NFKD", chat.get("title") or "Untitled chat")
    text = " ".join(text.encode("ascii", "ignore").decode().split())
    return text[:48] or "Untitled chat"


def status_for(state):
    runtime = state.get("threadRuntimeStatus") or {}
    kind = runtime.get("type")
    if kind == "systemError":
        return 5
    if state.get("pendingAsyncInput"):
        return 4
    flags = runtime.get("activeFlags", [])
    methods = [str(r.get("method", "")) for r in state.get("requests", [])]
    if any(f in flags for f in ("waitingOnApproval", "waitingOnUserInput")) or any(
            any(word in method.lower() for word in ("approval", "requestuserinput", "elicitation"))
            for method in methods):
        return 4
    if kind == "active":
        return 2
    if kind == "idle":
        return 3 if state.get("hasUnreadTurn") else 1
    return 0


def pending_async_input(full):
    """Match desktop question identities, retaining neither prompts nor answers."""
    entities = ((full.get("turnHistory") or {}).get("history") or {}).get("entitiesByKey", {})
    turns = list(entities.values()) if entities else full.get("turns", [])
    prefix, suffix = "<send_user_message_question_reply>", "</send_user_message_question_reply>"
    for turn in turns:
        if turn.get("status") != "inProgress" or not turn.get("turnId"):
            continue
        questions, answered = set(), set()
        for item in turn.get("items", []):
            if item.get("type") == "agentMessage":
                for index, _ in enumerate(item.get("questions") or []):
                    questions.add(json.dumps(["request_user_input_async", item.get("id"), index], separators=(",", ":")))
            if item.get("type") == "userMessage":
                content = item.get("content", [])
            elif item.get("type") == "steeringUserMessage" and item.get("status", "accepted") == "accepted":
                content = item.get("input", [])
            else:
                continue
            if not isinstance(content, list) or len(content) != 1 or content[0].get("type") != "text":
                continue
            text = content[0].get("text", "").strip()
            if not text.startswith(prefix) or not text.endswith(suffix):
                continue
            try:
                replies = json.loads(text[len(prefix):-len(suffix)])
                if isinstance(replies, dict):
                    replies = [replies]
                for reply in replies:
                    identity = json.loads(reply["questionItemId"])
                    answered.add(json.dumps(identity, separators=(",", ":")))
            except (ValueError, KeyError, TypeError):
                continue
        if questions - answered:
            return True
    return False


def apply_metadata_patches(state, patches):
    """Apply only relevant Immer metadata patches; never retain chat history."""
    result = copy.deepcopy(state)
    for patch in patches:
        path = patch.get("path")
        if not isinstance(path, list) or not path:
            raise ValueError("Unsupported patch path")
        if path[0] not in FIELDS:
            continue
        target = result
        for key in path[:-1]:
            target = target[key]
        key = path[-1]
        op = patch.get("op")
        if op == "remove":
            del target[key]
        elif op in ("replace", "add"):
            value = copy.deepcopy(patch["value"])
            if isinstance(target, list) and op == "add":
                target.insert(key, value)
            else:
                target[key] = value
        else:
            raise ValueError("Unsupported patch operation")
    return result


class LiveStates:
    def __init__(self):
        self.states = {}

    def receive(self, message, now):
        if message.get("method") == "client-status-changed":
            params = message.get("params", {})
            if params.get("status") == "disconnected":
                self.states = {k: v for k, v in self.states.items()
                               if v[3] != params.get("clientId")}
            return
        if message.get("method") != "thread-stream-state-changed":
            return
        p = message.get("params", {})
        if p.get("hostId") != "local":
            return
        thread_id = p.get("conversationId")
        change = p.get("change", {})
        if change.get("type") == "snapshot":
            full = change.get("conversationState", {})
            metadata = {k: copy.deepcopy(full[k]) for k in FIELDS if k in full}
            metadata["pendingAsyncInput"] = pending_async_input(full)
            self.states[thread_id] = (metadata,
                                     change.get("revision"), now, message.get("sourceClientId"))
        elif change.get("type") == "patches" and thread_id in self.states:
            old, revision, seen, owner = self.states[thread_id]
            if revision != change.get("baseRevision") or owner != message.get("sourceClientId"):
                self.states.pop(thread_id, None)
                return
            try:
                updated = apply_metadata_patches(old, change.get("patches", []))
                self.states[thread_id] = (updated, change.get("revision"), now, owner)
            except (KeyError, IndexError, TypeError, ValueError):
                self.states.pop(thread_id, None)

    def cards(self, chats, now):
        result = []
        for chat in chats:
            name = short_name(chat)
            live = self.states.get(chat["id"])
            status = status_for(live[0]) if live and now - live[2] < 12 else 0
            result.append({"id": chat["id"], "title": name, "subtitle": short_chat_title(chat), "status": status})
        return result + [{"id": "", "title": "No chat", "subtitle": "", "status": 0}] * (6 - len(result))

    def chat_page(self, chats, now, page):
        priority = {4: 0, 2: 1, 5: 2, 3: 3, 1: 4, 0: 5}
        def rank(chat):
            state = self.states.get(chat["id"])
            status = status_for(state[0]) if state and now - state[2] < 12 else 0
            return priority[status], -chat.get("updated_at", 0), chat["id"]
        ordered = sorted(chats, key=rank)
        pages = max(1, (len(ordered) + 5) // 6)
        page %= pages
        return self.cards(ordered[page * 6:page * 6 + 6], now), page, pages, len(ordered)


def permitted_tap(line, session, shown, now):
    parts = line.split()
    if len(parts) != 3 or parts[0] != "CYD_OPEN" or parts[1] != session:
        return None
    thread_id = parts[2]
    if not re.fullmatch(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}", thread_id):
        return None
    return thread_id if now - shown.get(thread_id, float("-inf")) < 10 else None


def valid_handshake(line, device_id=None):
    match = re.fullmatch(r"CYD_USB_READY 1 ([0-9a-f]{12})", line)
    return bool(match and (not device_id or match.group(1) == device_id))


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CYD serial port, e.g. COM11")
    parser.add_argument("--device-id", help="12 hex digits reported by CYD_HELLO; optional identity pin")
    parser.add_argument("--codex-home", type=Path, default=Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex"))))
    parser.add_argument("--seconds", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true", help="Read chats only; never open serial or chats")
    args = parser.parse_args()
    if not args.dry_run and not args.port:
        parser.error("--port is required unless --dry-run is used")
    if args.device_id and not re.fullmatch(r"[0-9a-fA-F]{12}", args.device_id):
        parser.error("--device-id must contain exactly 12 hex digits")
    if args.device_id:
        args.device_id = args.device_id.lower()
    root = Path(__file__).resolve().parents[1]
    database = args.codex_home / "state_5.sqlite"
    (root / ".local").mkdir(exist_ok=True)
    stop = root / ".local/usb-helper.stop"
    report = root / (".local/usb-helper-dry-run.json" if args.dry_run else ".local/usb-helper-status.json")
    session = uuid.uuid4().hex
    live = LiveStates()
    listener = None
    port = None
    device_verified = False
    serial_buffer = bytearray()
    shown = {}
    last_open = 0
    last_ack = 0
    last_send = last_read = last_follow = last_hello = 0
    chats = []
    subscribed = set()
    page = 0
    last_page = 0
    started = time.monotonic()
    try:
        while not stop.exists() and (not args.seconds or time.monotonic() - started < args.seconds):
            now = time.monotonic()
            if now - last_read >= 2:
                chats = probe.recent_local_chats(database, limit=-1)
                last_read = now
            if listener is None:
                try:
                    listener = probe.DesktopListener()
                    subscribed.clear()
                except OSError:
                    time.sleep(1)
            if listener:
                try:
                    for message in listener.poll():
                        live.receive(message, now)
                    if listener.client_id and now - last_follow >= 4:
                        visible, _, _, _ = live.chat_page(chats, now, page)
                        wanted = {c["id"] for c in watch_candidates(chats)}
                        wanted.update(c["id"] for c in visible if c["id"])
                        # Continue following active chats even as newer chats arrive.
                        wanted.update(c["id"] for c in chats if c["id"] in live.states
                                      and now - live.states[c["id"]][2] < 12
                                      and status_for(live.states[c["id"]][0]) in (2, 4))
                        for removed in subscribed - wanted:
                            listener.follow(removed, False)
                            live.states.pop(removed, None)
                        for thread_id in wanted:
                            listener.follow(thread_id)
                        subscribed = wanted
                        # Keep cleanup bounded during a long-running session.
                        listener.following = list(wanted)
                        last_follow = now
                except (OSError, ValueError, ConnectionError):
                    listener.close()
                    listener = None
                    live.states.clear()
            cards, page, pages, chat_count = live.chat_page(chats, now, page)
            if not args.dry_run and port is None:
                try:
                    port = serial.Serial()
                    port.port = args.port
                    port.baudrate = 115200
                    port.timeout = 0
                    port.write_timeout = 1
                    port.dtr = False
                    port.rts = False
                    port.open()
                    device_verified = False
                except (OSError, serial.SerialException):
                    if port:
                        port.close()
                    port = None
                    time.sleep(1)
            if port:
                try:
                    if now - last_hello >= 5:
                        port.write(b"CYD_HELLO\n")
                        last_hello = now
                    serial_buffer.extend(port.read(min(port.in_waiting, 4096)))
                    if len(serial_buffer) > 16384:
                        serial_buffer.clear()
                    while b"\n" in serial_buffer:
                        line, _, rest = serial_buffer.partition(b"\n")
                        serial_buffer = bytearray(rest)
                        line = line.decode("ascii", errors="replace").strip()
                        if valid_handshake(line, args.device_id):
                            device_verified = True
                        elif line == "CYD_ACK " + session:
                            last_ack = now
                        elif device_verified:
                            if line == "CYD_PAGE " + session and now - last_page > .4:
                                page = (page + 1) % pages
                                last_page = now
                                cards, page, pages, chat_count = live.chat_page(chats, now, page)
                            thread_id = permitted_tap(line, session, shown, now)
                            if thread_id and now - last_open > .4:
                                os.startfile("codex://threads/" + thread_id)
                                last_open = now
                    if device_verified and now - last_send >= 1:
                        data = json.dumps({"session": session, "cards": cards,
                                           "page": page + 1, "pages": pages}, separators=(",", ":"))
                        port.write(("CYD1 " + data + "\n").encode("ascii"))
                        shown = {k: v for k, v in shown.items() if now - v < 10}
                        shown.update({c["id"]: now for c in cards if c["id"]})
                        last_send = now
                except (OSError, serial.SerialException):
                    port.close()
                    port = None
                    device_verified = False
            if args.dry_run or now - getattr(main, "last_report", 0) >= 2:
                state = {"mode": "local chats by status", "page": page + 1, "pages": pages,
                         "chatCount": chat_count, "watchedChats": len(subscribed),
                         "desktopConnected": bool(listener and listener.client_id),
                         "deviceVerified": device_verified, "acknowledged": bool(last_ack and now - last_ack < 5),
                         "cards": cards, "updatedAt": time.time()}
                temp = report.with_suffix(".tmp")
                temp.write_text(json.dumps(state, indent=2), encoding="utf-8")
                try:
                    temp.replace(report)
                except PermissionError:
                    pass  # Retry next health update; Windows reader may hold the file.
                main.last_report = now
            time.sleep(.03)
    finally:
        if listener:
            listener.close()
        if port:
            port.close()


if __name__ == "__main__":
    main()
