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
