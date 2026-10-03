// ============================================================================
// ファイルの役割: 作業報告用の「自動撮影モード」です。起動オプション --capture <出力フォルダ> を
//                 付けたときだけ有効になり、1面・2面を決められた道順で自動で見て回りながら、
//                 動画（MP4）とスクリーンショット（PNG）を保存して終了します。
// 主な技術: Media Foundation（Sink WriterでH.264へ符号化）、WIC（PNG保存）、
//           バックバッファの読み戻し（ステージングテクスチャ）、キーフレームの補間
//
// ・キー入力を送らずにゲーム内でプレイヤーと視点を動かすため、他のウィンドウの操作を邪魔しません。
// ・ウィンドウは画面の外に置いて前面に出さず、音も出しません。
// ・時間は1フレーム=1/30秒で固定して進めるため、処理が重くても動画は実際の速さで再生されます。
// 通常の起動では何もしません。
//
// 起動オプション --benchmark <出力フォルダ> では、動画を撮らずに「処理の重さの計測」をします。
// モニターと同じ解像度・垂直同期なしで1面の決まった地点に立ち、懐中電灯を消した状態と点けた状態の
// フレーム時間とGPU時間（描画の段階ごと）を測って benchmark_result.txt に書き出します。
// ============================================================================

#pragma once

#include <d3d11.h>
#include <string>

namespace Tools::CaptureMode
{
    // 起動オプションを読み、--capture があれば有効にします（ウィンドウを作る前に呼びます）。
    void ConfigureFromCommandLine();
    bool IsActive();
    // --benchmark で起動したとき true です（IsActive も true になります）。
    bool IsBenchmark();

    // 撮影中のウィンドウの大きさです（720p）。
    constexpr unsigned int Width = 1280;
    constexpr unsigned int Height = 720;
    constexpr float FrameSeconds = 1.0f / 30.0f;

    // Sceneの更新の後・Objectの更新の前に呼び、プレイヤーと視点を道順に沿って動かします。
    void UpdateBeforeObjects();
    // 画面の表示（Present）の直前に呼び、描き終えた画面を動画と静止画に書き出します。
    void OnFrameRendered(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer);
    // 計測モードで、画面の表示（Present）にかかった時間を受け取ります。
    void OnPresentTimed(double milliseconds);
    // 終了時に動画を閉じます。
    void Shutdown();
}
