# Codex への依頼：森と1面の怖さの続き

このリポジトリ（C++20・DirectX 11・HLSL のホラーゲーム「通信途絶 / SIGNAL LOST」）で、**外の森と1面**の怖さを足す作業を続けてください。
最初に次の3つを読んでください。
- `CLAUDE.md`
- `docs/HANDOFF.md` の先頭にある11月19日の項目（森の稲光の人影、1面の天井裏の追跡、ヒューズを拾った瞬間、影のまわりのゆがみ、残像、天井と人影の大きさ）
- `ARCHITECTURE.md` の画面効果の表

## 守ること

- 返答・コメント・文書はすべて日本語。
- `git commit`・`git pull`・`git push` はしない。終わったら、変更したファイルと日本語のコミットメッセージ案を伝える。AI の署名（`Co-Authored-By` など）は付けない。
- **ゲームは起動しない**（このパソコンで音を出さないため）。`--capture`・`--benchmark`・`--forest` も使わない。見え方は作者が遊んで確かめるので、確かめてほしい点を HANDOFF に書く。
- **3面は作らない。** 森（`ForestScene`）と1面（`StageScene`）だけを触る。2面に影響する共通の処理を変えるときは、2面で何が変わるかを HANDOFF に書く。
- ファイルは消さず、移動する。テストは足さない。
- 数値には名前を付ける（定数と日本語の説明。名前のない数値を足さない）。値がたまたま同じ、別の意味の定数を使い回さない。
- 改行と文字コードは元のファイルに合わせる。作業ツリーは CRLF（`git ls-files --eol` で `w/crlf`）。`w/mixed` にしない。`.hlsl` に BOM を付けない。新しいファイルも CRLF・BOM なし。
- 新しいファイルは `Job_application_work_horror.vcxproj` と `.vcxproj.filters` に登録する（森は `Scene\Forest`、1面は `Scene\Stage1`、シェーダーは `Shader\Pixel` などのフィルター）。
- PowerShell 5.1 で動くコマンドにする（`&&` は使えない）。

## 作ってほしいもの（上から順に。1つ終わるごとにビルドする）

### 1. 画面効果：暗くなった画面の端から、手が伸びてくる
- 恐怖で視界が狭まる効果（`crtOverlayPS.hlsl` の `fearVision` の分岐）に、闇の中から何本もの細い指・手の形が中央へ伸びてくる模様を足す。
- 緊張（`fearVision`）が上がるほど長く伸び、心拍の脈（`fearBeat`）に合わせてぴくりと動く。
- 計算で作る模様にする（画像は使わない）。端の闇に溶けるように、色は暗く。
- 視線の中央（進むのに必要な所）は隠さない。

### 2. 画面効果：視界の端にだけ映る人影
- 画面の端（周辺視野）に、ぼんやりした黒い人影が一瞬だけ映る。そちらへ視点を向けると、見る前に消える。
- 1面で緊張が高いときに、まれに出す（同じ所に何度も出さない。間隔に名前を付ける）。
- 人影は画面空間の形（距離場や簡単な楕円の組み合わせ）で描き、モデルは置かない。
- 視点の向きの変化は、シーン側で前のフレームとの差から求めて渡す。

### 3. 森：懐中電灯でだけ見える、木の幹の痕跡
- 1面の壁の文字（`FlashlightWriting`、`flashlightRevealPS.hlsl`）と同じ仕組みで、森の木の幹に引っかき傷や矢印を描く。照らすと浮かぶ。
- 小道から研究所への道を、照らした人だけが分かるように示す（ガイドなしで分かる誘導）。
- 置き場所は配置ファイル（`assets/layout/forest_props.txt`）の place 行で決め、Unity のマップエディターで動かせるようにする。

### 4. 敵（人影）の怖さ
- 懐中電灯で照らしたとき、すぐ崩れずに、一瞬こちらへ一歩近づいてから崩れる（`ShadowMan` の見られたときの処理）。
- 崩れる瞬間に、真後ろで低い息づかいの音を鳴らす（既存の効果音を使う。音量と高さに名前を付ける）。
- 近づくと、首をかしげる・ゆっくり傾く動き（回転を少し足す）。
- 1面の追ってくる影（書類保管室・隠し部屋）の動きの規則は変えない。

## 知っておいてほしいこと（今までにはまった所）

- **画面効果の定数バッファ b0**（`TimeBuffer`）は、次の3か所で並びを合わせる。今は128バイト。
  - `FullScreenQuad.h` の `TimeBuffer`（`static_assert` で大きさを確かめている）
  - `shader/postProcessBuffer.hlsli`
  - `FullScreenQuad::Draw` の引数
  
  値を足すときは、16バイト単位で足し、`static_assert` を直す。重ねて描く段（`crtOverlayPS`）は、効果が全部0のときは描かれない。そのため、新しい効果も描く条件（`FullScreenQuad.cpp`）に足す。
- **b7 を借りるシェーダー**（`shadowDissolvePS`・`lightningFigurePS`）では、`common.hlsl` の b7 の値（デバッグ表示の番号・周回の深さ）を読まない。描いた後は b7 を元に戻す。
- **人影のモデル**は、実際の大きさ（身長約31）で作ってある。読み込むときは `ShadowModelScale` で割り、描く大きさは `ShadowSize = ShadowModelScale * ShadowEnlarge`（今は背が約54）。`ShadowModelScale` だけを変えても、見た目は変わらない。
- **壁の当たり判定は、上から見た四角だけ**で、高さを見ない。頭より上の物でも通り道をふさぐので、当たり判定を切る。
- **1面の天井**は y=-36.5（床 -100。壁は y=-99〜-37）。2面は -48.5。天井の高さを使う値は、次の4つ。
  - `StageScene.cpp` の `RoomOcclusionCeilingY`
  - `CeilingVoidY`（`Stage1AmbientSounds`・`Stage1CeilingStalker`）
  - `CeilingReflectionScare::CeilingY`
  - `Stage1Layout.cpp` の `StageRoomWallCenterY`・`StageRoomWallHeight`
- 扉は高さ50。扉の上には、当たり判定のない壁（`CreateDoorLintel`）がある。開ききった扉の板には当たり判定がある。
- 1面の物音・演出は、ほかの演出と重ならないよう `StageScene::IsAmbientSoundAllowed` を見る。新しい演出を足したら、その演出の間はここで物音を止める。
- 心拍・視界が狭まる効果・ラジオの雑音・残像は、`StageScene::ComputeThreatRate`（影の近さ）から来ている。

## 確かめ方（全部やる）

1. **ビルド：** `Job_application_work_horror/Job_application_work_horror.vcxproj` を MSBuild で Release と Debug（x64）。**警告0・エラー0**。ビルドの後処理で、シェーダーも全部コンパイルされる。
2. **シェーダー：** 変えたシェーダー以外のバイトコードが変わっていないことを確かめる。
   - HEAD を `git archive HEAD Job_application_work_horror/shader` で取り出す。
   - fxc（`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe`、`/E main /O3`。名前が `CS` で終わるものは cs_5_0、`VS` は vs_5_0、ほかは ps_5_0）で、`common.hlsl` 以外の全部を前後でコンパイルして比べる。
   - b0 を変えた場合、同じ b0 を読むシェーダー（`bloomCompositePS`・`exposurePS`・`volumetricFlashlightPS`）はバイトコードが変わる。`/Fc` で逆アセンブルし、`//` で始まる行を除いて命令が同じことを確かめる。
3. **改行と文字：** 変えたファイルが `git ls-files --eol` で `w/mixed` になっていないこと。`.hlsl`・`.hlsli` は `Job_application_work_horror/tools/check-text-format.ps1 -Path <ファイル>` を通す。
4. **文書：**
   - `README.md`：機能の一覧
   - `ARCHITECTURE.md`：仕組み
   - `docs/HANDOFF.md`：先頭の11月19日の項目の上に、やったこと・数値の場所・作者に確かめてほしい点を書く
   - `docs/AI_USAGE.md`：最後に、Codex が何をしたかを1段落で書く
