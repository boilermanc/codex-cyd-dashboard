# Notices

このプロジェクトはOpenAI、Work Louder、Espressifの公式製品ではなく、各社から承認、支援、保証を受けていません。OpenAI、ChatGPT、Codex、Codex Micro、Work Louderおよび各製品名・商標は、それぞれの権利者に帰属します。名称と識別子は互換対象を示す目的だけで使用しています。

## MITライセンスの参照実装

BLE HID互換トランスポートは、MIT Licenseで公開されている [ZyoungInc/codex-keyboard](https://github.com/ZyoungInc/codex-keyboard) の commit `2ee23a4ab696f94bb78d250f28cc4a9b879ba079` を基に、ESP32-2432S028R向けに変更しています。

Copyright (c) 2026 imliubo

ESP32-2432S028Rの表示・タッチ配線は、MIT Licenseで公開されている [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) を参照しています。

Copyright (c) 2023 Brian Lough

`lib/sensitive-xpt2046` はMIT Licenseで公開されている [PaulStoffregen/XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) v1.4を基に、圧力閾値を設定できるよう変更しています。

Copyright (c) 2015 Paul Stoffregen

各ライセンスの条件を満たすため、本リポジトリの `LICENSE` とこの通知を配布物へ含めてください。

## 互換性と安全性

文書化されていないHID識別子とRPC名は互換性のためにのみ使用しています。完全性、永続的な互換性、製品認証を表すものではありません。BLEはパスキーなしの `Just Works` ペアリングです。信頼できる環境で利用し、不要なペアリングはOSから削除してください。

## Sweetwater extension

Based on roflsunriz/esp32-codex-notifications, commit a96b80c5b8375aa347bed1826807d5c3b4b89b28. USB chat identity, per-chat cards, helper and media work by Sweetwater Technology. Atkinson Hyperlegible is bundled under assets/fonts/OFL.txt; its font license applies independently. Sweetwater trademarks and logo are not licensed as software.
