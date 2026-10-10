#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "ui-model.h"

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void testAgentGrid() {
  for (int index = 0; index < 6; ++index) {
    const int column = index % 2;
    const int row = index / 2;
    const InputAction action = actionAt(Page::Agents, static_cast<std::int16_t>(81 + column * 158),
                                        static_cast<std::int16_t>(58 + row * 57));
    require(action.kind == InputKind::AgentKey, "Agent領域がAgent Keyにならない");
    require(action.index == index, "Agent番号が一致しない");
  }
}

void testAgentEdges() {
  for (int i = 0; i < 6; ++i) {
    const int x = 6 + (i % 2) * 158, y = 33 + (i / 2) * 57;
    require(actionAt(Page::Agents, x, y).index == i, "card top-left");
    require(actionAt(Page::Agents, x + 149, y + 50).index == i, "card bottom-right");
    require(actionAt(Page::Agents, x + 150, y + 25).kind == InputKind::None, "right gap");
    require(actionAt(Page::Agents, x + 75, y + 51).kind == InputKind::None, "bottom gap");
  }
}

void testTabs() {
  require(actionAt(Page::Navigate, 53, 224).page == Page::Agents, "Agentタブ");
  require(actionAt(Page::Agents, 160, 224).page == Page::Commands, "Commandタブ");
  require(actionAt(Page::Agents, 270, 224).page == Page::Navigate, "Navigateタブ");
}

void testRotationControlAndCoordinates() {
  const ScreenPoint normal = orientPoint({23, 41}, false);
  require(normal.x == 23 && normal.y == 41, "通常向きの座標維持");
  const ScreenPoint inverted = orientPoint({23, 41}, true);
  require(inverted.x == 296 && inverted.y == 198, "180度回転座標");
  const ScreenPoint restored = orientPoint(inverted, true);
  require(restored.x == 23 && restored.y == 41, "180度回転の可逆性");

  require(bootGestureForDuration(49) == BootGesture::None, "BOOTノイズ除外");
  require(bootGestureForDuration(50) == BootGesture::RotateScreen, "BOOT短押し");
  require(bootGestureForDuration(1499) == BootGesture::RotateScreen,
          "BOOT短押し上限");
  require(bootGestureForDuration(1500) == BootGesture::CalibrateTouch,
          "BOOT長押し調整");
  require(touchThresholdForPressure(10) == 12, "押圧閾値の下限");
  require(touchThresholdForPressure(30) == 15, "軽いペンの押圧閾値");
  require(touchThresholdForPressure(600) == 120, "押圧閾値の上限");
}

void testTouchContactIsLockedUntilPhysicalRelease() {
  require(touchTransition(false, true, true) == TouchTransition::Press,
          "接触開始で一度だけ押下");
  require(touchTransition(true, true, true) == TouchTransition::None,
          "同じ接触の座標更新を無視");
  require(touchTransition(true, false, true) == TouchTransition::None,
          "圧力揺れを解放扱いしない");
  require(touchTransition(true, false, false) == TouchTransition::Release,
          "IRQ解放でのみリリース");
  require(touchTransition(false, false, false) == TouchTransition::None,
          "非接触を維持");
}

void testDisplayPowerFollowsHostLighting() {
  DisplayPowerSync power;
  power.reset(0);
  power.setConnected(true, 0);
  require(power.awake(), "初期状態は画面ON");

  power.observeLightingConfig(true);
  require(!power.observeThreadLighting(0x3F, true, 100),
          "接続直後の全消灯状態では即座に画面OFFにしない");
  require(!power.observeThreadLighting(0x3F, true, 4000),
          "接続直後の全消灯再送をすべて無視");
  require(power.awake(), "接続直後の画面ONを維持");

  power.observeLightingConfig(false);
  require(power.observeThreadLighting(0x3F, false, 6000) == false,
          "通常照明中は画面状態を変更しない");
  power.observeLightingConfig(true);
  require(power.observeThreadLighting(0x3F, true, 36000),
          "Desktopの全消灯ペアで画面OFF");
  require(!power.awake(), "Auto-dim後は画面OFF");

  require(power.wake(37000), "タッチ復帰で画面ONへ遷移");
  require(power.awake(), "タッチ復帰後は画面ON");
  power.observeLightingConfig(true);
  require(!power.observeThreadLighting(0x3F, true, 37500),
          "未割り当て状態の復帰再送で再消灯しない");
  require(!power.observeThreadLighting(0x3F, true, 41000),
          "復帰状態の複数再送で再消灯しない");
  require(power.awake(), "復帰直後の画面ONを維持");

  power.observeLightingConfig(true);
  require(power.observeThreadLighting(0x3F, true, 72000),
          "次のAuto-dim全消灯では再び画面OFF");
  require(power.observeThreadLighting(0x01, false, 73000),
          "Agent状態変更で画面を自動復帰");
  require(power.awake(), "Agent状態変更後は画面ON");
}

void testDisplaySleepsWhileDisconnected() {
  DisplayPowerSync power;
  power.reset(1000);
  require(power.awake(), "未接続起動直後は画面ON");
  require(!power.tick(30999), "未接続30秒未満は画面ON");
  require(power.tick(31000), "未接続30秒で画面OFF");
  require(!power.awake(), "未接続タイマー後は画面OFF");

  require(power.wake(32000), "切断中のタッチで画面復帰");
  require(!power.tick(61999), "切断中の復帰から30秒未満は画面ON");
  require(power.tick(62000), "切断中の復帰から30秒で再消灯");

  require(power.setConnected(true, 63000), "再接続で画面復帰");
  require(power.awake(), "再接続後は画面ON");
  require(!power.tick(200000), "接続中は切断タイマーを無効化");

  require(!power.setConnected(false, 201000), "切断時は即消灯しない");
  require(!power.tick(230999), "再切断から30秒未満は画面ON");
  require(!power.setConnected(true, 231000), "期限直前の再接続はONを維持");
  require(!power.tick(500000), "再接続で切断タイマーをキャンセル");
}

void testDisconnectTimerHandlesMillisWrap() {
  DisplayPowerSync power;
  constexpr std::uint32_t start = 0xFFFFFF00U;
  power.reset(start);
  require(!power.tick(start + 29999U), "millis周回前後の30秒未満");
  require(power.tick(start + 30000U), "millis周回後も30秒で消灯");
}

void testTouchSamplesMustBeStableBeforePress() {
  TouchSampleFilter filter;
  ScreenPoint output;
  require(!filter.push({100, 200}, output), "1点では未確定");
  require(!filter.push({104, 198}, output), "2点では未確定");
  require(filter.push({102, 201}, output), "近い3点で確定");
  require(output.x == 102 && output.y == 199, "3点平均");
  require(!filter.push({101, 200}, output), "同一接触の再通知を禁止");

  filter.reset();
  require(!filter.push({20, 20}, output), "リセット後1点");
  require(!filter.push({200, 200}, output), "座標飛びで再開始");
  require(!filter.push({201, 199}, output), "再開始後2点");
  require(filter.push({199, 201}, output), "再開始後の安定3点");
}

void testDragTrackingFollowsFingerWithoutRedelivery() {
  TouchSampleFilter filter;
  ScreenPoint output;
  require(!filter.push({100, 200}, output), "1点では未確定");
  require(!filter.push({104, 198}, output), "2点では未確定");
  require(filter.push({102, 201}, output), "近い3点で確定");
  for (int i = 0; i < 300; ++i) {
    require(!filter.push({150, 150}, output), "追従中は再通知しない");
  }
  // Integer EMA stalls within a few pixels of the finger.
  require(std::abs(output.x - 150) <= 5, "追従座標が指へ寄る");
  require(std::abs(output.y - 150) <= 5, "追従座標が指へ寄る");
  require(filter.current(output), "追従中も現在値を取得");
  filter.reset();
  require(!filter.current(output), "解放後は現在値なし");
}

void testNavigateDragKeepsOnePressAndMovesBothControls() {
  TouchSampleFilter filter;
  ScreenPoint point;
  bool captured = false;
  for (int i = 0; i < 3; ++i) {
    const bool newTap = filter.push({100, 170}, point);
    const bool hasPoint = filter.current(point);
    const TouchTransition transition = touchTransition(captured, hasPoint, true);
    if (transition == TouchTransition::Press) captured = true;
    require((transition == TouchTransition::Press) == newTap,
            "確定座標で一度だけ押下");
  }
  for (int i = 0; i < 40; ++i) {
    require(!filter.push({240, 70}, point), "ドラッグ中は再押下しない");
    require(filter.current(point), "ドラッグ中も座標を返す");
    require(touchTransition(captured, true, true) == TouchTransition::None,
            "ドラッグを別のタップとして扱わない");
  }
  require(point.y < 80, "縦ドラッグがスクロールへ届く");
  require(sleep_menu::sliderValueFromX(point.x, 0U, sleep_menu::kMinutesMax, 1U) >
              40U,
          "横ドラッグがスライダーへ届く");
  require(touchTransition(captured, true, false) == TouchTransition::Release,
          "接触終了で解放");
}

void testCommandGridAndProtocolIds() {
  const char* expected[] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12"};
  for (int index = 0; index < 6; ++index) {
    const int column = index % 3;
    const int row = index / 3;
    const InputAction action =
        actionAt(Page::Commands, static_cast<std::int16_t>(54 + column * 106),
                 static_cast<std::int16_t>(72 + row * 87));
    require(action.kind == InputKind::CommandKey, "Command領域がCommand Keyにならない");
    require(action.index == index, "Command番号が一致しない");
    require(std::strcmp(protocolKeyFor(action), expected[index]) == 0,
            "CommandのプロトコルIDが一致しない");
  }
}

void testNavigationAngles() {
  require(std::abs(actionAt(Page::Navigate, 53, 62).angle - 0.75F) < 0.001F, "上方向");
  require(std::abs(actionAt(Page::Navigate, 53, 174).angle - 0.25F) < 0.001F, "下方向");
  require(std::abs(actionAt(Page::Navigate, 37, 118).angle - 0.50F) < 0.001F, "左方向");
  require(std::abs(actionAt(Page::Navigate, 111, 118).angle) < 0.001F, "右方向");
  const InputAction decrement = actionAt(Page::Navigate, 200, 74);
  const InputAction increment = actionAt(Page::Navigate, 244, 74);
  const InputAction press = actionAt(Page::Navigate, 239, 157);
  require(decrement.kind == InputKind::EncoderStep &&
              std::strcmp(protocolKeyFor(decrement), "ENC_CW") == 0,
          "左ボタンで値を減らす");
  require(increment.kind == InputKind::EncoderStep &&
              std::strcmp(protocolKeyFor(increment), "ENC_CC") == 0,
          "右ボタンで値を増やす");
  require(press.kind == InputKind::EncoderPress &&
              std::strcmp(protocolKeyFor(press), "ENC_CLK") == 0,
          "ダイヤル押下");
}

void testGapsAndBoundsAreInactive() {
  require(actionAt(Page::Agents, -1, 100).kind == InputKind::None, "画面左外");
  require(actionAt(Page::Agents, 320, 100).kind == InputKind::None, "画面右外");
  require(actionAt(Page::Agents, 160, 80).kind == InputKind::None, "タイル間の隙間");
  require(actionAt(Page::Navigate, 150, 110).kind == InputKind::None, "操作外領域");
}

void testStatusColors() {
  require(statusKindForColor(0xFFFFFF, 0.0F) == StatusKind::Unassigned, "未割り当て");
  require(statusKindForColor(0xFFFFFF, 1.0F) == StatusKind::Idle, "待機");
  require(statusKindForColor(0x3291FF, 1.0F) == StatusKind::Thinking, "処理中");
  require(statusKindForColor(0x34C759, 1.0F) == StatusKind::Complete, "完了");
  require(statusKindForColor(0xFFB020, 1.0F) == StatusKind::Attention, "入力待ち");
  require(statusKindForColor(0xFF453A, 1.0F) == StatusKind::Error, "エラー");
  require(!isNotificationStatus(StatusKind::Thinking), "処理中を通知しない");
  require(isNotificationStatus(StatusKind::Complete), "完了を通知する");
  require(isNotificationStatus(StatusKind::Attention), "入力待ちを通知する");
  require(isNotificationStatus(StatusKind::Error), "エラーを通知する");
}

void testBacklightBrightnessFollowsActiveLighting() {
  require(std::abs(synchronizedLightingBrightness(1.0F, 0.72F, true) - 0.72F) <
              0.001F,
          "正の照明輝度を同期する");
  require(std::abs(synchronizedLightingBrightness(0.72F, 0.0F, false) - 0.72F) <
              0.001F,
          "消灯ゾーンの0は保存輝度を上書きしない");
  require(std::abs(synchronizedLightingBrightness(0.72F, 0.0F, true)) < 0.001F,
          "有効な照明指示の0はBrightness 0パーセントとして同期する");
  require(std::abs(synchronizedLightingBrightness(0.72F, 2.0F, true) - 1.0F) <
              0.001F,
          "上限を100パーセントへ丸める");
  require(std::abs(synchronizedLightingBrightness(1.0F, 0.01F, true) - 0.01F) <
              0.001F,
          "最小の1パーセント設定も同期する");

  require(backlightDuty(0.0F, true) == 0, "0パーセントはduty 0");
  require(backlightDuty(0.5F, true) == 128, "50パーセントは8-bit dutyへ丸める");
  require(backlightDuty(1.0F, true) == 255, "100パーセントは最大duty");
  require(backlightDuty(0.8F, false) == 0, "Auto-dim中は保存輝度に関係なく消灯");
}

void testIdleTimeoutSleepsAfterInactivity() {
  DisplayPowerSync power;
  power.reset(0);
  power.setConnected(true, 0);
  require(power.idleTimeoutSec() == 0U, "既定はアイドル消灯なし");
  power.setIdleTimeoutSec(600U);
  require(power.idleTimeoutSec() == 600U, "10分設定を保持");
  require(!power.tick(599999U), "期限前は画面ON");
  require(power.tick(600000U), "10分無操作で画面OFF");
  require(!power.awake(), "アイドル後は画面OFF");
  require(power.wake(610000), "タッチで復帰");
  power.setIdleTimeoutSec(0U);
  require(!power.tick(3600000U), "0は常時点灯");
  power.setIdleTimeoutSec(0xFFFFFFFFU);
  require(power.idleTimeoutSec() == 0U, "範囲外は無効");
}

void testSleepSliderMapping() {
  using namespace sleep_menu;
  require(timeoutFromParts(0U, 0U) == 0U, "0分0時間は無効化");
  require(timeoutFromParts(59U, 24U) == kTimeoutMaxSec, "最大24時間59分");
  require(minutesPart(7800U) == 10U, "分の取り出し");
  require(hoursPart(7800U) == 2U, "時間の取り出し");
  require(isValidTimeout(0U) && isValidTimeout(89940U), "範囲内を受理");
  require(!isValidTimeout(89941U), "範囲外を拒否");
  require(sliderValueFromX(kTrackX0, 0U, 59U, 1U) == 0U, "軌道左端");
  require(sliderValueFromX(kTrackX1, 0U, 59U, 1U) == 59U, "軌道右端");
  require(sliderXFromValue(0U, 0U, 59U) == kTrackX0, "つまみ左端");
  require(sliderXFromValue(59U, 0U, 59U) == kTrackX1, "つまみ右端");
  require(clampScroll(-1) == 0, "スクロール下限");
  require(clampScroll(9999) == kScrollMax, "スクロール上限");
  require(scrollFromTrackY(kScrollBarY0) == 0, "バー上端");
  require(scrollFromTrackY(kScrollBarY1) == kScrollMax, "バー下端");
}

void testNavigateSleepControls() {
  using namespace sleep_menu;
  const InputAction minutes =
      actionAt(Page::Navigate, sliderXFromValue(30U, 0U, 59U), kMinutesY - 60, 60);
  require(minutes.kind == InputKind::SleepMinutes, "分スライダー");
  require(minutes.index == 30, "分の値");
  const InputAction hours =
      actionAt(Page::Navigate, sliderXFromValue(2U, 0U, 24U), kHoursY - 100, 100);
  require(hours.kind == InputKind::SleepHours, "時間スライダー");
  require(hours.index == 2, "時間の値");
  const InputAction scrolled = actionAt(Page::Navigate,
                                        sliderXFromValue(30U, 0U, 59U),
                                        200, 55);
  require(scrolled.kind == InputKind::SleepMinutes, "スクロール追従");
  const InputAction bar =
      actionAt(Page::Navigate, kScrollBarX0 + 6, 190);
  require(bar.kind == InputKind::NavigateScroll, "スクロールバー");
  const InputAction joy =
      actionAt(Page::Navigate, 53, 22, 40);
  require(std::abs(joy.angle - 0.75F) < 0.001F, "スクロール後の joystick");
}

}  // namespace

int main() {
  testAgentGrid();
  testAgentEdges();
  testTabs();
  testRotationControlAndCoordinates();
  testTouchContactIsLockedUntilPhysicalRelease();
  testDisplayPowerFollowsHostLighting();
  testDisplaySleepsWhileDisconnected();
  testDisconnectTimerHandlesMillisWrap();
  testTouchSamplesMustBeStableBeforePress();
  testDragTrackingFollowsFingerWithoutRedelivery();
  testNavigateDragKeepsOnePressAndMovesBothControls();
  testCommandGridAndProtocolIds();
  testNavigationAngles();
  testGapsAndBoundsAreInactive();
  testStatusColors();
  testBacklightBrightnessFollowsActiveLighting();
  testIdleTimeoutSleepsAfterInactivity();
  testSleepSliderMapping();
  testNavigateSleepControls();
  std::cout << "ui-model: 19 tests passed\n";
  return 0;
}
