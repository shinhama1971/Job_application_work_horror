// ゲーム本編で使用する描画方式の既定値。
#include "TechDemo.h"
#include "CaptureMode.h"

namespace Tools::TechDemo
{
    ReflectionMethod GetReflectionMethod()
    {
        const int reflectionOverride = Tools::CaptureMode::GetReflectionMethodOverride();
        return reflectionOverride >= 0
            ? static_cast<ReflectionMethod>(reflectionOverride)
            : DefaultReflectionMethod;
    }
}
