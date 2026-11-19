// ゲーム本編の描画で使う方式。描画側の呼び出しは単独の技術デモとの共通化のために残す。
#pragma once
#include "shader/renderingConfig.hlsli"

namespace Tools::TechDemo
{
    // 番号はシェーダーと自動撮影の --reflection で使う値に合わせる。
    enum class ReflectionMethod
    {
        Off = SIGNAL_LOST_REFLECTION_OFF,
        CubeMap = SIGNAL_LOST_REFLECTION_CUBE_MAP,
        ScreenSpace = SIGNAL_LOST_REFLECTION_SCREEN_SPACE,
        RoomRay = SIGNAL_LOST_REFLECTION_ROOM_RAY
    };
    enum class ShadowMode { Off = 0, Hard = 1, Pcss = 2 };
    enum class PuddleMeshMode { FullFloor = 0, DetailedMesh = 1, MeshLod = 2 };
    enum class GrassLodMode { Off = 0, Distance = 1, DebugColors = 2 };

    inline constexpr auto DefaultReflectionMethod = ReflectionMethod::RoomRay;
    inline constexpr auto DefaultShadowMode = ShadowMode::Pcss;
    inline constexpr auto DefaultPuddleMeshMode = PuddleMeshMode::MeshLod;
    inline constexpr auto DefaultGrassLodMode = GrassLodMode::Distance;
    inline constexpr float WaterCausticsScale = 1.0f;

    // 比較ビルドでも同じ描画コードを使えるよう、本編の固定値はここに置く。
    inline constexpr bool UseTiledLighting = true;
    inline constexpr bool UseDepthPrepass = true;
    inline constexpr bool UseWaterRipples = true;
    inline constexpr bool UseAmbientOcclusion = true;
    inline constexpr bool UsePbr = true;
    inline constexpr bool UseLightShafts = true;
    inline constexpr bool UseBulbShadow = true;
    inline constexpr bool UseGrassCameraBend = true;

    ReflectionMethod GetReflectionMethod();
}
