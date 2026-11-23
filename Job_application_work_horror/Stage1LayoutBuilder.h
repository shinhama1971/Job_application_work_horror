// ============================================================================
// ファイルの役割: 1面の配置を組み立て、配置の読み直しに必要な操作を保持する。
// ============================================================================

#pragma once

#include "Stage1Layout.h"
#include "SceneLayoutBuilder.h"
#include "PropLayoutFile.h"

#include <array>
#include <functional>
#include <random>

namespace Stage1Layout
{
    using DirectX::SimpleMath::Color;
    using DirectX::SimpleMath::Vector3;

    // 天井の照明：名前、停電中も赤い非常灯として光るか、点滅の周期をずらす値（そろって点滅すると機械的に見えるため）
    struct CeilingLightSpec
    {
        const char* Name;
        bool Emergency;
        float FlickerOffset;
    };

    // 西棟の誘導で使う天井灯の数。
    constexpr std::size_t WestWingLightCount = 3;

    // 1面の施設を作る手順をまとめている。置き場所の読み方（配置のファイル）と、読み直したときの置き直し方を、手順のあいだで共有している
    class Stage1Builder final
    {
    public:
        Stage1Builder(Core::Game& game, StageObjects& objects, const PropLayout& layout);

        void BuildPlayerAndGround();

        void BuildLayoutProps();

        void BuildWestWingValves();

        void BuildSafeZones();

        void BuildDoorIndicatorAndLoopMarkers();

        void BuildMainCeilingLights();

        void BuildFusesAndWestWingDoor();

        void BuildBatteriesAndRecorders();

        void BuildSideRoomLights();

        void BuildPowerControls();

        void BuildEndAndStartDoors();

        void BuildWallWritings();

        void BuildKeypadRoom();

        void BuildEventsAndShadows();

        void BuildRouteLights();

    private:
        SceneLayoutBuilder m_Builder;
        StageObjects& m_Objects;
        const PropLayout& m_Layout;
        std::mt19937 m_FuseRandom;
        std::array<CeilingLight*, WestWingLightCount> m_WestWingLights{};

        const PropPlacement& At(const char* name) const
        {
            return PropLayoutFile::GetPlacement(m_Layout, name);
        }

        // 置き場所を当てはめ、配置ファイルを読み直したとき（Debug版の F5 と、Unity などで書き出した直後の自動の読み直し）に
        // 同じ物を置き直せるよう、置き直し方を名前と一緒に覚えている
        template<typename T>
        void Place(T& object, const char* name)
        {
            PropLayoutFile::ApplyPlacement(object, At(name));
            T* target = &object;
            m_Objects.placementBindings.emplace_back(name, [target](const PropPlacement& placement)
            {
                PropLayoutFile::ApplyPlacement(*target, placement);
            });
        }

        void Bind(const char* name, std::function<void(const PropPlacement&)> apply);

        // 配置のファイルの spot 行の候補から、プレイごとにランダムに1つ選んで置いている
        template<typename T>
        void PlaceAtRandomSpot(T& object, const char* group)
        {
            const std::vector<Vector3>& candidates = PropLayoutFile::GetSpots(m_Layout, group);
            std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
            PropLayoutFile::MoveTo(object, candidates[pick(m_FuseRandom)]);
        }

        void CreateDoorLintel(const Door& door, const char* name);

        Wall* CreateStageProp(const char* name, const Vector3& position, const Vector3& scale,
            const Color& diffuse, const Color& emission, float shininess, bool collisionEnabled);

        Wall* CreatePlacedStageProp(const char* name, const Color& diffuse, const Color& emission,
            float shininess, bool collisionEnabled);

        CeilingLight* CreateCeilingLight(const CeilingLightSpec& spec);

        FlashlightWriting* CreateWriting(const char* name, const char* texture, const char* alternateTexture,
            bool required = true);

        FlashlightWriting* CreateDigitClue(int order);

        ShadowMan* CreateHiddenShadow(const char* name);
    };
}
