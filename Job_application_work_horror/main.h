// ============================================================================
// ファイルの役割: Windowsエントリーポイントからアプリケーションを起動します。
// 主な技術: main関数（Release構成はmainCRTStartup経由でWindowsサブシステム起動）、アプリケーションライフサイクル
// ============================================================================

#pragma once

#include <stdio.h>
#include <windows.h>
#include <assert.h>
#include <functional>
#include <locale.h>
#include <string>

#pragma comment (lib,"winmm.lib")

