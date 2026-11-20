// ============================================================================
// ファイルの役割: 計測の進行と、ウォームアップ中に保存する画像の指定を撮影モードにつなぐ。
// ============================================================================

#pragma once

#include <filesystem>
#include <string>

struct ID3D11DeviceContext;
namespace Core { class Game; }

namespace Tools::CaptureBenchmark
{
    // 全地点の計測結果を書き出せたフレームだけ true を返す。
    bool Update(Core::Game* game, const std::filesystem::path& outputDirectory,
        bool vsync, bool figures, void (*log)(const std::string&));
    std::wstring GetScreenshotName(ID3D11DeviceContext* context);
}
