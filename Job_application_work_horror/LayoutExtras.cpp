// ============================================================================
// ファイルの役割: 配置ファイルの「何個でも置ける物」を作っている（説明は LayoutExtras.h を参照）。
// ============================================================================

#include "LayoutExtras.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "SceneLayoutBuilder.h"

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
                light->SetScale(22.0f, 2.0f, 10.0f);
                PropLayoutFile::ApplyPlacement(*light, placement);
                // 停電中も赤い非常灯として点け、照明ごとにちらつきの時刻をずらしている（同時にちらつかないように）
                light->SetEmergencyLight(true, 12.3f + static_cast<float>(spawned.Lights.size()) * 0.77f);
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
