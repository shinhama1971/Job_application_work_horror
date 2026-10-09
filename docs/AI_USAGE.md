# AIの利用箇所

この作品では、開発の途中から生成AIのコーディング支援ツールを使っています。
応募要項の「AIを利用したアセット・コードが含まれる場合は、利用箇所を具体的に明記」に従い、
どこにAIを使い、どこを自分で作ったかをここにまとめます。

> **【作者が記入・確認する欄】** と書かれた箇所は、AIの作業記録からは判断できないため、
> 作者本人が事実を確認して書き換えてください。提出前にこの注意書きごと削除します。

## 使用したAI

| 項目 | 内容 |
|---|---|
| ツール | Claude Code（Anthropic） |
| 使用期間 | 2026年9月24日〜 |
| 使い方 | 作者が内容を指示し、AIがコードの読解・実装・修正・ビルド確認を行う。変更はすべて作者が確認し、コミット・push・マージは作者が行う（AIはコミットしない運用） |
| それ以前（2026年8月〜9月19日） | 【作者が記入・確認する欄】他のAIツール（ChatGPT・Copilotなど）を使ったかどうか |

## AIが実装・変更した部分（2026年9月24日以降）

「AIの関与」の意味:
- **実装**: AIが新しくコードを書いた
- **修正**: 作者が作った既存のコードを、AIが直した・整理した

### ゲームの機能

| 機能 | 主なファイル | AIの関与 | 作者の関与【作者が記入・確認する欄】 |
|---|---|---|---|
| 監視カメラ4台と巡回型の異常確認（1面） | `StageSurveillanceCameras.h`, `SurveillancePatrol.h`, `StageSurveillanceController.*`, `shader/surveillanceFeedPS.hlsl` | 実装 | 遊びの内容の発案（監視カメラ・違和感を増やしたい） |
| 2面の周回中は歩くだけにする変更と、目的表示なしの設定 | `Stage2Scene*.cpp`, `GameSettings.*`, `PauseMenu.*`, `Hud*.cpp` | 実装 | |
| 2面の「背後の気配」 | `BehindPresence.h`, `Stage2SceneHorror.cpp` | 実装 | |
| 2面の1周目・2周目の異変を毎回ランダムな組み合わせにする（肖像画を「見つめ続ける」異変に変更、立体音響で出どころを探す「壁の向こうのノック」異変を追加） | `Stage2AnomalyPlan.h`, `PortraitAnomaly.h`, `ClockAnomaly.h`, `Stage2Scene*.cpp`, `Stage2Objective.*` | 実装 | 方針の選択（ランダム化） |
| 2面の隠れられるロッカー（隠れている間の視界・操作、影が見失って去る／見られて捕まる） | `Locker.*`, `Player.*`, `Camera.h`, `Stage2ScenePuzzles.cpp`, `HudScreens.cpp` | 実装 | 方針の選択（隠れる場所） |
| 1面・2面の心拍音・呼吸音（危険度に合わせて速く大きくなる、隠れている間は息を殺す、心拍に合わせた振動）と、その音の合成 | `TensionPulse.h`, `TensionPulseFeedback.h`, `Stage2SceneHorror.cpp`, `StageSceneEvents.cpp`, `sound.*`, `tools/generate-heartbeat-breath.ps1` | 実装 | 方針の選択（心拍音・呼吸音） |
| 作業報告用の自動撮影モード（自動で見て回り、動画とスクリーンショットを保存） | `CaptureMode.*`, `tools/capture-daily.ps1`, `Application.cpp`, `Renderer.cpp` | 実装 | 方針の選択（作業報告の素材を自動で集める） |
| 処理の重さの計測モード（ライトON/OFFのGPU時間の比較）と、高性能なGPUを選んで描画する修正 | `CaptureMode.*`, `Renderer.cpp`, `main.cpp` | 計測・原因の特定・実装 | 不具合の報告（ライトを点けるとFPSが下がり、カメラがかくかくする） |
| HUDの日本語の文字の不具合修正（小さい字「ュ」や「ー」が上に寄る、細い線が消えて字が崩れる） | `Hud.*`（`AddText`） | 原因の特定・修正 | |
| 描画解像度の設定（内蔵GPUでは自動で縮小）と、画面の拡大率への対応（HUDを画面の大きさに合わせて拡大縮小） | `Application.*`, `Renderer.*`, `RendererState.cpp`, `GameSettings.*`, `PauseMenu.*`, `Hud.*`, `HudScreens.cpp`, `Camera.cpp` | 実装 | 方針の選択 |
| Compute Shaderによるタイルベースライティング | `TiledLighting.*`, `shader/tiledLightCullingCS.hlsl`, `shader/common.hlsl` | 実装（授業資料の内容を元に、作者の依頼で組み込み） | 授業資料の提供 |
| 深度プリパス（Zプリパス）と、タイルの一番奥の深度による光源の絞り込み。計測モードで地点ごとの画面を保存し、導入前後を画素単位で比較 | `Object.h`, `Wall.*`, `Ground.*`, `Door.*`, `GameRendering.cpp`, `TiledLighting.*`, `shader/tiledLightCullingCS.hlsl`, `Renderer.*`, `CaptureMode.cpp` | 計測・実装・見た目の比較 | 方針の選択（深度の範囲で光源を絞る） |
| Compute Shaderによる自動露出（画面の明るさを測り、目が暗さ・明るさに慣れる） | `PostProcess.*`, `FullScreenQuad.*`, `shader/autoExposureCS.hlsl`, `shader/exposurePS.hlsl`, `DebugUI.*`, `Game.cpp` | 実装 | 方針の選択（自動露出） |
| X3DAudioによる立体音響（壁越しの音をこもらせる処理を含む） | `sound.*`, `Game.cpp` | 実装 | |
| 1面の姿の見えない物音（天井裏の足音・配管の音・遠くの扉） | `Stage1AmbientSounds.*` | 実装 | 1面の演出を増やしたいという発案 |
| 1面の懐中電灯で照らすと浮かぶ壁の文字（目を離すと書き換わる文字を含む） | `FlashlightWriting.*`, `Stage1WallWritings.*`, `shader/flashlightRevealPS.hlsl` | 実装 | |
| 1面の壁・床・天井の古さ（コンクリートの継ぎ目・気泡の穴・ひび・錆の垂れ跡・水位線・カビをシェーダーで計算）と部屋の角の暗がり（壁の形から計算する簡易AO）、面ごと・壁ごとに有効にする仕組み | `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/roomOcclusion.hlsli`, `shader/surfaceDetail.hlsli`, `shader/common.hlsl`, `Stage1Layout.*`, `Renderer.*`, `RendererState.cpp`, `Wall.*`, `Stage1Layout.cpp`, `StageScene.cpp` | 実装 | 方針の選択（壁の凹凸と汚れ） |
| 1面の暗証番号の扉（番号はプレイごとにランダム） | `KeypadLock.h`, `Stage1KeypadDoor.*`, `HudScreens.cpp`（`DrawKeypad`） | 実装 | |
| 1面の隠し部屋の閉じ込めイベント（鍵探し・記録端末・ライトで追い払う影） | `Stage1HiddenRoomEvent.*`, `KeyItem.*`, `Stage1Layout.cpp` | 実装 | 方針の選択（隠し部屋を充実させる） |
| 1面の拡張：西棟（浸水した機械室・鍵で開く必須の区画・音だけの演出）、書類保管室の棚の迷路、崩れた資材置き場 | `Stage1WestWing.*`, `Stage1Layout.*`, `StageScene*.cpp`, `Stage1Objective.*`, `Ground.*`, `GroundWaterEffects.cpp`, `WaterEffectSystem.h`, `shader/wetFloorPS.hlsl` | 設計案（地図）・実装 | 方針の選択（区画の追加・西棟は必須・影は出さない） |
| リザルト画面の「今回の発見」（壁の文字・隠し部屋・2階の異変）、残された記録の総数を4へ修正 | `GameState.h`, `HudScreens.cpp`, `Hud.cpp`, `ResultScene.*` | 実装 | |
| 1面のヒューズの置き場所をプレイごとに部屋の中の候補からランダムに選ぶ | `Stage1Layout.cpp`, `StageSceneDraw.cpp` | 実装 | |

### 設計の整理・品質改善

| 内容 | 主なファイル | AIの関与 |
|---|---|---|
| コードレビューと、指摘した重大な不具合の修正 | 複数 | 修正 |
| ポーズメニューを `PauseMenu` クラスへ分離 | `PauseMenu.*`, `Game.*` | 修正 |
| 1面・2面の配置を `Stage1Layout` / `Stage2Layout` へ分離し、Objectの名前を1か所に集約 | `Stage1Layout.*`, `Stage2Layout.*`, `SceneLayoutBuilder.h` | 修正 |
| 目的表示の選択を純粋関数へ分離 | `Stage1Objective.*`, `Stage2Objective.*` | 修正 |
| 監視カメラ巡回の処理を `StageScene` から分離 | `StageSurveillanceController.*` | 修正 |
| Sceneの毎フレームの名前検索をやめ、初期化時に取得したポインタを使う | `StageScene*.cpp`, `Stage2Scene*.cpp` | 修正 |
| 警告レベルを /W4 に上げて警告を0にする、コードの細かな作法の整理 | 複数 | 修正 |
| セーブデータの保存先を `%LOCALAPPDATA%\SignalLost` に変更 | `GamePersistence.cpp`, `GameSettings.cpp`, `utility.cpp` | 修正 |
| 2面のやり直し時の進行値の扱いの修正 | `GameState.h`, `Game.cpp` | 修正 |
| ソースコード全体のコメントの見直し（誤った説明の修正・不足の追加） | 複数 | 修正 |
| 未使用のコード・素材の削除、`TestPipe` を `PipeProp` に改名 | 複数 | 修正 |
| README・ARCHITECTURE.md の更新（機能追加・設計変更に合わせた説明の追記） | `README.md`, `ARCHITECTURE.md` | 修正 |

### ビルド環境

| 内容 | AIの関与 |
|---|---|
| Assimp 5.2.5 を `external/assimp` に同梱し、Releaseビルドを通す | 実装 |
| DirectXTKのSimpleMathを `external/directxtk` に同梱し、`C:\directxtk` への依存をなくす | 実装 |

### 取り消した作業

| 内容 | 状態 |
|---|---|
| GPUパーティクルによる空気中の埃 | AIが実装したが、作者の判断で取り消し（現在のコードには含まれない） |
| 単体テストプロジェクト | AIが追加したが、作者の判断で削除 |

## アセット

| アセット | 作り方 | AIの関与 |
|---|---|---|
| `assets/Audio/pipe_knock.wav`（配管を叩く音） | `tools/generate-pipe-knock.ps1` で数式から合成 | 合成スクリプトをAIが作成（画像・音声の生成AIは不使用） |
| `assets/texture/writing_*.png`（壁の文字） | `tools/generate-wall-writings.ps1` でフォント（HG行書体）から描画し、垂れ・かすれを加工 | 生成スクリプトをAIが作成（画像生成AIは不使用） |
| `assets/Audio` のその他の効果音・環境音 | 【作者が記入・確認する欄】入手元（自作・フリー素材サイト名・ライセンス） | 【作者が記入・確認する欄】 |
| `assets/model`（配管・ゴルフボールなど） | 【作者が記入・確認する欄】入手元とライセンス | 【作者が記入・確認する欄】 |

## 自分で作った部分【作者が記入・確認する欄】

2026年9月19日（Claude Codeを使い始める前の最後のコミット）の時点で、次の仕組みはすでにありました。
自分で作ったもの、授業で作ったものを元にしたもの、AIを使ったものを区別して書きます。

- DirectX 11の描画基盤（`Renderer`、シェーダー、テクスチャ、モデル読み込み）
- 懐中電灯のライティングとシャドウマップ
- 濡れた床と水たまりの平面反射（Planar Reflection）
- ポストプロセス（ブルーム・ビネット・フィルムグレインなど）
- Object・Scene・ObjectManagerの設計（`unique_ptr` による所有、遅延追加・遅延削除）
- 1面・2面の基本の流れ（ヒューズ探索、電力復旧、ループ廊下、信号盤パズル、追跡）
- 人影（`ShadowMan`）、扉、配電盤、アイテム、HUD

## AIのコードをどう確認したか【作者が記入・確認する欄】

例:
- 変更ごとに Debug / Release の両方でビルドし、警告0を確認した
- 実際に遊んで動作を確認し、問題があれば修正を依頼した
- 採用しなかった変更（埃のパーティクルなど）は取り消した
- 仕組みを説明できるよう、コードを読んで理解した部分（面接で説明できる範囲）
