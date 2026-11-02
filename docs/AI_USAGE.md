# AIの利用箇所

この作品では、開発の途中から生成AIのコーディング支援ツールを使っています。
応募要項の「AIを利用したアセット・コードが含まれる場合は、利用箇所を具体的に明記」に従い、
どこにAIを使い、どこを自分で作ったかをここにまとめます。

> **【作者が記入・確認する欄】** と書かれた箇所は、AIの作業記録からは判断できないため、
> 作者本人が事実を確認して書き換えてください。提出前にこの注意書きごと削除します。

## 使用したAI

| 項目 | 内容 |
|---|---|
| ツール | Claude Code（Anthropic）、Codex（OpenAI） |
| Codexの利用範囲 | 2026年10月31日：見えない影の濡れた足跡、4周目以降のドア、光の筋の半解像度化。後始末・画質設定・撮影・技術デモ同期と確認も支援 |
| 使用期間 | 2026年9月24日〜 |
| 使い方 | 作者が内容を指示し、AIがコードの読解・実装・修正・ビルド確認を行う。変更はすべて作者が確認し、コミット・push・マージは作者が行う（AIはコミットしない運用） |
| それ以前（2026年8月〜9月19日） | 【作者が記入・確認する欄】他のAIツール（ChatGPT・Copilotなど）を使ったかどうか |

## AIが実装・変更した部分（2026年9月24日以降）

「AIの関与」の意味:
- **実装**: AIが新しくコードを書いた
- **修正**: 作者が作った既存のコードを、AIが直した・整理した

### Codexが担当した追加・調整（2026年10月31日）

| 内容 | 主なファイル | 関与 |
|---|---|---|
| 見えない影が水を踏んだ後の裸足の跡 | Stage2Scene.h、Stage2Scene.cpp、Stage2SceneHorror.cpp | Codexが実装 |
| 4周目以降、ひとりでに開き、照らすと閉じるドア | Door.*、StageSceneEvents.cpp | Codexが実装 |
| 光の筋の半解像度計算と深度を使った拡大 | LightShafts.h、shader/lightShaftsCS.hlsl、horrorMist.hlsli、GameRendering.cpp、TiledLighting.* | Codexが実装・計測 |
| プロジェクト登録・改行、画質1〜3、撮影用の一時設定、デモ同期、README | vcxproj/filters、Game.*、CaptureMode.*、同期スクリプト、App.*、README | Codexが修正・確認。結果は作業報告参照 |

作者が3つの追加内容と作業の優先順位を指定しました。今回の作業はコミット・pushせず、作者の差分確認を待つ運用です。

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
| 懐中電灯の影を手元から作る変更と、PCSS（影の縁を距離で柔らかくする）、濡れた床の反射のむら、西棟の水面の光の揺らぎ（コースティクス）。撮影モードに書類保管室を追加 | `FlashlightRig.h`, `ShadowMap.cpp`, `shader/flashlightShadow.hlsli`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `shader/litTexturePS.hlsl`, `Renderer.*`, `RendererState.cpp`, `Ground.cpp`, `CaptureMode.cpp` | 実装・見た目の比較 | 方針の選択（1面のグラフィックの追加） |
| 1面の書類保管室の「照らすと止まる影」 | `Stage1LightStalker.h`, `ShadowMan.*`, `Stage1Layout.*`, `StageScene*.cpp`, `StageScene.h`, `Stage1Objective.*` | 実装 | 方針の選択（遊びの追加） |
| X3DAudioによる立体音響（壁越しの音をこもらせる処理を含む） | `sound.*`, `Game.cpp` | 実装 | |
| 1面の姿の見えない物音（天井裏の足音・配管の音・遠くの扉） | `Stage1AmbientSounds.*` | 実装 | 1面の演出を増やしたいという発案 |
| 1面の懐中電灯で照らすと浮かぶ壁の文字（目を離すと書き換わる文字を含む） | `FlashlightWriting.*`, `Stage1WallWritings.*`, `shader/flashlightRevealPS.hlsl` | 実装 | |
| 1面の壁・床・天井の古さ（コンクリートの継ぎ目・気泡の穴・ひび・錆の垂れ跡・水位線・カビをシェーダーで計算）と部屋の角の暗がり（壁の形から計算する簡易AO）、面ごと・壁ごとに有効にする仕組み | `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/roomOcclusion.hlsli`, `shader/surfaceDetail.hlsli`, `shader/common.hlsl`, `Stage1Layout.*`, `Renderer.*`, `RendererState.cpp`, `Wall.*`, `Stage1Layout.cpp`, `StageScene.cpp` | 実装 | 方針の選択（壁の凹凸と汚れ） |
| 1面の暗証番号の扉（番号はプレイごとにランダム） | `KeypadLock.h`, `Stage1KeypadDoor.*`, `HudScreens.cpp`（`DrawKeypad`） | 実装 | |
| 1面の隠し部屋の閉じ込めイベント（鍵探し・記録端末・ライトで追い払う影） | `Stage1HiddenRoomEvent.*`, `KeyItem.*`, `Stage1Layout.cpp` | 実装 | 方針の選択（隠し部屋を充実させる） |
| 1面の拡張：西棟（浸水した機械室・鍵で開く必須の区画・音だけの演出）、書類保管室の棚の迷路、崩れた資材置き場 | `Stage1WestWing.*`, `Stage1Layout.*`, `StageScene*.cpp`, `Stage1Objective.*`, `Ground.*`, `GroundWaterEffects.cpp`, `WaterEffectSystem.h`, `shader/wetFloorPS.hlsl` | 設計案（地図）・実装 | 方針の選択（区画の追加・西棟は必須・影は出さない） |
| リザルト画面の「今回の発見」（壁の文字・隠し部屋・2階の異変）、残された記録の総数を4へ修正 | `GameState.h`, `HudScreens.cpp`, `Hud.cpp`, `ResultScene.*` | 実装 | |
| 1面のヒューズの置き場所をプレイごとに部屋の中の候補からランダムに選ぶ | `Stage1Layout.cpp`, `StageSceneDraw.cpp` | 実装 | |
| 1面の南西の広間（技術デモの部屋を元にした寄り道の区画：柱・棚と机の一角・色の付いた警告灯・天井の照明・電池）。撮影モードに南西の広間を追加 | `Stage1Layout.cpp`, `CaptureMode.cpp`, `README.md` | 配置の設計・実装 | 方針の選択（技術デモの部屋を1面の西側に入れる） |
| 1面の西棟を技術デモの部屋の作り（柱・棚と机の一角・天井の照明・色の付いたランプ）に変え、排水バルブの遊びを追加（長押しで回すと水が奥へ引いていき、水の底のヒューズが見つかる）。撮影モードの西棟の場面を変更 | `DrainValve.*`, `Stage1WestWing.*`, `Stage1Layout.cpp`, `Stage1Objective.*`, `StageScene.*`, `StageSceneDraw.cpp`, `StageSceneEvents.cpp`, `CaptureMode.cpp`, `README.md`, `ARCHITECTURE.md` | 遊びの案・配置の設計・実装 | 方針の選択（西棟をデモの部屋の作りに・新しい遊びは排水バルブ） |
| 1面の、次に進む方向の天井照明を1つだけ切れかけの電球のように不規則にちかちかさせる演出（ぱちぱちと瞬く・ふっと消えて点き直す・間の長さを毎回変える。ほかの照明は落ち着いて点けたまま） | `CeilingLight.*`, `StageScene.*`, `Stage1Layout.*`, `README.md` | 実装 | 方針の選択（案内ではなく、進む方向の電気だけがちかちかする。ホラーゲーム「P.T.」を参考） |
| 1面の光の安全地帯（停電しても消えない暖かい電球を2か所。灯りの下で心拍・物音・画面のざらつきが静まり、ライトを消して休むと電池が少し戻る）。撮影モードに追加 | `Stage1SafeLights.*`, `Stage1Layout.*`, `StageScene*.cpp`, `StageScene.h`, `Stage1Objective.*`, `CaptureMode.cpp`, `README.md`, `ARCHITECTURE.md` | 配置の設計・実装 | 方針の選択（審査会の指摘「光の安全地帯を作る」に答える） |
| 水たまりの波のシミュレーション（Compute Shaderの2次元の波動方程式。足音と水滴で波が立ち、重なって岸で跳ね返り、床の反射を波打たせ、壁に光の輪を映す）。水たまりの形を `puddleMask.hlsli` に切り出して床と共通化。撮影モードに足音の波紋を追加 | `WaterRippleSimulation.*`, `shader/waterRippleCS.hlsl`, `shader/puddleMask.hlsli`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `Renderer.h`, `RendererState.cpp`, `Ground.*`, `GroundWaterEffects.cpp`, `WaterEffectSystem.h`, `CaptureMode.cpp`, `README.md`, `ARCHITECTURE.md` | 方式の設計・実装・見た目の調整 | 方針の選択（審査会の指摘「波紋のシミュレーション」「波打った反射を壁に映す」に答える） |
| 1面の壁のホラーらしい跡（血の手形と引きずった跡・引っかき傷・血の垂れ跡をシェーダーの距離関数で描き、懐中電灯で照らすと赤黒く浮かぶ）。マスの乱数の丸めの境目で模様が横縞になる不具合を、0.125ずらして直した。撮影モードに右の倉庫の手形を追加 | `shader/horrorMarks.hlsli`, `shader/litTexturePS.hlsl`, `CaptureMode.cpp`, `README.md`, `ARCHITECTURE.md` | 形と置き方の設計・実装・不具合の原因の調査 | 方針の選択（審査会の指摘「ホラーに特化したシェーダー」「おどろおどろしい物」に答える） |
| 水たまりの波を控えめに調整（波の大きさ約6割・消えるまでの時間を短く）。波の計算を描画の最初へ移してGPU時間を「水たまりの波」の段階として測り、ゲームの技術デモ（F2）に「水たまりの波」を追加。スタミナを調整（走れる時間を約10秒に延ばし、歩きながらでは戻りにくくした）。READMEに「試したことと直したこと（経緯）」を追加 | `WaterRippleSimulation.cpp`, `GpuTimer.h`, `Object.h`, `Ground.*`, `GameRendering.cpp`, `TechDemo.*`, `DebugUI.cpp`, `CaptureMode.cpp`, `PlayerMovement.h`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `README.md`, `ARCHITECTURE.md` | 実装・数値の調整・経緯の文章 | 方針の選択（波紋が強すぎるとの判断、続きの項目の選択） |
| READMEの冒頭に「ゲームの画面」と「ホラーのための描画（見てほしい順）」を画像付きで追加。自動撮影の画像を資料用に明るくする `tools/make-readme-images.ps1` を追加 | `README.md`, `docs/images/*.jpg`, `Job_application_work_horror/tools/make-readme-images.ps1` | 構成案・文章・画像の加工 | 方針の選択（審査会の指摘「ゲームを先に、その後で技術」「画像を明るく」「見る側の優先度」に答える） |
| 水たまりの波が、床に立った柱・棚・机の脚でも跳ね返るようにした（当たり判定のある床の箱を上から見た長方形で、カメラに近い順に最大32個をCompute Shaderへ渡す） | `WaterRippleSimulation.*`, `shader/waterRippleCS.hlsl`, `Ground.*`, `Wall.h`, `README.md`, `ARCHITECTURE.md` | 実装 | |
| 水たまりの反射の方式の比較（平面反射・キューブマップ・SSR）を、ゲームの技術デモ（F2）と別リポジトリの技術デモに追加。キューブマップは1フレームに1面ずつ撮り直し、SSRは深度プリパスの深度と前のフレームの画面を使う | `ReflectionProbe.*`, `ScreenSpaceReflection.*`, `Renderer.h`, `RendererState.cpp`, `PostProcess.h`, `GameRendering.cpp`, `Game.*`, `Ground.cpp`, `TechDemo.*`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `README.md`, `ARCHITECTURE.md` | 方式の設計・実装・比較画像と時間の計測 | 方針の選択（審査会の指摘「反射の方式を比べられるように置いておく」に答える。普段の方式は平面反射のまま） |
| 技術デモの比較動画（`--video <ファイル.mp4>` で全部の方式を3秒ずつ映してMP4に書き出す）。ゲームの撮影モードの動画の書き出しを `VideoWriter` に切り出し、ゲームと技術デモで共通にした | `VideoWriter.*`, `CaptureMode.cpp`, 技術デモの `App.*`・`main.cpp`・`shims/CaptureMode.*`・`README.md` | 実装 | 方針の選択（審査会の指摘「動画を用意する」に答える） |
| 2面に新しい異変「血の手形」を追加（照らすと奥の反対側の壁に次の手形が現れ、4つ目までたどる。1面の血の跡と同じ形の式を壁のシェーダーで描く。異変の候補は5つから2つ） | `HandprintAnomaly.h`, `Stage2AnomalyPlan.h`, `Stage2Scene.*`, `Stage2SceneHorror.cpp`, `Stage2SceneDraw.cpp`, `Stage2ScenePuzzles.cpp`, `Stage2Objective.*`, `Renderer.h`, `RendererState.cpp`, `shader/litTexturePS.hlsl`, `shader/waterCaustics.hlsli`, `README.md`, `ARCHITECTURE.md` | 遊びの設計・実装 | 方針の選択（2面の血の跡は、背景ではなく新しい異変にする） |
| 1面の小物の配置をテキストファイル（`assets/layout/stage1_props.txt`）へ移し、Debug構成で `F5` で読み直せる仕組みを追加（書き間違いは行番号付きで画面に出す）。移す前と後で撮影モードの画像が同じことを確認 | `PropLayoutFile.*`, `Stage1Layout.*`, `StageScene.*`, `StageSceneDraw.cpp`, `Stage1Objective.*`, `assets/layout/stage1_props.txt`, `README.md`, `ARCHITECTURE.md` | 書式の設計・実装・配置の移し替え | 方針の選択（審査会の指摘「ツールを使って効率を上げる」に答える） |
| Debug構成のデバッグ画面（F1）に配置エディタを追加（景色の中の小物をクリックで選び、位置・大きさ・色・当たり判定・影・光を変え、複製・削除し、変えた行だけを配置のファイルへ書き戻す）。書き戻しは、変えなければファイルが1バイトも変わらないことを確認 | `PropLayoutEditor.*`, `PropLayoutFile.*`, `StageScene.*`, `StageSceneDraw.cpp`, `README.md`, `ARCHITECTURE.md` | 実装・書き戻しの確認 | 方針の選択（ファイル化の続きとして、画面を見ながら置けるようにする） |
| 2面の見た目を1面とそろえた（床を濡れた床 `Ground` に替えて水たまりの反射と波を出し、水たまりを廊下の決まった3か所に置ける仕組みを追加。壁と天井に古さと角の暗がりを付け、2面では決まった血の跡・傷を描かない設定を追加。肖像画に額縁を追加。明るい壁に出ていたレンガのような格子を、壁のざらつきのノイズを替えて解消） | `Stage2Layout.*`, `Stage2Scene.cpp`, `Ground.*`, `GroundWaterEffects.cpp`, `WaterEffectSystem.h`, `GameRendering.cpp`, `Renderer.h`, `RendererState.cpp`, `shader/puddleMask.hlsli`, `shader/common.hlsl`, `shader/litTexturePS.hlsl`, 技術デモの `DemoScene.h`, `README.md`, `ARCHITECTURE.md`, `docs/images/*.jpg` | 原因の調査（撮影の比較）・実装 | 方針の選択（2面の見た目を1面とそん色ないようにする） |
| 2面に新しい異変「鏡」を追加（左の壁の古い鏡をのぞくと、すぐ後ろに人影が映り、振り返ると誰もいない）。水たまりの平面反射を任意の面へ一般化して壁の鏡を描き、鏡の中にだけ映る人影の仕組みを追加。古い鏡のシェーダー（銀の剥げ・くもり・照り返し）。デバッグ画面と自動撮影に鏡の異変を出す操作を追加 | `MirrorAnomaly.h`, `PlanarReflection.*`, `shader/mirrorPS.hlsl`, `GameRendering.cpp`, `Game.*`, `Object.h`, `Wall.h`, `Ground.h`, `ShadowMan.*`, `Renderer.*`, `GpuTimer.h`, `Scene.h`, `Stage2AnomalyPlan.h`, `Stage2Layout.*`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `Stage2Objective.*`, `DebugUI.cpp`, `CaptureMode.cpp`, `README.md`, `ARCHITECTURE.md` | 遊びの設計・描画の設計・実装 | 方針の選択（次の制作は「鏡の異変」） |
| タイトルで始める面（1面・2面）を選べるようにした（2面から始めたプレイはクリアしてもベスト記録に残さない）。自動撮影でタイトル画面を1枚撮るようにした。2面から直接始めても、1面から進んだときと同じ画面になることを撮影で確認 | `TitleScene.*`, `Hud.h`, `HudScreens.cpp`, `GameState.h`, `Game.cpp`, `CaptureMode.cpp`, `README.md` | 実装・確認 | 方針の選択（タイトルから1面と2面を選べるようにする） |
| 2面の肖像画を、シェーダーで描いた古い油絵の人物にした（閉じた目・ひび割れ・ニスの黄ばみ。照明は壁と同じシェーダーを読み込んで使う） | `shader/portraitPainting.hlsli`, `shader/portraitPS.hlsl`, `shader/litTexturePS.hlsl`, `Wall.h`, `Stage2Layout.cpp`, `README.md` | 絵の設計・実装 | 方針の選択（肖像画を絵にする） |
| 1面の「ふりかえるな」の文字が、振り返っていないのに書き換わる不具合を修正（横を見ただけで変わっていた。文字に背を向けたときだけ変えるようにした） | `Stage1WallWritings.*`, `Stage1Layout.cpp`, `StageSceneEvents.cpp`, `README.md` | 原因の調査・修正 | 不具合の指摘（振り返っていないのに文字が変わる） |
| 濡れた足跡（水たまりの後の自分の靴の跡と、2面の2周目以降に後をついて来る誰かの裸足の跡。照らすと止まる）、鏡に映る自分の姿（鏡の異変の周回では少し遅れて動く）、傷・印・偽の扉が焦げた縁から崩れて現れる・消える表現を追加。自動撮影に、崩れて浮かぶ印と濡れた足跡の場面を追加 | `WetFootprints.h`, `FootprintFollower.h`, `MirrorSelf.*`, `Ground.*`, `GroundWaterEffects.cpp`, `Wall.*`, `Renderer.h`, `MirrorAnomaly.h`, `Stage2Layout.*`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `CaptureMode.cpp`, `shader/wetFootprints.hlsli`, `shader/wetFloorPS.hlsl`, `shader/litTexturePS.hlsl`, `shader/common.hlsl`, 技術デモの `DemoScene.cpp`, `README.md`, `ARCHITECTURE.md` | 遊びと描画の設計・実装・撮影での確認 | 方針の選択（3・5・6を作る） |
| 肖像画の異変の目を、絵の前に浮かぶ赤い箱から、シェーダーで描いた目に替えた（見つめると開きかけ、異変で見開いて赤い瞳が見ている人を目で追う）。デバッグ画面と自動撮影に肖像画の異変を出す操作と場面を追加 | `shader/portraitPainting.hlsli`, `shader/litTexturePS.hlsl`, `Wall.*`, `Stage2Layout.*`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `Scene.h`, `DebugUI.cpp`, `CaptureMode.cpp`, `README.md` | 絵の目の設計・実装・撮影での確認 | 方針の選択（肖像画の目を絵の中で開かせる） |
| 時計の文字盤をシェーダーで描いた（ローマ数字・目盛り・針・ガラスのひび。箱の針を削除）、ロッカーで息を止める操作（影がロッカーの前まで探しに来る。息のゲージと聞かれる猶予）、懐中電灯の光の輪・くもりと電池の弱り（60%以下で暗く黄色く短く）、鏡に映る自分を円柱と楕円体の体にして歩くと手足を振るようにした、1面の西棟の水から後をついて来る誰かの足跡。自動撮影に電池が弱った場面を追加 | `shader/clockFace.hlsli`, `shader/clockPS.hlsl`, `shader/litTexturePS.hlsl`, `shader/flashlightLighting.hlsli`, `Wall.*`, `BreathHold.h`, `MirrorSelf.*`, `Player.cpp`, `Hud.h`, `HudScreens.cpp`, `Stage1Layout.*`, `StageScene.*`, `StageSceneEvents.cpp`, `Stage2Layout.*`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `CaptureMode.cpp`, `README.md` | 設計・実装・撮影での確認 | 方針の選択（候補1〜5を続けて作る） |
| 偽の扉の板をシェーダーで描いた木の扉にした（羽目板・木目・剥げた塗装・部屋番号の札・鍵穴）、天井の雨漏り（落ち続ける水滴と、広がっていく水たまり）、ロッカーの中で音が扉越しにこもり、探しに来た影の息づかいとロッカーに触れる音が聞こえるようにした、最後の追跡の山場（影の濡れた足跡、通り過ぎた照明が消える、肖像画の目と時計の針）。デバッグ画面と自動撮影に偽の扉と最後の追跡を出す操作と場面を追加 | `shader/doorPainting.hlsli`, `shader/doorPS.hlsl`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `Wall.*`, `WaterEffectSystem.h`, `GroundWaterEffects.cpp`, `Ground.*`, `Game.*`, `Stage1Layout.cpp`, `Stage2Layout.cpp`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `Scene.h`, `DebugUI.cpp`, `CaptureMode.cpp`, 技術デモの `DemoScene.cpp`, `README.md` | 設計・実装・撮影での確認 | 方針の選択（候補1〜4を続けて作る） |
| グラフィックをホラー寄りにした（共通の色調 `horrorGrade.hlsli`：暗い所の彩度を落として沈め、影を緑がかった暗い青・明るい所を黄ばんだ蛍光灯の色に。霧を闇の色に、環境光・電力復旧後の照明を弱め、懐中電灯の光の輪を狭め、周辺減光と暗い所の粒を強め、信号盤と記録の目印の光を抑えた）。READMEの画像を作り直した | `shader/horrorGrade.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/flashlightRevealPS.hlsl`, `shader/crtOverlayPS.hlsl`, `shader/signalPanelPS.hlsl`, `Player.cpp`, `CeilingLight.cpp`, `Stage2Layout.cpp`, `Stage2Scene.cpp`, `README.md`, `docs/images/*.jpg` | 色調と明るさの設計・撮影での比較 | 見た目の意見（グラフィックをもっとホラー寄りに） |
| 水たまりの波紋をゆっくりにした（波の式の速さを約半分、減衰を弱め、足音の輪・水滴の輪・浸水した床と雨漏りの波も遅く）。床の近くに溜まって流れる霧を追加（高さの霧の解析的な積分と流れるノイズ、懐中電灯の円錐の中だけ白く浮かぶ）、光の筋に煙のむらを追加。READMEの画像を作り直した | `WaterRippleSimulation.cpp`, `GroundWaterEffects.cpp`, `shader/horrorMist.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/volumetricFlashlightPS.hlsl`, `README.md`, `docs/images/*.jpg` | 速さと霧の設計・撮影での比較 | 見た目の意見（波紋が速すぎる、霧を入れてほしい） |
| サイレントヒルと P.T. を参考に見た目を調整（懐中電灯の縁が黄色くにじむ光の色、部屋全体に漂う霧と、電球の光を受けてかすむ霧、2面の廊下を暖かい白熱電球の色にして電圧の揺れを追加、水たまりの波を細い輪が何重にも広がる形に、暗い粒と明るい粒のフィルムの粒と画面の端の色ずれを常に重ねる）。READMEの画像を作り直した | `shader/flashlightLighting.hlsli`, `shader/horrorMist.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/crtOverlayPS.hlsl`, `CeilingLight.*`, `Stage2Layout.cpp`, `GroundWaterEffects.cpp`, `CaptureMode.cpp`, `README.md`, `docs/images/*.jpg` | 参考作品の見た目の分析・色と霧と粒の設計・撮影での比較 | 方針の選択（サイレントヒルと P.T. を参考にする） |
| 懐中電灯を消すとほとんど闇になるよう環境光を下げ、1面の通路と部屋の天井に照明を14灯追加（停電中も赤い非常灯として灯る）。非常灯を明るくし、暗い所でも光だまりの色が残るよう色調を緩めた。自動撮影に懐中電灯を消した場面を追加し、READMEの画像を作り直した | `Player.cpp`, `Stage1Layout.cpp`, `CeilingLight.cpp`, `shader/horrorGrade.hlsli`, `CaptureMode.cpp`, `README.md`, `docs/images/*.jpg` | 照明の配置と明るさの設計・撮影での確認（非常灯の光が色調で消えていた原因の調査） | 見た目の意見（ライトがないと普通に見える、1面の照明が少ない） |
| 影が近いと鳴る携帯ラジオの雑音（雑音を計算で合成するスクリプト、ループ音の強弱、画面の信号の乱れ）と、2面の廊下に吊るした揺れる裸電球（振り子・すきま風・異変や影の接近で大きく揺れる・電球の位置の点光源）を追加 | `RadioStatic.h`, `tools/generate-radio-static.ps1`, `assets/Audio/radio_static.wav`, `sound.*`, `Game.*`, `PostProcess.*`, `HangingBulb.*`, `StageScene.*`, `StageSceneEvents.cpp`, `Stage2Layout.*`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `README.md` | 音の合成と揺れの設計・実装・撮影での確認 | 方針の選択（サイレントヒルと P.T. を参考に、続きを作る） |
| 1面のループを、廊下の突き当たりのドアを開けると開始地点の奥の壁のドアへそのままつながる形（P.T.を参考）に変更し、同じ突き当たりのドアを電力の復旧と出口の送電の後に開けて通ると2面へ進むようにした（ドアの配置、開けた瞬間に立ち位置と向きを写す変換、裏側の通路、背後で閉まるドア、鍵の条件、目的の表示と案内の矢印、自動撮影の場面） | `Stage1Layout.*`, `StageScene.*`, `StageSceneEvents.cpp`, `StageSceneDraw.cpp`, `Stage1Objective.cpp`, `Stage1AmbientSounds.cpp`, `Door.h`, `Scene.h`, `CaptureMode.cpp`, `assets/layout/stage1_props.txt`, `README.md` | 設計・実装・撮影での確認 | 仕様の決定（最後にドアを置き、開けると最初の場所へ続く、電気をつけて開けると2面）、ドアを置く場所の指定 |
| 壁・扉・小物を物理ベースの材質（PBR）で描くようにした（GGX・Smith・Schlickの鏡面反射、金属・粗さ・濡れ具合、配置ファイルの色ごとの仕上げ、扉の真鍮の取っ手、環境マップによる映り込みと粗さに応じたぼかし、材質のデバッグ表示、GPU時間の計測項目） | `shader/pbrLighting.hlsli`, `shader/litTexturePS.hlsl`, `shader/common.hlsl`, `Renderer.h`, `Material.h`, `Wall.*`, `Door.*`, `ReflectionProbe.*`, `GameRendering.cpp`, `Game.*`, `GpuTimer.h`, `PropLayoutFile.*`, `PropLayoutEditor.cpp`, `assets/layout/stage1_props.txt`, `FuseBox.cpp`, `DrainValve.cpp`, `Locker.cpp`, `Item.cpp`, `KeyItem.cpp`, `PipeProp.cpp`, `Stage2Layout.cpp`, `DebugUI.cpp`, `CaptureMode.cpp`, `README.md` | 設計・実装・撮影と計測での確認 | 方針の選択（PBRで壁・扉・金属を表現したい） |
| 金属の錆と擦れ傷（場所ごとの色・金属らしさ・粗さ）、揺れる裸電球の点光源の影（距離のキューブマップ）、天井の照明から落ちる光の筋、2面の新しい異変「見えない影」（携帯ラジオの雑音だけを頼りにやり過ごす）、1面のループで戻るたびに開始地点の部屋が崩れていく演出（浮かぶ文字・故障する照明・ドアの向こうの叩く音と取っ手の揺れ）を追加 | `shader/pbrLighting.hlsli`, `shader/pointShadow.hlsli`, `shader/pointShadowVS.hlsl`, `shader/pointShadowPS.hlsl`, `shader/horrorMist.hlsli`, `shader/flashlightShadow.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `PointShadowMap.*`, `ShadowMap.*`, `GameRendering.cpp`, `Game.*`, `GpuTimer.h`, `HangingBulb.*`, `UnseenPresenceAnomaly.h`, `Stage2AnomalyPlan.h`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `Stage2Objective.*`, `Scene.h`, `DebugUI.cpp`, `FlashlightWriting.h`, `Stage1Layout.*`, `StageScene.*`, `StageSceneEvents.cpp`, `tools/generate-wall-writings.ps1`, `assets/texture/writing_lap*.png`, `CaptureMode.cpp`, `README.md` | 設計・実装・撮影での確認 | 方針の選択（次の作業の候補1〜5を順に作る） |
| 画面空間の環境遮蔽（SSAO。Compute Shaderで半分の解像度で計算し、深さを見ながらぼかす）と、壁を伝う水の筋を追加。光の筋の重さを計測して軽くした（照明より下の区間だけを5点で調べ、霧のむらを1回だけ求める）。本描画のGPU時間を約8msから約4msへ | `AmbientOcclusion.*`, `shader/ambientOcclusionCS.hlsl`, `shader/ambientOcclusionBlurCS.hlsl`, `shader/screenAmbientOcclusion.hlsli`, `shader/horrorMist.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/common.hlsl`, `TiledLighting.*`, `GameRendering.cpp`, `Game.*`, `GpuTimer.h`, `CaptureMode.cpp`, `DebugUI.cpp`, `README.md` | 設計・実装・撮影と計測での確認 | 方針の選択（シェーダーを一気に追加・修正する） |
| 技術デモ（ゲームの F2 と GraphicsTechDemo）に、画面空間の環境遮蔽・PBR・天井の照明の光の筋・揺れる電球の影の「なし／あり」の切り替えを追加（PBRを止める印を影の定数バッファで渡す、デモの部屋の棚・柱・机・木箱に金属らしさと粗さを付ける）。半分の解像度の光の筋で出ていた斜めの縞を修正（2画素おきのずらしをやめて調べる点を増やし、拡大の重みを物の縁だけで下げる）。同期スクリプトと確認スクリプトの文字コード（BOM）を修正 | `TechDemo.*`, `GameRendering.cpp`, `ShadowMap.*`, `shader/flashlightShadow.hlsli`, `shader/litTexturePS.hlsl`, `shader/lightShaftsCS.hlsl`, `shader/horrorMist.hlsli`, `tools/check-text-format.ps1`, GraphicsTechDemo の `src/App.*`・`src/DemoScene.*`・`tools/sync-from-game.ps1`・`README.md`, `README.md` | 実装・撮影での確認（Claude Code） | 不具合の報告（斜めの線が見える）、作業の指示 |
| 2面の周回ごとに廊下の色調が赤黒く沈み、霧が濃くなる演出（色調と霧の共通処理に「周回の深さ」を1つ渡す。定数バッファ b7 の余りを使う） | `shader/horrorGrade.hlsli`, `shader/horrorMist.hlsli`, `shader/common.hlsl`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/flashlightRevealPS.hlsl`, `Renderer.h`, `RendererState.cpp`, `Stage2Scene.*` | 実装・撮影での確認（Claude Code） | 発案（P.T. のように周回ごとに廊下が染まっていく）、作業の指示 |
| タイトル画面からゲーム開始までの待ち時間の短縮（原因をシェーダーの実行時コンパイル約17秒と特定し、ビルド時の事前コンパイルと、.csoの保存・新しさの確認を追加） | `RendererResources.cpp`, `Renderer.h`, `main.cpp`, `Job_application_work_horror.vcxproj`, `.gitignore` | 原因の特定・実装・計測（Claude Code） | 不具合の報告（ゲーム開始まで時間がかかる） |
| FPSの上下の修正（計測モードに垂直同期ありの計測 `--vsync` と、フレーム時間の最大・ばらつきを追加して原因を表示の方式と特定し、スワップチェーンをフリップ方式に変更。約31fps→120fps） | `Renderer.cpp`, `CaptureMode.*`, GraphicsTechDemo の `shims/CaptureMode.*` | 原因の特定・実装・計測（Claude Code） | 不具合の報告（FPSが乱高下する） |
| 水たまりの反射を、平面反射（反転したカメラで描き直す）から、床のシェーダーで光線を飛ばすレイ反射（SSR＋部屋の形との交差）に変更。計測モードに反射の方式を指定する `--reflection` を追加し、平面反射と重さ・見た目を比較。技術デモにも「レイ反射」を追加 | `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `GameRendering.cpp`, `RendererState.cpp`, `Renderer.h`, `TechDemo.*`, `CaptureMode.*`, GraphicsTechDemo の `src/App.*`・`README.md` | 方式の提案・実装・計測・撮影での比較（Claude Code） | 方針の指示（カメラを2つ使う方法以外で、レイを飛ばす方法にしたい） |
| 2面に8つ目の異変「水たまり」を追加。中央の水たまりをのぞくと、向こう側の人影が水面にだけ映り、のぞき続けると近づき、顔を上げると誰もいない。人影はレイ反射の光線とカプセルの交差で描き、物としては置かない。自動撮影に水たまりの場面を追加 | `PuddleReflectionAnomaly.h`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `Renderer.h`, `RendererState.cpp`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `Stage2AnomalyPlan.h`, `Stage2Objective.*`, `Scene.h`, `DebugUI.cpp`, `CaptureMode.cpp` | 異変の提案・実装・撮影での確認と調整（Claude Code） | 提案の採用（レイ反射を使った、水たまりにだけ映る異変） |
| 見えない影が照明と懐中電灯の光を遮り、床と壁に影だけを落とす（解析的なカプセルの影）。ラジオの雑音が強いと映像が一瞬止まるコマ落ち。水たまりの異変の2周目の出方。レイ反射に画面の外の小物（棚・柱など）を追加。計測モードに人影を置いて測る `--figures` を追加し、重さを測って軽くした | `shader/figureShadow.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `shader/crtOverlayPS.hlsl`, `PostProcess.*`, `FullScreenQuad.*`, `Renderer.*`, `RendererState.cpp`, `Object.h`, `Wall.*`, `GameRendering.cpp`, `PuddleReflectionAnomaly.h`, `Stage2Scene*.cpp`, `Stage2Scene.h`, `CaptureMode.cpp` | 提案・実装・撮影と計測での確認（Claude Code） | 提案の採用（候補をまとめて制作するよう指示） |
| 見えない影の影を照明の下の光の筋（霧）にも落とし、影を濃く・縁を鋭くした。懐中電灯で照らすと見えない影が気づいて速く寄ってくる遊びを追加。1面の西棟に、水面にだけ映る天井の逆さまの人影（見上げると何もいない）を追加。技術デモに「見えない人影の影」の項目を追加。光の筋の重さを測って軽くした | `shader/lightShaftsCS.hlsl`, `shader/figureShadow.hlsli`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `LightShafts.h`, `Renderer.*`, `RendererState.cpp`, `UnseenPresenceAnomaly.h`, `CeilingReflectionScare.h`, `StageScene.*`, `Stage2SceneHorror.cpp`, `Scene.h`, `DebugUI.cpp`, `CaptureMode.cpp`, GraphicsTechDemo の `src/App.*`・`README.md` | 提案・実装・撮影と計測での確認（Claude Code） | 提案の採用（候補をまとめて制作するよう指示） |
| 懐中電灯の光の帯に、見えない影の人の形の切れ目を追加。見えない影が水たまりを通ると波紋と水音。最後の追跡の影を水面にも映し、水面なら見ても罰がないようにした。READMEに11月1日の画像3枚と撮影記録を追加 | `shader/volumetricFlashlightPS.hlsl`, `Stage2SceneHorror.cpp`, `Stage2Objective.cpp`, `CaptureMode.cpp`, `README.md`, `docs/images/*_20261101.*` | 提案・実装・撮影での確認・画像の選定（Claude Code） | 提案の採用（候補をまとめて制作するよう指示） |
| 反転したカメラで景色を描き直す平面反射をコードごと外し、2面の壁の鏡を水たまりと同じレイ反射に変更。鏡に映る自分の体と鏡の異変の人影を、鏡のシェーダーの中でカプセルと光線の交差で描くようにした。計測モードに鏡の前の地点を追加し、外す前と後を測った | `shader/rayReflection.hlsli`, `shader/mirrorPS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/litTextureVS.hlsl`, `shader/waterCaustics.hlsli`, `MirrorSelf.*`, `Renderer.*`, `RendererState.cpp`, `GameRendering.cpp`, `Game.*`, `Object.h`, `Wall.h`, `Ground.h`, `ShadowMan.*`, `Stage2*.cpp`, `TechDemo.*`, `DebugUI.*`, `CaptureMode.*`, GraphicsTechDemo の `src/App.*`・`README.md` | 実装・撮影と計測での確認（Claude Code） | 反転カメラを使わない方針の決定、鏡をレイ反射で描く案と比較用の平面反射も消す案の選択 |
| 一人称の「懐中電灯を持った右手」を追加。Mayaのスクリプトで手と袖（距離関数を Surface Nets で面にした形）と懐中電灯（回転体）を作ってFBXに書き出し、手元だけの射影行列・深度の範囲を手前に寄せる描画・肌と布と金属のシェーダー・視点の揺れの遅れ・歩き・走り・スイッチの動きを付けた | `tools/maya/build_flashlight_hand.py`, `assets/model/flashlight_hand/*`, `FirstPersonHands.*`, `Player.*`, `shader/viewModel*.hlsl*` | モデルを作るスクリプト・描画・動きの実装と撮影での確認（Claude Code） | モデルに変える方針の決定（時間がかかってもよい）、手と懐中電灯から始める順番の承認 |
| コードで箱や円柱を組んでいた人影・ロッカー・扉・配電盤・鍵・ヒューズ・電池・天井の照明・裸電球・排水バルブを、Mayaのスクリプトで作ったモデル（面取り・部位ごとのマテリアル・頂点カラーの環境遮蔽）に差し替えた。読み込みは `PropModel` にまとめ、各クラスの描き方（範囲ごとのマテリアル・影・深度プリパス）は変えていない | `tools/maya/build_props.py`, `assets/model/props/*.fbx`, `PropModel.*`, `ShadowMan.cpp`, `Locker.cpp`, `Door.cpp`, `FuseBox.cpp`, `KeyItem.cpp`, `Item.cpp`, `BatteryItem.cpp`, `CeilingLight.cpp`, `HangingBulb.cpp`, `DrainValve.cpp` | モデルを作るスクリプト・差し替え・撮影での確認（Claude Code） | まとめて差し替える指示 |
| 一人称で見えない自分の体が、天井の照明や裸電球の光を遮り、床と壁に影を落とすようにした（胴・頭・両脚・両腕のカプセルの解析的な影。歩くと脚と腕を振る）。自動撮影に、電球を背にして足元の影を写す場面を追加 | `shader/figureShadow.hlsli`, `shader/litTexturePS.hlsl`, `shader/wetFloorPS.hlsl`, `shader/waterCaustics.hlsli`, `shader/lightShaftsCS.hlsl`, `Renderer.h`, `RendererState.cpp`, `Player.*`, `CaptureMode.cpp` | 方式の提案・実装・撮影での確認（Claude Code） | 体の影を付ける指示 |
| 鏡に映る自分の体と、鏡・水たまり・西棟の水面にだけ映る人影を、カプセルの組み合わせから、焼いた距離場（3Dテクスチャ）のスフィアトレーシングに変更。自分の体は懐中電灯を前に持った立ち姿と部位の色を焼き、関節のまわりに回した空間で読んで手足を振る。カプセルの版と同じ条件で計測し、重さが変わらないことを確認 | `tools/maya/build_figure_volumes.py`, `assets/volume/*.sdfv`, `FigureVolumes.*`, `shader/figureVolume.hlsli`, `shader/mirrorPS.hlsl`, `shader/wetFloorPS.hlsl`, `Game.*`, `GameRendering.cpp` | 方式の提案・実装・撮影と計測での確認（Claude Code） | 距離場で描く案の選択 |
| 1面の建物の壁32枚と柱10本、2面の廊下の壁・天井・縁取り・配管を、C++の配置から配置ファイル（stage1_props.txt・新しい stage2_props.txt）へ移した。部屋の角の暗がりとレイ反射に使う壁は room の指定で選ぶ。移す前と後の自動撮影を画素で比べ、見た目が変わらないことを確認 | `assets/layout/stage1_props.txt`, `assets/layout/stage2_props.txt`, `PropLayoutFile.*`, `Stage1Layout.cpp`, `Stage2Layout.*` | 実装・撮影の比較での確認（Claude Code） | マップを外部のエディターで作れるようにする方針と、1つずつ進める順番の決定 |
| 扉・天井の照明・電池・配電盤や端末・人影・プレイヤーの開始位置など、進行のコードが作る物50個（1面35・2面15）の置き場所を、配置ファイルの place 行（位置・向き・大きさ）から読むようにした。C++の座標はスクリプトで抜き出して移し、移す前と後の自動撮影を比べて確認 | `assets/layout/stage1_props.txt`, `assets/layout/stage2_props.txt`, `PropLayoutFile.*`, `Stage1Layout.cpp`, `Stage2Layout.cpp` | 実装・撮影の比較での確認（Claude Code） | 次の一歩として進める指示 |
| 残りの、表や関数の引数で置いていた物（壁の文字・暗証番号の手がかり・排水バルブとランプ・ループ廊下の目印・2面の信号盤と目印・肖像画と時計と偽の扉・異変の印と傷・ロッカー・裸電球）の置き場所も place 行へ移し、ヒューズと鍵の候補の場所を spot 行にした。移す前と後の自動撮影を比べて確認 | `assets/layout/stage1_props.txt`, `assets/layout/stage2_props.txt`, `PropLayoutFile.*`, `Stage1Layout.cpp`, `Stage2Layout.cpp` | 実装・撮影の比較での確認（Claude Code） | 次の一歩として進める指示 |
| Unity 6000.3 のマップエディター（MapEditor）を作成。配置ファイルをシーンへ読み込み（prop は色の箱、place は向きの付いた印、spot は球）、動かした結果を、変わった数値だけ書き換えて配置ファイルへ書き戻す。バッチモードで、何も変えない往復で元のファイルと同じになることと、動かした行だけが変わることを確認 | `MapEditor/Assets/Editor/MapLayout/*.cs`, `MapEditor/Assets/MapLayout/*.cs`, `MapEditor/.gitignore` | プロジェクトの作成・エディター拡張の実装・バッチでの確認（Claude Code） | Unity で作る方針と、Unity 6 で作る指示 |
| Unity のマップエディターで、ゲームと同じ FBX のモデルを表示するようにした（棚・机・警告灯などの見た目のモデルは Wall.cpp の表、扉・照明・電池などの置き場所は Stage1Layout.cpp・Stage2Layout.cpp の作り方から種類を読み取る）。ゲームの自動撮影と同じ視点の画像をバッチで撮り、ゲームの画面と見比べた | `MapEditor/Assets/Editor/MapLayout/GameModelCatalog.cs`, `MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs`, `MapEditor/Assets/Editor/MapLayout/MapLayoutBatch.cs`, `MapEditor/Assets/MapLayout/MapLayoutItem.cs`, `MapEditor/.gitignore` | 実装・バッチでの確認（Claude Code） | 次の一歩として進める指示 |
| 当たり判定のある箱（Wall）で置いていた小物の見た目を、Mayaのスクリプトで作ったモデルに差し替えられるようにした（当たり判定・影の有無・水面に映す箱は箱のまま。長い物は箱の一番長い向きへ回す）。1面の書類棚・机・木箱・崩れた資材・梁・キャビネット・配電箱・ポンプ・タンク・排水口・柱・管の柱・天井沿いの配管・扉の上の表示灯・警告灯（排水バルブの上のランプも）・安全地帯の電球とコード・卓上ライトと、2面のロッカーの本体・配管・扉の上の表示灯・鏡と肖像画と時計と偽の扉の額縁を差し替え（警告灯とライトは、光る部位だけを光らせ、枠や笠は光らないマテリアルで描き分ける）。配置ファイルに model の指定を追加 | `tools/maya/build_wall_props.py`, `assets/model/props/wall_*.fbx`, `Wall.*`, `PropModel.*`, `PropLayoutFile.*`, `assets/layout/stage1_props.txt`, `Stage1Layout.cpp`, `Stage2Layout.cpp` | モデルを作るスクリプト・仕組み・差し替え・撮影での確認（Claude Code） | まとめて差し替える指示 |

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
| `assets/model/flashlight_hand`・`assets/model/props`（手と懐中電灯、人影・ロッカー・扉などの小物） | `tools/maya/build_flashlight_hand.py`・`tools/maya/build_props.py` を Maya 2025 の `mayapy` で実行して作成 | モデルを作るスクリプトをAIが作成（画像・3Dの生成AIは不使用） |
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
