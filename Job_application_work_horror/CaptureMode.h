// ============================================================================
// ファイルの役割: 作業報告用の「自動撮影モード」。起動オプション --capture <出力フォルダ> を
//                 付けたときだけ有効になり、1面・2面を決められた道順で自動で見て回りながら、
//                 動画（MP4）とスクリーンショット（PNG）を保存して終了している。
// 主な技術: Media Foundation（Sink WriterでH.264へ符号化）、WIC（PNG保存）、
//           バックバッファの読み戻し（ステージングテクスチャ）、キーフレームの補間
//
// ・キー入力を送らずに、ゲームの中でプレイヤーと視点を直接動かしているため、他のウィンドウの操作を邪魔しない。
// ・ウィンドウは画面の外に置いて前面に出さず、音も出していない。
// ・時間は1フレーム=1/30秒で固定して進めているため、処理が重くても動画は実際の速さで再生される。
// 通常の起動では何もしていない。
//
// 起動オプション --benchmark <出力フォルダ> では、動画を撮らずに「処理の重さの計測」をしている。
// モニターと同じ解像度・垂直同期なしで1面の決まった地点に立ち、懐中電灯を消した状態と点けた状態の
// フレーム時間とGPU時間（描画の段階ごと）を測って benchmark_result.txt に書き出している。
// ============================================================================

#pragma once

#include <d3d11.h>
#include <string>

namespace Tools::CaptureMode
{
    // 起動オプションを読み、--capture か --benchmark があれば有効にしている（ウィンドウを作る前に呼んでいる）。
    void ConfigureFromCommandLine();
    // 自動撮影モード（または計測モード）で起動したかどうか
    bool IsActive();
    // --benchmark で起動したとき true になる（IsActive も true になる）。
    bool IsBenchmark();

    // 撮影中のウィンドウと動画の大きさ（720p）。
    constexpr unsigned int Width = 1280;
    constexpr unsigned int Height = 720;
    // 撮影中の1フレームの長さ（30fps）
    constexpr float FrameSeconds = 1.0f / 30.0f;

    // Sceneの更新の後・Objectの更新の前に呼ばれ、プレイヤーと視点を道順に沿って動かしている。
    void UpdateBeforeObjects();
    // 画面の表示（Present）の直前に呼ばれ、描き終えた画面を動画と静止画に書き出している。
    void OnFrameRendered(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer);
    // 計測モードで、画面の表示（Present）にかかった時間を受け取っている。
    void OnPresentTimed(double milliseconds);
    // 終了時に動画を閉じている（閉じないとMP4が壊れる）。
    void Shutdown();
}
