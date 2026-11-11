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
      ├─ ReflectionProbe / ScreenSpaceReflection
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
- `ModelCache`（1回の実行の中での使い回し）とは別に、`StaticMesh` が Assimp で変換した結果をファイル（`cache/model/*.mesh`）に保存し、
  次の実行からはそれを読みます。どちらのモデルの読み込み（`ModelCache`・`PropModel`・`Player`）も `StaticMesh::Load` を通るので、呼ぶ側は変えていません。
  ファイルの先頭に印・版・`VERTEX_3D` と `MATERIAL` の大きさ・元のモデルの大きさと更新日時を書き、1つでも違えば Assimp で読み直します。
  テクスチャの画像は保存せず、読むたびにファイル名から作り直すので、所有の形（`unique_ptr` で `StaticMesh` から渡す）はこれまでと同じです。

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
ポーズメニューを開け閉めするのは `Game::Update`（1面・2面・外の森・導入）で、メニューを描くのは各シーンの `Draw`（`Hud::DrawPause`）です。
新しいシーンでポーズを開けるようにしたときは、そのシーンの `Draw` にも `DrawPause` を足します（足さないと、ゲームが止まっただけに見える。11月10日まで、外の森がそうなっていた）。外の森と導入では階の番号に0を渡し、「現在 研究所の外」「この場面をやり直す」と表示します。

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
- `Stage1WestWing`: 西棟（浸水した機械室）の進行（鍵を探す → 扉を開ける → 排水バルブで水を抜く → 奥でヒューズを探す → 取った）と、姿の見えない物音（水の滴る音・背後で水の中を歩く音・扉が閉まる音）の予約。床一面の水は `Ground::SetFloodRegion` で、描画（`wetFloorPS` の `FloodRect`）・足音・波紋に反映する。
  バルブ（`DrainValve`）は、調べている対象（`InteractionSystem` の選んだ物）でEキーを押し続けている間だけ回り、1つ回すごとに水の手前の端を奥の排水口へ動かして、水に浸かった範囲を縮めていく
- `Stage1SafeLights`: 光の安全地帯（停電しても消えない暖かい電球の下）。円の範囲の中にいるかと、なめらかに上下する「落ち着き」（0〜1）を持ち、ライトを消して休んでいる間に戻す電池の量を返す。
  落ち着きはSceneが、心拍の強さ（`ComputeThreatRate`）・姿の見えない物音を鳴らすか（`IsAmbientSoundAllowed`）・画面のざらつきと周辺の暗さに使う
- `Stage1HiddenRoomEvent`: 暗証番号の扉の先の部屋の閉じ込めイベント（待機 → 閉じ込め → 脱出の状態機械）。鍵（`KeyItem`）の位置を3か所から選び、記録端末の起動後は保管灯を順に点けて、鍵に対応する灯を2回ずつ点滅させます。
  見つけるまで影を出してライトで追い払わせます。触れられると電池を奪われます
- `Stage1LightStalker`: 書類保管室の「照らすと止まる影」（待機 → 出現 → 触れた後の間の状態機械）。懐中電灯で照らしている間は止まり、光が外れると歩くより速く近づく。
  部屋を4単位の格子に分け、プレイヤーのマスから幅優先探索で各マスまでの歩数を求め、歩数が減る隣のマスへ進むことで棚の迷路を回り込む
  （探索はプレイヤーが別のマスへ移ったときだけやり直す）。それでも行き詰まったら、見ていない間に別の場所へ移る。照らされているか（光の円の中で、間に壁や扉がないか）の判定と、
  影の表示・足音・電池を奪う処理は `StageScene::UpdateArchiveStalker` が行い、このクラスは判定結果を受け取って動きだけを決めます

各クラスは時間・フェーズのみを管理します（監視カメラ巡回の実行役 `StageSurveillanceController` を除く）。照明、振動、PostProcess、Objectへの命令は
`StageScene` が行うため、演出対象の所有権をシーケンスへ渡しません。

#### 進む先の天井灯

`StageScene::UpdateRouteLights` が進行とプレイヤーの位置に合わせて天井灯を1灯選びます。`CeilingLight::UpdateRouteFlicker` は不ぞろいな間隔で2回の明るい点灯を作り、ときどき長い明滅を挟みます。発光パネルと周囲の点光源は同じ明るさを使うため、停電中は床や壁に落ちる赤い光でも方向が分かります。

#### 施設案内図

配電盤の横に、`stage1_props.txt` の `place Stage1FacilityMap` で印刷された案内図を置きます。`FlashlightWriting` の板と2枚の画像の切り替えを使い、印刷物は深度プリパスで裏の壁を隠します。`printedSurfacePS.hlsl` は共通の照明・影・霧を使い、紙面のつや消し材質を拡散反射で描きます。暗がりの計算には配置時の壁からの距離を渡します。壁のシェーダーでは、穴やひび、カビ、水の筋や傷が現れる範囲で細部を計算します。`AdvanceCorridorLoop` が最初の周回で扉の消えた画像に切り替えます。画像は `tools/generate-facility-map.ps1` から作り直せます。

#### 録音と無線

- `VoiceRecorder` は本体・スピーカー・ボタン・ランプを箱のメッシュで描き、ランプと小さな点光源を同じ明るさで変化させます。調べると赤の点滅から緑の点灯へ変わり、`TakePlayRequest` が一度だけ再生要求を返します。
- `Stage1Layout` が録音機4台を作り、配置ファイルの `place` 行を適用して `StageObjects::recorders` に参照を保持します。`StageScene::UpdateTransmissions` が再生要求を受け取り、字幕を予約して `GameState` の再生数を増やします。再生数はリザルトの「今回の発見」と完全探索の判定に使います。
- `Stage1Transmissions` は `StoryText.h` の録音3行と無線の台詞を同じ列に並べ、1行ずつ表示します。1本目のヒューズを取って14秒経過し、字幕が空くと最初の無線を予約します。続く入室と影の出現は字幕の再生中も判定し、成立した順に予約します。4つ目の録音の後に最後の応答と「通信途絶」を流し、以後の嘘の無線を止めます。
- `StageScene::Draw` は `Hud::DrawRadio` で字幕を描き、録音の見出しと帯は琥珀色、無線は緑で区別します。携帯ラジオの雑音には、影による危険度と字幕の再生による雑音の強さのうち大きい方を渡します。

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
- `Stage2AnomalyPlan`: 1周目・2周目に見つける必要がある異変を、偽ドア・時計・肖像画・ノック・血の手形・鏡・見えない影から毎回ランダムに2つ選ぶ（入力や描画に依存しない純粋な状態クラス）
- `Locker`: 隠れられるロッカーの扉と「隠れる」操作。隠れている間の移動・視点の制限は`Player`、影の振る舞い（見失う・見られて捕まる）は`Stage2Scene::UpdateHiddenFromStalker`が担当
- `KnockingAnomaly`: 壁の向こうのノック異変（音の出どころの選択、ノックの間隔、出どころの壁の前で耳を澄ませた時間）
- `HandprintAnomaly`: 血の手形の異変（現れた手形の数、照らし続けた時間、少しずつ浮かぶ見え方）。手形の場所と見え方は `Renderer::SetWallHandprints`（b12）で壁のシェーダーへ渡し、`litTexturePS` が1面の血の跡と同じ `HorrorHandprint` の式で描く
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
- 1面の小物（進行のコードが使わない箱）は `PropLayoutFile` がテキストファイル（`assets/layout/stage1_props.txt`）から読み、
  `StageObjects::layoutProps` に名前とWallの組で持ちます。Debug構成では `F5` で読み直し、同じ名前の物は置き直し、
  新しい名前は作り、ファイルから消えた物は隠します。扉・アイテム・バルブなど進行のコードが作る物は、`place` 行を当てはめるときに
  「この名前の物はこう置き直す」という関数（`PropLayoutFile::PlacementBindings`）を `StageObjects` / `Stage2Objects` に覚えておき、
  読み直したときに `ReapplyPlacements` で呼び直します。プレイヤーの位置と spot から選んだ場所は、遊んでいる途中の状態なので置き直しません。
  `PropLayoutFile::FileChangeWatcher` が0.5秒ごとにファイルの更新時刻を調べ、Unity などで書き出されたら F5 と同じく自動で読み直します（1面・2面とも Debug構成だけ）。
- Debug構成のデバッグ画面（F1）には `PropLayoutEditor` を出しています。クリックした位置から伸ばした光線と小物の箱の
  当たり判定（スラブ法）で選び、ImGuiで値を変えるとすぐWallへ当てはめます。`PropLayoutFile::Save` は書き戻す前に
  ファイルを読み直し、値の変わった行だけを書き換えます（複製した小物は元の行の後ろへ足し、消した小物の行は消します）。
  書き戻した後は読み直して、行番号を新しくしています。
- 毎フレームの文字列ハッシュ検索をなくします。
- ポインタは生成時に受け取るため、名前の打ち間違いでObjectを取得し損ねることがありません。
- 2面の照明は `Stage2Light` 列挙型で指定し、演出テーブルに名前文字列を持たせません。
- 保持するObjectはどれもSceneの終了までObjectManagerから破棄されないため、ポインタが無効になることはありません
  （名前付き `ShadowMan` は期限切れで非表示になるだけで破棄されません）。

## PlayerとGround

### Player

- `PlayerMovement`: 移動、重力、Sprint、Stamina、HeadBob
- `FlashlightSystem`: ON/OFF、電池消費、低残量通知、フリッカー、点灯補間
- `FirstPersonHands`: 一人称で画面の右下に映る、懐中電灯を持った右手のモデル。視点の回転に遅れて付いてくる揺れ（ばね）、歩き・走り・息・スイッチ・電池の揺れ、ロッカーの中では手を下げる。プレイヤーの視点の本描画のときだけ描く（監視映像には映さない）

衝突はステージの `Wall` / `Door` に依存するため `Player` に残しています。
ライトの色・範囲・壁接近時の減光も描画結果へ直結するため、`Player` が適用します。

### Ground

- `WaterEffectSystem`: 水たまり判定、落下水滴、着水波紋、足音波紋。水たまりの置き方（`PuddleLayout`）は、1面は床全体のマス目に乱数で置き、2面はマス目を41ずらして廊下の決まった3か所に置く（シェーダーへの表 b13 にずらし量も渡す）

`Ground` には床メッシュ、水たまりのメッシュ（水たまり・雨漏り・浸水した範囲ごとの多角形。画面の外は描かず、遠いほど反射を軽くする）、Material、濡れ床Shader（水面の反射の光線は、このシェーダーの中で飛ばす）を残しています。

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
  2. 技術デモでキューブマップを選んだときだけReflectionProbe更新（普段のレイ反射は本描画のシェーダーの中で計算）
  3. 監視映像などSceneの補助カメラ描画
  4. 深度プリパス（不透明な壁・床・扉の深度だけ）→ タイル別ライトリスト作成（Compute Shader）→ ワールドObject描画
  5. PostProcess（ブルーム → 自動露出の測光 → 露出・画面効果の合成）
  6. SceneのHUD・画面演出
  7. Debug UI
```

### 深度プリパス（Zプリパス）

本描画の前に、不透明な壁・床・扉の深度だけを本描画の深度バッファへ描きます（`Object::WritesDepthPrepass` / `DrawDepthPrepass`）。

- 本描画では、奥に隠れた画素が深度の判定で先に捨てられ（Early-Z）、壁の古さ・部屋の角の暗がり・照明を計算する重いピクセルシェーダーが、
  最終的に見える画素でしか動かなくなります（重なり描き＝オーバードローの削減）。
- 深度は本描画と同じ頂点シェーダー（`litTextureVS`）・同じワールド行列（`MakeWorldMatrix`）で、ピクセルシェーダーを外して描きます。
  同じシェーダーに同じ入力を与えると深度が完全に一致するため、本描画の深度の判定（`LESS_EQUAL`）を必ず通り、ちらつきは出ません。
- 半透明の物・`clip` を使う物（人影の消え方、壁の文字）はプリパスに描きません。
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
| 影 | GPU | `ShadowMap`（1024x1024、手元の位置から作る。`FlashlightRig.h`）、`flashlightShadow.hlsli`（PCSS：遮る物の探索8か所と比較9か所） | 深度の描画と比較は画素ごとの処理 |
| 壁・床・天井の古さ、凹凸、部屋の角の暗がり | GPU（PS） | `GetWallAgeing`、`GetFloorAgeing`、`surfaceDetail.hlsli`、`roomOcclusion.hlsli` | 画像素材を読まずにワールド座標から計算する（メモリ帯域の代わりに計算量を使う） |
| 壁のホラーらしい跡（血の手形・引っかき傷・血の垂れ跡） | GPU（PS） | `horrorMarks.hlsli`（`litTexturePS` の壁の古さから呼ぶ） | 壁に沿ったマス目ごとの乱数で置き場所と種類を決め、距離関数で形を作る。画像素材を読まない |
| 水たまりの反射 | GPU（PS） | `wetFloorPS.hlsl` の `TraceRayReflection`、`rayReflection.hlsli`（部屋の形・小物・扉へのレイ反射に、前後の深度が一致するSSRの色を混ぜる。扉は本描画と同じ開閉角度の箱を使い、近くの壁と扉は斜めからも輪郭が読めるようにする） | 反転したカメラで景色を描き直さず、床の画素ごとに反射の光線を飛ばす |
| 水たまりのメッシュと LOD | CPU・GPU（PS） | `Ground::BuildPuddleSurfaces`（マス目の表から、岸が必ず収まる楕円の多角形を作る）、`Ground::BuildDynamicSurfaces`（雨漏りと浸水した範囲。毎フレーム）、`Ground::DrawWaterSurfaces`、`wetFloorPS.hlsl` の `WaterSurfaceLevel`（b10） | 床の本体は乾いた床（水たまりの計算なし）として描き、メッシュを床の0.05上に描き重ねる。深度プリパスにもメッシュを描くので、下の床の画素は捨てられる。画面の外か（`Camera::IsSphereVisible`）と描画グループはメッシュごとに CPU で決める。反射は画素までの距離を使い、SSRの寄与を105〜155で弱め、部屋の形による反射を300〜380で霧の色へ移す |
| 壁の鏡（2面） | GPU（PS） | `mirrorPS.hlsl`（`rayReflection.hlsli` を床と共通で使う）、`GameRendering.cpp`（カメラに近い小物の箱16個を b12 で渡す） | 鏡の画素ごとに反射の光線を飛ばす。床と違い下向きの光線が多いため、床の高さとの交差も見ている |
| 鏡に映る自分の姿・鏡の異変の人影（2面） | CPU・GPU（PS） | `MirrorSelf`（1秒60回の姿勢の記録から遅らせた姿勢と手足の振りを b12 で渡す）、`MirrorAnomaly`、`mirrorPS.hlsl`、`FigureVolumes`（焼いた距離場の3Dテクスチャ t6・t13・s2）、`shader/figureVolume.hlsli`（体も人影も距離場のスフィアトレーシング。体は関節のまわりに回した空間で読んで手足を振る） | 一人称で体がなく、異変の人影も物として置かないため、鏡のシェーダーの中だけに形を出している |
| 一人称の手と懐中電灯 | CPU・GPU（VS・PS） | `FirstPersonHands`（揺れの計算、b9 に手元だけの射影行列と、前の壁で跳ね返る懐中電灯の光）、`shader/viewModel*.hlsl*`（肌・布・金属） | ビューポートの深度の範囲を 0〜0.02 にして描き、壁にめり込まないようにしている（本描画は near=1 なので、それより手前に壁は来ない） |
| 建物と小物の配置のファイル | CPU（読み込み時） | `PropLayoutFile`（`assets/layout/stage1_props.txt`・`stage2_props.txt`）、`Stage1Layout`・`Stage2Layout` | 建物の壁・柱・天井・配管・小物はファイルに書き（`prop` 行）、`room` を付けた壁を、部屋の角の暗がり（b11）とレイ反射の部屋の形に使う。扉・照明・電池・端末など進行のコードが作る物は、種類と動きをコードが決め、置き場所（位置・向き・大きさ）だけを `place` 行から読む（`PropLayoutFile::ApplyPlacement`）。ヒューズと鍵の候補の場所は `spot` 行。ループの瞬間移動の計算に使う最後のドア・開始地点のドアとその裏の通路と表示、MirrorAnomaly の面に合わせた鏡と枠、監視カメラの表で置く開かずの扉は、計算と結び付いているので C++ に残している |
| Unity のマップエディター | エディター（C#） | `MapEditor/Assets/Editor/MapLayout`（`MapLayoutDocument` が配置ファイルを行ごとに読み書き、`MapLayoutEditor` がシーンへの読み込みと書き出し、`MapLayoutBatch` が往復の確認）、`MapEditor/Assets/MapLayout`（GameObject と行を結ぶ印） | ゲームは配置ファイルだけを読み、Unity には依存しない。書き出しは変わった数値だけを、ゲームの `FormatNumber` と同じ書き方で書き換える |
| 外の森 | CPU（読み込み時）・GPU | `ForestScene`（タイトルの1面、`--forest`、デバッグ画面のボタンで開く。研究所の玄関（`LabEntrance`）から1面へ入る）、`assets/layout/forest_props.txt`、`tools/maya/build_forest_props.py`（`forest_*` のモデル。一覧は `Wall.cpp`） | 1面・2面と同じ仕組み（配置ファイル・`Ground`・`Player`・`Wall` の見た目のモデル・Debug版の読み直し）だけで組み、場面だけを足した。木は、見た目のモデルを「標準の大きさ」で割って箱に合わせる仕組みを使い、標準の大きさを幹にして、枝を箱の外へ出している（当たり判定は幹だけ、カリングの球は箱の対角線の半分で枝まで届く） |
| 箱の小物の見た目のモデル | CPU（読み込み時） | `Wall::SetVisualModel`（モデルの一覧は `Wall.cpp`）、`PropLayoutFile`（配置ファイルの `model` の指定） | 当たり判定・影・深度プリパス・水面に映す箱は箱の大きさのまま。モデルは標準の大きさで割って1×1×1の箱の空間にし、ワールド行列の拡大で置いた箱の大きさに伸ばす。部位の色は頂点の色で付け、マテリアルの色は白にしている。警告灯とライトは、光る部位（レンズ・電球）を後ろに並べ、その前までを光る色のないマテリアルで描き分けている |
| 自分の体の影 | CPU・GPU（PS） | `Player`（足もとの位置・向き・歩く手足の振りを b12 の PlayerBody へ）、`shader/figureShadow.hlsli` の `GetPlayerBodyShadow`（litTexturePS・wetFloorPS の点光源ごと） | 体は描かず、シャドウマップにも入れない。胴・頭・両脚・両腕のカプセルが、画素から光源への線分を遮るかで影を求めるので、どの照明でも影が落ちる。懐中電灯の光は目の位置から出るので遮らない |
| 小物と人影のモデル | CPU（読み込み時） | `PropModel`（FBXの部位をマテリアルの名前で選び、頂点カラーの環境遮蔽 × 部位の色を頂点の色にして、各クラスの頂点の配列へ足す） | モデルは実際の大きさで作り、各クラスが掛ける拡大率で位置と法線を割っている。描き方は箱で組んでいたときと同じ |
| 濡れた足跡 | CPU・GPU（PS） | `WetFootprints`（輪のバッファ・乾き）、`Ground`（b10へ渡す）、`shader/wetFootprints.hlsli`、`FootprintFollower`（2面の誰かの足跡） | 足跡ごとに近くの画素だけ形を計算し、遠い画素は距離で省いている |
| 崩れて現れる・消える | GPU（PS） | `Wall::DissolveIn` / `DissolveOut`、`litTexturePS.hlsl`（clip と焦げた縁） | マテリアルの詰め物を崩れ具合に使い、定数バッファを増やしていない |
| 水たまりの反射の比較（技術デモF2） | GPU | `ReflectionProbe`（キューブマップ128x128を1フレームに1面）、`ScreenSpaceReflection`（深度の写し取り）、`wetFloorPS.hlsl`（キューブマップを反射の向きで読む・SSRの光線をたどる） | レイ反射と、重さと正確さの違いを見比べるため。普段はレイ反射 |
| 西棟の水面の光の揺らぎ（コースティクス） | GPU（PS） | `waterCaustics.hlsli`（`litTexturePS` から呼ぶ） | 壁・天井の画素ごとに、水面で折り返した点と懐中電灯の配光から求める |
| 水たまりの波のシミュレーション | GPU（CS） | `WaterRippleSimulation`、`waterRippleCS.hlsl`（256x256マス、ピンポン、1秒に60歩）。床は `wetFloorPS`、壁は `waterCaustics.hlsli` が高さを読む | 全マスが同じ式で隣のマスだけを読むので、GPUで並列に計算できる。結果のテクスチャはCPUへ読み戻さない。描画の最初（`Object::RunGpuSimulation`）で実行し、GPU時間は約0.012ms |
| ブルーム | GPU（CS） | `bloomExtractCS` → `bloomBlurHorizontalCS` → `bloomBlurVerticalCS` | ぼかしに使う画素を `groupshared` に一度読み込み、グループ内で使い回してテクスチャの読み込みを減らす |
| 自動露出の測光と目の慣れ | GPU（CS） | `autoExposureCS.hlsl` | 4096点の明るさを1グループ256スレッドで合計する。結果はGPUのバッファに置いたまま露出のシェーダーが読み、CPUへ読み戻さない |
| 画面効果（露出・光の筋・CRT風） | GPU（PS） | `FullScreenQuad` | 画面全体の画素ごとの処理 |

処理の重さは `--benchmark` で、フレーム時間・CPU時間・描画パスごとのGPU時間（`GpuTimer`）を分けて計測します。
`GpuTimer` は `TIMESTAMP` / `TIMESTAMP_DISJOINT` クエリを4フレーム分順に使い回し、結果を待たずに取得するため、
計測のためにCPUがGPUを待つことはありません。

## ライティングモデル

壁・扉・小物には金属/粗さとGGX・Smith・Schlickの鏡面反射を導入しています。従来の拡散光・ホラー向けの色調と組み合わせており、全描画をリニア/HDRにした純粋なPBRレンダラーではありません。

| 要素 | 現在の実装 | 場所 |
|---|---|---|
| 拡散光 | Lambert（`N·L`）を、暗い面が真っ黒にならないよう底上げ（天井灯は `0.20 + lambert * 0.80`、床の懐中電灯は `0.25 + lambert * 0.75`）。壁・小物の懐中電灯は、正面の面も明るくなりすぎないよう `lerp(0.22, 0.45, lambert)` に抑えている | `litTexturePS.hlsl`、`wetFloorPS.hlsl`、`flashlightLighting.hlsli` |
| 鏡面反射 | 壁・扉・小物はGGXのPBR系の材質、濡れた床には従来の水面反射・光沢も残す | `pbrLighting.hlsli`、`litTexturePS.hlsl`、`wetFloorPS.hlsl` |
| フレネル | `pow(1 - N·V, 4)` の近似（濡れた床の反射の強さ、湿った壁の光沢） | `wetFloorPS.hlsl`、`litTexturePS.hlsl` |
| 環境光 | 上向きの面は青白く、下向きの面は暖かくする半球環境光 | `flashlightLighting.hlsli` |
| 遮蔽 | 部屋の形からの解析的な角の暗がりに加え、半解像度SSAOを本描画へ適用 | `roomOcclusion.hlsli`、`AmbientOcclusion.*` |
| 懐中電灯 | 内側・外側の円錐による配光、レンズのむら、シャドウマップ。距離減衰は届く範囲の端で0になる減衰に、距離の2乗に反比例する物理的な減衰を3割ほど混ぜたもの | `flashlightLighting.hlsli`、`flashlightShadow.hlsli` |
| 色空間 | テクスチャ・バックバッファとも `R8G8B8A8_UNORM` で、ガンマ空間のまま計算 | `Texture.cpp`、`Renderer.cpp` |
| 露出 | シーンが決めた目標の露出（演出の意図）に、Compute Shaderで測った画面の明るさの対数平均（中央を重く）から求めた目の慣れの倍率（0.95〜1.08）を掛ける。暗い所へは約3秒、明るい所へは約0.4秒で慣れる | `PostProcess`、`autoExposureCS.hlsl`、`exposurePS.hlsl` |

### 懐中電灯の影・濡れた床・水面の光

- **影を手元から作る**：光（明るさの円）は壁の文字を読みやすいよう視線の中央から照らし、シャドウマップだけを目より少し右下（手元、`FlashlightRig.h`）から作っています。
  光源と視点が同じ位置だと、物の影はその物の真後ろに隠れて画面に映りません。影だけを数単位ずらすことで、棚や配管の影が壁や床に映り、ライトを振ると影が動きます。
  光そのものも手元から出す形を試しましたが、近くの壁では光の円が右下にずれ、壁の文字の左端が光の外に出たため、影だけをずらす形にしています。
- **PCSS**：影の縁を、遮る物から離れた所ほど柔らかくしています。周りのシャドウマップを8か所読んで遮る物の平均の距離を求め、
  半影の幅 = 光源の大きさ ×（受ける面までの距離 − 遮る物までの距離）÷ 遮る物までの距離 で、比較する範囲を決めます（9か所の比較、画素ごとに配置を回す）。
- **濡れた床の反射のむら**：浮いた埃や油膜を表す、ゆっくり流れるノイズで水面の荒さを場所ごとに変え、荒い所ほど反射の画像を縦長の範囲で読んでぼかしています
  （濡れた床に映る照明が縦に伸びて見える現象）。汚れた所は映り込みも弱くしています。
- **水面の光の揺らぎ（コースティクス、1面の西棟）**：壁や天井の点を水面で上下に折り返した点に、懐中電灯の光が向かっているかを、ふつうの照明と同じ配光の式で求めます。
  光が水面に当たる点で、流れるノイズから作った網目の模様を掛け、水面で跳ね返った光が揺らいで映るようにしています。浸水した範囲・水面の高さ・時間は `Ground` が毎フレーム定数バッファ（b12）で渡し、
  懐中電灯を持つプレイヤーの視点のビュー行列も同じ定数バッファで渡し、カメラの位置はシェーダーでその行列から求めています（水面の反射を描くときも、懐中電灯の視点で計算する）。
- **水たまりの波のシミュレーション（波動方程式）**：各マスの次の高さを「2×今の高さ − 1つ前の高さ ＋ 速さ²×まわり4マスとの差の合計」で求め、少し弱めます（減衰）。
  足音（走るほど大きく強い）と水滴は、その場所を押し下げるおもりとして足します。乾いた床のマスと、床に立った柱・棚・机の脚の中（カメラに近い順に最大32個を、上から見た長方形で渡す）は高さを0に固定するので、波は岸や柱で跳ね返り、隣の輪と重なって干渉します。
  速さ²（0.35）は計算が発散しない上限（0.5）より小さくし、1歩の時間は1/60秒に固定して、フレームレートに左右されないようにしています。計算する範囲はカメラのまわりだけで、カメラが動くとマス目の単位で中身をずらします。
  床では高さの傾きで法線を傾けて反射を波打たせ、懐中電灯の光をきらっと返します。壁では、水面で折り返した点の高さのへこみ（ラプラシアン）が光を集めると考え、光の輪として映します（`waterCaustics.hlsli`）。

### 壁・床の凹凸を懐中電灯の光に反映（11月3日に修正）

`litTexturePS` と `wetFloorPS` は、壁の古さ・床の目地の凹凸で傾けた法線をビュー空間へ移すのに、ピクセルシェーダーでビュー行列（`View`、b1）を使っています。
以前はb1を頂点シェーダーにしか設定しておらず、ピクセルシェーダーでは0の行列になっていました。そのため、壁と1面の床の懐中電灯は面の向きによらず一番暗い値（`0.25 + lambert * 0.75` の0.25）で当たり、
壁の天井灯・懐中電灯の鏡面反射と環境の映り込み、1面の濡れた床の懐中電灯の光沢も、向きが0のベクトルになって効いていませんでした（天井灯の拡散光はワールド空間の法線で計算していたため、凹凸が効いていた）。

`Renderer::Init` でb1をピクセルシェーダーにも設定して直し、そのままでは明るくなりすぎる所を次のように調整しました。

- **明るさ**：正面から照らした壁は約4倍（0.25 → 1.0）になったため、壁・小物の懐中電灯の拡散光を `lerp(0.22, 0.45, lambert)`（`GetFlashlightDiffuseResponse`）に抑えました。
  横から当たる面は前とほぼ同じ、正面の面は約1.8倍で、凹凸の陰影はこの幅の中で出ます。1面の計測地点の画面の平均の明るさは、前の1.05〜1.5倍です。
  2面の床は前から頂点シェーダーで回した正しい法線で計算していたので、床の式（`0.25 + lambert * 0.75`）は変えていません。
- **凹凸の強さ**：扉や家具などの古さを付けない面は、壁と同じ強さでは、木の扉が叩いた金属のようにまだらに見えたため、半分（0.36）にしました。建物の壁と天井は前のままです。
- **法線の折れ**：画面上の微分から法線を作る `ApplyHeightToNormal` を、2×2画素で同じ値になる `ddx`・`ddy` から、行・列ごとに差を取る `ddx_fine`・`ddy_fine` に変えました。
  光が凹凸に反応するようになって、法線の折れが細かい段差に見えたためです。
- **処理の重さ**：計測モードの本描画は、どの地点も0.1〜0.2ms増えました（鏡面反射と環境の映り込みが、実際の向きで計算されるようになった分）。

### PBR系の材質を導入する前の判断（経緯）

- 光源は懐中電灯と少数の天井照明が中心で、「どこがどれだけ見えるか」をゲームの難しさとして直接調整したかったため。
- 金属・光沢のある材質がほとんどなく、コンクリート・漆喰・濡れた床の表現は、底上げした拡散光とフレネル近似で足りていたため。

### リニア/HDRのPBRへ進める場合の残りの変更

1. テクスチャを `_SRGB` 形式で読み込み、ライティングをリニア空間で計算する。
2. 描画先を浮動小数点のHDRにし、トーンマッピングで表示用の範囲へ変換する（ブルームの中間バッファはすでにFP16）。
3. `Material` のroughness・metallicは追加済み。今後はリニア/HDRの光の単位と整合させる。
4. GGX・Schlick・Smithの鏡面反射は導入済み。今後は拡散光を `(1 - F)(1 - metallic) * albedo / π` にして、エネルギー保存を守る。
5. 粗さに応じたキューブマップの鏡面反射は導入済み。拡散の照度や物理的なプリフィルターは今後の検討。
6. 自動露出（実装済み）を、HDRの値の平均輝度から露出を決める本来の形にし、倍率の範囲を広げる（今はガンマ空間の明るさを測り、ホラーの暗さを保つため倍率を小さい範囲に抑えている）。

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
- 雨漏りの `AddLeak` で作る水滴は、雨漏り1か所につき片方の水滴だけ、近くで着水した瞬間にその位置から音を鳴らします。西棟の環境音の水滴も同じ音源を使い、音量は `Stage1WestWing` で調整します。
- プレイヤーの水音は、波紋と同じ足元の判定と `PlayerMovement::IsSprinting` で、歩き用・走り用の短い音を選びます。`WaterEffectSystem::IsInsidePuddle` は床のシェーダーと同じマス目・向き・岸のゆがみから、水面の濃さが半分以上の場所を水たまりとします。音源は `tools/generate-water-steps.ps1` で、BigSoundBank の実録から一歩ずつ切り出します。人影・見えない足跡は従来の水音を使い、2面の危険度に渡す歩きと走りの強さも従来どおりです。

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

画質設定と半解像度の光の筋については [README](README.md) と [計測表](docs/QUALITY_BENCHMARK_20261031.md) を参照してください。光の筋・SSAOは深度プリパス後、通常の本描画の前に計算し、別視点では画面空間の結果を再利用しません。

## 1面の3本目のヒューズの2つの道

3本目のヒューズは、西棟（`Stage1WestWing`）と、暗証番号の扉の先の部屋（`Stage1KeypadDoor`・`Stage1HiddenRoomEvent`）の2か所に置いています。暗証番号の部屋のヒューズ（配置ファイルの `place Stage1KeypadRoomFuse`）は、`StageScene::UpdateThirdFuseRoutes` が、3本目を探す段階（ヒューズ2本・ループのドアを2回通った・電力はまだ）になったときだけ出しています。先に出すと、2本目（左の倉庫）を取る前に2本そろってしまい、ヒューズの数で決めている進行と食い違うためです。どちらか一方を取ったら、もう一方を毎フレーム消しています（西棟が水を抜いた後にヒューズを出しても、次の判定で消える）。目的表示（`Stage1Objective` の `SelectThirdFuseText`）は、西棟の鍵を拾っていれば西棟の手順、暗証番号の扉が開いていればその部屋、どちらもまだなら2つの道を両方示します。

## タイトル画面の後ろの森

`TitleScene` は、森の場面と同じ `ForestScene::ApplyAtmosphere`・`LoadLayout`・`BuildWorld` で森の世界を作り、プレイヤーを入力を読まない状態（`Player::SetAttractMode`）にしています。カメラの位置と向きは、場面の表（始めと終わりの位置・向き・秒数）を smoothstep で補間して毎フレーム決め、場面が変わるときに `ForestEvents` を作り直して、街灯や窓の演出が毎回また起きるようにしています。題名のノイズの強さ（場面の切り替えと、ときどき来る短い乱れ）は、`Hud::DrawTitle` の文字のずれ・色のにじみ・横の帯と、`PostProcess::SetSignalInterference`・`SetPresenceStatic` の両方に使っています。タイトルが終わると、画面効果・視点の操作・画面の乱れを普段に戻します。

タイトルでも本描画の全部の段階（環境マップ・SSAO・光の筋）を動かすようになったため、前のシーンの状態が次のシーンに残らないよう、シーンを切り替えるときに、環境マップの次に撮る面（`ReflectionProbe::Invalidate`）と、光の筋の揺らぎの時間（`LightShafts::ResetTime`）を最初に戻しています。これで、タイトルを変えても1面の自動撮影の画像は前と同じになります（51枚の比較で確認）。

## 森から研究所へ

タイトルの1面は `SceneName::Forest` から始まります。`ForestRadio` が、プレイヤーの z が決めた位置を越え、かつ前の行が消えていたら次の行を流します。こうして、走っても行が重なったり飛んだりしないようにしています。文章は `StoryText.h` にまとめ、導入（`IntroScene`）と同じものを使っています。字幕は `Hud::DrawRadio` で、ゲーム中のHUDの後に描いています。研究所の玄関は、扉の見た目と当たり判定を配置ファイルの `LabDoor`（`Wall`）が持ち、その手前に見た目のない調べる対象 `LabEntrance` を置いています。森の場面にも `InteractionSystem` を持たせ、調べられたら `ForestScene` が扉の音と暗転を出します。`Game::ChangeScene` は森から導入へ切り替えるとき、`IntroScene` を目的画面から始めます。この経路では完全な暗転から黒い背景の目的画面につなぎます。森の背景と電波ノイズ・雑音は、タイトルから受信記録を始める経路で使います。1面に入ると `GameState::BeginRun` で記録を始めるので、森で歩いた時間はクリアの時間に含めません。

## 森の雨

`ForestScene::RainEnabled`（今は `true`）が `false` のときは、`BuildWorld` で `ForestRain` を作らず、地面の雨の輪も付けず、`Game::ChangeScene` で雨音も鳴らしません（`if constexpr` で切り替えている）。

雨の強さは `ForestScene::RainStrength`（今は0.35）を、`ForestRain::SetIntensity`・`Ground::SetRainIntensity`・雨音の音量（`0.35 + 0.65 × 強さ` 倍）に渡しています。`ForestRain::Draw` は、雨粒としぶきの数に強さを掛けた数だけ `DrawInstanced` し（雨粒の位置は番号ごとの乱数なので、前から決まった数だけ描いても偏らない）、落ちる速さと筋の長さに `0.78 + 0.22 × 強さ` を掛けています。1粒の濃さ（シェーダーの `Intensity`）は `0.6 + 0.4 × 強さ` にとどめ、弱さは主に粒の数で出しています。水たまりの雨の輪（`Ground::AddRainRipples`）は、波を計算している範囲（`WaterRippleSimulation::WorldSize` の一辺192）全体に雨粒を落とし、範囲の面積に合わせて1秒の粒の数を増やしています（以前はカメラから48までにしか落とさず、外側の水たまりに輪が出なかった）。1粒の輪の強さは雨の強さで変えず、弱い雨は輪の数だけを減らしています。

レンズのしぶきは、`WaterEffectSystem::TriggerFootstepRipple` が走っているときだけ（2.8秒に1回まで）`PostProcess::TriggerLensMoisture` を呼んで付けます。`PostProcess` は付くたびに乱数の種（`m_LensDropSeed`。黄金比ずつ進める）を変え、付いてからの秒数（`m_LensDropAge`）を数え、どちらも画面効果の定数バッファ（b0 の最後の2つ。以前の詰め物の場所）で `crtOverlayPS.hlsl` に渡しています。シェーダーは種から6粒の位置（画面の下から4割まで）・大きさ・速さを決め、秒数から流れた量と縦の伸びを計算し、粒の中は中心から逆向きに0.7倍した位置の景色を読んで、凸レンズのように逆さに映しています。負荷の比較用の `SIGNAL_LOST_DISABLE_FOREST_RAIN=1` は、`SetIntensity` で上書きされないよう別の印（`m_DisabledByEnvironment`）で持っています。

`ForestScene::BuildWorld` の最後に `ForestRain` を作っています。加算合成で深度を書かないので、ほかの物を全部描いた後に描く必要があり、物は作った順に描かれるため、最後に作っています。`shader/rainVS.hlsl` は `SV_InstanceID` が雨粒の数より小さければ雨粒の筋、それ以降なら地面のしぶきの四角形を作ります。描かない雨粒（地面より下、屋根の下、目のすぐ前）は、クリップ空間の外の点にして捨てています。屋根は、当たり判定があり、地面から15以上浮いた、上から見て広い `Wall` を2秒ごとに集め直して、最大8個を定数バッファで渡しています。`shader/rainPS.hlsl` は、懐中電灯の円錐（`GetFlashlightBeamProfile`・影の地図）と翼の光、全部の点光源を足しています。雨粒は画面の一部にしかないので、タイルごとのリストではなく点光源の一覧を直接回しています。水たまりの輪は `Ground::AddRainRipples` が、カメラのまわりに落とした雨粒のうち水たまりに入ったものだけを `WaterRippleSimulation::AddImpulse` に渡しています（1フレーム12個まで。足音の分を残している）。雨音は、シーンを作った後に前の環境音を止める `Game::ChangeScene` で鳴らしています。同じ理由で、導入の無線の雑音もここで鳴らすように直しました（`IntroScene` の中で鳴らすと、直後に止められていた）。

## 1面の前の導入

タイトルの後ろの森の作り方とカメラの動きは `ForestBackdrop` にまとめ、`TitleScene`（場面の表を繰り返す）と `IntroScene`（研究所へ近づく1つの場面を1回だけ）の両方で使っています。`ForestBackdrop::Uninit` が、森の画面効果・画面の乱れ・視点の操作を普段に戻します。`IntroScene` は経過時間だけで、見出し → 受信記録の4行 → 「通信途絶」 → 目的の画面、と進めます。受信記録は、UTF-8 の文字の数で区切って1文字ずつ出しています。表示は `Hud::IntroView` に詰めて `Hud::DrawIntro` に渡しています。行の始まりと「通信途絶」の瞬間に電波の乱れを強め、その強さを `PostProcess::SetSignalInterference`・`SetPresenceStatic` と文字のずれの両方に使っています。目的の画面で Enter を押すと `SceneName::Stage` へ切り替えます（押し間違えないよう0.6秒待つ）。

## 森の入口の演出

`ForestScene::Update` が毎フレーム `ForestEvents::Update` にプレイヤーの足元を渡しています。`ForestEvents` は配置ファイルから作った小物（`PropLayoutFile::SpawnedProps`）を名前で探し、街灯（`PathLampBulb`・`PathLampBulb_1`）・研究所の2階の窓（`LabWindow2F4`）・玄関の灯り（`LabPorchLight`）の明るさを `Wall::SetEmissionScale` で変えています。倍率は `Wall::UploadMaterial` で光る色に、`Wall::CollectPointLights` で点光源の強さに掛けるので、面の光り方と周りの照らし方が一緒に変わります（値が変わったときだけマテリアルを送り直します）。ちらつきは時間だけから決まる式（不安定な時間の中だけ、1秒に14回、途切れるかを乱数で決める）なので、自動撮影では毎回同じ結果になります。窓が消えた瞬間の恐怖の強さを返し、`ForestScene` が `PostProcess::SetFearVision` へ渡しています。

草むらを通る「見えない何か」は、`ForestEvents` が足元の位置と強さを決め、`ForestScene` が `ForestGrass::SetUnseenWalker` で草へ渡しています。`grassCS.hlsl` は、プレイヤーと同じように目標の葉先をずらすほか、踏まれた倒れ具合を `GrassState.Tip.w`、倒れる向きを `GrassState.Velocity.w`（どちらも使っていなかった値）に覚え、毎秒0.2ずつ戻しています。バッファの大きさは変えずに、通った跡を残せます。

## 森の草の流れ

`ForestScene` は配置を読んで木・岩を作った後、`ForestGrass` を作ります。配置を読み直したら `Rebuild()` を予約し、次の `RunGpuSimulation()` で根元を植え直します。根元は不変のStructuredBuffer、葉先・速度はSRV/UAVのStructuredBufferです。CSとVSの読み書きが重ならないよう、それぞれ処理後にスロットを外します。

`grassCS.hlsl` は流れるノイズの風とプレイヤーから離れる力を目標とし、ばね・減衰を半陰的オイラー法の2分割で進めます。`grassVS.hlsl` は曲線の制御点を順にlerpし、位置と接線から葉の帯を作ります。`grassPS.hlsl` は葉の裏にも届く照明、懐中電灯の影、弱い濡れた光沢を加え、既存の霧・色調を適用します。

ユーザー提供の「Compute Shader 実践 Part.9：草レンダリング」を確認しました。p.29〜32の状態バッファ→CSで揺らす→ベジェ曲線→VSで形を作る構成を使い、森固有の配置除外・区画カリング・LODを組み合わせています。資料から変更した点と未実装の発展項目は [対応表](docs/FOREST_GRASS_PART9.md) に記録しています。

`Shader::Create` に入力レイアウトなしの指定を追加しました。既存のメッシュは従来の指定を既定とし、キャッシュキーも区別します。草はメイン視点にだけ描き、影を落とすメッシュや反射用の形状は持ちません。

`grassVS.hlsl` は、ビュー行列の回転の逆（転置）でカメラの前と上の向きをワールドへ戻し、見下ろす量に応じて葉先をカメラの上の向き（水平にした向き）へ倒しています（授業資料 p.23）。LOD の段階（1〜3）と色の表示の有無、曲げの強さは区画ごとの定数バッファ（b9、48バイト）で渡し、技術デモ（`TechDemo::GetGrassLodMode`・`UsesGrassCameraBend`）で切り替えます。

草を植えるとき、当たり判定のある物の足元に加えて、枯れた茂み（`forest_brush`）の枝の内側と、見えている水たまりの水面（`Ground::IsNearVisiblePuddle`。水たまりのメッシュと同じ楕円で、足元の判定 `IsInsidePuddle` より広い）を避けています。森の空は `SkyBackdrop`（`shader/skyBackdropVS.hlsl`・`skyBackdropPS.hlsl`）が、射影行列とビュー行列から画素ごとの視線の向きを戻し、描画の奥の端までの霧を重ねています。最初に作るObjectなので最初に描き、深度の判定（LESS_EQUAL）で手前の物の画素は残ります。

## 森の岩と樹皮の材質

岩は配置ファイルの `model forest_rock` を付けた `Wall` です。`Wall::SetVisualModel` が岩のモデルのときだけシェーダーを `rockPS.hlsl` に替え（配置を読み直して岩でなくなったら `litTexturePS.hlsl` に戻す。どの材質かは `Wall::NaturalSurface` で覚えている）、`rockPS.hlsl` は `ROCK_SURFACE` を付けて `litTexturePS.hlsl` を読み込みます。`litTexturePS` の「テクスチャがなく光らない面」の分岐の中で、`ROCK_SURFACE` のときは壁の汚れ・古さ・金属の錆の代わりに `GetRockSurface`（`rockSurface.hlsli`）の色・粗さ・濡れ具合・高さを使い、高さは壁と同じ `ApplyHeightToNormal` で法線にしています。照明・影・霧・PBR の計算は壁と共通です。模様はワールドの位置だけから決まるので、岩を動かしても大きさを変えても模様の細かさは変わらず、配置ファイルの色の `rough` がそのまま粗さの元になります。

枯れ木・倒木・枯れた茂み（`forest_tree_dead`・`forest_tree_fork`・`forest_fallen_log`・`forest_brush`）は、同じ仕組みで `barkPS.hlsl`（`BARK_SURFACE`、`barkSurface.hlsli`）に替えています。樹皮の筋は向きが大事なので、`Wall::RebuildVisualModel` がモデルを箱の向きへ回すときに木目の向き（長い向きへ回すモデルはモデルの x、それ以外は上）も同じ行列で回して `m_GrainAxis` に覚え、`Wall::UploadMaterial` が箱の回転を当ててワールドの向きにし、マテリアルの環境の色（`Ambient`。この面では使っていない値）に入れて渡します。シェーダーは木目の向きを y にした座標へ回し、幹の横を向いた2つの投影面で溝と板を求め、面の向きで混ぜます。木目の向きは物ごとに1つなので、斜めに伸びる枝では筋が枝に正確には沿いませんが、枝は細く遠いので目立ちません。

研究所の外壁（配置ファイルで `facade` を付けた箱）は、`PropLayoutFile` が `Wall::SetFacadeSurface` で印を付け、続く `Wall::SetVisualModel` がモデルのない箱でその印があれば `facadePS.hlsl`（`FACADE_SURFACE`、`facadeSurface.hlsli`）に替えます（岩・樹皮と同じ `NaturalSurface` の仕組みに `Facade` を足した）。外壁の模様は、縦の面では（横の位置, 地面からの高さ）、上を向いた面では（x, z）を座標にして計算し、雨だれは各階の窓の下の水切りの高さ（床から19）と屋上の縁（地面から150）から下へ伸ばしています。

雨で濡れた岩と樹皮は、`litTexturePS.hlsl` が岩・樹皮の材質を受け取った後に `ApplyForestRain`（`shader/forestWetness.hlsli`）を通し、色・濡れ具合と、水の筋の分の高さを足しています（粗さは `MakePbrSurface` が濡れ具合から下げる）。雨の強さと水の筋の時間は、b7（`DEBUG_VIEW_BUFFER`）の使っていなかった詰め物の場所に `RainWetness`・`RainTime` として入れ、`ForestRain::Update` が `Renderer::SetRainWetness` で毎フレーム送り、`ForestRain::Uninit` で0に戻します（森を出たら濡らさない）。濡らす強さは、雨の強さ0で0.5、1で1です（弱い雨でも、降り続いた森の表面は十分濡れているため）。

## シェーダーの事前コンパイルとビルドの速さ

ビルドの後（PostBuildEvent）に、実行ファイルを `--compile-shaders <shaderフォルダ>` で起動し、`Renderer::PrecompileShaders` が .hlsl を .cso へコンパイルします。名前の最後（VS・PS・CS）か頭（PS_）からシェーダーモデルを決め、どれにも当てはまらない物（common.hlsl）は include 用として飛ばします。

.cso が古いかどうかは `GetShaderSourceTime` で決めます。シェーダーのファイルの行の頭の `#include "名前"` をたどり（入れ子も含む。読む側と同じフォルダから探す。`D3DCompileFromFile` の `D3D_COMPILE_STANDARD_FILE_INCLUDE` と同じ）、自分と読んでいるファイルのうち一番新しい更新時刻と .cso の時刻を比べます。調べた結果はファイルごとに覚え、多くのシェーダーが読む .hlsli を何度も読み直さないようにしています。.hlsl がないとき（配布用のフォルダ）は一番古い時刻として扱い、.cso をそのまま使います。ゲームの起動時（`Renderer::CompileShader`）も同じ判定を使うので、ビルドで作り直さなかった .cso を、起動時に古いと判定することはありません。

作り直すシェーダーは、`std::for_each(std::execution::par, …)` で CPU のコアに分けて同時にコンパイルします。1つ分の処理（`CompileShaderFile`）は共有の変数や画面に触らず、エラーの文は1つ分ずつの入れ物に入れ、全部終わってからまとめてビルドの出力と `shader-compile-errors.txt` に出します。

C++ は、プロジェクトの中の `.cpp` を `/MP` で並べてコンパイルし、Release のリンク時の最適化は `/LTCG:INCREMENTAL`（`UseFastLinkTimeCodeGeneration`）にしています。
