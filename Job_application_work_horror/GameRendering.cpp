// ============================================================================
// ファイルの役割: カリング、影、水面反射、本描画の順序を管理します。
// ============================================================================

#include "Game.h"
#include "Scene.h"
#include "Renderer.h"
#include "Ground.h"
#include "Texture2D.h"
#include "ScreenDustOverlay.h"
#include "Player.h"
#include "Wall.h"
#include "Door.h"
#include "CeilingLight.h"
#include "FuseBox.h"
#include "Item.h"
#include "BatteryItem.h"
#include "ShadowMan.h"
#include "DebugUI.h"

#include <algorithm>
#include <cmath>

namespace
{
    // 描画内容を持つワールドオブジェクトだけを対象にします。
    // HUD、プレイヤー、各種トリガーは従来どおり描画経路を通すため挙動を変えません。
    bool UsesCameraCulling(const Object* object)
    {
        return dynamic_cast<const Wall*>(object) != nullptr ||
            dynamic_cast<const Door*>(object) != nullptr ||
            dynamic_cast<const CeilingLight*>(object) != nullptr ||
            dynamic_cast<const FuseBox*>(object) != nullptr ||
            dynamic_cast<const Item*>(object) != nullptr ||
            dynamic_cast<const BatteryItem*>(object) != nullptr ||
            dynamic_cast<const ShadowMan*>(object) != nullptr;
    }

    float GetConservativeCullingRadius(const Object& object)
    {
        const DirectX::SimpleMath::Vector3 scale = object.GetScale();
        // 各メッシュはローカル[-0.5, 0.5]を基準に作られているため、
        // scaleの半対角を境界球にし、複合形状用の余白も加えます。
        return 0.5f * std::sqrt(
            scale.x * scale.x + scale.y * scale.y + scale.z * scale.z) + 3.0f;
    }

    bool IsVisibleToCamera(
        const Object& object,
        const Camera& camera,
        bool testVertical)
    {
        return !UsesCameraCulling(&object) || camera.IsSphereVisible(
            object.GetPosition(),
            GetConservativeCullingRadius(object),
            testVertical);
    }

    bool IsRelevantToShadowMap(const Object& object, const Camera& camera)
    {
        const float radius = GetConservativeCullingRadius(object);
        const DirectX::SimpleMath::Vector3 offset =
            object.GetPosition() - camera.GetPosition();
        // ShadowMapのfarは280。少し外側まで残し、画面端へ落ちる影を保護します。
        const float shadowRange = 292.0f + radius;
        const float distanceSquared =
            offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
        return distanceSquared <= shadowRange * shadowRange &&
            camera.IsSphereVisible(object.GetPosition(), radius + 18.0f, false);
    }

    bool IsNearEnoughForReflection(const Object& object, const Camera& camera)
    {
        const float radius = GetConservativeCullingRadius(object);
        const DirectX::SimpleMath::Vector3 offset =
            object.GetPosition() - camera.GetPosition();
        // 1/6解像度の反射では遠景の細部は判別できないため、遠方を省略します。
        // 大型の壁はradius分だけ範囲を広げ、背景が欠けないようにします。
        const float reflectionRange = 340.0f + radius;
        return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <=
            reflectionRange * reflectionRange;
    }
}

namespace Core
{
    // 影・反射などの事前パスを必要なフレームだけ更新し、
    // 本描画をPostProcessへ取り込んでからHUDとデバッグUIを重ねます。
    void Game::Draw()
    {
        Debug::UI::BeginFrame();
        unsigned int mainDrawn = 0;
        unsigned int mainCulled = 0;
        unsigned int shadowDrawn = 0;
        unsigned int shadowCulled = 0;
        unsigned int reflectionDrawn = 0;
        unsigned int reflectionCulled = 0;
        bool reflectionSkipped = false;
        const unsigned int shadowInterval =
            m_Instance->m_EffectLevel >= 2
                ? 1u
                : (m_Instance->m_EffectLevel == 1 ? 2u : 3u);
        const bool updateShadow =
            (m_Instance->m_ShadowFrameIndex++ % shadowInterval) == 0u;
        if (updateShadow)
        {
            m_Instance->m_ShadowMap.Begin(m_Instance->m_Camera);
            for (auto& o : m_Instance->m_Objects)
            {
                if (o->IsDestroy() || !o->CastsShadow())
                {
                    continue;
                }

                if (!IsRelevantToShadowMap(*o, m_Instance->m_Camera))
                {
                    ++shadowCulled;
                    continue;
                }

                ++shadowDrawn;
                o->DrawShadow();
            }
            m_Instance->m_ShadowMap.End();
        }
        else
        {
            m_Instance->m_ShadowMap.Bind();
        }

        if (m_Instance->m_CurrentScene == SceneName::Stage)
        {
            bool reflectionVisible = false;
            for (const auto& object : m_Instance->m_Objects)
            {
                const Ground* ground = dynamic_cast<const Ground*>(object.get());
                if (!object->IsDestroy() && ground != nullptr &&
                    ground->IsAnyPuddleVisible(m_Instance->m_Camera))
                {
                    reflectionVisible = true;
                    break;
                }
            }

            // 平面反射はステージをもう一度描くため、通常品質では30Hzに抑えます。
            // 水面の波・歪み・波紋は本描画側で60Hz更新されるので動きは維持されます。
            // 60Hz反射は高品質設定だけに限定し、ゲーム全体の60fpsを優先します。
            const unsigned int minimumReflectionInterval =
                m_Instance->m_EffectLevel >= 2
                    ? 1u
                    : (m_Instance->m_EffectLevel == 1 ? 2u : 4u);
            const unsigned int reflectionInterval = (std::max)(
                Debug::UI::GetReflectionUpdateInterval(),
                minimumReflectionInterval);
            // 水面が画面へ入った最初のフレームは直ちに更新し、古い反射を見せません。
            const bool updateReflection = reflectionVisible &&
                (!m_Instance->m_WasReflectionVisible ||
                    (m_Instance->m_ReflectionFrameIndex++ %
                        reflectionInterval) == 0u);
            if (updateReflection)
            {
                m_Instance->m_PlanarReflection.Begin(
                    m_Instance->m_Camera,
                    -99.5f);

                for (auto& o : m_Instance->m_Objects)
                {
                    if (o->IsDestroy() ||
                        dynamic_cast<Ground*>(o.get()) != nullptr ||
                        dynamic_cast<Player*>(o.get()) != nullptr ||
                        dynamic_cast<Texture2D*>(o.get()) != nullptr ||
                        dynamic_cast<ScreenDustOverlay*>(o.get()) != nullptr)
                    {
                        continue;
                    }

                    // 反射カメラは上下が反転するため、左右・前後・距離だけを判定します。
                    // これにより縦方向の誤判定を避けつつ廊下後方を大きく削減できます。
                    if (!IsVisibleToCamera(*o, m_Instance->m_Camera, false) ||
                        !IsNearEnoughForReflection(*o, m_Instance->m_Camera))
                    {
                        ++reflectionCulled;
                        continue;
                    }

                    ++reflectionDrawn;
                    o->Draw(&m_Instance->m_Camera);
                }

                m_Instance->m_PlanarReflection.End(
                    m_Instance->m_Camera);
            }
            else
            {
                m_Instance->m_PlanarReflection.Bind();
                reflectionSkipped = !reflectionVisible;
            }
            m_Instance->m_WasReflectionVisible = reflectionVisible;
        }
        else
        {
            m_Instance->m_WasReflectionVisible = false;
        }

        Renderer::DrawStart();

        for (auto& o : m_Instance->m_Objects)
        {
            if (!o->IsDestroy())
            {
                if (!IsVisibleToCamera(*o, m_Instance->m_Camera, true))
                {
                    ++mainCulled;
                    continue;
                }

                ++mainDrawn;
                o->Draw(&m_Instance->m_Camera);
            }
        }

        Debug::UI::SetCullingStats(
            mainDrawn,
            mainCulled,
            shadowDrawn,
            shadowCulled,
            reflectionDrawn,
            reflectionCulled,
            reflectionSkipped);

        m_Instance->m_PostProcess.CaptureBackBuffer();
        m_Instance->m_PostProcess.Draw();

        // Draw HUD and scene overlays after bloom so text stays sharp.
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Draw(&m_Instance->m_Camera);
        }
        Debug::UI::Draw(m_Instance->m_PostProcess);


        Renderer::DrawEnd();
    }
}
