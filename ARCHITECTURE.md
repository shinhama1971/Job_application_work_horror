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
      ├─ PauseMenu                ポーズ中の入力解釈と設定変更
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
- DirectXリソースと XAudio2 本体（`IXAudio2`）は `Microsoft::WRL::ComPtr` で管理します。
  XAudio2 のボイスは COM オブジェクトではないため、`Sound::Uninit` で `DestroyVoice` を呼んで解放します。
- 手動の `delete` は行いません。
- 共有所有（`std::shared_ptr`）は `ModelCache` だけに限定しています。同じモデルを複数の Object で使うとき、
  頂点バッファ・テクスチャ・バウンディング情報を1つだけ読み込んで共有するためです。
  キャッシュ自身も参照を保持し、終了時に `ModelCache::Clear` でD3Dデバイス破棄前にまとめて解放します。
  Scene・Object の所有は引き続き `std::unique_ptr` による単一所有です。

## Gameから分離した責務

| クラス | 責務 |
|---|---|
| `GameState` | アイテム数、電力状態、プレイ時間、捕獲・異変・ミス・取得数、ベスト記録 |
| `GameSettings` | 明るさ、演出品質、視点感度、音量、設定の保存と読み込み、段階値から実際の倍率への変換 |
| `PauseMenu` | ポーズの開閉、項目選択、設定値の変更、やり直し・タイトル・終了の入力解釈 |
| `ObjectManager` | Object所有、名前検索、型検索、追加、遅延追加、削除 |

`Game` にはゲーム全体の `Update`、`Draw`、Scene切り替え、各システムの呼び出しを残しています。
ポーズ中は `PauseMenu` が返す結果（設定変更の有無と `Command`）を受け取り、設定のPostProcess・Camera・音量への反映と
シーン操作だけを `Game` が行います。

## Sceneの責務

### StageScene

1面全体の目的と進行を統括します。時間とフェーズだけで完結する演出状態は以下へ分離しています。

- `ScareLightSequence`: 照明連鎖演出
- `StagePowerSequence`: 電力復旧と出口通電
- `ExitOmenSequence`: 出口前兆演出

- `SurveillancePatrol`: 監視カメラ巡回（映像で異常のあるカメラを報告し、現地で異常を見て確認する）

各クラスは時間・フェーズのみを管理します。照明、振動、PostProcess、Objectへの命令は
`StageScene` が行うため、演出対象の所有権をシーケンスへ渡しません。

#### 監視カメラ巡回

- カメラの設置位置と、各カメラで起こせる異常（人影・消灯・開かずの扉）は `StageSurveillanceCameras.h` のテーブルで定義します。
- `SurveillancePatrol` は状態（待機・映像確認・現地確認・完了）、正誤判定、制限時間、捕獲判定だけを持ち、
  乱数で決めた異常と視線判定の結果をSceneから受け取ります。描画・入力に依存しないため、進行ルールだけを取り出して読めます。
- 映像は `Scene::RenderOffscreen` で本描画の前に別カメラからRenderTextureへ描き、HUDへ貼ります。
  別視点に含めないObject（画面全体のノイズなど）は `Object::DrawsInAuxiliaryView` で除外し、型判定は行いません。

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
- `BehindPresence`: 背後の気配（視界の外に出現し、見ていない間だけ近づく）の出現間隔と判定

`Stage2Scene` に残るループ番号、最終イベント許可、出口状態はステージ進行そのものなので、
別クラスへ移さず統括責務として保持します。

### SceneからのObject参照

各Sceneは以後使うObjectの非所有ポインタを `StageObjects` / `Stage2Objects` にまとめて保持します。

- 2面は配置を `Stage2Layout` に分離しています。`Stage2Layout::Build` がObjectを生成して配置し、
  生成したポインタをそのまま `Stage2Objects` に入れて返します。名前を書くのは生成時の1か所だけで、
  生成した名前の一覧も記録して返すため、Sceneの終了時はその一覧で破棄します。
- 1面は `Init` の最後に一度だけ名前で `Game::RequireObj<T>` を呼んで取得します。
- 毎フレームの文字列ハッシュ検索をなくします。
- 名前の打ち間違いや生成漏れは、実行中に黙って `nullptr` になるのではなく、起動直後にObject名を示して検出します。
- 2面の照明は `Stage2Light` 列挙型で指定し、演出テーブルに名前文字列を持たせません。
- 保持するObjectはどれもSceneの終了までObjectManagerから破棄されないため、ポインタが無効になることはありません
  （名前付き `ShadowMan` は期限切れで非表示になるだけで破棄されません）。

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
  0. 全Objectから点光源を収集してGPUへ転送
  1. ShadowMap更新
  2. 必要な場合のみPlanarReflection更新
  3. 監視映像などSceneの補助カメラ描画
  4. タイル別ライトリスト作成（Compute Shader）→ ワールドObject描画
  5. PostProcess
  6. SceneのHUD・画面演出
  7. Debug UI
```

### タイルベースライティング（`Effect::TiledLighting`）

点光源は Compute Shader で画面のタイル（16x16ピクセル）ごとに絞り込み、各ピクセルは
自分のタイルに影響する光源だけを計算します。G-Bufferを使う完全なDeferredにはせず、
既存のForwardシェーダー（濡れ床・半透明・ボリュームライト）を保ったまま光源を増やせる
Forward+（Tiled Forward）の構成です。

1. `Game::Draw` の最初に、全Objectの `CollectPointLights` から点光源を集めて StructuredBuffer（SRV）へ転送
   （天井照明のほか、看板・表示灯・信号マーカーなども自分の発光色で光源を出します。上限256個）
2. 本描画の直前に `tiledLightCullingCS.hlsl` を Dispatch（1タイル = 1スレッドグループ64スレッド）
   - 代表スレッドがタイルの視錐台（左右上下の4平面）を作り、64スレッドで光源を分担して影響球と判定
   - 当たった光源番号を `groupshared` の配列へ `InterlockedAdd` で追加し、タイル別リスト（UAV）へ書き出し
3. ピクセルシェーダーは `SV_Position` から自分のタイルを求め、リストの光源だけを計算
4. 反射・監視映像など別視点の描画では、タイルがプレイヤー視点と一致しないため全光源を計算

Debug構成では ImGui の Shader debug view で「Light tiles」を選ぶと、タイルごとの光源数を色で確認できます。

描画パスへの参加可否は `Object` の仮想関数で問い合わせます。Rendererが具象型を列挙して
`dynamic_cast` する構造にはしていません。

## 立体音響（`Sound` + X3DAudio）

環境音やUIの音は従来どおり位置を持たずに鳴らし、ワールド上で起きる音だけを `Game::PlayAudioCueAt` で
位置付きで鳴らします。

- `Sound::PlayAt` は最大16音の枠を持ち、同じ効果音が重なっても別々の位置で鳴らせます
- 毎フレーム `Game::Update` がカメラの位置と向きを `Sound::UpdateListener` へ渡し、
  X3DAudioで各音の左右・距離の出力行列を計算し直します（振り向くと聞こえる方向が変わる）
- 聞き手と音源の間の遮蔽量は `Game::ComputeSoundOcclusion` が壁（`Wall`）と閉じた扉（`Door`）との
  線分判定で求めます。`Sound` は壁や扉の型を知らず、`std::function` で結果だけを受け取ります
- 遮蔽された音は音量を下げ、ローパスフィルターで高音を削ってこもらせます。遠い音も少し高音を落とします
- 追ってくる人影（`ShadowMan`）は、進んだ距離に合わせて自分の足元から足音を鳴らします

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

- Release x64: 警告レベル /W4 で警告0、エラー0
- Debug x64: 警告レベル /W4 で警告0、エラー0
- Debug実行ファイルの起動スモークテスト済み

進行ロジックは描画・入力・音声に依存しない状態クラスへ分離しており、単体で検証しやすい構造にしています。

リファクタリングではゲームの見た目、操作感、シーン進行、演出タイミングを変更していません。
