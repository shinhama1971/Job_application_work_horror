# DirectXTK（SimpleMathのみ）

このプロジェクトが DirectX Tool Kit（DirectXTK）から使っているのは、ベクトル・行列の
ラッパー `SimpleMath` だけです。ライブラリ全体（Debug版のlibだけで約42MB）ではなく、
必要な部分だけを同梱しています。

| ファイル | 内容 |
|---|---|
| `include/SimpleMath.h`, `include/SimpleMath.inl` | DirectXTKのヘッダーそのまま |
| `lib/DirectXTK_SimpleMath-mtd.lib` | Debug用（静的CRT `/MTd`、x64） |
| `lib/DirectXTK_SimpleMath-mt.lib` | Release用（静的CRT `/MT`、x64） |
| `LICENSE` | DirectXTKのライセンス（MIT License） |

## libの作り方

公式ソース（https://github.com/microsoft/DirectXTK）を静的CRTで
ビルドした `DirectXTK.lib` から、`SimpleMath.obj` と `pch.obj` だけを取り出して作りました（各約0.5MB）。
`SimpleMath.obj` には `Vector3::Zero` などの定数と、インライン化されない一部の関数が入っています。
`SimpleMath.obj` はプリコンパイル済みヘッダーを使ってコンパイルされているため、`pch.obj` も一緒に必要です
（無いとリンク時に LNK2011 になります）。

```bat
lib /EXTRACT:Bin\Desktop_2017_Win10\x64\Debug\SimpleMath.obj /OUT:SimpleMath.obj DirectXTK.lib
lib /EXTRACT:Bin\Desktop_2017_Win10\x64\Debug\pch.obj /OUT:pch.obj DirectXTK.lib
lib /OUT:DirectXTK_SimpleMath-mtd.lib SimpleMath.obj pch.obj
```

Release用は、パスの `Debug` を `Release` に、出力名を `DirectXTK_SimpleMath-mt.lib` にして同じ手順で作ります。
