# アーキテクチャ概要

## 目的

本作は C++ / DirectX 11 / HLSL で制作した一人称ホラーゲームです。
この資料は、ゲームの挙動を変えずに行った責務分割と、現在の所有権設計を説明します。

## 全体構造

```text
Application
  └─ Game                         ゲームループとシステム統括
      ├─ Scene                    現在の画面を単一所有
      │   ├─ StageScene           1面の進行統括
      │   └─ Stage2Scene          2面の進行統括
      ├─ ObjectManager            Objectの所有・検索・遅延追加削除
      ├─ GameState                プレイ進行と成績
      ├─ GameSettings             設定値と永続化
      ├─ Camera
      ├─ PostProcess
      ├─ ShadowMap
      ├─ PlanarReflection
      └─ Sound
```

`Game` は各システムを呼び出す上位調整役です。個別の状態やObjectコンテナを直接処理せず、
`GameState`、`GameSettings`、`ObjectManager` へ処理を委譲します。

## 所有権

- `Game` のインスタンスと現在の `Scene` は `std::unique_ptr` による単一所有です。
- `ObjectManager` が `std::vector<std::unique_ptr<Object>>` でObjectの実体を所有します。
- 名前検索用Mapと外部へ返すポインタは非所有です。
- Objectの追加・削除は走査終了後に遅延実行し、コンテナ走査中の無効化を防ぎます。
- DirectXリソースは `Microsoft::WRL::ComPtr` で管理します。
- 手動の `delete` や共有所有への置き換えは行いません。

## Gameから分離した責務

| クラス | 責務 |
|---|---|
| `GameState` | アイテム数、電力状態、プレイ時間、捕獲・異変・ミス・取得数、ベスト記録 |
| `GameSettings` | 明るさ、演出品質、視点感度、音量、設定の保存と読み込み |
| `ObjectManager` | Object所有、名前検索、型検索、追加、遅延追加、削除 |

`Game` にはゲーム全体の `Update`、`Draw`、Scene切り替え、各システムの呼び出しを残しています。

## Sceneの責務

### StageScene

1面全体の目的と進行を統括します。時間とフェーズだけで完結する演出状態は以下へ分離しています。

- `ScareLightSequence`: 照明連鎖演出
- `StagePowerSequence`: 電力復旧と出口通電
- `ExitOmenSequence`: 出口前兆演出

各クラスは時間・フェーズのみを管理します。照明、振動、PostProcess、Objectへの命令は
`StageScene` が行うため、演出対象の所有権をシーケンスへ渡しません。

### Stage2Scene

2面全体のループ進行と、分離した仕組みの呼び出しを統括します。

- `SignalPuzzle`: 信号盤の入力順序
- `NoiseThreatSystem`: 足音危険度と追跡通知
- `ClockAnomaly`: 時計異変
- `FalseDoorAnomaly`: 偽ドア異変
- `PortraitAnomaly`: 肖像異変
- `ScratchAnomaly`: 引っかき傷メッセージ
- `ObservedScareSequence`: 注視時の照明演出
- `CaughtSequence`: 捕獲後の暗転と復帰
- `FinalSequence`: 最終演出と追跡
- `LightZoneProgress`: ライトゾーン通過状況
- `PuzzleFeedback`: パズル失敗通知と再試行補助
- `QuietRecovery`: 静止・消灯による危険回復

`Stage2Scene` に残るループ番号、最終イベント許可、出口状態はステージ進行そのものなので、
別クラスへ移さず統括責務として保持します。

## PlayerとGround

### Player

- `PlayerMovement`: 移動、重力、Sprint、Stamina、HeadBob
- `FlashlightSystem`: ON/OFF、電池消費、低残量通知、フリッカー、点灯補間

衝突はステージの `Wall` / `Door` に依存するため `Player` に残しています。
ライトの色・範囲・壁接近時の減光も描画結果へ直結するため、`Player` が適用します。

### Ground

- `WaterEffectSystem`: 水たまり判定、落下水滴、着水波紋、足音波紋

`Ground` には床メッシュ、Material、濡れ床Shader、平面反射との接続を残しています。

## 更新と描画の流れ

```text
Game::Update
  1. Scene::Update
  2. ObjectManager::UpdateAll
  3. 破棄予約を反映
  4. Scene切り替え予約を反映（切り替える場合は追加予約を破棄）
  5. Sceneを維持する場合だけ追加予約を反映

Game::Draw
  1. ShadowMap更新
  2. 必要な場合のみPlanarReflection更新
  3. ワールドObject描画
  4. PostProcess
  5. SceneのHUD・画面演出
  6. Debug UI
```

描画パスへの参加可否は `Object` の仮想関数で問い合わせます。Rendererが具象型を列挙して
`dynamic_cast` する構造にはしていません。

## dynamic_castの方針

描画判定やデバッグScene判定に使われていた `dynamic_cast` は削除しました。
現在は `ObjectManager::CastObject<T>` の型安全な汎用検索だけに残しています。

通常の具象Object検索は `typeid` 確認後に `static_cast` します。`Interactable` のように
`Object` とは別の基底クラスを多重継承している型ではポインタ調整が必要なため、フォールバックに
`dynamic_cast` を使用します。これを無理に削除すると型安全性が下がるため、意図的に維持しています。

## 分割を止める基準

- `Hud` はUI頂点生成とUI描画だけを担当しているため、行数だけを理由に分割しません。
- `Renderer` はDirectX 11の描画状態とGPUへの設定を担当する描画基盤として維持します。
- Sceneは個別システムを所有せず、進行を統括する責務を残します。
- 小さなデータクラスを増やすだけで依存関係が減らない場合は分割しません。

## 検証

- Release x64: 警告0、エラー0
- Debug x64: 警告0、エラー0
- Debug実行ファイルの起動スモークテスト済み
- シーケンス状態クラスは発火時刻、順序、キャンセルを単体確認済み

リファクタリングではゲームの見た目、操作感、シーン進行、演出タイミングを変更していません。
