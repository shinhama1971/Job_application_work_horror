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
| Compute Shaderによるタイルベースライティング | `TiledLighting.*`, `shader/tiledLightCullingCS.hlsl`, `shader/common.hlsl` | 実装（授業資料の内容を元に、作者の依頼で組み込み） | 授業資料の提供 |
| X3DAudioによる立体音響（壁越しの音をこもらせる処理を含む） | `sound.*`, `Game.cpp` | 実装 | |
| 1面の姿の見えない物音（天井裏の足音・配管の音・遠くの扉） | `Stage1AmbientSounds.*` | 実装 | 1面の演出を増やしたいという発案 |
| 1面の懐中電灯で照らすと浮かぶ壁の文字（目を離すと書き換わる文字を含む） | `FlashlightWriting.*`, `Stage1WallWritings.*`, `shader/flashlightRevealPS.hlsl` | 実装 | |
| 1面の暗証番号の扉（番号はプレイごとにランダム） | `KeypadLock.h`, `Stage1KeypadDoor.*`, `HudScreens.cpp`（`DrawKeypad`） | 実装 | |
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
