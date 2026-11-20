# 数値定義の確認メモ

作品『通信途絶 / SIGNAL LOST』。2026年11月20日。比較元はローカルの `main` の `8b19375`。

[今回の後続確認と検証結果](#後続確認の総覧) ｜ [前回までの引き継ぎ](HANDOFF.md)

## 調査範囲

C++ 255本、HLSL 89本、UnityのC# 6本、PowerShell・Pythonの制作スクリプト18本の計368本を走査した。外部ライブラリ、Unityのキャッシュ、ビルド成果物、退役コードは集計対象外。コメントと文字列を外して数値リテラルを抽出し、描画の受け渡し、件数、進行条件、調整値の参照先を確認した。色・座標・字幕を含むデータ表、API形式、数式の数値も抽出されるため、抽出数は修正件数と異なる。

## 描画リソースを共有した34項目

C++とHLSLが [renderingConfig.hlsli](../Job_application_work_horror/shader/renderingConfig.hlsli) を読み、リソースを渡す側と受ける側で同じ番号を使う。番号を変更するときに両側を探して更新する手間と、修正漏れによる描画不良を減らす。

| 枠 | 共通定義 | C++の設定側 | HLSLの宣言側 |
|---|---|---|---|
| `b0` | `SIGNAL_LOST_WORLD_BUFFER_SLOT` | [Renderer.cpp](../Job_application_work_horror/Renderer.cpp) `WorldBufferSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `b1` | `SIGNAL_LOST_VIEW_BUFFER_SLOT` | [Renderer.cpp](../Job_application_work_horror/Renderer.cpp) `ViewBufferSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `b2` | `SIGNAL_LOST_PROJECTION_BUFFER_SLOT` | [Renderer.cpp](../Job_application_work_horror/Renderer.cpp) `ProjectionBufferSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `b3` | `SIGNAL_LOST_LIGHT_BUFFER_SLOT` | [Renderer.cpp](../Job_application_work_horror/Renderer.cpp) `LightBufferSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl)、[volumetricFlashlightPS.hlsl](../Job_application_work_horror/shader/volumetricFlashlightPS.hlsl) |
| `b4` | `SIGNAL_LOST_MATERIAL_BUFFER_SLOT` | [Renderer.cpp](../Job_application_work_horror/Renderer.cpp) `MaterialBufferSlot`、[Material.h](../Job_application_work_horror/Material.h) `ConstantBufferSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `b5` | `SIGNAL_LOST_TEXTURE_TRANSFORM_BUFFER_SLOT` | [Renderer.cpp](../Job_application_work_horror/Renderer.cpp) `TextureBufferSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `b6` | `SIGNAL_LOST_TILED_LIGHT_BUFFER_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) `ShadingParamsSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `b8` | `SIGNAL_LOST_FLASHLIGHT_SHADOW_BUFFER_SLOT` | [ShadowMap.cpp](../Job_application_work_horror/ShadowMap.cpp) `ShadowBufferSlot` | [flashlightShadow.hlsli](../Job_application_work_horror/shader/flashlightShadow.hlsli)、[grassVS.hlsl](../Job_application_work_horror/shader/grassVS.hlsl)、[litTextureVS.hlsl](../Job_application_work_horror/shader/litTextureVS.hlsl)、[rainVS.hlsl](../Job_application_work_horror/shader/rainVS.hlsl)、[shadowDepthVS.hlsl](../Job_application_work_horror/shader/shadowDepthVS.hlsl)、[volumetricFlashlightPS.hlsl](../Job_application_work_horror/shader/volumetricFlashlightPS.hlsl) |
| `b9` | `SIGNAL_LOST_OBJECT_DRAW_BUFFER_SLOT` | [FirstPersonHands.cpp](../Job_application_work_horror/FirstPersonHands.cpp) `ViewModelBufferSlot`、[ForestGrass.cpp](../Job_application_work_horror/ForestGrass.cpp) `GrassDrawBufferSlot`、[ForestRain.cpp](../Job_application_work_horror/ForestRain.cpp) `RainDrawBufferSlot` | [viewModel.hlsli](../Job_application_work_horror/shader/viewModel.hlsli)、[grassVS.hlsl](../Job_application_work_horror/shader/grassVS.hlsl)、[rainVS.hlsl](../Job_application_work_horror/shader/rainVS.hlsl) |
| `b10` | `SIGNAL_LOST_WET_FLOOR_BUFFER_SLOT` | [Ground.cpp](../Job_application_work_horror/Ground.cpp) `WetFloorBufferSlot` | [wetFloorPS.hlsl](../Job_application_work_horror/shader/wetFloorPS.hlsl) |
| `b11` | `SIGNAL_LOST_ROOM_OCCLUSION_BUFFER_SLOT` | [RendererState.cpp](../Job_application_work_horror/RendererState.cpp) `RoomOcclusionBufferSlot`、[DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) `RoomShapeSlot` | [roomOcclusion.hlsli](../Job_application_work_horror/shader/roomOcclusion.hlsli) |
| `b12` | `SIGNAL_LOST_WATER_CAUSTICS_BUFFER_SLOT` | [RendererState.cpp](../Job_application_work_horror/RendererState.cpp) `WaterCausticsBufferSlot` | [waterCausticsLayout.hlsli](../Job_application_work_horror/shader/waterCausticsLayout.hlsli) |
| `b13` | `SIGNAL_LOST_PUDDLE_CELL_BUFFER_SLOT` | [Ground.cpp](../Job_application_work_horror/Ground.cpp) `PuddleCellBufferSlot`、[WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp) `PuddleCellBufferSlot` | [puddleMask.hlsli](../Job_application_work_horror/shader/puddleMask.hlsli) |
| `t1` | `SIGNAL_LOST_CRT_ECHO_TEXTURE_SLOT` | [FullScreenQuad.cpp](../Job_application_work_horror/FullScreenQuad.cpp) `EchoTextureSlot` | [crtOverlayPS.hlsl](../Job_application_work_horror/shader/crtOverlayPS.hlsl) |
| `t2` | `SIGNAL_LOST_POINT_SHADOW_TEXTURE_SLOT` | [PointShadowMap.cpp](../Job_application_work_horror/PointShadowMap.cpp) `ShadowSlot` | [pointShadow.hlsli](../Job_application_work_horror/shader/pointShadow.hlsli) |
| `t3` | `SIGNAL_LOST_AUTO_EXPOSURE_TEXTURE_SLOT` | [FullScreenQuad.cpp](../Job_application_work_horror/FullScreenQuad.cpp) `AutoExposureTextureSlot` | [exposurePS.hlsl](../Job_application_work_horror/shader/exposurePS.hlsl) |
| `t4` | `SIGNAL_LOST_SSR_SCENE_TEXTURE_SLOT` | [ScreenSpaceReflection.cpp](../Job_application_work_horror/ScreenSpaceReflection.cpp) `SceneSlot` | [rayReflection.hlsli](../Job_application_work_horror/shader/rayReflection.hlsli) |
| `t5` | `SIGNAL_LOST_FLASHLIGHT_SHADOW_TEXTURE_SLOT` | [ShadowMap.cpp](../Job_application_work_horror/ShadowMap.cpp) `ShadowTextureSlot` | [flashlightShadow.hlsli](../Job_application_work_horror/shader/flashlightShadow.hlsli)、[volumetricFlashlightPS.hlsl](../Job_application_work_horror/shader/volumetricFlashlightPS.hlsl) |
| `t6` | `SIGNAL_LOST_SELF_VOLUME_TEXTURE_SLOT` | [FigureVolumes.cpp](../Job_application_work_horror/FigureVolumes.cpp) `SelfSlot` | [figureVolume.hlsli](../Job_application_work_horror/shader/figureVolume.hlsli) |
| `t7` | `SIGNAL_LOST_REFLECTION_PROBE_TEXTURE_SLOT` | [ReflectionProbe.h](../Job_application_work_horror/ReflectionProbe.h) `DefaultTextureSlot` | [wetFloorPS.hlsl](../Job_application_work_horror/shader/wetFloorPS.hlsl) |
| `t8` | `SIGNAL_LOST_SSR_DEPTH_TEXTURE_SLOT` | [ScreenSpaceReflection.cpp](../Job_application_work_horror/ScreenSpaceReflection.cpp) `DepthSlot` | [rayReflection.hlsli](../Job_application_work_horror/shader/rayReflection.hlsli) |
| `t9` | `SIGNAL_LOST_RIPPLE_HEIGHT_TEXTURE_SLOT` | [RendererState.cpp](../Job_application_work_horror/RendererState.cpp) `RippleHeightTextureSlot`、[WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp) `RippleHeightTextureSlot` | [waterCaustics.hlsli](../Job_application_work_horror/shader/waterCaustics.hlsli) |
| `t10` | `SIGNAL_LOST_TILED_POINT_LIGHTS_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) `PointLightSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `t11` | `SIGNAL_LOST_TILED_LIGHT_INDICES_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) `TileIndexSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `t12` | `SIGNAL_LOST_TILED_LIGHT_COUNTS_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) `TileCountSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `t13` | `SIGNAL_LOST_FIGURE_VOLUME_TEXTURE_SLOT` | [FigureVolumes.cpp](../Job_application_work_horror/FigureVolumes.cpp) `FigureSlot` | [figureVolume.hlsli](../Job_application_work_horror/shader/figureVolume.hlsli) |
| `t14` | `SIGNAL_LOST_ENVIRONMENT_PROBE_TEXTURE_SLOT` | [Game.cpp](../Job_application_work_horror/Game.cpp) `EnvironmentProbeTextureSlot` | [pbrLighting.hlsli](../Job_application_work_horror/shader/pbrLighting.hlsli)、[rayReflection.hlsli](../Job_application_work_horror/shader/rayReflection.hlsli) |
| `t15` | `SIGNAL_LOST_SCREEN_AO_TEXTURE_SLOT` | [AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp) `OcclusionSlot` | [screenAmbientOcclusion.hlsli](../Job_application_work_horror/shader/screenAmbientOcclusion.hlsli) |
| `t16` | `SIGNAL_LOST_LIGHT_SHAFT_TEXTURE_SLOT` | [LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp) `ShaftResultSlot` | [horrorMist.hlsli](../Job_application_work_horror/shader/horrorMist.hlsli) |
| `t17` | `SIGNAL_LOST_SSR_PREVIOUS_DEPTH_TEXTURE_SLOT` | [ScreenSpaceReflection.cpp](../Job_application_work_horror/ScreenSpaceReflection.cpp) `PreviousDepthSlot` | [rayReflection.hlsli](../Job_application_work_horror/shader/rayReflection.hlsli) |
| `t18` | `SIGNAL_LOST_SCREEN_INDIRECT_LIGHT_TEXTURE_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) `IndirectLightSlot` | [screenAmbientOcclusion.hlsli](../Job_application_work_horror/shader/screenAmbientOcclusion.hlsli) |
| `t19` | `SIGNAL_LOST_POINT_LIGHT_WALL_MASKS_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) `WallMaskSlot` | [common.hlsl](../Job_application_work_horror/shader/common.hlsl) |
| `s1` | `SIGNAL_LOST_SHADOW_COMPARISON_SAMPLER_SLOT` | [ShadowMap.cpp](../Job_application_work_horror/ShadowMap.cpp) `ComparisonSamplerSlot` | [flashlightShadow.hlsli](../Job_application_work_horror/shader/flashlightShadow.hlsli) |
| `s2` | `SIGNAL_LOST_FIGURE_VOLUME_SAMPLER_SLOT` | [FigureVolumes.cpp](../Job_application_work_horror/FigureVolumes.cpp) `SamplerSlot` | [figureVolume.hlsli](../Job_application_work_horror/shader/figureVolume.hlsli) |

## ゲーム・UI・材質・制作ツールの変更

| 対象 | 元の定義 | 修正内容と理由 |
|---|---|---|
| タイトルの開始先 | 入力側の4件、遷移先、HUDの見出し・説明を個別に管理 | 行のタイトル・説明・シーンを [TitleStageChoices.h](../Job_application_work_horror/TitleStageChoices.h) の1行にまとめた。入力の上限とHUDの表示件数も行数から求める。 |
| 操作説明 | 左下の説明の件数3 | 表の要素数を走査して、説明を足したときの更新漏れを防ぐ。 [HudScreens.cpp](../Job_application_work_horror/HudScreens.cpp) |
| 暗証番号の警告 | ミス3回を演出とHUDが別々に判定 | `KeypadLock::WarningMistakeCount` を共通参照し、物音と案内を同じ条件で出す。 [KeypadLock.h](../Job_application_work_horror/KeypadLock.h)、[Stage1KeypadDoor.cpp](../Job_application_work_horror/Stage1KeypadDoor.cpp)、[HudScreens.cpp](../Job_application_work_horror/HudScreens.cpp) |
| 左の倉庫の演出 | ヒューズ数が2以上で終了 | `StorageScareEndsAtFuseCount` に命名し、2本目を取る前の演出だと読めるようにした。 [StageSceneEvents.cpp](../Job_application_work_horror/StageSceneEvents.cpp) |
| 2面の異変 | 2周目を数値2で指定 | デバッグ表示とリザルトの異変登録を `Stage2SecondLoop` に揃えた。 [Stage2Scene.cpp](../Job_application_work_horror/Stage2Scene.cpp) |
| 2面の材質 | 肖像画・時計・偽の扉の通常色をC++とHLSLで個別に定義 | 材質の通常色と色比率の計算で使う基準色を共通にした。異変で色を変えるときも基準が揃う。 [Stage2Layout.cpp](../Job_application_work_horror/Stage2Layout.cpp)、[litTexturePS.hlsl](../Job_application_work_horror/shader/litTexturePS.hlsl) |
| 時計の文字盤 | 数値1、2、3と空白0をグリフの比較と表に使用 | I・V・X・空白に名前を付け、2ビットの並びは同じ表現を使う。 [clockFace.hlsli](../Job_application_work_horror/shader/clockFace.hlsli) |
| 森の無線・導入 | 無線の行数3と、導入の時刻表の配列幅を別管理 | 無線は本文の配列から行数を求め、森の発生位置と導入の時間表の件数をコンパイル時に確認する。使われていなかった導入の4行の試案と時刻表を整理した。 [StoryText.h](../Job_application_work_horror/StoryText.h)、[ForestRadio.h](../Job_application_work_horror/ForestRadio.h)、[IntroScene.h](../Job_application_work_horror/IntroScene.h)、[IntroScene.cpp](../Job_application_work_horror/IntroScene.cpp) |
| Unityの配置エディター | 候補を2件で打ち切る `.Take(2)` | `ambiguousMatchCount` として、一意に特定できるかを確認する件数だと明示した。 [MapLayoutEditor.cs](../MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs) |

### 音声スクリプトで名前を付けた調整値

| 対象 | 調整項目 |
|---|---|
| [generate-rain.ps1](../Job_application_work_horror/tools/generate-rain.ps1) | `$rainHighCutHz = 6000.0`、`$rainLowCutHz = 500.0`、`$rumbleCutHz = 250.0`、`$fineDropsPerSecond = 90.0`、`$largeDropsPerSecond = 2.0`、`$dropDecayLengths = 5.0`、`$tailBlendSeconds = 0.5`、`$targetPeak = 0.7` |
| [generate-radio-static.ps1](../Job_application_work_horror/tools/generate-radio-static.ps1) | `$lowPassCutoffHz = 3000.0`、`$highPassCutoffHz = 300.0`、`$crackleMinSeconds = 0.004`、`$crackleExtraSeconds = 0.020`、`$crackleMinGain = 0.6`、`$crackleExtraGain = 0.9`、`$loopBlendSeconds = 0.25`、`$targetPeak = 0.7`、`$wobbleBase = 0.72`、`$wobbleFirstGain = 0.18`、`$wobbleSecondGain = 0.10`、`$wobbleFirstHz = 2.3`、`$wobbleSecondHz = 0.7`、`$wobbleSecondPhase = 1.3`、`$staticGain = 2.2` |
| [generate-pipe-knock.ps1](../Job_application_work_horror/tools/generate-pipe-knock.ps1) | `$thumpGain = 0.45`、`$thumpDecaySeconds = 0.06`、`$thumpFrequencyHz = 74.0`、`$impactNoiseSeconds = 0.012`、`$impactNoiseGain = 0.35`、`$attackSeconds = 0.002`、`$tailFadeSeconds = 0.3`、`$targetPeak = 0.8` |
| [generate-heartbeat-breath.ps1](../Job_application_work_horror/tools/generate-heartbeat-breath.ps1) | `$secondBeatSeconds = 0.235`、`$heartFadeSeconds = 0.1`、`$heartPeak = 0.85`、`$exhaleStart = 0.05`、`$exhaleEnd = 0.95`、`$exhaleLength = 0.90`、`$inhaleStart = 1.15`、`$inhaleEnd = 1.95`、`$inhaleLength = 0.80`、`$inhaleGain = 0.55`、`$breathPeak = 0.7`、`$breathUpperBandResponse = 0.22`、`$breathLowerBandResponse = 0.04` |

## 数値を読むときの判断基準

| 分類 | 根拠・例 |
|---|---|
| 設定の定義 | 調整の意味が名前で分かる値を集約する。例はヒューズの必要数、音が鳴る距離、グループ幅。 |
| データ表 | 各列の意味が型・構造・見出しで分かる形で持つ。例はステージの座標、RGB成分、字幕の時刻、乱数の標本、文字のビット列。 |
| 数式 | 変換式を読める形で表す。例はUVからNDCへの `2 * uv - 1`、標本標準偏差の `n - 1` と計算可能な最小件数、矩形の半寸法。 |
| 形式・局所的な添字 | WAVのフィールド幅、色や頂点の並び、2ビットずつ詰めた時計の文字、同じパスの連続した一時領域は、宣言や表の見出しから意味を追う。CPUとGPUなど別の場所で共有する仕様は、共有定義と照合で管理する。 |

今回確認したのは、現在の自作ソースに対する静的な抽出と参照先の照合。プレイ中の視認性や音の聞こえ方は、実際の場面で確認する項目。

## 更新ファイル一覧

- [Job_application_work_horror/AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp)
- [Job_application_work_horror/DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp)
- [Job_application_work_horror/FigureVolumes.cpp](../Job_application_work_horror/FigureVolumes.cpp)
- [Job_application_work_horror/FirstPersonHands.cpp](../Job_application_work_horror/FirstPersonHands.cpp)
- [Job_application_work_horror/ForestGrass.cpp](../Job_application_work_horror/ForestGrass.cpp)
- [Job_application_work_horror/ForestRadio.cpp](../Job_application_work_horror/ForestRadio.cpp)
- [Job_application_work_horror/ForestRadio.h](../Job_application_work_horror/ForestRadio.h)
- [Job_application_work_horror/ForestRain.cpp](../Job_application_work_horror/ForestRain.cpp)
- [Job_application_work_horror/FullScreenQuad.cpp](../Job_application_work_horror/FullScreenQuad.cpp)
- [Job_application_work_horror/Game.cpp](../Job_application_work_horror/Game.cpp)
- [Job_application_work_horror/Ground.cpp](../Job_application_work_horror/Ground.cpp)
- [Job_application_work_horror/HudScreens.cpp](../Job_application_work_horror/HudScreens.cpp)
- [Job_application_work_horror/IntroScene.cpp](../Job_application_work_horror/IntroScene.cpp)
- [Job_application_work_horror/IntroScene.h](../Job_application_work_horror/IntroScene.h)
- [Job_application_work_horror/Job_application_work_horror.vcxproj](../Job_application_work_horror/Job_application_work_horror.vcxproj)
- [Job_application_work_horror/Job_application_work_horror.vcxproj.filters](../Job_application_work_horror/Job_application_work_horror.vcxproj.filters)
- [Job_application_work_horror/KeypadLock.h](../Job_application_work_horror/KeypadLock.h)
- [Job_application_work_horror/LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp)
- [Job_application_work_horror/Material.h](../Job_application_work_horror/Material.h)
- [Job_application_work_horror/PointShadowMap.cpp](../Job_application_work_horror/PointShadowMap.cpp)
- [Job_application_work_horror/ReflectionProbe.h](../Job_application_work_horror/ReflectionProbe.h)
- [Job_application_work_horror/Renderer.cpp](../Job_application_work_horror/Renderer.cpp)
- [Job_application_work_horror/RendererState.cpp](../Job_application_work_horror/RendererState.cpp)
- [Job_application_work_horror/ScreenSpaceReflection.cpp](../Job_application_work_horror/ScreenSpaceReflection.cpp)
- [Job_application_work_horror/ShadowMap.cpp](../Job_application_work_horror/ShadowMap.cpp)
- [Job_application_work_horror/Stage1KeypadDoor.cpp](../Job_application_work_horror/Stage1KeypadDoor.cpp)
- [Job_application_work_horror/Stage2Layout.cpp](../Job_application_work_horror/Stage2Layout.cpp)
- [Job_application_work_horror/Stage2Scene.cpp](../Job_application_work_horror/Stage2Scene.cpp)
- [Job_application_work_horror/StageSceneEvents.cpp](../Job_application_work_horror/StageSceneEvents.cpp)
- [Job_application_work_horror/StoryText.h](../Job_application_work_horror/StoryText.h)
- [Job_application_work_horror/TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp)
- [Job_application_work_horror/TitleScene.cpp](../Job_application_work_horror/TitleScene.cpp)
- [Job_application_work_horror/TitleScene.h](../Job_application_work_horror/TitleScene.h)
- [Job_application_work_horror/TitleStageChoices.h](../Job_application_work_horror/TitleStageChoices.h)
- [Job_application_work_horror/WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp)
- [Job_application_work_horror/shader/clockFace.hlsli](../Job_application_work_horror/shader/clockFace.hlsli)
- [Job_application_work_horror/shader/common.hlsl](../Job_application_work_horror/shader/common.hlsl)
- [Job_application_work_horror/shader/crtOverlayPS.hlsl](../Job_application_work_horror/shader/crtOverlayPS.hlsl)
- [Job_application_work_horror/shader/exposurePS.hlsl](../Job_application_work_horror/shader/exposurePS.hlsl)
- [Job_application_work_horror/shader/figureVolume.hlsli](../Job_application_work_horror/shader/figureVolume.hlsli)
- [Job_application_work_horror/shader/flashlightShadow.hlsli](../Job_application_work_horror/shader/flashlightShadow.hlsli)
- [Job_application_work_horror/shader/grassVS.hlsl](../Job_application_work_horror/shader/grassVS.hlsl)
- [Job_application_work_horror/shader/horrorMist.hlsli](../Job_application_work_horror/shader/horrorMist.hlsli)
- [Job_application_work_horror/shader/litTexturePS.hlsl](../Job_application_work_horror/shader/litTexturePS.hlsl)
- [Job_application_work_horror/shader/litTextureVS.hlsl](../Job_application_work_horror/shader/litTextureVS.hlsl)
- [Job_application_work_horror/shader/pbrLighting.hlsli](../Job_application_work_horror/shader/pbrLighting.hlsli)
- [Job_application_work_horror/shader/pointShadow.hlsli](../Job_application_work_horror/shader/pointShadow.hlsli)
- [Job_application_work_horror/shader/puddleMask.hlsli](../Job_application_work_horror/shader/puddleMask.hlsli)
- [Job_application_work_horror/shader/rainVS.hlsl](../Job_application_work_horror/shader/rainVS.hlsl)
- [Job_application_work_horror/shader/rayReflection.hlsli](../Job_application_work_horror/shader/rayReflection.hlsli)
- [Job_application_work_horror/shader/renderingConfig.hlsli](../Job_application_work_horror/shader/renderingConfig.hlsli)
- [Job_application_work_horror/shader/roomOcclusion.hlsli](../Job_application_work_horror/shader/roomOcclusion.hlsli)
- [Job_application_work_horror/shader/screenAmbientOcclusion.hlsli](../Job_application_work_horror/shader/screenAmbientOcclusion.hlsli)
- [Job_application_work_horror/shader/shadowDepthVS.hlsl](../Job_application_work_horror/shader/shadowDepthVS.hlsl)
- [Job_application_work_horror/shader/viewModel.hlsli](../Job_application_work_horror/shader/viewModel.hlsli)
- [Job_application_work_horror/shader/volumetricFlashlightPS.hlsl](../Job_application_work_horror/shader/volumetricFlashlightPS.hlsl)
- [Job_application_work_horror/shader/waterCaustics.hlsli](../Job_application_work_horror/shader/waterCaustics.hlsli)
- [Job_application_work_horror/shader/waterCausticsLayout.hlsli](../Job_application_work_horror/shader/waterCausticsLayout.hlsli)
- [Job_application_work_horror/shader/wetFloorPS.hlsl](../Job_application_work_horror/shader/wetFloorPS.hlsl)
- [Job_application_work_horror/tools/generate-heartbeat-breath.ps1](../Job_application_work_horror/tools/generate-heartbeat-breath.ps1)
- [Job_application_work_horror/tools/generate-pipe-knock.ps1](../Job_application_work_horror/tools/generate-pipe-knock.ps1)
- [Job_application_work_horror/tools/generate-radio-static.ps1](../Job_application_work_horror/tools/generate-radio-static.ps1)
- [Job_application_work_horror/tools/generate-rain.ps1](../Job_application_work_horror/tools/generate-rain.ps1)
- [MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs](../MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs)

前回までに確認した項目と検証結果は [HANDOFF.md](HANDOFF.md) に記録している。

## 検証結果
- Release・Debug（x64）ともに MSBuild 成功。警告・エラーは各0件。検証用の出力先と /Z7 を使用した。
- SM 5.0、FXC /O3 でシェーダー54本のバイトコードが変更前後で一致した。時計の符号値を命名した後も、時計を参照する2本を再比較して一致した。
- UnityのC# 6本をコンパイルし、警告・エラー0件。タイトルの4つの遷移先と表示文の並びも変更前後で照合した。
- 変更した制作スクリプトで書き出した rain、radio_static、pipe_knock、heartbeat、breath のWAVは5本すべてバイト単位で一致した。
- 実プレイとUnityエディター内の操作は未検証。

## 後続確認の総覧

自作ソース369本（C++ 255、HLSL 89、Unity C# 6、制作スクリプト19）を再走査。今回の変更はソース・制作スクリプト40ファイル。前回までの修正を含め、提出前に見る項目を次の表から辿れる。

| 範囲 | 今回と前回までの対応 |
|---|---|
| 描画・計算 | 画面に表示するリソースのバインド先は前節の34項目。今回、Compute Shaderのバインド先33項目と画面合成のバッファ3項目も同じ値を参照するようにした。自動露出の輝度・倍率の添字、入力・出力の連続性も確認した。 |
| 演出・進行 | 照明が迫る演出、電力復旧、出口送電、出口の前兆を、時刻に対応するイベント名で管理する。前回までの人影、ヒューズ、目的表示、異変、台詞、設定順の修正は [HANDOFF.md](HANDOFF.md) の11月19・20日の欄にまとまっている。 |
| 制作・配布 | WAVの出力5スクリプトがモノラルPCMのヘッダーを共有する。水音の切り出しはRIFF/WAVEの識別子を確認する。日次撮影の再実行ごとの保存先と、配布ZIPの完了判定は [HANDOFF.md](HANDOFF.md) を参照。 |
| 動作確認の範囲 | ゲームを使わないビルド・出力比較は下記の結果。暗さ、誘導、演出の見え方、GPU時間は実プレイを伴う確認項目。 |

### Compute Shaderの共通定義

番号の照合先は [computeDispatchConfig.hlsli](../Job_application_work_horror/shader/computeDispatchConfig.hlsli)。C++の計算パスとHLSLから同じ名前で参照する。

| 枠 | 定義 | 実行側 | HLSL側 |
|---|---|---|---|
| `b0` | `SIGNAL_LOST_AO_PARAMS_SLOT` | [AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp) | [ambientOcclusionCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionCS.hlsl)、[ambientOcclusionBlurCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionBlurCS.hlsl) |
| `t0` | `SIGNAL_LOST_AO_DEPTH_SLOT` | [AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp) | [ambientOcclusionCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionCS.hlsl)、[ambientOcclusionBlurCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionBlurCS.hlsl) |
| `t1` | `SIGNAL_LOST_AO_SECOND_INPUT_SLOT` | [AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp) | [ambientOcclusionCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionCS.hlsl)、[ambientOcclusionBlurCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionBlurCS.hlsl) |
| `u0` | `SIGNAL_LOST_AO_OUTPUT_SLOT` | [AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp) | [ambientOcclusionCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionCS.hlsl)、[ambientOcclusionBlurCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionBlurCS.hlsl) |
| `b0` | `SIGNAL_LOST_DDGI_PARAMS_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiCommon.hlsli](../Job_application_work_horror/shader/ddgiCommon.hlsli) |
| `s0` | `SIGNAL_LOST_DDGI_SAMPLER_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiCommon.hlsli](../Job_application_work_horror/shader/ddgiCommon.hlsli) |
| `t0` | `SIGNAL_LOST_DDGI_POINT_LIGHTS_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiTraceCS.hlsl](../Job_application_work_horror/shader/ddgiTraceCS.hlsl) |
| `t1` | `SIGNAL_LOST_DDGI_RAY_RESULTS_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiUpdate.hlsli](../Job_application_work_horror/shader/ddgiUpdate.hlsli) |
| `t2` | `SIGNAL_LOST_DDGI_IRRADIANCE_ATLAS_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiCommon.hlsli](../Job_application_work_horror/shader/ddgiCommon.hlsli) |
| `t3` | `SIGNAL_LOST_DDGI_DISTANCE_ATLAS_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiCommon.hlsli](../Job_application_work_horror/shader/ddgiCommon.hlsli) |
| `t4` | `SIGNAL_LOST_DDGI_PREVIOUS_ATLAS_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiUpdate.hlsli](../Job_application_work_horror/shader/ddgiUpdate.hlsli) |
| `t5` | `SIGNAL_LOST_DDGI_SCENE_DEPTH_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiApplyCS.hlsl](../Job_application_work_horror/shader/ddgiApplyCS.hlsl) |
| `u0` | `SIGNAL_LOST_DDGI_OUTPUT_SLOT` | [DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp) | [ddgiTraceCS.hlsl](../Job_application_work_horror/shader/ddgiTraceCS.hlsl)、[ddgiUpdate.hlsli](../Job_application_work_horror/shader/ddgiUpdate.hlsli)、[ddgiApplyCS.hlsl](../Job_application_work_horror/shader/ddgiApplyCS.hlsl) |
| `b0` | `SIGNAL_LOST_GRASS_SIM_PARAMS_SLOT` | [ForestGrass.cpp](../Job_application_work_horror/ForestGrass.cpp) | [grassCS.hlsl](../Job_application_work_horror/shader/grassCS.hlsl) |
| `t0` | `SIGNAL_LOST_GRASS_SIM_INPUT_SLOT` | [ForestGrass.cpp](../Job_application_work_horror/ForestGrass.cpp) | [grassCS.hlsl](../Job_application_work_horror/shader/grassCS.hlsl) |
| `u0` | `SIGNAL_LOST_GRASS_SIM_OUTPUT_SLOT` | [ForestGrass.cpp](../Job_application_work_horror/ForestGrass.cpp) | [grassCS.hlsl](../Job_application_work_horror/shader/grassCS.hlsl) |
| `b0` | `SIGNAL_LOST_SHAFT_PARAMS_SLOT` | [LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp) | [lightShaftsCS.hlsl](../Job_application_work_horror/shader/lightShaftsCS.hlsl) |
| `t0` | `SIGNAL_LOST_SHAFT_DEPTH_SLOT` | [LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp) | [lightShaftsCS.hlsl](../Job_application_work_horror/shader/lightShaftsCS.hlsl) |
| `t1` | `SIGNAL_LOST_SHAFT_POINT_LIGHTS_SLOT` | [LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp) | [lightShaftsCS.hlsl](../Job_application_work_horror/shader/lightShaftsCS.hlsl) |
| `u0` | `SIGNAL_LOST_SHAFT_OUTPUT_SLOT` | [LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp) | [lightShaftsCS.hlsl](../Job_application_work_horror/shader/lightShaftsCS.hlsl) |
| `b0` | `SIGNAL_LOST_TILE_CULL_PARAMS_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) | [tiledLightCullingCS.hlsl](../Job_application_work_horror/shader/tiledLightCullingCS.hlsl) |
| `t0` | `SIGNAL_LOST_TILE_CULL_LIGHTS_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) | [tiledLightCullingCS.hlsl](../Job_application_work_horror/shader/tiledLightCullingCS.hlsl) |
| `t1` | `SIGNAL_LOST_TILE_CULL_DEPTH_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) | [tiledLightCullingCS.hlsl](../Job_application_work_horror/shader/tiledLightCullingCS.hlsl) |
| `u0` | `SIGNAL_LOST_TILE_CULL_INDICES_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) | [tiledLightCullingCS.hlsl](../Job_application_work_horror/shader/tiledLightCullingCS.hlsl) |
| `u1` | `SIGNAL_LOST_TILE_CULL_COUNTS_SLOT` | [TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp) | [tiledLightCullingCS.hlsl](../Job_application_work_horror/shader/tiledLightCullingCS.hlsl) |
| `b0` | `SIGNAL_LOST_RIPPLE_PARAMS_SLOT` | [WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp) | [waterRippleCS.hlsl](../Job_application_work_horror/shader/waterRippleCS.hlsl) |
| `t0` | `SIGNAL_LOST_RIPPLE_PREVIOUS_STATE_SLOT` | [WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp) | [waterRippleCS.hlsl](../Job_application_work_horror/shader/waterRippleCS.hlsl) |
| `u0` | `SIGNAL_LOST_RIPPLE_NEXT_STATE_SLOT` | [WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp) | [waterRippleCS.hlsl](../Job_application_work_horror/shader/waterRippleCS.hlsl) |
| `t0` | `SIGNAL_LOST_BLOOM_COMPUTE_INPUT_SLOT` | [PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp) | [bloomExtractCS.hlsl](../Job_application_work_horror/shader/bloomExtractCS.hlsl)、[bloomDownsampleCS.hlsl](../Job_application_work_horror/shader/bloomDownsampleCS.hlsl)、[bloomBlurHorizontalCS.hlsl](../Job_application_work_horror/shader/bloomBlurHorizontalCS.hlsl)、[bloomBlurVerticalCS.hlsl](../Job_application_work_horror/shader/bloomBlurVerticalCS.hlsl) |
| `u0` | `SIGNAL_LOST_BLOOM_COMPUTE_OUTPUT_SLOT` | [PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp) | [bloomExtractCS.hlsl](../Job_application_work_horror/shader/bloomExtractCS.hlsl)、[bloomDownsampleCS.hlsl](../Job_application_work_horror/shader/bloomDownsampleCS.hlsl)、[bloomBlurHorizontalCS.hlsl](../Job_application_work_horror/shader/bloomBlurHorizontalCS.hlsl)、[bloomBlurVerticalCS.hlsl](../Job_application_work_horror/shader/bloomBlurVerticalCS.hlsl) |
| `b0` | `SIGNAL_LOST_AUTO_EXPOSURE_PARAMS_SLOT` | [PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp) | [autoExposureCS.hlsl](../Job_application_work_horror/shader/autoExposureCS.hlsl) |
| `t0` | `SIGNAL_LOST_AUTO_EXPOSURE_SCENE_SLOT` | [PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp) | [autoExposureCS.hlsl](../Job_application_work_horror/shader/autoExposureCS.hlsl) |
| `u0` | `SIGNAL_LOST_AUTO_EXPOSURE_STATE_SLOT` | [PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp) | [autoExposureCS.hlsl](../Job_application_work_horror/shader/autoExposureCS.hlsl) |

### 調整・整合性の修正

| 修正箇所 | 修正理由 |
|---|---|
| 画面効果・トーンマッピング・画面の埃の定数バッファ | 画面を合成する各パスで、C++とHLSLが同じバインド先を参照する。 [FullScreenQuad.cpp](../Job_application_work_horror/FullScreenQuad.cpp)、[ScreenDustOverlay.cpp](../Job_application_work_horror/ScreenDustOverlay.cpp)、[renderingConfig.hlsli](../Job_application_work_horror/shader/renderingConfig.hlsli) |
| 自動露出 | 状態の保存領域、計算パス、描画パスで「順応した対数輝度」と「露出倍率」の添字を共有する。 [PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp)、[autoExposureCS.hlsl](../Job_application_work_horror/shader/autoExposureCS.hlsl)、[exposurePS.hlsl](../Job_application_work_horror/shader/exposurePS.hlsl) |
| 1面の照明イベント | `case 0` などが時刻表の並びに依存していた。時刻とイベントを一行に置き、実行側の分岐もイベント名を参照する。 [ScareLightSequence.h](../Job_application_work_horror/ScareLightSequence.h)、[StagePowerSequence.h](../Job_application_work_horror/StagePowerSequence.h)、[ExitOmenSequence.h](../Job_application_work_horror/ExitOmenSequence.h)、[StageSceneEvents.cpp](../Job_application_work_horror/StageSceneEvents.cpp) |
| WAVの形式 | PCMの形式番号、サンプル幅、バイト数を共有し、各スクリプトの基本ヘッダーを一か所で書く。水音は切り出し前にRIFF/WAVEを確認する。 [MonoPcmWave.ps1](../Job_application_work_horror/tools/MonoPcmWave.ps1)、[generate-water-steps.ps1](../Job_application_work_horror/tools/generate-water-steps.ps1) |

### 今回更新したファイル

- [Job_application_work_horror/AmbientOcclusion.cpp](../Job_application_work_horror/AmbientOcclusion.cpp)
- [Job_application_work_horror/DDGIVolume.cpp](../Job_application_work_horror/DDGIVolume.cpp)
- [Job_application_work_horror/ExitOmenSequence.h](../Job_application_work_horror/ExitOmenSequence.h)
- [Job_application_work_horror/ForestGrass.cpp](../Job_application_work_horror/ForestGrass.cpp)
- [Job_application_work_horror/FullScreenQuad.cpp](../Job_application_work_horror/FullScreenQuad.cpp)
- [Job_application_work_horror/LightShafts.cpp](../Job_application_work_horror/LightShafts.cpp)
- [Job_application_work_horror/PostProcess.cpp](../Job_application_work_horror/PostProcess.cpp)
- [Job_application_work_horror/ScareLightSequence.h](../Job_application_work_horror/ScareLightSequence.h)
- [Job_application_work_horror/ScreenDustOverlay.cpp](../Job_application_work_horror/ScreenDustOverlay.cpp)
- [Job_application_work_horror/StagePowerSequence.h](../Job_application_work_horror/StagePowerSequence.h)
- [Job_application_work_horror/StageSceneEvents.cpp](../Job_application_work_horror/StageSceneEvents.cpp)
- [Job_application_work_horror/TiledLighting.cpp](../Job_application_work_horror/TiledLighting.cpp)
- [Job_application_work_horror/WaterRippleSimulation.cpp](../Job_application_work_horror/WaterRippleSimulation.cpp)
- [Job_application_work_horror/shader/PS_HorrorDust.hlsl](../Job_application_work_horror/shader/PS_HorrorDust.hlsl)
- [Job_application_work_horror/shader/ambientOcclusionBlurCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionBlurCS.hlsl)
- [Job_application_work_horror/shader/ambientOcclusionCS.hlsl](../Job_application_work_horror/shader/ambientOcclusionCS.hlsl)
- [Job_application_work_horror/shader/autoExposureCS.hlsl](../Job_application_work_horror/shader/autoExposureCS.hlsl)
- [Job_application_work_horror/shader/bloomBlurHorizontalCS.hlsl](../Job_application_work_horror/shader/bloomBlurHorizontalCS.hlsl)
- [Job_application_work_horror/shader/bloomBlurVerticalCS.hlsl](../Job_application_work_horror/shader/bloomBlurVerticalCS.hlsl)
- [Job_application_work_horror/shader/bloomDownsampleCS.hlsl](../Job_application_work_horror/shader/bloomDownsampleCS.hlsl)
- [Job_application_work_horror/shader/bloomExtractCS.hlsl](../Job_application_work_horror/shader/bloomExtractCS.hlsl)
- [Job_application_work_horror/shader/computeDispatchConfig.hlsli](../Job_application_work_horror/shader/computeDispatchConfig.hlsli)
- [Job_application_work_horror/shader/ddgiApplyCS.hlsl](../Job_application_work_horror/shader/ddgiApplyCS.hlsl)
- [Job_application_work_horror/shader/ddgiCommon.hlsli](../Job_application_work_horror/shader/ddgiCommon.hlsli)
- [Job_application_work_horror/shader/ddgiTraceCS.hlsl](../Job_application_work_horror/shader/ddgiTraceCS.hlsl)
- [Job_application_work_horror/shader/ddgiUpdate.hlsli](../Job_application_work_horror/shader/ddgiUpdate.hlsli)
- [Job_application_work_horror/shader/exposurePS.hlsl](../Job_application_work_horror/shader/exposurePS.hlsl)
- [Job_application_work_horror/shader/grassCS.hlsl](../Job_application_work_horror/shader/grassCS.hlsl)
- [Job_application_work_horror/shader/lightShaftsCS.hlsl](../Job_application_work_horror/shader/lightShaftsCS.hlsl)
- [Job_application_work_horror/shader/postProcessBuffer.hlsli](../Job_application_work_horror/shader/postProcessBuffer.hlsli)
- [Job_application_work_horror/shader/renderingConfig.hlsli](../Job_application_work_horror/shader/renderingConfig.hlsli)
- [Job_application_work_horror/shader/tiledLightCullingCS.hlsl](../Job_application_work_horror/shader/tiledLightCullingCS.hlsl)
- [Job_application_work_horror/shader/toneMapPS.hlsl](../Job_application_work_horror/shader/toneMapPS.hlsl)
- [Job_application_work_horror/shader/waterRippleCS.hlsl](../Job_application_work_horror/shader/waterRippleCS.hlsl)
- [Job_application_work_horror/tools/MonoPcmWave.ps1](../Job_application_work_horror/tools/MonoPcmWave.ps1)
- [Job_application_work_horror/tools/generate-heartbeat-breath.ps1](../Job_application_work_horror/tools/generate-heartbeat-breath.ps1)
- [Job_application_work_horror/tools/generate-pipe-knock.ps1](../Job_application_work_horror/tools/generate-pipe-knock.ps1)
- [Job_application_work_horror/tools/generate-radio-static.ps1](../Job_application_work_horror/tools/generate-radio-static.ps1)
- [Job_application_work_horror/tools/generate-rain.ps1](../Job_application_work_horror/tools/generate-rain.ps1)
- [Job_application_work_horror/tools/generate-water-steps.ps1](../Job_application_work_horror/tools/generate-water-steps.ps1)

### 最新の検証結果
- Release・Debug x64をビルドし、警告・エラー各0件。検証用出力先と /Z7 を使用。
- SM 5.0のシェーダー54本で、FXC /O3 のバイトコードが前後一致。
- 照明演出4系統について、約1/60秒・0.1秒・0.9秒の時間刻みで、変更前後のイベントと完了状態を比較し一致。
- WAV 7本が前後一致。奇数長のチャンクを含むWAVも読み込み、壊れたRIFF識別子はエラーとして扱った。
- ゲーム画面とUnityエディター上の操作は未確認。


### 再確認で見つかった1件

2面の信号盤の件数が判定、配置、信号の乱れ、HUDで重複していた。SignalPuzzle::SignalCount を使い、色表と件数の一致をコンパイル時に検査する。HUDで周回を表示するときは Stage2FinalLoopCount を参照する。現行の信号盤数・周回数はともに3で、表示区画数と進行率は変更前と一致。Release・Debug x64は警告0・エラー0。実プレイは未確認。
