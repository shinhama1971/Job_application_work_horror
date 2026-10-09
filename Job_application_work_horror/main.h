// ============================================================================
// ファイルの役割: プログラムの入口で使うヘッダーをまとめ、ライブラリをリンクしている。
// 主な技術: main関数（Release構成はmainCRTStartup経由でWindowsサブシステムとして起動）、アプリケーションの一生の管理
// ============================================================================

#pragma once

#include <stdio.h>
#include <windows.h>
#include <assert.h>
#include <functional>
#include <locale.h>
#include <string>

// timeBeginPeriodなどのマルチメディアタイマーを使うためのライブラリ
#pragma comment (lib,"winmm.lib")

