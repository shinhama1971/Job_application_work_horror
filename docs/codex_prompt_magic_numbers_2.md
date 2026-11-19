# Codex への依頼：マジックナンバーの残り（2回目）

このリポジトリ（C++20・DirectX 11・HLSL のホラーゲーム「通信途絶 / SIGNAL LOST」）で、数値に名前を付ける作業を続けてください。
最初に `CLAUDE.md` と `docs/HANDOFF.md` の「Codex の分の確かめと、シェーダーの続き（11月19日、Claude Code）」を読んでください。

## 守ること

- 返答・コメント・文書はすべて日本語。
- `git commit`・`git pull`・`git push` はしない。終わったら、変更したファイルと日本語のコミットメッセージ案を伝える。AI の署名（`Co-Authored-By` など）は付けない。
- **値は変えない。** 名前を付けるだけ。ゲームの見た目も動きも変えない。
- **ゲームは起動しない**（このパソコンで音を出さないため）。`--capture` や `--benchmark` も使わない。
- ファイルは消さず、移動する。テストは足さない。
- **値がたまたま同じ別の意味の定数を使い回さない。** 前回、肖像画の襟の色に目の動きの定数を使っていた所があった。意味ごとに名前を付ける。
- 名前は、その値が何のためにあるかを表すものにする。各定数の上に、日本語で短い説明を付ける。
- 改行と文字コードは元のファイルに合わせる。作業ツリーは CRLF（`git ls-files --eol` で `w/crlf`）。`w/mixed` にしない。`.hlsl` に BOM を付けない。日本語を含む `.ps1` は BOM 付き UTF-8。
- PowerShell 5.1 で動くコマンドにする（`&&` は使えない）。

## もう終わっているもの（やり直さない）

- **シェーダー：** `common.hlsl` 以外の53本すべて。残りの数字は、データの表（`g_Kernel`・`g_ShadowDisk`・`g_PointShadowTaps`・時計の数字の形）、決まった式（`t*t*(3-2t)`、解の公式の 4、4近傍の 4、画面を覆う三角形の 3）、`x * 0.5f + 0.5f` の変換だけ。
- **C++：**
  - シェーダーの番号（`〜Slot`）、計測と動画の設定、演出の秒数・しきい値・既定値。
  - 演出の段階の enum（`EscapePhase`・`EntranceEventPhase`・`StorageScarePhase`・`FuseWatcherState`）と `NotStarted`。
  - まとめた値：`RequiredFuseCount`・`ShaderDebugView`・`GameSettings::*EffectLevel`・`MathUtility::NextXorShift32`。
  - 1画素のバイト数、キューブマップの面の数、四角形の頂点の数。
- **Unity C#：** 撮影用のカメラと光の値（`MapLayoutBatch.cs`）と、印の大きさ（`MapLayoutEditor.cs`）。

## 残っているもの（ここをお願いします）

数え方：コメント・文字列・`constexpr`/`const` の宣言の行を除き、0・1・2・0.5 以外の数を探す。
次のスクリプトで候補を出せる（Git Bash）。

```
cd Job_application_work_horror
grep -rnE "(^|[^A-Za-z_0-9.])([3-9]|[1-9][0-9]+)(u|U|\.[0-9]+f?)?([^A-Za-z_0-9.]|$)" --include=*.cpp --include=*.h . | grep -v "external/\|imgui\|retired\|stb_\|constexpr\|//"
```

1. **1面の天井照明の番号**
   - `Stage1Layout.cpp` の道案内の `light(4)`・`light(5)`・`light(8)`・`light(7)`・`light(3)`・`light(2)`、`corners[3]`。
   - `StageSceneEvents.cpp` には、すでに `LoopExitLightIndex = 7` や `MiddleCorridorLightIndex` などがある。
   - 照明の番号を1か所（例：`Stage1Layout.h`）に名前付きでまとめ、両方から使う。番号と照明の対応は、配置のコードとコメントで確かめる。
2. **乱数の種**
   - `MakeRandomSeed(1101u)`〜`(1106u)`・`(1201u)`（`KnockingAnomaly.h`・`Stage1AmbientSounds.h`・`Stage1HiddenRoomEvent.h`・`Stage1KeypadDoor.h`・`Stage1WestWing.h`・`StageSurveillanceController.h`・`Stage2Scene.h`）と、`ForestGrass.cpp` の `20261106u`。
   - 重ならないことが分かるよう、1か所の一覧（例：`CaptureMode.h` の近く）にまとめる。値は変えない（自動撮影の結果が変わるため）。
3. **メッシュを組み立てる所の数**
   - `HangingBulb.cpp`（3・6）、`FlashlightWriting.cpp`（4 と索引の並び）、`VoiceRecorder.cpp`・`Wall.cpp` の `faceIndices + 12`、`FullScreenQuad.cpp`・`ScreenDustOverlay.cpp` の頂点の並び（`m_Vertices[3]`）。
   - 頂点の数・1面の索引の数として名前を付ける。索引の並び（`{ 0, 1, 2, 2, 1, 3 }` のような表）はそのままでよい。
4. **D3D の配列の数**
   - `DDGIVolume.cpp`（`nullSRVs[6]`・`traceInputs[4]`）、`TiledLighting.cpp`（`nullViews[3]`）、`AmbientOcclusion.cpp`（`occlusionInputs` の 2）、`LightShafts.cpp`（`inputs` の 2）。
   - 設定するテクスチャの数として名前を付ける。シェーダーの `register(t0〜)` の数と合っていることを確かめる。
5. **文字の扱い**
   - `Hud.cpp` の UTF-8 の読み取り（`0xF0u`・`0xE0u`・`6u`・`3`）と、`<< 32u`・`% 8u`。
   - バイトの印やビット数として名前を付ける。数字のグリフの表（`Glyph{ 0x1E, ... }`）はデータなのでそのまま。
6. **バッファの長さ**
   - `HudScreens.cpp` の `timeText[16]`、`LightShafts.cpp` の `mode[8]`、`PropLayoutEditor.h` の `m_Filter[64]`、`PropLayoutFile.cpp` の `text[32]`、`input.h` の `keyState[256]`（仮想キーの数）。
   - 意味のある名前を付ける。
7. **Unity C#**
   - `MapEditor/Assets/Editor/MapLayout/MapLayoutEditor.cs`・`MapLayoutDocument.cs`・`GameModelCatalog.cs` で、配置ファイルの列の番号を直接書いている所（`ReadVector(line, 2)`・`Tokens[8]`・`IndexOf("model", 12)`・`SetNumber(5, …)` など）。
   - ファイル形式の列の番号として名前を付ける。C++ 側の `PropLayoutFile.cpp` の列と合っているかも確かめる。
   - Unity は開けないので、型と名前が C# として正しいかを丁寧に見る。

迷ったもの（名前を付けると読みにくくなるもの）は直さず、理由を報告に書いてください。

## 確かめ方（全部やる）

1. **ビルド：** `Job_application_work_horror/Job_application_work_horror.vcxproj` を MSBuild で Release と Debug（x64）。**警告0・エラー0**。ビルドの後処理でシェーダーもコンパイルされる。
2. **シェーダーを触った場合：** HEAD（`git archive HEAD Job_application_work_horror/shader`）と今のシェーダーを、fxc（`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe`、`/E main /O3`、名前が `CS` で終わるものは cs_5_0、`VS` は vs_5_0、ほかは ps_5_0）でコンパイルし、バイトコードが一致することを確かめる。
   - **`main(` を含まないファイルも対象にする**（`barkPS`・`clockPS`・`doorPS` などは `main` を読み込む側。`common.hlsl` だけ除く）。53本すべて一致させる。
   - `static const` にするとバイトコードが変わる所がある（`wetFloorPS`・`common.hlsl` のデバッグ番号）。その場合は `#define` にする。
   - `#define` を `#if` の片側だけに置かない。使う所より前に置く。
3. **値が消えていないか：** 変えたファイルごとに、HEAD と今のファイルから数（コメントを除く）を取り出して比べる。消えた数は、別のファイルへ移したものだけであることを確かめる。
4. **改行と文字：** 変えたファイルが `git ls-files --eol` で `w/mixed` になっていないこと。`.hlsl`・`.hlsli` は `Job_application_work_horror/tools/check-text-format.ps1 -Path <ファイル>` を通す。
5. **文書：**
   - `docs/HANDOFF.md`：上の節の続きに、やったこと・残したもの（理由つき）・確かめた結果を書く。
   - `docs/AI_USAGE.md`：最後に、Codex が何をしたかを1段落で書く。
