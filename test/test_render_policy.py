from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "device-ui.cpp").read_text(encoding="utf-8")
MAIN_SOURCE = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening + 1 : index]
    raise AssertionError(f"function is not closed: {signature}")


class RenderPolicyTests(unittest.TestCase):
    def test_frequent_updates_never_redraw_the_whole_screen(self) -> None:
        for signature in (
            "void DeviceUi::setState",
            "void DeviceUi::setPage",
            "void DeviceUi::showPressed",
            "void DeviceUi::tick",
        ):
            body = function_body(signature)
            self.assertNotIn("drawAll(", body, signature)
            self.assertNotIn("fillScreen(", body, signature)

    def test_press_redraws_only_the_affected_control(self) -> None:
        body = function_body("void DeviceUi::showPressed")
        self.assertIn("drawPressedAction(action)", body)
        self.assertIn("InputKind::PageSwitch", body)

    def test_status_and_animation_updates_are_local(self) -> None:
        state_body = function_body("void DeviceUi::setState")
        self.assertIn("drawNotification()", state_body)
        self.assertIn("drawConnection()", state_body)
        self.assertIn("drawAgent(index)", state_body)

        tick_body = function_body("void DeviceUi::tick")
        self.assertIn("drawAgent(index)", tick_body)
        self.assertNotIn("drawAgents()", tick_body)

    def test_microphone_uses_a_thin_lower_arc(self) -> None:
        body = function_body("void DeviceUi::drawCommandIcon")
        self.assertIn("fillRoundRect(x - 6, y - 17, 13, 25, 6", body)
        self.assertIn("drawArc(x, y + 2, 14, 13, 270, 90", body)
        self.assertNotIn("drawArc(x, y, 16, 12, 0, 180", body)

    def test_drag_uses_continuous_point_from_the_single_touch_sample(self) -> None:
        touch_body = function_body("bool DeviceUi::readTouch")
        self.assertIn("touchFilter_.current(stable)", touch_body)
        self.assertNotIn("if (!touchFilter_.push", touch_body)
        self.assertEqual(MAIN_SOURCE.count("ui.readTouch("), 1)
        self.assertNotIn("readDragPoint", MAIN_SOURCE)

    def test_navigate_redraw_stays_inside_the_content_area(self) -> None:
        self.assertNotIn("drawAll(", function_body("void DeviceUi::setSleepTimeoutSec"))
        self.assertNotIn("drawAll(", function_body("void DeviceUi::setNavigateScroll"))
        self.assertNotIn("drawAll(", function_body("void DeviceUi::refresh"))
        content_body = function_body("void DeviceUi::drawContent")
        self.assertIn("display_.setViewport(", content_body)
        self.assertIn("display_.resetViewport()", content_body)

    def test_navigate_updates_are_buffered_before_pushing_pixels(self) -> None:
        refresh_body = function_body("void DeviceUi::refresh")
        self.assertIn("contentSprite_.fillSprite(kBackground)", refresh_body)
        self.assertIn("drawNavigate()", refresh_body)
        self.assertIn("contentSprite_.pushSprite(", refresh_body)
        self.assertLess(refresh_body.index("drawNavigate()"),
                        refresh_body.index("contentSprite_.pushSprite("))

    def test_header_is_continuous_and_connection_is_drawn_above_it(self) -> None:
        header = function_body("void DeviceUi::drawHeader")
        self.assertIn("board::kScreenWidth", header)
        self.assertLess(header.index("display_.fillRect("), header.index("drawConnection()"))
        self.assertIn("kPanel", function_body("void DeviceUi::drawNotification"))
        self.assertIn("kPanel", function_body("void DeviceUi::drawConnection"))


if __name__ == "__main__":
    unittest.main()
