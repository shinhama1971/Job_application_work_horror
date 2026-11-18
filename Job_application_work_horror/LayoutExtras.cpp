// ============================================================================
// ファイルの役割: 配置ファイルの「何個でも置ける物」を作っている（説明は LayoutExtras.h を参照）。
// ============================================================================

#include "LayoutExtras.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "SceneLayoutBuilder.h"

namespace
{
    // 足した照明のちらつきの時刻のずらし：最初の値と、1灯ごとに足す値（同時にちらつかないように）
    constexpr float ExtraLightFlickerOffset = 12.3f;
    constexpr float ExtraLightFlickerStep = 0.77f;
}

namespace LayoutExtras
{
    Spawned Spawn(SceneLayoutBuilder& builder, const PropLayout& layout, PropLayoutFile::PlacementBindings& bindings)
    {
        Spawned spawned;
        for (const PropPlacement& placement : layout.Placements)
        {
            const std::string_view name = placement.Name;
            if (name.starts_with(LightPrefix))
            {
                CeilingLight* light = builder.Create<CeilingLight>(placement.Name);
                // size を書いていない行は、通路の標準の器具の大きさにしている
                light->SetScale(CeilingLight::StandardSize.x, CeilingLight::StandardSize.y, CeilingLight::StandardSize.z);
                PropLayoutFile::ApplyPlacement(*light, placement);
                // 停電中も赤い非常灯として点け、照明ごとにちらつきの時刻をずらしている（同時にちらつかないように）
                light->SetEmergencyLight(true, ExtraLightFlickerOffset + static_cast<float>(spawned.Lights.size()) * ExtraLightFlickerStep);
                bindings.emplace_back(placement.Name, [light](const PropPlacement& moved)
                {
                    PropLayoutFile::ApplyPlacement(*light, moved);
                });
                spawned.Lights.push_back(light);
            }
            else if (name.starts_with(BatteryPrefix))
            {
                BatteryItem* battery = builder.Create<BatteryItem>(placement.Name);
                PropLayoutFile::ApplyPlacement(*battery, placement);
                bindings.emplace_back(placement.Name, [battery](const PropPlacement& moved)
                {
                    PropLayoutFile::ApplyPlacement(*battery, moved);
                });
                spawned.Batteries.push_back(battery);
            }
        }
        return spawned;
    }
}
