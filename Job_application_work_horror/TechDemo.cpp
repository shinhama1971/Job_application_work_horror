// ============================================================================
// ファイルの役割: 描画の技術を、その場で切り替えて見比べる「技術デモ」（TechDemo.hの説明を参照）。
// 主な技術: 方式の切り替え、GPU時間の平均、HUDの操作パネル
// ============================================================================

#include "TechDemo.h"

#include "Game.h"
#include "GpuTimer.h"
#include "Hud.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "input.h"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // 切り替えられる項目
    enum class Topic
    {
        TiledLighting,
        DepthPrepass,
        PlanarReflection,
        FlashlightShadow,
        AutoExposure,
        WaterCaustics,
        Count
    };
    constexpr int TopicCount = static_cast<int>(Topic::Count);

    // 項目ごとの名前、方式の名前（最大3つ）、普段の方式、説明（2行）
    struct TopicInfo
    {
        const char* Name;
        std::array<const char*, 3> Variants;
        int VariantCount;
        int DefaultVariant;
        std::array<const char*, 2> Description;
    };

    // 並びは Topic と同じにしている
    const std::array<TopicInfo, TopicCount> Topics =
    { {
        { "タイルベースライティング",
          { "全部の光源を計算", "タイルで光源を絞る", "タイルごとの光源の数" }, 3, 1,
          { "画面を16x16のタイルに分け、Compute Shaderで",
            "タイルに届く光源だけを選んで計算している" } },
        { "深度プリパス",
          { "なし", "あり", nullptr }, 2, 1,
          { "先に深度だけを描き、奥に隠れた画素の",
            "重いライティングの計算を省いている" } },
        { "水面の反射",
          { "なし", "あり", nullptr }, 2, 1,
          { "床の高さで上下を反転したカメラで景色を描き、",
            "水たまりにだけ、波でゆらして映している" } },
        { "懐中電灯の影",
          { "影なし", "硬い影", "PCSS（柔らかい影）" }, 3, 2,
          { "遮る物から離れた所ほど影の縁をぼかしている",
            "（物の根元はくっきり、遠くの壁ではぼんやり）" } },
        { "自動露出",
          { "固定", "自動", nullptr }, 2, 1,
          { "Compute Shaderで画面の明るさの平均を測り、",
            "暗い所では目が慣れるように明るくしている" } },
        { "水面の光の揺らぎ",
          { "なし", "あり", nullptr }, 2, 1,
          { "西棟の水面で跳ね返った懐中電灯の光が、",
            "壁と天井に揺らいで映る（西棟で見られる）" } },
    } };

    // 開いているか、選んでいる項目、項目ごとの今の方式
    bool g_Active = false;
    int g_SelectedTopic = 0;
    std::array<int, TopicCount> g_Variants{};
    // タイルごとの光源の数の表示（デバッグ表示7）を設定したか（閉じたときなどに戻すため）
    bool g_HeatmapApplied = false;

    // パネルを描くHUD（Init で作り、Uninit で解放している）
    std::unique_ptr<Hud> g_Hud;

    // 表示するGPU時間（ミリ秒）。毎フレームの値はばらつくため、少しずつ追いかける平均にしている
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> g_AverageMilliseconds{};
    std::array<bool, static_cast<std::size_t>(GpuPass::Count)> g_HasAverage{};

    // 項目の今の方式を返している（閉じているときは普段の方式）
    int GetVariant(Topic topic)
    {
        const int index = static_cast<int>(topic);
        return g_Active ? g_Variants[index] : Topics[index].DefaultVariant;
    }

    // すべての項目を普段の方式に戻している
    void ResetVariants()
    {
        for (int index = 0; index < TopicCount; ++index)
        {
            g_Variants[index] = Topics[index].DefaultVariant;
        }
    }

    // GPU時間の平均を更新している（描いていない段階は、平均を消している）
    void UpdateAverage(const GpuTimer& gpuTimer, GpuPass pass)
    {
        const std::size_t index = static_cast<std::size_t>(pass);
        const GpuTiming timing = gpuTimer.GetTiming(pass);
        if (timing.Status == GpuTimingStatus::Skipped)
        {
            g_HasAverage[index] = false;
            return;
        }
        if (timing.Status != GpuTimingStatus::Available)
        {
            return;
        }
        g_AverageMilliseconds[index] = g_HasAverage[index]
            ? g_AverageMilliseconds[index] * 0.9 + timing.Milliseconds * 0.1
            : timing.Milliseconds;
        g_HasAverage[index] = true;
    }

    // 「名前 1.23ms」の形の文字列を作っている（描いていなければ「--」）
    std::string FormatTiming(const char* label, GpuPass pass)
    {
        const std::size_t index = static_cast<std::size_t>(pass);
        if (!g_HasAverage[index])
        {
            return std::string(label) + " --";
        }
        char text[32];
        std::snprintf(text, sizeof(text), " %.2fms", g_AverageMilliseconds[index]);
        return std::string(label) + text;
    }
}

namespace Tools::TechDemo
{
    void Init()
    {
        ResetVariants();
        g_Hud = std::make_unique<Hud>();
        g_Hud->Init();
    }

    void Uninit()
    {
        if (g_Hud)
        {
            g_Hud->Uninit();
            g_Hud.reset();
        }
        g_Active = false;
    }

    void Update(Effect::PostProcess& postProcess)
    {
        // F2で開け閉めしている。閉じたら、すべて普段の方式に戻している
        if (Input::GetKeyTrigger(VK_F2))
        {
            g_Active = !g_Active;
            ResetVariants();
            if (!g_Active)
            {
                postProcess.SetAutoExposureEnabled(true);
            }
        }

        if (g_Active)
        {
            // ↑↓で項目を選び、←→で方式を切り替えている（端まで行ったら反対の端へ回る）
            if (Input::GetKeyTrigger(VK_UP))
            {
                g_SelectedTopic = (g_SelectedTopic + TopicCount - 1) % TopicCount;
            }
            if (Input::GetKeyTrigger(VK_DOWN))
            {
                g_SelectedTopic = (g_SelectedTopic + 1) % TopicCount;
            }
            const int variantCount = Topics[g_SelectedTopic].VariantCount;
            int& variant = g_Variants[g_SelectedTopic];
            if (Input::GetKeyTrigger(VK_LEFT))
            {
                variant = (variant + variantCount - 1) % variantCount;
            }
            if (Input::GetKeyTrigger(VK_RIGHT))
            {
                variant = (variant + 1) % variantCount;
            }

            postProcess.SetAutoExposureEnabled(GetVariant(Topic::AutoExposure) == 1);
        }

        // タイルごとの光源の数を、シェーダーのデバッグ表示（7）で色分けして見せている
        const bool showHeatmap = GetVariant(Topic::TiledLighting) == 2;
        if (showHeatmap)
        {
            Renderer::SetDebugViewMode(7);
            g_HeatmapApplied = true;
        }
        else if (g_HeatmapApplied)
        {
            Renderer::SetDebugViewMode(0);
            g_HeatmapApplied = false;
        }
    }

    bool IsActive()
    {
        return g_Active;
    }

    bool UsesTiledLighting()
    {
        return GetVariant(Topic::TiledLighting) != 0;
    }

    bool UsesDepthPrepass()
    {
        return GetVariant(Topic::DepthPrepass) == 1;
    }

    bool UsesPlanarReflection()
    {
        return GetVariant(Topic::PlanarReflection) == 1;
    }

    int GetShadowMode()
    {
        return GetVariant(Topic::FlashlightShadow);
    }

    float GetWaterCausticsScale()
    {
        return GetVariant(Topic::WaterCaustics) == 1 ? 1.0f : 0.0f;
    }

    void Draw(const GpuTimer& gpuTimer)
    {
        for (std::size_t pass = 0; pass < static_cast<std::size_t>(GpuPass::Count); ++pass)
        {
            UpdateAverage(gpuTimer, static_cast<GpuPass>(pass));
        }

        if (!g_Active || !g_Hud)
        {
            return;
        }

        // 項目の一覧（名前と今の方式）
        std::vector<Hud::TechDemoRow> rows;
        rows.reserve(TopicCount);
        for (int index = 0; index < TopicCount; ++index)
        {
            rows.push_back({
                Topics[index].Name,
                Topics[index].Variants[g_Variants[index]],
                index == g_SelectedTopic });
        }

        // 選んでいる項目の説明と、GPU時間
        const TopicInfo& selected = Topics[g_SelectedTopic];
        std::vector<std::string> infoLines;
        infoLines.emplace_back(selected.Description[0]);
        infoLines.emplace_back(selected.Description[1]);
        if (g_SelectedTopic == static_cast<int>(Topic::TiledLighting))
        {
            const Effect::TiledLighting* tiledLighting =
                Core::Game::GetInstance()->GetTiledLighting();
            infoLines.push_back(
                "光源 " + std::to_string(tiledLighting->GetLightCount()) +
                "個   タイル " + std::to_string(tiledLighting->GetTileCount()) + "個");
        }
        infoLines.push_back(
            FormatTiming("GPU合計", GpuPass::Total) + "   " +
            FormatTiming("本描画", GpuPass::MainScene));
        infoLines.push_back(
            FormatTiming("反射", GpuPass::Reflection) + "   " +
            FormatTiming("影", GpuPass::Shadow) + "   " +
            FormatTiming("画面効果", GpuPass::PostProcess));

        g_Hud->DrawTechDemoPanel(rows, infoLines);
    }
}
