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
- `Stage1AmbientSounds`: 姿の見えない物音（天井裏の足音・配管を叩く音・遠くの扉）の間隔・種類・場所の決定。
  視線の外にある場所だけを選び、時間差で鳴らす足音や打音を予約します。鳴らしてよいか（演出中でないか）はSceneが判断して渡します
- `Stage1WallWritings`: 懐中電灯で照らすと浮かぶ壁の文字の進行（ライトの中心で照らし続けると既読、ループ廊下の文字は目を離した隙に書き換え、電力復旧で全て消える）。
  文字1枚の描画と「読める状態か」の判定は `FlashlightWriting`（Object）が担当します
- `Stage1KeypadDoor`: 暗証番号の扉（任意探索）。入力画面の操作、正解・不正解の反応、プレイごとにランダムな番号と、懐中電灯で浮かぶ手がかりの数字の差し替え。
  番号・入力中の数字・失敗回数の状態は、入力や描画に依存しない `KeypadLock` が持ちます
- `Stage1WestWing`: 西棟（浸水した機械室）の進行（鍵を探す → 扉を開ける → 奥でヒューズを探す → 取った）と、姿の見えない物音（水の滴る音・背後で水の中を歩く音・扉が閉まる音）の予約。床一面の水は `Ground::SetFloodRegion` で、描画（`wetFloorPS` の `FloodRect`）・足音・波紋に反映する
- `Stage1HiddenRoomEvent`: 暗証番号の扉の先の部屋の閉じ込めイベント（待機 → 閉じ込め → 脱出の状態機械）。鍵（`KeyItem`）の位置をランダムに選び、
  見つけるまで影を出してライトで追い払わせます。触れられると電池を奪われます

各クラスは時間・フェーズのみを管理します（監視カメラ巡回の実行役 `StageSurveillanceController` を除く）。照明、振動、PostProcess、Objectへの命令は
`StageScene` が行うため、演出対象の所有権をシーケンスへ渡しません。

#### 監視カメラ巡回

- カメラの設置位置と、各カメラで起こせる異常（人影・消灯・開かずの扉）は `StageSurveillanceCameras.h` のテーブルで定義します。
- `SurveillancePatrol` は状態（待機・映像確認・現地確認・完了）、正誤判定、制限時間、捕獲判定だけを持ち、
  乱数で決めた異常と視線判定の結果を受け取ります。描画・入力に依存しないため、進行ルールだけを取り出して読めます。
- `StageSurveillanceController` は巡回の実行役です。入力を `SurveillancePatrol` へ渡し、異常の見た目（人影・消灯・扉）、
  監視映像の描画、通知、捕獲演出を担当します。`StageScene` は `Update`・`RenderFeeds`・`DrawFeed` を呼ぶだけで、
  巡回を終えたフレームに `Update` が返す `true` を見て記録端末の完了通知を出します。
- 映像は `Scene::RenderOffscreen` で本描画の前に別カメラからRenderTextureへ描き、HUDへ貼ります。
  別視点に含めないObject（画面全体のノイズなど）は `Object::DrawsInAuxiliaryView` で除外し、型判定は行いません。

#### 目的表示

- 画面上部の目的・通知・ヒントの文章は、1面は `SelectStage1Objective`、2面は `SelectStage2Objective` が選びます。
  どちらも状態を受け取って文章を返すだけの関数で、優先順位はこの関数の中だけで決まります。
- Sceneは `MakeObjectiveInput` で状態を集めて渡します。1面は監視カメラの残り秒数などを含む文章を組み立てるため、
  `std::string` で返します（2面は固定の文章だけなので `std::string_view`）。

### Stage2Scene

2面全体のループ進行と、分離した仕組みの呼び出しを統括します。

- `SignalPuzzle`: 信号盤の入力順序
- `NoiseThreatSystem`: 足音危険度と追跡通知
- `ClockAnomaly`: 時計異変
- `FalseDoorAnomaly`: 偽ドア異変
- `PortraitAnomaly`: 肖像異変（ライトで照らしたまま見つめ続けた時間の計測）
- `Stage2AnomalyPlan`: 1周目・2周目に見つける必要がある異変を、偽ドア・時計・肖像画・ノックから毎回ランダムに2つ選ぶ（入力や描画に依存しない純粋な状態クラス）
- `Locker`: 隠れられるロッカーの扉と「隠れる」操作。隠れている間の移動・視点の制限は`Player`、影の振る舞い（見失う・見られて捕まる）は`Stage2Scene::UpdateHiddenFromStalker`が担当
- `KnockingAnomaly`: 壁の向こうのノック異変（音の出どころの選択、ノックの間隔、出どころの壁の前で耳を澄ませた時間）
- `TensionPulse`: 自分の心拍音と呼吸音をいつ・どれくらいの大きさで鳴らすかを、危険度と「隠れているか」から決める（入力や描画に依存しない状態クラス）。危険度は2面では危険ゲージと同じ`Stage2Scene::ComputeThreatRate`、1面では近くの影までの距離（`StageScene::ComputeThreatRate`）から渡す。鳴らす処理は1面・2面共通の`TensionPulseFeedback.h`
- `ScratchAnomaly`: 引っかき傷メッセージ
- `ObservedScareSequence`: 注視時の照明演出
- `CaughtSequence`: 捕獲後の暗転と復帰
- `FinalSequence`: 最終演出と追跡
- `LightZoneProgress`: ライトゾーン通過状況
- `PuzzleFeedback`: パズル失敗通知と再試行補助
- `QuietRecovery`: 静止・消灯による危険回復
- `BehindPresence`: 背後の気配（視界の外に出現し、見ていない間だけ近づく）の出現間隔と判定
- `SelectStage2Objective`: 画面上部の目的・通知・ヒントの文章の選択（状態を受け取り文章を返す純粋関数。
  優先順位はこの関数だけで決まり、Sceneは `MakeObjectiveInput` で状態を集めて渡すだけです）
- `Stage2Notices`: Sceneが持つ一時的な通知（周回・充電器・記録・信号盤）の残り時間

`Stage2Scene` に残るループ番号、最終イベント許可、出口状態はステージ進行そのものなので、
別クラスへ移さず統括責務として保持します。

### SceneからのObject参照

各Sceneは以後使うObjectの非所有ポインタを `StageObjects` / `Stage2Objects` にまとめて保持します。

- 配置は `Stage1Layout` / `Stage2Layout` に分離しています。`Build` がObjectを生成して配置し、
  生成したポインタをそのまま `StageObjects` / `Stage2Objects` に入れて返します。
- 名前を書くのは生成時の1か所だけです（`SceneLayoutBuilder` が生成と同時に名前を記録します）。
  Sceneの終了時はその一覧で破棄するため、生成・取得・破棄で名前の一覧が食い違うことはありません。
- 破棄を名前で行うのは、すでに破棄されたObjectに対しても安全だからです。将来、途中で破棄される
  Objectを配置に加えても、解放済みのポインタに触れることがありません。
- 毎フレームの文字列ハッシュ検索をなくします。
- ポインタは生成時に受け取るため、名前の打ち間違いでObjectを取得し損ねることがありません。
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
  4. 深度プリパス（不透明な壁・床・扉の深度だけ）→ タイル別ライトリスト作成（Compute Shader）→ ワールドObject描画
  5. PostProcess
  6. SceneのHUD・画面演出
  7. Debug UI
```

### 深度プリパス（Zプリパス）

本描画の前に、不透明な壁・床・扉の深度だけを本描画の深度バッファへ描きます（`Object::WritesDepthPrepass` / `DrawDepthPrepass`）。

- 本描画では、奥に隠れた画素が深度の判定で先に捨てられ（Early-Z）、壁の古さ・部屋の角の暗がり・照明を計算する重いピクセルシェーダーが、
  最終的に見える画素でしか動かなくなります（重なり描き＝オーバードローの削減）。
- 深度は本描画と同じ頂点シェーダー（`litTextureVS`）・同じワールド行列（`MakeWorldMatrix`）で、ピクセルシェーダーを外して描きます。
  同じシェーダーに同じ入力を与えると深度が完全に一致するため、本描画の深度の判定（`LESS_EQUAL`）を必ず通り、ちらつきは出ません。
- 半透明の物・`clip` を使う物（人影の消え方、壁の文字、2面の水たまりの面）はプリパスに描きません。
- 深度バッファは `R32_TYPELESS` で作り、書き込みは `D32_FLOAT`、タイルベースライティングのCompute Shaderからの読み取りは `R32_FLOAT` として使います。

`--benchmark`（RTX 3050 Laptop、1920x1080、ライトON）での本描画のGPU時間:

| 地点 | 導入前 | 導入後 |
|---|---|---|
| 開始地点 | 2.28ms | 1.34ms |
| 廊下の奥 | 2.69ms | 1.33ms |
| 中央ホール | 2.76ms | 1.36ms |
| 左の倉庫 | 4.81ms | 1.45ms |
| 暗証番号の扉 | 4.64ms | 1.26ms |
| ループ廊下 | 3.44ms | 1.36ms |
| 西棟 | 2.44ms | 1.41ms |

棚や壁が重なって見える場所ほど効果が大きく、どの地点もほぼ同じ時間に揃いました。各地点の画面を導入前と画素単位で比べ、
左の倉庫では1画素も変わらないこと、ほかの地点も壁と壁の交わる線の1画素の差（深度の精度が16ビットから32ビットに上がったため）と、
HUDの文字のフェードの進み具合の差だけであることを確認しています。

なお、深度プリパスを導入する前に、別の深度テクスチャへ壁などの深度を描いてタイルの光源を絞るだけの実装も試しましたが、
光源の計算は重さの原因ではなく、プリパスの分だけ0.05〜0.2ms遅くなりました。深度を本描画と共有し、オーバードローの削減と
光源の絞り込みの両方に使う形に切り替えています。

### タイルベースライティング（`Effect::TiledLighting`）

点光源は Compute Shader で画面のタイル（16x16ピクセル）ごとに絞り込み、各ピクセルは
自分のタイルに影響する光源だけを計算します。G-Bufferを使う完全なDeferredにはせず、
既存のForwardシェーダー（濡れ床・半透明・ボリュームライト）を保ったまま光源を増やせる
Forward+（Tiled Forward）の構成です。

1. `Game::Draw` の最初に、全Objectの `CollectPointLights` から点光源を集めて StructuredBuffer（SRV）へ転送
   （天井照明のほか、看板・表示灯・信号マーカーなども自分の発光色で光源を出します。上限256個）
2. 本描画の直前に `tiledLightCullingCS.hlsl` を Dispatch（1タイル = 1スレッドグループ64スレッド）
   - 代表スレッドがタイルの視錐台（左右上下の4平面）を作り、64スレッドで光源を分担して影響球と判定
   - 64スレッドで深度プリパスの深度を4画素ずつ読み、`InterlockedMax` でタイルの一番奥の深度を求め、それより奥にある光源（壁の向こうの部屋の照明など）を外す。
     半透明の物は不透明な物より手前にしか見えないため、一番奥の深度だけを使えば取りこぼさない（手前側の深度では絞らない）
   - 当たった光源番号を `groupshared` の配列へ `InterlockedAdd` で追加し、タイル別リスト（UAV）へ書き出し
3. ピクセルシェーダーは `SV_Position` から自分のタイルを求め、リストの光源だけを計算
4. 反射・監視映像など別視点の描画では、タイルがプレイヤー視点と一致しないため全光源を計算

Debug構成では ImGui の Shader debug view で「Light tiles」を選ぶと、タイルごとの光源数を色で確認できます。

描画パスへの参加可否は `Object` の仮想関数で問い合わせます。Rendererが具象型を列挙して
`dynamic_cast` する構造にはしていません。

## GPUとCPUの分担

画素ごと・光源ごとの大量の並列計算はGPU、分岐が多くゲームの状態に依存する処理はCPUで行います。
当たり判定のように結果をゲームの進行が使う処理は、GPUからの読み戻し（待ちが発生する）を避けるためCPUに置いています。

| 処理 | 担当 | 実装 | CPU/GPUを選んだ理由 |
|---|---|---|---|
| ゲーム進行・状態機械・入力 | CPU | 各Scene、各Sequence、`PauseMenu` | 分岐が多く、フレームごとの処理量は小さい |
| 当たり判定（線分・平面・三角形・球・AABB） | CPU | `Collision`、`Player` | 結果を移動や進行に即座に使うため |
| 視錐台カリング | CPU | `GameRendering.cpp`（Objectごとの境界球） | Objectは数百以下のため、CPUで判定して描画コール自体を減らす方が効果が大きい。本描画・影・反射でパスごとに判定 |
| 点光源の収集と転送 | CPU → GPU | `Object::CollectPointLights` → `TiledLighting::SetLights` | 毎フレーム `MAP_WRITE_DISCARD` で StructuredBuffer へ書き込み。収集用の `vector` は使い回して再確保しない |
| 音の遮蔽判定 | CPU | `Game::ComputeSoundOcclusion` | 壁・扉との線分判定。結果を `Sound` へ数値で渡す |
| 壁の文字を読めるかの判定 | CPU | `FlashlightWriting::IsBeingRead` | 距離・視線の角度・遮蔽で判定し、進行（既読）に使う |
| 描画パスの間引き | CPU | `GameRendering.cpp` | 影は設定に応じて1〜3フレームに1回、反射は水面が見えるときだけ・静止中は間隔をあけて更新 |
| 頂点変換・ライティング | GPU（VS/PS） | `litTextureVS/PS`、`wetFloorPS` | 画素ごとの計算 |
| 深度プリパス | GPU（VSのみ） | `Object::DrawDepthPrepass`（壁・床・扉） | 本描画のEarly-Zで、隠れた画素の重いピクセルシェーダーを省く |
| タイルごとの光源の絞り込み | GPU（CS） | `tiledLightCullingCS.hlsl` | タイル数×光源数の組み合わせが大きく、画面座標に依存する |
| 影 | GPU | `ShadowMap`（1024x1024）、`flashlightShadow.hlsli`（比較サンプラーで5か所のPCF） | 深度の描画と比較は画素ごとの処理 |
| 壁・床・天井の古さ、凹凸、部屋の角の暗がり | GPU（PS） | `GetWallAgeing`、`GetFloorAgeing`、`surfaceDetail.hlsli`、`roomOcclusion.hlsli` | 画像素材を読まずにワールド座標から計算する（メモリ帯域の代わりに計算量を使う） |
| 水たまりの反射 | GPU | `PlanarReflection`（縦横半分の解像度） | 上下を反転したカメラで別に描画 |
| ブルーム | GPU（CS） | `bloomExtractCS` → `bloomBlurHorizontalCS` → `bloomBlurVerticalCS` | ぼかしに使う画素を `groupshared` に一度読み込み、グループ内で使い回してテクスチャの読み込みを減らす |
| 画面効果（露出・光の筋・CRT風） | GPU（PS） | `FullScreenQuad` | 画面全体の画素ごとの処理 |

処理の重さは `--benchmark` で、フレーム時間・CPU時間・描画パスごとのGPU時間（`GpuTimer`）を分けて計測します。
`GpuTimer` は `TIMESTAMP` / `TIMESTAMP_DISJOINT` クエリを4フレーム分順に使い回し、結果を待たずに取得するため、
計測のためにCPUがGPUを待つことはありません。

## ライティングモデル

本作のライティングは物理ベース（PBR）ではなく、暗いホラーの絵作りを数値で直接調整する経験的なモデルです。

| 要素 | 現在の実装 | 場所 |
|---|---|---|
| 拡散光 | Lambert（`N·L`）を、暗い面が真っ黒にならないよう底上げ（例：`0.25 + lambert * 0.75`） | `litTexturePS.hlsl`、`wetFloorPS.hlsl` |
| 鏡面反射 | 濡れた床だけ Blinn-Phong（ハーフベクトル） | `wetFloorPS.hlsl` |
| フレネル | `pow(1 - N·V, 4)` の近似（濡れた床の反射の強さ、湿った壁の光沢） | `wetFloorPS.hlsl`、`litTexturePS.hlsl` |
| 環境光 | 上向きの面は青白く、下向きの面は暖かくする半球環境光 | `flashlightLighting.hlsli` |
| 遮蔽 | 部屋の形から解析的に求める角の暗がり（SSAOではない） | `roomOcclusion.hlsli` |
| 懐中電灯 | 内側・外側の円錐による配光、レンズのむら、シャドウマップ。距離減衰は届く範囲の端で0になる減衰に、距離の2乗に反比例する物理的な減衰を3割ほど混ぜたもの | `flashlightLighting.hlsli`、`flashlightShadow.hlsli` |
| 色空間 | テクスチャ・バックバッファとも `R8G8B8A8_UNORM` で、ガンマ空間のまま計算 | `Texture.cpp`、`Renderer.cpp` |
| 露出 | シーンが決めた目標の露出（0.85〜1.20）へなめらかに近づける（画面の輝度は測っていない） | `PostProcess` |

### PBRにしていない理由

- 光源は懐中電灯と少数の天井照明が中心で、「どこがどれだけ見えるか」をゲームの難しさとして直接調整したかったため。
- 金属・光沢のある材質がほとんどなく、コンクリート・漆喰・濡れた床の表現は、底上げした拡散光とフレネル近似で足りていたため。

### PBRへ移行する場合に必要な変更

1. テクスチャを `_SRGB` 形式で読み込み、ライティングをリニア空間で計算する。
2. 描画先を浮動小数点のHDRにし、トーンマッピングで表示用の範囲へ変換する（ブルームの中間バッファはすでにFP16）。
3. `Material` に roughness・metallic を追加する。
4. 鏡面反射を Cook-Torrance（GGXの法線分布、Schlickのフレネル、Smithの幾何減衰）に、拡散光を `(1 - F)(1 - metallic) * albedo / π` にして、エネルギー保存を守る。
5. 半球環境光を、キューブマップによるIBL（拡散の照度と、粗さごとの鏡面反射）に置き換える。
6. 露出を、Compute Shaderで画面の平均輝度を求める自動露出にする。

### タイルベースライティングの既知の制限

- タイルの一番奥の深度で奥の光源は外していますが、手前側（一番手前の深度）では絞っていません。半透明の物を取りこぼさないためです。
  手前の壁と奥の壁が同じタイルに映る場合は、その間の空間の光源も残ります。奥行き方向にも分けるClustered方式で改善できます。
- 反射・監視映像など別視点の描画では、タイルがプレイヤー視点と一致しないため全光源を計算しています。

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
