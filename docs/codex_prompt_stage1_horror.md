# Codex への依頼：1面の怖さ・驚きの続き

このリポジトリ（C++20・DirectX 11・HLSL のホラーゲーム「通信途絶 / SIGNAL LOST」）で、**1面（`StageScene`）のホラー・恐怖・驚きの演出**を足す作業を続けてください。
最初に次の3つを読んでください。

- `CLAUDE.md`
- `docs/HANDOFF.md` の先頭にある11月20日の4つの項目（長いファイルの分割、呼吸する壁、振り返ると伸びる廊下、提出前の見直し）
- `ARCHITECTURE.md` の「分割を止める基準」と画面効果の表

## 守ること

- 返答・コメント・文書はすべて日本語。
- `git commit`・`git pull`・`git push` はしない。終わったら、変更したファイルと日本語のコミットメッセージ案を伝える。AI の署名（`Co-Authored-By` など）は付けない。
- **ゲームは起動しない**（このパソコンで音を出さないため）。`--capture`・`--benchmark`・`--forest` も使わない。見え方は作者が遊んで確かめるので、確かめてほしい点を HANDOFF に書く。
  - 自動撮影の道順（`CaptureTours.cpp`）に、確かめるための撮影の場所を足すのはよい（作者が撮影して確かめる）。
- **1面だけを触る。** 3面は作らない。森と2面に影響する共通の処理（`Camera`・`Wall`・`PostProcess`・シェーダー）を変えるときは、森と2面で何が変わるかを HANDOFF に書く。
- ファイルは消さず、移動する。テストは足さない。
- 数値には名前を付ける（定数と日本語の説明。名前のない数値を足さない）。値がたまたま同じ、別の意味の定数を使い回さない。
- 改行と文字コードは元のファイルに合わせる。
  - 作業ツリーは CRLF（`git ls-files --eol` で `w/crlf`）。`w/mixed` にしない。
  - `.hlsl`・`.hlsli` に BOM を付けない。新しいファイルも CRLF・BOM なし。
- 新しいファイルは `Job_application_work_horror.vcxproj` と `.vcxproj.filters` に登録する（1面は `Scene\Stage1` のフィルター）。
  - 新しいシェーダーは、`grassVS.hlsl`・`breathingWallVS.hlsl` と同じく `<None Include=...>` で登録する。
  - 新しい `.hlsli` は `<ClInclude>` で登録する。
- **1つのファイルを長くしすぎない**（目安は800行。関数は150行）。演出が大きいときは、`Stage1CeilingStalker`・`Stage1CorridorStretch` のように、進行だけを持つクラス（描画・音に依存せず「合図」を返す）を別のファイルに作り、`StageScene` は合図を受けて照明・音・画面効果を動かす形にする。
- PowerShell 5.1 で動くコマンドにする（`&&` は使えない）。

## 作ってほしいもの（上から順に。1つ終わるごとにビルドする）

作者は「シェーダー（描画の技術）とゲーム性（怖さの仕掛け）」を優先している。どれも、1面のループ廊下を1周した後（`m_CorridorLoopCount >= 1`）から出し、周回が進むほど強くするのが基本。

### 1. 目の端にだけ見える影（シェーダー＋ゲーム性）

- 画面の端（周辺視野）に、ぼんやりした黒い人影がうっすら見える。視線をそちらへ向けると、画面の中央に近づくほど薄くなり、見る前に消える。
- 人影の透明度を、画面の中心からの距離で変えるシェーダーで描く（中心に近いほど透明）。
  - 人影の形は、既存の人影（`ShadowMan`・`shadowDissolvePS`）を使うか、画面空間の形（距離場や楕円の組み合わせ）で描く。
- 緊張が高いとき（`StageScene::ComputeThreatRate`）に、まれに出す。同じ所に続けて出さない。出す間隔に名前を付ける。
- 出す場所は、プレイヤーの横〜斜め後ろの、壁の内側（部屋の中）。壁の中に出さない。

### 2. 監視映像の中でだけ、真後ろに誰かが立っている（驚き）

- 監視カメラの映像（`StageSurveillanceController`）を見ている間、映像の中のプレイヤーの真後ろに人影が映る。映像を閉じて振り向いても、誰もいない。
- 3面の通信室に、「人影は補助の描画（別のカメラの映像）の間だけ出す」仕組みがあるので参考にする（`Stage3Scene` の通信卓の映像）。
- 1回だけ起こす。映像を閉じた直後に、背後で小さな物音を鳴らす（既存の効果音。音量と高さに名前を付ける）。

### 3. 足音が1歩多い（音）

- 歩いて立ち止まった後に、背後で足音が1回だけ鳴る（`SOUND_CUE_FOOTSTEP` を、プレイヤーの足音より少し低く）。
- たまにしか起きない。周回が進むほど起きやすくする。
- 立ち止まったことは、プレイヤーの横の動き（`Player::IsMovingHorizontally`）が止まったことで判定する。
- `StageScene::IsAmbientSoundAllowed` が false の間は鳴らさない。

### 4. 通ってきたドアが壁になっている（驚き）

- ある部屋（例：左右の倉庫のどちらか）に入って振り返ると、入ってきたドアが無く、ただの壁になっている。
  - しばらくするか、別の出口を探して出ると元に戻る。
- ドアはカメラに映っていない間だけ差し替える（`Camera::IsSphereVisible` で判定）。
- 壁は、配置ファイル（`assets/layout/stage1_props.txt`）の place 行で置き、Unity のマップエディターで動かせるようにする。
- 閉じ込めは、隠し部屋（`Stage1HiddenRoomEvent`）と重ならないようにする。出られなくなる状態を作らない（必ず別の出口があるか、時間で戻る）。

### 5. 見ていない間に家具が動く（ゲーム性）

- 倉庫の椅子（小物の `Wall`）が、振り向くたびに少しずつこちらを向いている。最後には、ドアの前に積まれている。
- 動かすのは、その小物がカメラに映っていない間だけ（`Camera::IsSphereVisible`）。
- 通り道をふさがない（当たり判定のある小物は、ドアの開く範囲と通り道に置かない）。

### 6. ドアを抜けると左右が逆の部屋（シェーダー＋驚き）

- 開始地点の部屋に戻ったはずなのに、左右が反転していて、壁の文字も鏡文字になっている。
- 「振り返ると伸びる廊下」と同じく、`Camera` のビュー行列の前に変換をかけて、見え方だけを変える（`Camera::SetWorldStretchZ` を参考に、x を -1 倍する変換を足す）。
- **左右を反転すると三角形の向き（表と裏）が逆になる。** 裏の面を描かない設定の物が消えるので、反転している間はラスタライザーの表の向きを逆にする。
- 当たり判定・音・操作の左右は元のまま。そのため、反転は短い間（扉を通った直後の数秒や、部屋の中だけ）にする。
- 作者に、酔わないか確かめてもらう。

### 7. 進むほど天井が下がる通路（圧迫感）

- ある通路で、奥へ進むほど天井が低くなり、最後は頭すれすれになる。出口の手前で照明が消える。
- 天井は1面の天井（y=-36.5）より下に、別の天井板の `Wall` を置いて下げる。
  - プレイヤーの目の高さと、天井の高さを使う値（下の「知っておいてほしいこと」）に気をつける。

### 8. 偽のエンディング（一番大きな驚き）

- 電力を戻して出口から出ると、クリアとリザルトの画面が出る。数秒後、画面がノイズで崩れ、暗い廊下に戻される。
- リザルトの見た目は `Hud::DrawResult`（`HudMenus.cpp`）を使い、`StageScene` の中で重ねて描く（本当のシーンの切り替えはしない）。
- **ベスト記録は更新しない。** 本当のクリアは、その後にもう一度出口へ向かったとき。
- ノイズの崩れは、既存の画面効果（`crtOverlayPS`・`PostProcess`）を使う。

## 知っておいてほしいこと（今までにはまった所）

### ファイルの場所（11月20日に分けた）

- 1面の演出は3つのファイルに分かれている。値は `Stage1EventSettings.h` にまとめ、3つのファイルで共有している。
  - `StageSceneEvents.cpp`：進行（電力・出口・物音・壁の文字・危険度・無線）
  - `StageSceneLoop.cpp`：ループ廊下（周回・伸びる廊下・呼吸する壁）
  - `StageSceneScares.cpp`：驚かせる演出（入口・倉庫・照明の連鎖・ヒューズ・天井裏の足音・書類保管室の影・足跡）
- 新しい演出の値も、`Stage1EventSettings.h` に足す。
- HUD は3つのファイル（`HudTitle.cpp`・`HudMenus.cpp`・`HudScreens.cpp`）と、共通の `HudText.h`。
- 自動撮影の道順は `CaptureTours.cpp`。撮影の段取りの時刻は `CaptureTours.h`。

### 1面の形

- **ループ廊下**は L 字。
  - 南北の区間：x=-45〜45、z=37.5（南のドア）〜275
  - 東西の区間：z=235〜275、x=45〜196（最後のドア）
- 最後のドアを開けると、開始地点の奥のドア（x=0、z=-180）の裏側へ移る（`StageScene::AdvanceCorridorLoop`）。
- **天井**は y=-36.5（床 -100。壁は y=-99〜-37）。天井の高さを使う値は、次の4つ。
  - `StageScene.cpp` の `RoomOcclusionCeilingY`
  - `CeilingVoidY`（`Stage1AmbientSounds`・`Stage1CeilingStalker`）
  - `CeilingReflectionScare::CeilingY`
  - `Stage1Layout.cpp` の `StageRoomWallCenterY`・`StageRoomWallHeight`
- **壁の当たり判定は、上から見た四角だけ**で、高さを見ない。頭より上の物でも通り道をふさぐので、当たり判定を切る。
- 扉は高さ50。扉の上には、当たり判定のない壁（`CreateDoorLintel`）がある。開ききった扉の板には当たり判定がある。
- 部屋の形（配置ファイルで `room` を付けた壁）は32枚まで。1面はちょうど32枚なので、`room` の壁は増やせない。

### 見え方だけを変える仕組み

- **`Camera::SetWorldStretchZ`**：ビュー行列の前に「カメラの位置を中心に z 方向へ拡大する行列」をかけている。
  - 光・影・当たり判定は元の位置のまま。
  - `GetMainMatrices` を使う所（ライトカリング・画面上の位置）は同じ行列なので、食い違わない。
  - `IsSphereVisible` も、伸ばした後の位置と大きさで判定している。
  - シーンの始めと終わりで1に戻している。新しい変換を足すときも、同じように戻す。
- **`Wall::EnableBreathing`**：箱の1面を格子に分け、`breathingWallVS.hlsl` で押し出している。
  - b9（`SIGNAL_LOST_OBJECT_DRAW_BUFFER_SLOT`）を、頂点シェーダーにだけ入れている（本描画と深度プリパスの両方）。
  - 懐中電灯の向き・円錐・点いているかは、頂点シェーダーからも `LightBuffer`（b3）で読める。

### 画面効果と定数バッファ

- **画面効果の定数バッファ b0**（`TimeBuffer`）は、次の3か所で並びを合わせる。今は128バイト。
  - `FullScreenQuad.h` の `TimeBuffer`（`static_assert` で大きさを確かめている）
  - `shader/postProcessBuffer.hlsli`
  - `FullScreenQuad::Draw` の引数
  
  値を足すときは、16バイト単位で足し、`static_assert` を直す。重ねて描く段（`crtOverlayPS`）は、効果が全部0のときは描かれない。そのため、新しい効果も描く条件（`FullScreenQuad.cpp`）に足す。
- **b7 を借りるシェーダー**（`shadowDissolvePS`・`lightningFigurePS`）では、`common.hlsl` の b7 の値（デバッグ表示の番号・周回の深さ）を読まない。描いた後は b7 を元に戻す。
- C++ と HLSL で同じ値を使うときは、`shader/renderingConfig.hlsli` に `#define` で書いて両方から読む（例：`SIGNAL_LOST_BREATHING_WALL_CYCLE_SECONDS`）。

### 人影と演出の決まり

- **人影のモデル**は、実際の大きさ（身長約31）で作ってある。
  - 読み込むときは `ShadowModelScale` で割り、描く大きさは `ShadowSize = ShadowModelScale * ShadowEnlarge`（今は背が約54）。
  - `ShadowModelScale` だけを変えても、見た目は変わらない。
- 1面の物音・演出は、ほかの演出と重ならないよう `StageScene::IsAmbientSoundAllowed` を見る。新しい演出を足したら、その演出の間はここで物音を止める。
- 心拍・視界が狭まる効果・ラジオの雑音・残像は、`StageScene::ComputeThreatRate`（影の近さ）から来ている。

## 確かめ方（全部やる）

1. **ビルド：** `Job_application_work_horror/Job_application_work_horror.vcxproj` を MSBuild で Release と Debug（x64）。**警告0・エラー0**。ビルドの後処理で、シェーダーも全部コンパイルされる。
2. **シェーダー：** 変えたシェーダー以外のバイトコードが変わっていないことを確かめる。
   - HEAD を `git archive HEAD Job_application_work_horror/shader` で取り出す。
   - fxc（`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe`、`/E main /O3`。名前が `CS` で終わるものは cs_5_0、`VS` は vs_5_0、ほかは ps_5_0）で、`common.hlsl` 以外の全部を前後でコンパイルして比べる。
   - b0 を変えた場合、同じ b0 を読むシェーダー（`bloomCompositePS`・`exposurePS`・`volumetricFlashlightPS`）はバイトコードが変わる。`/Fc` で逆アセンブルし、`//` で始まる行を除いて命令が同じことを確かめる。
3. **改行と文字：** 変えたファイルが `git ls-files --eol` で `w/mixed` になっていないこと。`.hlsl`・`.hlsli` は `Job_application_work_horror/tools/check-text-format.ps1 -Path <ファイル>` を通す。
4. **長さ：** 足した関数が150行、ファイルが800行を大きく超えていないこと。
5. **文書：**
   - `README.md`：機能の一覧（1面の演出の並び）
   - `ARCHITECTURE.md`：仕組み
   - `docs/HANDOFF.md`：先頭の項目の上に、やったこと・数値の場所・作者に確かめてほしい点を書く
   - `docs/AI_USAGE.md`：最後に、Codex が何をしたかを1段落で書く

## 前の依頼について

`docs/codex_prompt_forest_stage1.md`（森の手が伸びる画面効果・木の幹の痕跡・人影の怖さ）は、まだ手を付けていない。作者は今、1面を優先しているので、この依頼の後に回す。ただし、その中の「視界の端にだけ映る人影」は、この依頼の1番と同じものなので、こちらで作る。
