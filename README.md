# 通信途絶

C++ / DirectX 11 / HLSLで制作した、一人称視点の短編ホラーゲームです。
暗い施設を探索して電力を復旧し、異変が繰り返される廊下からの脱出を目指します。

## 作品概要

- ジャンル：一人称探索ホラー
- 対応環境：Windows x64
- ステージ数：2
- 開発言語：C++20、HLSL
- 描画API：DirectX 11
- デバッグUI：Dear ImGui（Debug構成）

### 1面

施設内を探索して3個のヒューズを回収し、分電盤へ戻して電力を復旧します。
照明、濡れた床、水たまりの反射、影の出現などを使って探索中の緊張感を作っています。

### 2面

同じ廊下を繰り返しながら変化を見つける、ループ型のステージです。
偽ドア、時計、肖像、傷跡、足音による危険度、信号盤パズル、追跡イベントを段階的に進めます。

## 操作方法

| 操作 | キーボード・マウス | XInputコントローラー |
|---|---|---|
| 移動 | `W` `A` `S` `D` | 左スティック |
| 視点操作 | マウス | 右スティック |
| 調べる・操作する | `E` | A |
| 懐中電灯 | `F` | Y |
| 走る | `Shift` | 左スティック押し込み |
| 目的ヒント | `H` | LB |
| ポーズ | `Esc` または `P` | START |
| タイトル開始 | `Enter` | A または START |
| タイトル終了 | `Q` | B |

Debug構成では `F1` でImGuiデバッグ画面を表示できます。

## ビルド方法

1. `Job_application_work_horror.slnx` をVisual Studioで開きます。
2. 構成を `Release`、プラットフォームを `x64` にします。
3. 「ソリューションのビルド」を実行します。

プロジェクト設定はC++20、Platform Toolset `v145`、Windows SDK `10.0.26100.0`です。
`external` 以下にDear ImGuiなど必要なサードパーティーファイルを同梱しています。

## 技術的な見どころ

- HLSLによる懐中電灯、濡れ床、ブルーム、ビネット、フィルムグレインなどの画面表現
- Planar Reflectionによる水たまりの反射
- Shadow Mapと描画対象ごとのカリング
- 視界内に水面がある場合だけ反射パスを更新する負荷制御
- `std::unique_ptr`によるScene・Objectの単一所有
- `ComPtr`によるDirectXリソース管理
- Objectの遅延追加・遅延削除による更新ループの安全性確保
- 1面・2面のイベント進行を状態クラスへ分離
- Releaseではゲーム画面だけを表示し、DebugではImGuiで演出値や進行状態を確認可能

## 主な構成

```text
Game
  ├─ GameState
  ├─ GameSettings
  ├─ ObjectManager
  ├─ StageScene
  │   ├─ ScareLightSequence
  │   ├─ StagePowerSequence
  │   └─ ExitOmenSequence
  ├─ Stage2Scene
  │   ├─ SignalPuzzle
  │   ├─ NoiseThreatSystem
  │   ├─ 各Anomaly
  │   └─ 各Sequence
  ├─ Player
  │   ├─ PlayerMovement
  │   └─ FlashlightSystem
  └─ Ground
      └─ WaterEffectSystem
```

詳しい責務、所有権、更新・描画順、`dynamic_cast`の方針は
[ARCHITECTURE.md](ARCHITECTURE.md)を参照してください。

## ビルド確認状況

- Release x64：警告0、エラー0
- Debug x64：警告0、エラー0
- Debug実行ファイル：起動スモークテスト済み

## 提出時の注意

- 実行環境ZIPには実行ファイル、DLL、`assets`、`shader`、必要な設定ファイルを含めます。
- プロジェクトZIPにはソース、プロジェクトファイル、サードパーティーヘッダー・ライブラリを含めます。
- `.vs`、中間生成物、不要な`obj`ファイルは提出物から除外します。
