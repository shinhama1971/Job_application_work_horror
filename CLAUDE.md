# このリポジトリで作業する Claude Code へ

C++20・DirectX 11・HLSL で作っているホラーゲーム「通信途絶 / SIGNAL LOST」（就職作品）です。
ゲーム本体は `Job_application_work_horror/`、Unity のマップエディターは `MapEditor/` にあります。
仕組みの説明は [ARCHITECTURE.md](ARCHITECTURE.md)、遊び方と機能の一覧は [README.md](README.md)、
AI の使い方の記録は [docs/AI_USAGE.md](docs/AI_USAGE.md)、作業の引き継ぎは [docs/HANDOFF.md](docs/HANDOFF.md) を読んでください。

## 作者との約束

- **返答はすべて日本語**で書く（途中の短い報告・質問・まとめも）。コードのコメントと文書も日本語。
- **`git commit`・`git pull`・`git push` はしない。** 変更はコミットせずに残し、最後に変更したファイルとコミットのメッセージ案（日本語）を伝える。作者が自分で決めてコミットする。
- コミットのメッセージや PR に **`Co-Authored-By: Claude` などの AI の署名を付けない**。
- コマンドは PowerShell 5.1 で動くように書く（`&&` は使えない。`;` か `if ($?) { }`）。コマンドを伝えるときは、1つずつ別のコードブロックにする。
- 「次に何をするか」を提案するときは、**シェーダー（描画の技術）とゲーム性（ホラーの仕掛け・遊び）を先に**挙げる。README・ツールの整備は後。
- キー入力を送る操作はしない。テストを勝手に足さない。ファイルは消さずに移動する。

## 作業のしかた

- ビルド：`Job_application_work_horror/Job_application_work_horror.vcxproj` を MSBuild で Release と Debug の両方（x64）。**警告0**を保つ。
- 見た目の確認：自動撮影 `--capture <フォルダ>`（1面→2面の道順）、`--forest --capture <フォルダ>`（森の道順）、`--benchmark <フォルダ>`（計測モード）。
  確認のためだけにコードへ一時的な処理を入れるときは、行の終わりに `// TEMPVERIFY` を付け、終わったら必ず消して、`git diff` で関係ない行まで消していないか確かめる。
- 文字と改行の確認：`Job_application_work_horror/tools/check-text-format.ps1 -Path <ファイル>`（.hlsl に BOM を付けない、改行を混ぜない）。
  日本語を含む .ps1 は BOM 付き UTF-8 にする（PowerShell 5.1 で文字化けしないため）。
- 機能を足したら README・ARCHITECTURE・docs/AI_USAGE.md も更新する。
