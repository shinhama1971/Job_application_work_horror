// ============================================================================
// ファイルの役割: DirectX 11の画面を読み戻し、撮影した画像をPNGで保存する。
// ============================================================================

#pragma once

#include <d3d11.h>
#include <string>

namespace Tools::CaptureImage
{
    bool SavePng(const std::wstring& path, const BYTE* rgba, UINT rowPitch, UINT width, UINT height);
    bool SaveBackBufferPng(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer, const std::wstring& path);
}
