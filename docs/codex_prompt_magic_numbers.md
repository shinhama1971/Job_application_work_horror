# Codex への依頼：マジックナンバーの整理の続き

このリポジトリ（C++20・DirectX 11・HLSL のホラーゲーム「通信途絶」）で、名前の付いていない数（マジックナンバー）に名前を付ける作業の続きをお願いします。
C++ の進行・演出・材質と `HudScreens.cpp` は終わっています（`docs/HANDOFF.md` の「マジックナンバーの一括整理（11月19日）」を読んでください）。

## 守ること

- 返答・コメント・文書はすべて日本語。`CLAUDE.md` の約束に従う。
- `git commit`・`git pull`・`git push` はしない。終わったら、変更したファイルと日本語のコミットメッセージ案を伝える。
- **値は変えない。** 名前を付けるだけにする。ファイルごとに、変更の前後で数の値の一覧を比べる（`git show HEAD:<ファイル>` と今のファイルから小数を取り出して比べる）。消えた値は「共通の定数へ移した」「×2 などで同じ値になる」物だけにする。
- 名前は「何の値か」が分かる英語の名前にし、上に日本語のコメントで意味を書く。同じ意味の値が何か所にもあれば1か所にまとめる。
- 0・1・0.5・2 のような、式の形そのものの数（半分・2倍・1から引く）は残してよい。`struct` の既定値やデータの表（撮影の道順・置き場所の候補）は、名前が付いているので残してよい。
- ゲームを起動して音を出さない（このパソコンでは音を出さない約束）。確かめはビルドと数の値の比べで行う。
- ファイルは CRLF。Git Bash の `sed -i` は CRLF を LF に変えるので使わないか、使った後で CRLF に戻す。`.hlsl` に BOM を付けない（`Job_application_work_horror/tools/check-text-format.ps1 -Path <ファイル>` で確かめる）。

## 残っている作業

1. **`Job_application_work_horror/Hud.cpp`（約110個）**：普段の HUD（目的表示・電池のゲージ・調べる操作の案内・知らせ・記録・ヒント）の位置・大きさ・色。`HudScreens.cpp` と同じく、画面の部分ごとに名前空間（例：`ObjectiveLayout`・`BatteryLayout`）を作って定数を置く。`Hud.h` には `GlyphAdvance`・`GlyphRows`・`ChapterCardSeconds`・`Radio*` などの共通の定数がある。
2. **シェーダー（`Job_application_work_horror/shader/`、約4,400個）**：多い順に `litTexturePS.hlsl`・`wetFloorPS.hlsl`・`crtOverlayPS.hlsl`、絵を描く部品の `portraitPainting.hlsli`・`facadeSurface.hlsli`・`barkSurface.hlsli`・`clockFace.hlsli`・`doorPainting.hlsli`・`rockSurface.hlsli`・`horrorMarks.hlsli`・`rayReflection.hlsli`・`mirrorPS.hlsl`・`pbrLighting.hlsli`・`volumetricFlashlightPS.hlsl`・`bloomCompositePS.hlsl`。
   - 光と水の計算（反射の強さ・霧・減衰・ブルームの重み）は `static const float` で名前を付ける。
   - C++ と同じ値を使っている物（例：`GroundWaterEffects.cpp` の水たまりの乱数の係数 `NoiseHashScale` など、`wetFloorPS.hlsl` と同じ式）は、両方のコメントに「〜と同じ値」と書く。
   - 絵の形だけを決める値（模様の座標など）は、意味のまとまりごとに名前を付ける。読みにくくなるだけの所は、理由をコメントに書いて残してよい。
3. **確かめ**：`Job_application_work_horror/Job_application_work_horror.vcxproj` を MSBuild で Release と Debug（x64）の両方ビルドし、警告0を保つ。シェーダーは起動時にコンパイルされるので、`fxc` などでコンパイルが通るかも確かめる。
4. **文書**：`docs/HANDOFF.md` の該当の項目に続きを書き、`docs/AI_USAGE.md` に Codex が行ったことを1段落で足す。

## 終わったら伝えること

- 変更したファイルの一覧と、ファイルごとの「消えた値」（あれば理由）。
- ビルドの結果（Release・Debug の警告の数）。
- 日本語のコミットメッセージ案。
