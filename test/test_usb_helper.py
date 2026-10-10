import importlib.util
from pathlib import Path
import unittest
import json

spec = importlib.util.spec_from_file_location("helper", Path(__file__).resolve().parents[1] / "scripts/usb-helper.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
ID = "11111111-1111-4111-8111-111111111111"


class UsbHelperTests(unittest.TestCase):
    def test_handshake_requires_protocol_and_optional_identity(self):
        self.assertTrue(helper.valid_handshake("CYD_USB_READY 1 123456abcdef"))
        self.assertTrue(helper.valid_handshake("CYD_USB_READY 1 123456abcdef", "123456abcdef"))
        self.assertFalse(helper.valid_handshake("CYD_USB_READY 1 123456abcdef", "000000000000"))
        self.assertFalse(helper.valid_handshake("CYD_USB_READY 2 123456abcdef"))
        self.assertFalse(helper.valid_handshake("CYD_USB_READY 1 invalid"))

    def test_nonblocking_question_needs_input_until_answered(self):
        item = {"type": "agentMessage", "id": "call_123", "questions": [{"title": "Question"}]}
        turn = {"turnId": "turn_1", "status": "inProgress", "items": [item]}
        full = {"turnHistory": {"history": {"entitiesByKey": {"turn:turn_1": turn}}}}
        self.assertTrue(helper.pending_async_input(full))
        self.assertEqual(helper.status_for({"threadRuntimeStatus": {"type": "active"}, "pendingAsyncInput": True}), 4)
        reply = [{"questionItemId": json.dumps(["request_user_input_async", "call_123", 0]), "answer": "Yes"}]
        turn["items"].append({"type": "steeringUserMessage", "status": "accepted", "input": [
            {"type": "text", "text": "<send_user_message_question_reply>" + json.dumps(reply) + "</send_user_message_question_reply>"}]})
        self.assertFalse(helper.pending_async_input(full))
        turn["items"] = [item]
        turn["status"] = "completed"
        self.assertFalse(helper.pending_async_input(full))

    def test_project_names_and_fallback(self):
        self.assertEqual(helper.short_name({"cwd": r"C:\repo\example-project", "title": "video"}), "example-project")
        self.assertEqual(helper.short_name({"cwd": "", "title": "A long chat"}), "A long chat")
        self.assertEqual(helper.short_name({"cwd": "repo", "project": "Actual project", "title": "chat"}), "Actual project")

    def test_status_meanings(self):
        self.assertEqual(helper.status_for({}), 0)
        self.assertEqual(helper.status_for({"threadRuntimeStatus": {"type": "active"}}), 2)
        self.assertEqual(helper.status_for({"threadRuntimeStatus": {"type": "active", "activeFlags": ["waitingOnApproval"]}}), 4)
        self.assertEqual(helper.status_for({"threadRuntimeStatus": {"type": "systemError"}}), 5)
        self.assertEqual(helper.status_for({"threadRuntimeStatus": {"type": "idle"}, "hasUnreadTurn": True}), 3)
        self.assertEqual(helper.status_for({"threadRuntimeStatus": {"type": "idle"}, "hasUnreadTurn": False}), 1)

    def test_duplicate_projects_and_expired_status(self):
        live = helper.LiveStates()
        live.states[ID] = ({"threadRuntimeStatus": {"type": "active"}}, 1, 100, "owner")
        chats = [{"id": ID, "cwd": "SpectIQ", "title": "one"}, {"id": "other", "cwd": "SpectIQ", "title": "two"}]
        cards = live.cards(chats, 101)
        self.assertEqual([c["title"] for c in cards[:2]], ["SpectIQ", "SpectIQ"])
        self.assertEqual([c["subtitle"] for c in cards[:2]], ["one", "two"])
        self.assertEqual(cards[0]["status"], 2)
        self.assertEqual(live.cards(chats, 113)[0]["status"], 0)
        self.assertEqual(len(cards), 6)

    def test_patch_state_and_gap(self):
        live = helper.LiveStates()
        def event(change):
            return {"method": "thread-stream-state-changed", "sourceClientId": "owner", "params": {
                "hostId": "local", "conversationId": ID, "change": change}}
        live.receive(event({"type": "snapshot", "revision": 1, "conversationState": {
            "threadRuntimeStatus": {"type": "active"}, "turnHistory": {"secret": "not retained"}}}), 100)
        self.assertNotIn("turnHistory", live.states[ID][0])
        live.receive(event({"type": "patches", "baseRevision": 1, "revision": 2, "patches": [
            {"op": "replace", "path": ["threadRuntimeStatus"], "value": {"type": "idle"}}]}), 101)
        self.assertEqual(helper.status_for(live.states[ID][0]), 1)
        live.receive(event({"type": "patches", "baseRevision": 1, "revision": 3, "patches": []}), 102)
        self.assertNotIn(ID, live.states)

    def test_taps_remain_bound_to_displayed_uuid(self):
        shown = {ID: 100}
        self.assertEqual(helper.permitted_tap("CYD_OPEN session " + ID, "session", shown, 105), ID)
        self.assertIsNone(helper.permitted_tap("CYD_OPEN old " + ID, "session", shown, 105))
        self.assertIsNone(helper.permitted_tap("CYD_OPEN session " + ID, "session", shown, 111))
        self.assertIsNone(helper.permitted_tap("CYD_OPEN session ../../bad", "session", shown, 105))

    def test_disconnect_invalidates_live_data(self):
        live = helper.LiveStates()
        live.states[ID] = ({}, 1, 100, "owner")
        live.receive({"method": "client-status-changed", "params": {"clientId": "owner", "status": "disconnected"}}, 101)
        self.assertFalse(live.states)

    def test_active_project_beyond_first_six_is_promoted(self):
        live = helper.LiveStates()
        chats = [{"id": str(i), "cwd": "repo" + str(i), "title": "chat", "updated_at": 100-i} for i in range(20)]
        live.states["19"] = ({"threadRuntimeStatus": {"type": "active"}}, 1, 100, "owner")
        cards, page, pages, count = live.chat_page(chats, 101, 0)
        self.assertEqual(cards[0]["id"], "19")
        self.assertEqual((page, pages, count), (0, 4, 20))

    def test_pages_cover_every_chat_even_in_same_project(self):
        live = helper.LiveStates()
        chats = [{"id": str(i), "cwd": "repo" + str(i // 2), "title": "chat", "updated_at": i} for i in range(30)]
        seen = []
        for page in range(5):
            cards, _, pages, count = live.chat_page(chats, 100, page)
            seen += [c["id"] for c in cards if c["id"]]
        self.assertEqual(len(seen), 30)
        self.assertEqual(len(set(seen)), 30)
        self.assertEqual((pages, count), (5, 30))

    def test_waiting_working_error_done_order(self):
        live = helper.LiveStates()
        states = [{"type": "idle"}, {"type": "active"}, {"type": "systemError"},
                  {"type": "active", "activeFlags": ["waitingOnUserInput"]}]
        chats = [{"id": str(i), "cwd": "same-project", "title": "chat" + str(i)} for i in range(4)]
        for i, state in enumerate(states):
            live.states[str(i)] = ({"threadRuntimeStatus": state, "hasUnreadTurn": True}, 1, 100, "owner")
        cards, _, _, _ = live.chat_page(chats, 101, 0)
        self.assertEqual([c["status"] for c in cards[:4]], [4, 2, 5, 3])

    def test_watchlist_includes_older_projects(self):
        chats = [{"id": str(i), "cwd": "repo" + str(i), "title": "chat"} for i in range(70)]
        self.assertEqual(len(helper.watch_candidates(chats)), 70)
        self.assertEqual(helper.short_name({"cwd": "example", "title": "chat"}), "example")
        self.assertEqual(helper.short_name({"cwd": "another-project", "title": "chat"}), "another-project")


if __name__ == "__main__":
    unittest.main()
