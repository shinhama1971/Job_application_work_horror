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
| 配置ファイルの読み直しを、place 行の物（扉・照明・電池・壁の文字・ロッカー・裸電球など）と2面にも広げた。置き場所を当てはめるときに置き直し方を名前と一緒に覚え、読み直したときに呼び直す。Debug 版では配置ファイルの更新時刻を見張り、Unity から書き出した直後に自動で読み直す。読み直した後の部屋の形（部屋の角の暗がり）も渡し直す | `PropLayoutFile.*`, `Stage1Layout.*`, `Stage2Layout.*`, `StageScene.*`, `Stage2Scene.*`, `Stage2SceneDraw.cpp`, `Stage2Objective.*`, `README.md`, `ARCHITECTURE.md` | 設計・実装・ビルドと撮影での確認（Claude Code） | 次の一歩として進める指示 |
| 壁・床の凹凸が懐中電灯の光に反映されていなかった問題の修正（ビュー行列b1をピクセルシェーダーにも設定）と、それに合わせた調整（壁・小物の懐中電灯の拡散光の幅、扉や家具の凹凸の強さ、法線を作る微分を `ddx_fine` に変更）。計測モードの画像で前後を比べて調整した | `Renderer.cpp`, `shader/litTexturePS.hlsl`, `shader/flashlightLighting.hlsli`, `shader/surfaceDetail.hlsli`, `ARCHITECTURE.md` | 原因の調査・実装・調整（Claude Code） | 修正の指示 |
| 審査会の指摘への対応（11月5日）。水たまりを、水たまりごとのメッシュに分け、画面の外は描かず、遠いほど反射を軽くした（技術デモ F2 で前の方式と比べられ、F1 のデバッグ表示で細かさを色で見られる）。「暗くて見えない」「壁に近づくと何も見えない」に、懐中電灯の光の輪を広げ、近い壁で弱める量を減らし、長く何もなかった壁際に目印の物を12個置いた | `Ground.*`, `GroundWaterEffects.cpp`, `shader/wetFloorPS.hlsl`, `TechDemo.*`, `DebugUI.cpp`, `Player.cpp`, `assets/layout/stage1_props.txt`, `README.md`, `ARCHITECTURE.md` | 設計・実装・ビルドと撮影・計測での確認（Claude Code） | 対応する指摘の選択と優先順位、指摘の意味の確認（どこが分かりにくかったか） |
| 審査会の指摘「インパクトが少し弱い」への対応。緊張が高いほど画面の端から視界が狭まり（ぼけ・色抜け・暗さ）、心拍に合わせて脈打つ画面効果を足した（1面・2面共通） | `PostProcess.*`, `FullScreenQuad.*`, `shader/crtOverlayPS.hlsl`, `TensionPulseFeedback.h`, `StageSceneEvents.cpp`, `Stage2SceneHorror.cpp`, `README.md` | 実装・ビルドと撮影での確認（Claude Code） | 案の中から「恐怖で視界が狭まる」を選んだ |
| 外の森（試作）の場面を作った。Maya のスクリプトで枯れ木・倒木・岩・枯れた茂みのモデル、配置ファイルで小道と研究所と灯り、ゲームに `ForestScene`（タイトルの3つ目の選択、`--forest`、デバッグ画面のボタンで開く）、Unity のマップエディターに「森を読み込む」、自動撮影に森の道順を足した | `ForestScene.*`, `TitleScene.*`, `HudScreens.cpp`, `Game.*`, `GameRendering.cpp`, `CaptureMode.cpp`, `DebugUI.cpp`, `Wall.cpp`, `tools/maya/build_forest_props.py`, `assets/layout/forest_props.txt`, `assets/model/props/forest_*.fbx`, `MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs`, `MapLayoutBatch.cs`, vcxproj, `README.md`, `ARCHITECTURE.md` | 設計・実装・ビルドと撮影での確認（Claude Code） | 森の導入の案、建物を研究所にすること、Unity で置けてゲームでも見られる範囲までにすることの決定 |
| Unity のマップエディターで、シーンから消した物（prop・spot）が配置ファイルからも消えるようにした（place は残して知らせる。読み込んだ後に足された行は消さない）。使われなくなった配管（PipeProp）の置き場所の行を消した。バッチの確認に、消したときの確認を足した | `MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs`, `MapLayoutBatch.cs`, `GameModelCatalog.cs`, `MapEditor/Assets/MapLayout/MapLayoutRoot.cs`, `assets/layout/stage1_props.txt`, `README.md` | 原因の調査・実装・バッチでの確認（Claude Code） | 不具合の報告（Unity で消しても消えない）、スタート地点の配管をコードから外した |
| 当たり判定のある箱（Wall）で置いていた小物の見た目を、Mayaのスクリプトで作ったモデルに差し替えられるようにした（当たり判定・影の有無・水面に映す箱は箱のまま。長い物は箱の一番長い向きへ回す）。1面の書類棚・机・木箱・崩れた資材・梁・キャビネット・配電箱・ポンプ・タンク・排水口・柱・管の柱・天井沿いの配管・扉の上の表示灯・警告灯（排水バルブの上のランプも）・安全地帯の電球とコード・卓上ライトと、2面のロッカーの本体・配管・扉の上の表示灯・鏡と肖像画と時計と偽の扉の額縁を差し替え（警告灯とライトは、光る部位だけを光らせ、枠や笠は光らないマテリアルで描き分ける）。配置ファイルに model の指定を追加 | `tools/maya/build_wall_props.py`, `assets/model/props/wall_*.fbx`, `Wall.*`, `PropModel.*`, `PropLayoutFile.*`, `assets/layout/stage1_props.txt`, `Stage1Layout.cpp`, `Stage2Layout.cpp` | モデルを作るスクリプト・仕組み・差し替え・撮影での確認（Claude Code） | まとめて差し替える指示 |

### 1面の録音と嘘の無線（2026年11月9日）

録音機4台・無線の字幕・リザルトの集計が入った作業を引き継ぎ、Codex（OpenAI）が字幕の出る条件を調べました。自動撮影の画像を場面ごとに見比べ、西棟へ移る途中の座標が書類保管室に入っていたことを特定しました。撮影の道順で途中の座標を通らないようにし、ゲーム内では他の字幕の再生中も入室と影の出現を受け付けて、順番に表示するようにしました。確認用の自動再生を使い、1つ目と4つ目の録音の3行、最後の応答と通信途絶、ランプの色の変化を画像で確認しました。

### 施設案内図と水滴の音

審査会の「どこに行けばいいか分からない」という感想を受け、作者が選んだ案をもとに、Codex（OpenAI）が配電盤横の案内図と最初の周回後の変化を実装しました。図はスクリプトで描き、廊下の先の扉だけが消える2枚を用意しました。水滴については西棟の環境音の音量を上げ、近くの雨漏りに着水と同期した音を加えました。水たまりの足音素材は候補を調べ、配布元で利用条件を確認しました。

自動撮影で配電盤前を同じ視点から撮り、1周目の前後で扉の記号だけが変わることを確かめました。確認用の撮影位置は撮影後に外しました。

作者の指定で、Codex（OpenAI）がプレイヤーの水たまりの足音を歩き用と走り用に分けました。既存の水音の余韻を短くし、走り用にだけ靴底の着地音を重ねました。人影の水音は従来の音源を使います。

試作では水たまりらしく聞こえないという指摘を受け、歩き用・走り用とも Joseph Sardin の水を踏む実録（BigSoundBank、CC0）の別々の一歩に差し替えました。水が見える位置と足元の判定がずれていたため、床のシェーダーの形に足元の判定を合わせました。
確認用の自動撮影で移動を与え、西棟で歩き用・走り用の新しいWAVをXAudio2へ渡したこと、2面で乾いた床と水たまりの音が分かれることをログで確かめました。確認用の移動とログ出力は外しました。

### 水たまりの反射の調整（2026年11月11日）

作者から水たまりが液体金属のように見え、移動すると反射像がかくつくという指摘を受けた。Codex（OpenAI）が、今の深度で見つけた画面座標のまま前フレームの色を読んでいた処理を修正した。当たった点を前のカメラへ投影し直し、前の深度と合う点の色を使う。部屋の形から求めた反射を主体にして、見下ろした水面の反射と照り返しも弱めた。
続いて、作者の画像で扉と壁が映りにくいことを確認した。Codex が扉板の位置と向きを画面外の反射に追加し、近くの縦の面に当たったときだけ斜めからの反射を強めた。

### 森から1面へ入るときのちらつき修正

作者から、玄関を調べた後に扉が数秒ちらついて映ると報告があった。Codex が、目的画面の背景用に森を作り直し、扉を映しながら受信記録用のノイズをかけていた処理を確認した。森から入ったときは、完全な暗転から黒い背景の目的画面へ切り替えるようにした。

### 進む先の天井灯の調整

作者から進む方向の赤い天井灯を目立たせたいという要望があり、Codex が選ばれた1灯の点灯を少し長い2回にして、光る間の器具と周囲の光を強めた。

### 水面の反射の距離切り替え

少し移動すると水面の反射が消えるという報告を受け、Codex が水面メッシュの描画グループと反射処理の関係を調べた。反射は各画素の距離に応じて徐々に弱め、SSRが床に近すぎる面を拾った場合は、光線の先を探すようにした。

### 壁に近づいたときの描画負荷

作者から壁の近くでFPSが下がると報告があり、Codex が中央ホールと西棟を2つの距離で計測した。画面を覆う施設案内図の裏で壁を描いていたため、印刷物を深度プリパスに加えた。壁の模様が出る範囲で細部を計算し、壁までの距離の比較には二乗を使うようにした。

### 暗証番号の部屋の保管灯

作者から暗証番号の部屋に仕掛けを足したいという要望があり、Codex が記録端末と鍵探しをつなげた。端末を調べると保管灯が順に点き、鍵の位置に対応した1灯が2回ずつ点滅する。

案内図を見に近づくと依然としてFPSが落ちるという報告を受け、Codex が紙面の描画をつや消し用に切り替え、壁の一覧を画素ごとに検索する処理を省いた。案内図前の3つの距離で計測した。

### 西棟の排水跡（2026年11月11日）

- Codexで既存の排水進行を水面の高さへ反映し、柱の湿った帯と床の残り水をHLSLで描き分けた。

### 研究所を照らす稲光（2026年11月12日）

- Codexで森の稲光を実装し、自動撮影で建物の輪郭と照明の消え方を比較した。

### 書類保管室の無線を疑う手がかり

- Codexで、影を照らして止まる様子を見てから無線が届くように変更した。初出現時の文言を物音と影の気配に合わせた。

### 3面の開発用プレビュー

- Codexで見取り図を基に階段踊り場、配線の廊下、中継室、通信室、非常口と屋外を実装した。既存の配置読み込み、操作対象、補助カメラ、雨の処理を使い、通信卓で試験停止してから脱出する流れを作った。

- 3面に記録室・点検通路・回線室を追加し、保守記録を手がかりにした接続順と停電後の手動解錠を実装した。

- 3面の回線の謎を3段階にし、林の中継箱と送信小屋を増設した。原本の回収・消去から道路に出るまでの場面と字幕を追加した。

- 3面に遮蔽壁で区切った観測区画と通信室の裏口を作り、南の回線盤と北の消灯・歩行を選べるようにした。

### 審査会の前の点検（2026年11月12日）

- Claude Codeで、Release・Debug のビルド、計測モード（1面・森）、自動撮影（1面→2面・森・3面）を通して点検した。
- 見つけた不具合を直した：別のウィンドウへ切り替えて最小化しても、裏でゲームが進み続け、GPU も使い続けていた。切り替えたときにポーズ画面を開き、最小化の間は更新と描画を止めるようにした（`Game::OnFocusLost`、`Application::MainLoop`）。
- GPU のドライバーが応答しなくなったとき（デバイスの消失）に、画面が止まったままにならず、原因を伝えて終了するようにした（`Renderer::DrawEnd`）。

### 森の水たまりの雨の輪を遠くまで（2026年11月13日）

- 作者の「少し近づかないと雨の波紋が見えない」という指摘を受け、Claude Codeが、波の高さと計算範囲を一時的に色で塗って撮影し、森では波を計算する範囲（一辺192 ≒ 8メートル）がカメラのすぐ前だけだったことを見つけた。
- 森だけ範囲を一辺576・768x768マスに広げ（`Ground::SetRippleArea`、`WaterRippleSimulation::Init`）、シェーダーはマス目の数をテクスチャの大きさから読むようにした。離れた雨粒の輪を大きく強くし、床のシェーダーで離れた輪の光り方を強めた。計測モードで重さの増加（約0.11ms）を確かめた。

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

## 森の草の作業（2026年11月6日）

`HANDOFF_GRASS.md` を元にCodex（OpenAI）が続きを担当しました。引き継ぎ時点で `ForestGrass.h/.cpp` と `shader/grassCS.hlsl` は存在していました。この3ファイルと `HANDOFF_GRASS.md` は、Claude Code（Anthropic）が授業資料Part.9の確認と設計の後に作成しました。

今回Codexが追加・修正した範囲は、`grassVS.hlsl`・`grassPS.hlsl`、森への組み込み、入力レイアウトなしのシェーダー作成、プロジェクト登録、草の数のDebugUI表示、森の計測地点と比較用の草の停止、文書です。ビルド・自動撮影・GPU計測で確認しています。続けてユーザー提供ZIP内のPart.9本文と実装図を確認し、採用した考え方・変更した点・未実装の発展項目を docs/FOREST_GRASS_PART9.md に記録しました。

## 森の草の手直しと空の霧（2026年11月7日）

Claude Code（Anthropic）が、Codexの作業を読んでビルドと自動撮影で確認した後、次を直しました。懐中電灯で照らした草が黒い影に見える問題（葉の色を地面に近い明るさにし、根元の暗さの二重掛けをやめた）、水たまりの水面と枯れた茂みに草が生える問題（`Ground::IsNearVisiblePuddle` を追加）、森の地平線の線（空に霧を重ねる `SkyBackdrop`）、森の右上の「記録 0 / 4」。いずれも草を止めた撮影・直す前の撮影と同じ場面で比べ、地平線は画素の明るさも測って確かめました。
Claude Code（Anthropic）が、授業資料 Part.9 の p.23（下を向いたとき、カメラの上方向へ草を曲げて側面を見せる）をスライドで確かめて草の頂点シェーダーに足し、技術デモ（F2）に「草のLOD」「草をカメラの上方向へ曲げる」を足しました。確認のときだけ、環境変数で方式を切り替える一時的なコードを入れて、曲げのなし・ありと LOD の色を自動撮影で、LOD のなし・ありを計測モードで比べ、確認の後にそのコードは消しました。
Claude Code（Anthropic）が、森の入口の灯りの演出（`ForestEvents`。街灯のちらつきと、通り過ぎた後ろの街灯が消える、研究所の2階の窓の明かりが消える、玄関の灯りの明滅）と、灯りの明るさの倍率（`Wall::SetEmissionScale`）を足しました。演出の前と後の自動撮影で、窓の明かりが消えていることと、街灯が早く消えすぎていないこと（街灯の前の1枚の明るさ）を確かめました。
続けて Claude Code（Anthropic）が、草むらを通る「見えない何か」（`ForestEvents` の足元の動きと足音、`grassCS.hlsl` の踏まれた葉の倒れ具合と向きの記憶、`ForestGrass::SetUnseenWalker`）を足しました。確認のときだけ「見えない何か」を止める一時的なコードを入れ、止めたときと通ったときの自動撮影の差を画素で比べて、草が反応している場所を確かめました。最初は跡が目で分かりにくかったため、押し倒す半径と倒れ方を強め、跡が残る時間を延ばしてから撮り直し、確認の後に一時的なコードは消しました。
Claude Code（Anthropic）が、タイトル画面を作り直しました（後ろに3Dの森を映すカメラの場面の切り替え、題名の電波が乱れるノイズ、左側の縦のメニュー、`Player::SetAttractMode`、森の世界の作り方の共通化）。確認のために一時的なコードでタイトルを長く撮影して見た目を確かめ、1面・2面の自動撮影を直前の commit の版と比べました。1面の光の安全地帯の2枚だけが変わったため、変更を1つずつ前の版に当てて原因を絞り込み、タイトルでも環境マップと光の筋を動かすようになったことで、それらの状態（次に撮る面、揺らぎの時間）がシーンをまたいで残っていたと分かったので、シーンの切り替えで戻すようにしました。一時的なコードは確認の後に消しました。

Claude Code（Anthropic）が、1面の3本目のヒューズを2通りから選べるようにしました（暗証番号の扉の先の部屋のヒューズ、`StageScene::UpdateThirdFuseRoutes`、目的表示と案内の矢印の分岐）。確認のときだけ、3本目の段階まで進めて暗証番号の部屋のヒューズを拾い、西棟のヒューズを出そうとする一時的なコードを入れ、各段階の状態と目的表示の文章をファイルに書き出して確かめました。確認の後に一時的なコードは消し、1面の自動撮影が変わっていないことも確かめました。

Claude Code（Anthropic）が、1面の部屋の入口に目印の灯り（11か所）を付けました。配置ファイルから壁と扉を上から見た地図を描いて入口の場所を求め、自動撮影と計測モードで、見え方とGPU時間を前と比べて確かめました。

Claude Code（Anthropic）が、懐中電灯の翼の光（スピル）と照り返しの点光源を足し、1面の壁で重なっていた2周目の文字と暗証番号の手がかり（ひとつめ）を、手がかりの方を同じ仕切りの倉庫の側へ移して直しました。光の変更は、森と1面の自動撮影を前と並べて見比べ、計測モードで重さを確かめました（1回だけ2面の鏡の前でGPUが全体に遅くなったため、もう一度測って一時的なものだと確かめました）。

Claude Code（Anthropic）が、1面の前の導入（`IntroScene`、`Hud::DrawIntro`、タイトルと共通の森の背景 `ForestBackdrop`）を作りました。文章（受信記録・目的）は Claude Code が案を出し、作者が確認して決めました。確認のときだけ、自動撮影でタイトルから導入へ進めて各段階を撮る一時的な処理を入れ、確認後に消しました。

Claude Code（Anthropic）が、外の森の雨（`ForestRain`、`shader/rainVS.hlsl`・`rainPS.hlsl`、水たまりの雨の輪 `Ground::AddRainRipples`、雨音の合成 `tools/generate-rain.ps1`）を作りました。自動撮影の画像を見て明るさと筋の長さを調整し、計測モードで雨あり・なしのGPU時間を比べました。あわせて、導入の無線の雑音がシーンの切り替えの直後に止められていた不具合を直しました。

Claude Code（Anthropic）が、タイトルの1面を、雨の森を歩いて研究所に入ってから始まる流れにしました（`ForestRadio` の無線の字幕、`LabEntrance` の玄関、`StoryText.h` への文章のまとめ、`IntroScene` を目的の画面から始める仕組み、タイトルの選択肢の変更）。確認のときだけ、自動撮影で森の途中の字幕と、玄関から入った後の画面を撮る一時的な処理を入れ、確認後に消しました。

Claude Code（Anthropic）が、作者が4行に増やした無線の文章に合わせて、森の無線の字幕（`ForestRadio`）と導入（`IntroScene`）を4行に対応させ、行の数が文章とずれたらビルドで気づけるようにしました。あわせて、森の雨を `ForestScene::RainEnabled` で止められるようにし、今は止めています。

Claude Code（Anthropic）が、作者の「前みたいに強い雨じゃなくて少し弱い雨で」という指示で、森の雨を戻し、強さ（`ForestScene::RainStrength`）で雨粒としぶきの数・落ちる速さ・水たまりの輪・雨音をまとめて弱められるようにしました（11月10日）。強さ0.45で撮った画像では、街灯の下でまだ強く見えたので、0.35に下げて撮り直しました。どのくらいの強さにするかは作者が決めました。

Claude Code（Anthropic）が、作者の「LODの範囲をもう少し広げたい」という指示で、森の草の LOD（5節140・3節280・330〜410で消える）と水たまりのメッシュの LOD（SSRまで170・光線まで340）の距離を広げ、自動撮影と計測モードで見た目と重さを確かめました（11月10日）。広げる対象（両方）は作者が選びました。

作者が遊んで撮った画像で「かなり近づかないと水たまりに雨の輪が広がらない」と指摘したのを受け、Claude Code（Anthropic）が、雨粒をカメラのまわり48までにしか落としていなかったことと、弱い雨で輪の強さまで下げていたことを見つけ、波を計算している範囲全体に落とし、輪の強さは一定にしました（11月10日）。

作者が「水たまりを歩いたときに画面に付く水滴に違和感がある」と伝え、出し方の案（出さない・自然な見た目にする・雨で上を見たときに付く形にする）から「自然な見た目にする」を選んだのを受け、Claude Code（Anthropic）が、走って水を蹴ったときだけ、毎回違う位置に付いて流れ落ちる、景色を逆さに映すしぶきに作り直しました（`crtOverlayPS.hlsl`・`PostProcess`・`FullScreenQuad`・`GroundWaterEffects.cpp`）。最初の版は薄すぎて撮影の画像で見えなかったため、粒の中の景色を逆さに映すように変えて撮り直しました。確認のときだけ、撮影中にしぶきを付け続ける一時的な処理を入れ、確認後に消しました（11月10日）。

作者が「森の場面などで ESC を押しても選択画面にならず、ゲームを終了できない」と伝えたのを受け、Claude Code（Anthropic）が、森ではポーズメニューが開いてゲームは止まっているのに、森の場面の描画にメニューを描く処理がなかったことを見つけて直しました。導入の受信記録でも ESC でメニューが開くようにし、外では「現在 研究所の外」「この場面をやり直す」と表示するようにしました（11月10日）。続けて作者が、終了の確認の画面の文字化けを画像で伝えたのを受け、UTF-8 の文字列を `MessageBoxA` に渡していたのを `MessageBoxW` に直しました（起動エラーの表示も同じ）。

作者が「1面で、雨が水たまりに落ちる音が全く聞こえない」と伝えたのを受け、Claude Code（Anthropic）が、水たまりに落ちる水滴のうち音を鳴らすのが雨漏りの1粒だけだったことを見つけ、近く（70以内）に落ちたほかの水滴も、0.25〜0.5秒の間を空けて少し小さく鳴らすようにしました（11月10日）。

作者が次の候補から「濡れた木や岩」を選んだのを受け、Claude Code（Anthropic）が、外の森の雨の強さに合わせて岩と樹皮を濡らすシェーダー（`shader/forestWetness.hlsli`。上を向いた面ほど濡れる・ひびや溝に水がたまる・水を吸って暗くなり光沢が出る・横の面を流れ落ちる水の筋）を作り、雨の強さを b7 の詰め物の場所で渡すようにしました（`Renderer::SetRainWetness`・`ForestRain`）。最初のビルドで、雨のシェーダー（`rainVS.hlsl`）にある名前と同じ名前を付けてしまいコンパイルが失敗したので、名前を変えました。確認のときだけ、自動撮影の道順に岩と木の前の地点を足して撮り、確認後に消しました（11月10日）。

作者が「水たまりの反射が、前のカメラを反転させる方式（平面反射）より濁っていて、映っていない」と伝えたのを受け、Claude Code（Anthropic）が、自動撮影の画像と床のシェーダーを見て、SSRの歩数が少なく当たりの位置を詰めていないこと、当たりを歩数に比例して薄めていたこと、映り込みが弱いこと、床の霧が反射の上から全部かかっていることを原因として挙げ、それぞれ直しました（`rayReflection.hlsli`・`wetFloorPS.hlsl`）。直す前と後を同じ撮影で並べて比べ、計測モードで重さを確かめました（11月10日）。
続けて作者が「カメラを2つ用意していた頃の方がきれいだった。カメラを増やさず、今のレイを飛ばす方法のままで近づけてほしい」と伝えたのを受け、Claude Code（Anthropic）が、反射の光線を波立った水面の向きのまま飛ばしていたことを見つけ、穏やかな水面の向きに波を3割だけ足した向きで飛ばすようにし、画面の外の壁・天井にコンクリートと天井板の模様を付けました（Claude Code が途中でコンピュートシェーダーで画素を上下反転して書き移す方法（SSPR）を提案しましたが、作者の指示でやめました）。
さらに作者が、Claude Code の挙げた案から「画面の外を環境マップで映す」と「離れた物ほどぼかす」を選んだのを受け、Claude Code（Anthropic）が、光線が部屋の形に当たった位置から PBR の環境マップを読む処理（視差の補正）と、前のフレームの画面の縮小画像を作って映る物までの距離でぼやけた段を読む処理を作りました（`rayReflection.hlsli`・`wetFloorPS.hlsl`・`ScreenSpaceReflection`・`Renderer::SetReflectionEnvironment`）。自動撮影の縮小した画像では違いが小さく、どの場所でどれだけ良くなったかは、作者が歩いて確かめる必要があります（11月10日）。
続けて作者が「水たまりの反射がかくかくしていて気持ちが悪い」と伝え、プレイの録画を渡しました。Claude Code（Anthropic）が、録画を Windows の動画の編集の仕組み（Windows.Media.Editing）で0.25秒ごとの静止画に切り出して見比べ、床を見下ろしたときに SSR が床そのものに誤って当たり、明るい床がギザギザの塊で映って跳ねていることを見つけました。当たった所の高さで床への当たりを捨て、縮小画像のブロックと、環境マップの撮る位置のずれも直しました（11月10日）。

作者が「森の研究所の建物がほかに比べて見劣りする。高さも欲しいし、見た目ももう少し良くしてほしい」と伝えたのを受け、Claude Code（Anthropic）が、研究所を3階建てにして階段の塔・3階の窓・窓の下の水切り・端の柱・屋上の機械と手すり壁を配置ファイルに足し、外壁の材質のシェーダー（`facadeSurface.hlsli`・`facadePS.hlsl`。型枠の跡・Pコンの穴・雨だれ・白華・根元の泥と苔・ひび）と、配置ファイルの `facade` の印を作りました。撮影で、雨だれが等間隔のしま模様に見えたので列の幅と位置をばらつかせ、その後まったく出なくなった所があったのを直しました。確認のときだけ、自動撮影の道順に研究所を離れて見上げる地点を足し、確認後に消しました（11月10日）。

作者が「作品をネットに公開してみたい。Release で作品を出せるようにフォルダを管理してほしい」と伝えたのを受け、Claude Code（Anthropic）が、配布用のフォルダと zip を作るスクリプト（`tools/make-release.ps1`）、遊ぶ人向けの説明書と、ライブラリ・素材の一覧の下書き（`distribution/README.txt`・`THIRD_PARTY_NOTICES.txt`）を作り、配布用のフォルダから起動したときに exe の場所を作業フォルダにする処理（`main.cpp`）を足しました。配布用のフォルダの exe を、別の作業フォルダから森と1面→2面の自動撮影で動かし、最後まで動くことと、フォルダの中のファイルが書き換わらないことを確かめました。効果音の配布元や作者名など、Claude Code が分からない所は【要確認】として残し、作者が埋めます（11月10日）。

作者の「ソースファイルのコメントを確認し、作者や AI について書いた所を消し、語尾を『〜している・〜する』にそろえ、何の処理をしているかをもう少し書いてほしい」という指示で、Claude Code（Anthropic）がソースとツールのスクリプトのコメントを洗い出しました。経緯（「以前は」「作者の指示で」）を書いていたコメントを処理と理由の説明に書き直し、スクリプトのコメントの敬体（〜します・〜です）約60行を常体にそろえました（11月10日）。

作者の「ビルドに時間がかかるので短縮したい」という相談で、Claude Code（Anthropic）が、C++ のファイルを1つずつコンパイルしていたこと（`/MP` がない）と、シェーダーを1つ変えると全部のシェーダーを1つずつ作り直していたことを見つけ、`/MP`・`/LTCG:INCREMENTAL` の設定と、`#include` をたどって必要な分だけ並列で作り直す事前コンパイルにしました。変える前と後で同じ条件のビルド時間を測りました（ヘッダーを変えたとき 357秒 → 69秒など）。途中で、`file_time_type::min()` が Windows の `min` マクロとぶつかるコンパイルエラーを出し、直しました（11月10日）。

作者が「FPS が120で安定しない」「Debug も軽くしたい」と伝えたのを受け、Claude Code（Anthropic）が、計測モードを垂直同期あり・なし、Release・Debug で測り比べ、Release は全地点で120fps を保っていて、落ちていたのは Debug 版（CPU の時間が Release の5〜9倍）だと突き止めました。一時的にデバッグレイヤーを切った版、「マイコードのみ」を切った版、`inline` の関数を埋め込む版（`/Ob1`）を順に測り、効いた2つ（デバッグレイヤーを `--d3d-debug` を付けたときだけにする、「マイコードのみ」を切る）だけを残しました。バックバッファを3枚にする案と `/Ob1` は、測って効果がはっきりしなかったので戻しました（11月12日）。

Claude Code（Anthropic）が、作者が用意した効果音（MP3）をWAVに変換し、足音の1歩分などを切り出して差し替えました（研究所の中と外の森で足音を分け、鍵のかかった扉・水滴・排水の音を新しく分けた）。どの音をどこに使うかは作者が決めました。

Claude Code（Anthropic）が、モデルの読み込みを速くしました（`StaticMesh` が Assimp で変換した結果を `cache/model/` に保存し、次からはそれを読む。元のモデルが変わったら読み直す）。確認のときだけ読み込みの時間をファイルに書き出す一時的なコードを入れ、Debug 版の森で1回目（Assimp で読んで保存）と2回目（保存から読む）を比べ、自動撮影の画像が1画素も変わらないことを確かめました。1面→2面の自動撮影も2回比べ、差が出た2面の数枚は、変更前の版どうしでも同じだけ揺れる場面（動く人影のタイミング）だと確かめました。一時的なコードは確認の後に消しました。
続けて Claude Code（Anthropic）が、外の森の岩を岩の PBR の材質で描くようにしました（`shader/rockSurface.hlsli`・`rockPS.hlsl`、`Wall::SetVisualModel` での切り替え）。どちらも作者の指示（モデルの読み込みを速くする、岩にも PBR を付ける）です。確認のときだけ自動撮影に岩へ近づく道順と、岩のシェーダーを切る一時的なコードを入れ、同じ視点で前と後を見比べました。最初は鉱物の粒がマス目どおりに並んで水玉模様に見え、ひびが丸い輪になっていたため、粒の位置をばらつかせ、ひびの座標をノイズで折り曲げて撮り直しました。一時的なコードは確認の後に消しました。
続けて Claude Code（Anthropic）が、作者の指示で、枯れ木・倒木・枯れた茂みを樹皮の PBR の材質で描くようにしました（`shader/barkSurface.hlsli`・`barkPS.hlsl`、`Wall` から木目の向きを渡す仕組み）。確認のときだけ、自動撮影に幹と倒木へ近づく道順・撮った瞬間のカメラの位置の記録・樹皮のシェーダーを切る一時的なコードを入れ、同じ視点で前と後を見比べました。撮影の時刻がキーフレームより少し後になって次の場所へ動き始めていたこと、目の高さが地面から約40あることを位置の記録で見つけ、止まる区間と見下ろす角度を直して撮り直しました。最初は溝が格子に沿った階段の形になり、次に溝が埋もれて見えず、板の横のひびが黒い四角になったため、ノイズの平均・溝の作り方・ひびの端の消し方を順に直しました。一時的なコードは確認の後に消し、1面の自動撮影が変わっていないこと（24枚とも差0）も確かめました。

Codex（OpenAI）が、作者の指示でゲーム本体の F2 技術デモを外しました。通常描画の既定値と撮影・計測用の反射方式の指定を維持し、Release・Debug（x64）でビルドしました。

Codex（OpenAI）が、作者の指示で F1 のデバッグ画面に、明るい室内での水たまり反射と、森の草の LOD の色分けを追加しました。自動撮影にも対応する表示番号を追加しました。
