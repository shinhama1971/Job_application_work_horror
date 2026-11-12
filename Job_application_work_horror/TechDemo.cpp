// ゲーム本編で使用する描画方式の既定値。
#include "TechDemo.h"
#include "CaptureMode.h"

namespace Tools::TechDemo
{
    bool UsesTiledLighting() { return true; }
    bool UsesDepthPrepass() { return true; }

    int GetReflectionMethod()
    {
        const int reflectionOverride = Tools::CaptureMode::GetReflectionMethodOverride();
        return reflectionOverride >= 0 ? reflectionOverride : 3;
    }

    int GetShadowMode() { return 2; }
    float GetWaterCausticsScale() { return 1.0f; }
    bool UsesWaterRipples() { return true; }
    int GetPuddleMeshMode() { return 2; }
    bool UsesAmbientOcclusion() { return true; }
    bool UsesPbr() { return true; }
    bool UsesLightShafts() { return true; }
    bool UsesBulbShadow() { return true; }
    int GetGrassLodMode() { return 1; }
    bool UsesGrassCameraBend() { return true; }
}
