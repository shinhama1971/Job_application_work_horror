// ============================================================================
// ファイルの役割: 1フレームの描画の順番（影 → 水面の反射 → 監視映像 → 本描画 → 画面効果 → HUD）と、描かない物の省き方を管理している。
// 主な技術: 何段階にも分けた描画、シャドウマップ、視錐台カリング、ポストプロセス、描き直す頻度の調整
// ============================================================================

#include "Game.h"
#include "Scene.h"
#include "Renderer.h"
#include "ScreenDustOverlay.h"
#include "Player.h"
#include "DebugUI.h"

#include <algorithm>
#include <cmath>

namespace
{
    // カリングに使う境界球を、見える側に寄せて大きめに作っている
    WorldBoundingSphere GetConservativeCullingBounds(const Object& object)
    {
        if (object.HasModelBounds())
        {
            WorldBoundingSphere bounds = object.GetWorldBoundingSphere();
            // 画面端の余白を残し、実際のモデルの大きさを使った境界球でも急に消えないようにしている。
            bounds.Radius += 3.0f;
            return bounds;
        }

        const DirectX::SimpleMath::Vector3 scale = object.GetScale();
        WorldBoundingSphere bounds;
        bounds.Center = object.GetPosition();
        // コードで頂点を作ったメッシュは、大きさ（Scale）の対角線の半分に余白を足した半径にしている。
        bounds.Radius = 0.5f * std::sqrt(
            scale.x * scale.x + scale.y * scale.y + scale.z * scale.z) + 3.0f;
        return bounds;
    }

    // カメラに映るかを判定している（カリングしない設定のObjectは常に描いている）
    bool IsVisibleToCamera(
        const Object& object,
        const Camera& camera,
        bool testVertical)
    {
        if (!object.UsesCameraCulling())
        {
            return true;
        }

        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        return camera.IsSphereVisible(
            bounds.Center, bounds.Radius, testVertical);
    }

    // 影を落とす可能性があるかを判定している（影の届く距離の中にあり、視野の近くにある物だけ）
    bool IsRelevantToShadowMap(const Object& object, const Camera& camera)
    {
        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        const float radius = bounds.Radius;
        const DirectX::SimpleMath::Vector3 offset =
            bounds.Center - camera.GetPosition();
        // 影の投影のfar=280の境目に大きな物の一部が掛かる場合も残すため、
        // 中心までの距離の判定には半径と固定の余白を足している。
        const float shadowRange = 292.0f + radius;
        const float distanceSquared =
            offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
        return distanceSquared <= shadowRange * shadowRange &&
            camera.IsSphereVisible(bounds.Center, radius + 18.0f, false);
    }

    // 水面の反射に映す価値があるかを、距離で判定している
    bool IsNearEnoughForReflection(const Object& object, const Camera& camera)
    {
        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        const float radius = bounds.Radius;
        const DirectX::SimpleMath::Vector3 offset =
            bounds.Center - camera.GetPosition();
        // 縦横半分の解像度の反射では見分けにくい遠くの物は省いている。大きな物だけは半径の分を
        // 判定の距離に足し、背景になる壁が欠けないようにしている。
        const float reflectionRange = 230.0f + radius;
        return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <=
            reflectionRange * reflectionRange;
    }

}

namespace Core
{
    // 監視カメラなどの補助カメラの視点で、補助の映像に映してよいObjectだけを描いている
    void Game::DrawWorldForAuxiliaryCamera(Camera& camera)
    {
        for (auto& object : m_ObjectManager.GetAllObjects())
        {
            if (object->IsDestroy() || !object->DrawsInAuxiliaryView())
            {
                continue;
            }
            object->Draw(&camera);
        }
    }

    // 影・反射などの事前の描画は必要なフレームだけ更新し、
    // 本描画を画面効果に取り込んでから、HUDとデバッグ画面を重ねている。
    // GPU時間は描画の段階ごとにGpuTimerで測っている。
    void Game::Draw()
    {
        Debug::UI::BeginFrame();
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        m_Instance->m_GpuTimer.BeginFrame(context);

        // 水面の光の揺らぎ（壁のピクセルシェーダー）で使う、プレイヤーの視点のビュー行列を渡している。
        // 懐中電灯はこの視点から照らしているため、反射を描くときも同じ行列を使う。
        {
            DirectX::SimpleMath::Matrix mainView;
            DirectX::SimpleMath::Matrix mainProjection;
            m_Instance->m_Camera.GetMainMatrices(mainView, mainProjection);
            Renderer::SetWaterCausticsView(mainView);
        }

        // 光を放つObjectから、このフレームの点光源を集めてGPUへ送っている。
        // この後の反射・監視映像の描画では全部の光源を、本描画ではタイルごとのリストを使っている。
        m_Instance->m_FramePointLights.clear();
        for (const auto& object : m_Instance->m_ObjectManager.GetAllObjects())
        {
            if (!object->IsDestroy())
            {
                object->CollectPointLights(m_Instance->m_FramePointLights);
            }
        }
        m_Instance->m_TiledLighting.SetLights(m_Instance->m_FramePointLights);

        unsigned int mainDrawn = 0;
        unsigned int mainCulled = 0;
        unsigned int shadowDrawn = 0;
        unsigned int shadowCulled = 0;
        unsigned int reflectionDrawn = 0;
        unsigned int reflectionCulled = 0;
        bool reflectionSkipped = false;
        // 影を描き直す間隔：エフェクト設定が高なら毎フレーム、中なら2フレーム、低なら3フレームに1回
        const unsigned int shadowInterval =
            m_Instance->m_Settings.GetEffectLevel() >= 2
                ? 1u
                : (m_Instance->m_Settings.GetEffectLevel() == 1 ? 2u : 3u);
        const bool updateShadow =
            (m_Instance->m_ShadowFrameIndex++ % shadowInterval) == 0u;
        // 影：影を落とす物だけを、ライトから見た深度としてシャドウマップへ描いている
        if (updateShadow)
        {
            m_Instance->m_GpuTimer.BeginPass(GpuPass::Shadow, context);
            m_Instance->m_ShadowMap.Begin(m_Instance->m_Camera);
            for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
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
            m_Instance->m_GpuTimer.EndPass(GpuPass::Shadow, context);
        }
        // 描き直さないフレームは、前に描いたシャドウマップをそのまま使っている
        else
        {
            m_Instance->m_GpuTimer.SkipPass(GpuPass::Shadow);
            m_Instance->m_ShadowMap.Bind();
        }

        // 水面の反射は1面だけ。水たまりが画面に映っているかを先に調べている
        if (m_Instance->m_CurrentScene == SceneName::Stage)
        {
            bool reflectionVisible = false;
            for (const auto& object :
                m_Instance->m_ObjectManager.GetAllObjects())
            {
                if (!object->IsDestroy() &&
                    object->IsPlanarReflectionSurfaceVisible(
                        m_Instance->m_Camera))
                {
                    reflectionVisible = true;
                    break;
                }
            }

            // 移動中は反射カメラも動くため毎フレーム描き直している。止まっている間は反射像を
            // 使い回し、扉や照明の変化を拾うためだけに、間隔をあけて描き直している。
            const DirectX::SimpleMath::Vector3 reflectionCameraPosition =
                m_Instance->m_Camera.GetPosition();
            const DirectX::SimpleMath::Vector3 reflectionCameraForward =
                m_Instance->m_Camera.GetForward();
            const DirectX::SimpleMath::Vector3 reflectionPositionDelta =
                reflectionCameraPosition -
                m_Instance->m_LastReflectionCameraPosition;
            const bool reflectionCameraMoved =
                !m_Instance->m_HasReflectionCameraPose ||
                reflectionPositionDelta.LengthSquared() > 0.03f * 0.03f ||
                reflectionCameraForward.Dot(
                    m_Instance->m_LastReflectionCameraForward) < 0.99998f;
            const unsigned int minimumReflectionInterval =
                m_Instance->m_Settings.GetEffectLevel() == 0 ? 2u : 1u;
            const unsigned int movingReflectionInterval = (std::max)(
                Debug::UI::GetReflectionUpdateInterval(),
                minimumReflectionInterval);
            const unsigned int idleReflectionInterval =
                m_Instance->m_Settings.GetEffectLevel() >= 2
                    ? 3u
                    : (m_Instance->m_Settings.GetEffectLevel() == 1 ? 6u : 8u);
            const unsigned int reflectionInterval = reflectionCameraMoved
                ? movingReflectionInterval
                : (std::max)(movingReflectionInterval, idleReflectionInterval);
            // 水面が画面に入った最初のフレームはすぐに描き直し、古い反射を見せないようにしている。
            const bool updateReflection = reflectionVisible &&
                (!m_Instance->m_WasReflectionVisible ||
                    (m_Instance->m_ReflectionFrameIndex++ %
                        reflectionInterval) == 0u);
            if (updateReflection)
            {
                m_Instance->m_GpuTimer.BeginPass(
                    GpuPass::Reflection, context);
                // 床の高さ（-99.5）を鏡の面にして、上下を反転したカメラで描いている
                m_Instance->m_PlanarReflection.Begin(
                    m_Instance->m_Camera,
                    -99.5f);

                for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
                {
                    if (o->IsDestroy() ||
                        !o->ContributesToPlanarReflection())
                    {
                        continue;
                    }

                    // 反射カメラは上下が反転するため、左右・前後・距離だけを判定している。
                    // これで上下方向の判定ミスを避けつつ、廊下の後ろ側の物を大きく減らしている。
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
                m_Instance->m_GpuTimer.EndPass(
                    GpuPass::Reflection, context);
                m_Instance->m_LastReflectionCameraPosition =
                    reflectionCameraPosition;
                m_Instance->m_LastReflectionCameraForward =
                    reflectionCameraForward;
                m_Instance->m_HasReflectionCameraPose = true;
            }
            // 描き直さないフレームは、前の反射像を使っている（水面が見えていなければ省いたことを記録している）
            else
            {
                m_Instance->m_GpuTimer.SkipPass(GpuPass::Reflection);
                m_Instance->m_PlanarReflection.Bind();
                reflectionSkipped = !reflectionVisible;
            }
            m_Instance->m_WasReflectionVisible = reflectionVisible;
        }
        else
        {
            m_Instance->m_GpuTimer.SkipPass(GpuPass::Reflection);
            m_Instance->m_WasReflectionVisible = false;
            m_Instance->m_HasReflectionCameraPose = false;
        }

        // 監視映像など、Sceneが持つ補助カメラの映像は、本描画の前に描いておいている。
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->RenderOffscreen();
        }

        // 本描画：レンダーターゲットを消してから、カメラに映る物だけを描いている
        m_Instance->m_GpuTimer.BeginPass(GpuPass::MainScene, context);
        Renderer::DrawStart();
        // 深度プリパス：不透明な壁・床・扉の深度だけを、本描画の深度バッファへ先に描いている。
        // ・本描画では、奥に隠れた画素が深度の判定で先に捨てられ、重いピクセルシェーダーを動かさずに済む。
        // ・タイルベースライティングでは、タイルごとの一番奥の深度より奥の光源を外せる。
        // 本描画と同じカリングの判定を使い、本描画で描かない物の深度は書かないようにしている。
        Renderer::SetDepthEnable(true);
        for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
        {
            if (o->IsDestroy() || !o->WritesDepthPrepass() ||
                !IsVisibleToCamera(*o, m_Instance->m_Camera, true))
            {
                continue;
            }
            o->DrawDepthPrepass(&m_Instance->m_Camera);
        }
        // プレイヤー視点のタイルごとのライトリストを、Compute Shaderで作ってから描いている。
        m_Instance->m_TiledLighting.BuildTiles(m_Instance->m_Camera, true);

        for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
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
        m_Instance->m_GpuTimer.EndPass(GpuPass::MainScene, context);

        // デバッグ画面に表示する、描いた数と省いた数を渡している
        Debug::UI::SetCullingStats(
            mainDrawn,
            mainCulled,
            shadowDrawn,
            shadowCulled,
            reflectionDrawn,
            reflectionCulled,
            reflectionSkipped);

        // 描き終えた画面を取り込み、露出・ブルーム・光の筋・ノイズなどの画面効果を重ねている
        m_Instance->m_GpuTimer.BeginPass(GpuPass::PostProcess, context);
        m_Instance->m_PostProcess.CaptureBackBuffer();
        m_Instance->m_PostProcess.Draw(&m_Instance->m_GpuTimer);
        m_Instance->m_GpuTimer.EndPass(GpuPass::PostProcess, context);

        // ブルームの後にHUDと画面の文字を描き、文字の輪郭がぼけないようにしている。
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Draw(&m_Instance->m_Camera);
        }
        Debug::UI::Draw(m_Instance->m_PostProcess);

        // GPU時間の計測を締めくくり、画面に表示している（Present）
        m_Instance->m_GpuTimer.EndFrame(context);
        Renderer::DrawEnd();
    }
}
