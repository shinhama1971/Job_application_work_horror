// ============================================================================
// ファイルの役割: 1フレームの描画の順番（影 → 水面の反射 → 監視映像 → 本描画 → 画面効果 → HUD）と、描かない物の省き方を管理している。
// 主な技術: 何段階にも分けた描画、シャドウマップ、視錐台カリング、ポストプロセス、描き直す頻度の調整
// ============================================================================

#include "Game.h"
#include "AmbientOcclusion.h"
#include "DDGIVolume.h"
#include "FigureVolumes.h"
#include "GpuTimer.h"
#include "LightShafts.h"
#include "PointShadowMap.h"
#include "PostProcess.h"
#include "ReflectionProbe.h"
#include "ScreenSpaceReflection.h"
#include "ShadowMap.h"
#include "TiledLighting.h"
#include "Application.h"
#include "Scene.h"
#include "Renderer.h"
#include "ScreenDustOverlay.h"
#include "Player.h"
#include "DebugUI.h"
#include "TechDemo.h"
#include "HangingBulb.h"
#include "CaptureMode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    // 影を落とす物の範囲に足すゆとり（影の端が切れないように）と、反射に映す物の画面の外の判定のゆとり
    constexpr float ShadowBoundsMargin = 3.0f;
    constexpr float ReflectionCullMargin = 18.0f;
}

namespace
{
    // カリングに使う境界球を、見える側に寄せて大きめに作っている
    WorldBoundingSphere GetConservativeCullingBounds(const Object& object)
    {
        if (object.HasModelBounds())
        {
            WorldBoundingSphere bounds = object.GetWorldBoundingSphere();
            // 画面端の余白を残し、実際のモデルの大きさを使った境界球でも急に消えないようにしている。
            bounds.Radius += ShadowBoundsMargin;
            return bounds;
        }

        const DirectX::SimpleMath::Vector3 scale = object.GetScale();
        WorldBoundingSphere bounds;
        bounds.Center = object.GetPosition();
        // コードで頂点を作ったメッシュは、大きさ（Scale）の対角線の半分に余白を足した半径にしている。
        bounds.Radius = 0.5f * std::sqrt(
            scale.x * scale.x + scale.y * scale.y + scale.z * scale.z) + ShadowBoundsMargin;
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
            camera.IsSphereVisible(bounds.Center, radius + ReflectionCullMargin, false);
    }

    // 水面の反射に映す価値があるかを、距離で判定している
    bool IsNearEnoughForReflection(const Object& object, const Camera& camera)
    {
        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        const float radius = bounds.Radius;
        const DirectX::SimpleMath::Vector3 offset =
            bounds.Center - camera.GetPosition();
        // キューブマップの反射と環境マップでは見分けにくい遠くの物は省いている。大きな物だけは半径の分を
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
    // GPU時間は描画の段階ごとにGpuTimerで測っている。段階ごとの中身は、この下の関数に分けている。
    void Game::Draw()
    {
        Debug::UI::BeginFrame();
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        Game& game = *m_Instance;
        game.m_GpuTimer->BeginFrame(context);
        const int quality = game.m_Settings.GetEffectLevel();
        ++game.m_EnvironmentQualityFrame;
        game.PrepareFrameState(quality);

        // 水面の光の揺らぎ（壁のピクセルシェーダー）で使う、プレイヤーの視点のビュー行列を渡している。
        // 懐中電灯はこの視点から照らしているため、反射を描くときも同じ行列を使う。
        // 水面の反射の方式（0=なし、1=キューブマップ、2=SSR、3=レイ反射（普段）。撮影・計測では --reflection で指定する）も、ここで床のシェーダーへ渡している。
        // SSRとレイ反射は、深度プリパスの深度と前のフレームの画面を使う（レイ反射は、使えないときは部屋の形との交差だけで映す）。
        // 2面の壁の鏡は、方式に関係なく、いつもレイ反射で映している（mirrorPS）
        const auto reflectionMethod = Tools::TechDemo::GetReflectionMethod();
        const bool usesScreenSpaceRays = Tools::TechDemo::UseDepthPrepass &&
            (reflectionMethod == Tools::TechDemo::ReflectionMethod::ScreenSpace ||
                reflectionMethod == Tools::TechDemo::ReflectionMethod::RoomRay);
        DirectX::SimpleMath::Matrix reflectionView, reflectionProjection;
        game.m_Camera.GetMainMatrices(reflectionView, reflectionProjection);
        Renderer::SetWaterCausticsView(reflectionView);
        Renderer::SetReflectionMode(static_cast<int>(reflectionMethod), reflectionProjection);
        game.SubmitReflectionProps();

        game.RunGpuSimulations(context);
        game.CollectFramePointLights();

        DrawStats stats;
        game.DrawShadowPass(context, stats);
        game.DrawBulbShadowPass(context, quality);
        const bool hasWetFloor = game.HasWetFloor();
        game.DrawReflectionProbe(
            context, reflectionMethod == Tools::TechDemo::ReflectionMethod::CubeMap, hasWetFloor, stats);
        game.DrawEnvironmentProbe(context, quality, hasWetFloor);

        // 監視映像など、Sceneが持つ補助カメラの映像は、本描画の前に描いておいている。
        if (game.m_Scene)
        {
            game.m_Scene->RenderOffscreen();
        }

        // 鏡と水たまりのシェーダーが光線を通す、人の形の距離場（t6・t13・s2）
        game.m_FigureVolumes->Bind();
        // 本描画：レンダーターゲットを消してから、カメラに映る物だけを描いている
        game.m_GpuTimer->BeginPass(GpuPass::MainScene, context);
        Renderer::DrawStart();
        Renderer::SetDepthEnable(true);
        const bool useDepthPrepass = Tools::TechDemo::UseDepthPrepass;
        if (useDepthPrepass)
        {
            game.DrawDepthPrepass();
        }
        // 深度プリパスの深度を使う画面の効果（SSAO・DDGI・光の筋）は、濡れた床のある場面で、画質が低以外のときだけ使う
        const bool screenEffects = useDepthPrepass && hasWetFloor && quality >= 1;
        game.ComputeAmbientOcclusion(context, screenEffects && Tools::TechDemo::UseAmbientOcclusion);
        game.ComputeGlobalIllumination(context, screenEffects && Tools::TechDemo::UseAmbientOcclusion &&
            Debug::UI::IsGlobalIlluminationEnabled() && Tools::CaptureMode::UsesGlobalIllumination());
        game.m_TiledLighting->SetLightShaftsActive(
            game.ComputeLightShafts(context, screenEffects && Tools::TechDemo::UseLightShafts));
        // プレイヤー視点のタイルごとのライトリストを、Compute Shaderで作ってから描いている。
        // 使わない方式では、全部の光源を画素ごとに順に計算させている。
        if constexpr (Tools::TechDemo::UseTiledLighting)
        {
            game.m_TiledLighting->BuildTiles(game.m_Camera, useDepthPrepass);
        }
        else
        {
            game.m_TiledLighting->BindAllLights();
        }
        // SSRとレイ反射のときは、深度プリパスの深度を写し取り、前のフレームの画面と一緒に床のシェーダーへ渡している。
        // （反射の光線をたどる計算は、本描画の床のピクセルシェーダーで行う）
        const bool useScreenSpaceReflection = hasWetFloor && usesScreenSpaceRays;
        if (useScreenSpaceReflection)
        {
            game.m_ScreenSpaceReflection->Prepare(game.m_PostProcess->GetCapturedSceneSRV());
        }
        game.DrawMainSceneObjects(stats);
        game.m_GpuTimer->EndPass(GpuPass::MainScene, context);
        if (useScreenSpaceReflection)
        {
            game.m_ScreenSpaceReflection->Unbind();
        }

        // デバッグ画面に表示する、描いた数と省いた数を渡している
        Debug::UI::SetCullingStats(
            stats.MainDrawn,
            stats.MainCulled,
            stats.ShadowDrawn,
            stats.ShadowCulled,
            stats.ReflectionDrawn);

        // 描き終えた画面を取り込み、露出・ブルーム・光の筋・ノイズなどの画面効果を重ねている
        game.m_GpuTimer->BeginPass(GpuPass::PostProcess, context);
        const bool captured = game.m_PostProcess->CaptureBackBuffer();
        game.m_ScreenSpaceReflection->FinishFrame(
            reflectionView, reflectionProjection, useScreenSpaceReflection && captured);
        game.m_PostProcess->Draw(game.m_GpuTimer.get());
        game.m_GpuTimer->EndPass(GpuPass::PostProcess, context);

        // ブルームの後にHUDと画面の文字を描き、文字の輪郭がぼけないようにしている。
        if (game.m_Scene)
        {
            game.m_Scene->Draw(&game.m_Camera);
        }
        Debug::UI::Draw(*game.m_PostProcess);

        // GPU時間の計測を締めくくり、画面に表示している（Present）
        game.m_GpuTimer->EndFrame(context);
        Renderer::DrawEnd();
    }

    // フレームの始めに、画質と技術デモの設定を描画の仕組みへ渡し、本描画だけで使う物を外している
    void Game::PrepareFrameState(int quality)
    {
        m_TiledLighting->SetLightShaftsDisabled(quality == 0 || !Tools::TechDemo::UseLightShafts);
        // PBRを止めたとき、壁と小物のシェーダーは拡散反射だけで描く（技術デモの比較用。印はb8で渡している）
        m_ShadowMap->SetPbrEnabled(Tools::TechDemo::UsePbr);
        // 画面空間の環境遮蔽は本描画の直前に作るまで使わせない（反射・環境マップ・影など、先に描くほかの視点では使わない）
        m_TiledLighting->SetAmbientOcclusionActive(false);
        m_TiledLighting->SetLightShaftsActive(false);
        m_LightShafts->Unbind();
    }

    // 画面の外の小物（棚・ロッカー・柱・2面の廊下の壁）も、水面のレイ反射と壁の鏡に映すため、
    // カメラに近い小物の箱を選んで床と鏡のシェーダーへ渡している
    void Game::SubmitReflectionProps()
    {
        struct ReflectionPropCandidate
        {
            float DistanceSquared;
            DirectX::SimpleMath::Vector4 Box[3];
        };
        std::vector<ReflectionPropCandidate> candidates;
        {
            const DirectX::SimpleMath::Vector3 eye = m_Camera.GetPosition();
            for (const auto& object : m_ObjectManager.GetAllObjects())
            {
                Object::ReflectionProxy proxy{};
                if (object->IsDestroy() || !object->GetReflectionProxy(proxy))
                {
                    continue;
                }
                // 箱までの水平の距離で比べている（120より遠い物は、水面ではほとんど霧に沈むので映さない）
                const auto& center = proxy.Center;
                const auto& half = proxy.HalfSize;
                const auto& axis = proxy.DoorWidthAxis;
                const DirectX::SimpleMath::Vector2 offset(eye.x - center.x, eye.z - center.z);
                const DirectX::SimpleMath::Vector2 localOffset = axis.LengthSquared() > 0.5f
                    ? DirectX::SimpleMath::Vector2(offset.Dot(axis), offset.Dot({ -axis.y, axis.x }))
                    : offset;
                const float dx = (std::max)(std::abs(localOffset.x) - half.x, 0.0f);
                const float dz = (std::max)(std::abs(localOffset.y) - half.z, 0.0f);
                const float distanceSquared = dx * dx + dz * dz;
                constexpr float ReflectionPropMaxDistance = 120.0f;
                if (distanceSquared > ReflectionPropMaxDistance * ReflectionPropMaxDistance)
                {
                    continue;
                }
                candidates.push_back({ distanceSquared, {
                    DirectX::SimpleMath::Vector4(center.x, center.y, center.z, axis.x),
                    DirectX::SimpleMath::Vector4(half.x, half.y, half.z, axis.y),
                    DirectX::SimpleMath::Vector4(proxy.Color.x, proxy.Color.y, proxy.Color.z, 0.0f) } });
            }
            const std::size_t keep = (std::min)(candidates.size(),
                static_cast<std::size_t>(WATER_CAUSTICS_BUFFER::MaxReflectionProps));
            std::partial_sort(candidates.begin(), candidates.begin() + keep, candidates.end(),
                [](const ReflectionPropCandidate& left, const ReflectionPropCandidate& right)
                {
                    return left.DistanceSquared < right.DistanceSquared;
                });
            candidates.resize(keep);
        }
        std::array<DirectX::SimpleMath::Vector4, WATER_CAUSTICS_BUFFER::MaxReflectionProps * 3> props{};
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            for (int corner = 0; corner < 3; ++corner)
            {
                props[index * 3 + corner] = candidates[index].Box[corner];
            }
        }
        Renderer::SetReflectionProps(props.data(), static_cast<int>(candidates.size()));
    }

    // 描画の前にGPUで計算する物（水たまりの波のCompute Shader）を実行している。
    // 更新の処理ではなくここで実行し、そのGPU時間を「水たまりの波」の段階として測っている。
    void Game::RunGpuSimulations(ID3D11DeviceContext* context)
    {
        m_GpuTimer->BeginPass(GpuPass::WaterRipples, context);
        for (const auto& object : m_ObjectManager.GetAllObjects())
        {
            if (!object->IsDestroy())
            {
                object->RunGpuSimulation();
            }
        }
        m_GpuTimer->EndPass(GpuPass::WaterRipples, context);
    }

    // 光を放つObjectから、このフレームの点光源を集めてGPUへ送っている。
    // この後の反射・監視映像の描画では全部の光源を、本描画ではタイルごとのリストを使っている。
    void Game::CollectFramePointLights()
    {
        m_FramePointLights.clear();
        for (const auto& object : m_ObjectManager.GetAllObjects())
        {
            if (!object->IsDestroy())
            {
                object->CollectPointLights(m_FramePointLights);
            }
        }
        m_TiledLighting->SetLights(m_FramePointLights);
    }

    // 影：影を落とす物だけを、ライトから見た深度としてシャドウマップへ描いている。
    // 描き直す間隔：エフェクト設定が高なら毎フレーム、中なら2フレーム、低なら3フレームに1回
    void Game::DrawShadowPass(ID3D11DeviceContext* context, DrawStats& stats)
    {
        const unsigned int shadowInterval =
            m_Settings.GetEffectLevel() >= 2
                ? 1u
                : (m_Settings.GetEffectLevel() == 1 ? 2u : 3u);
        const bool updateShadow = (m_ShadowFrameIndex++ % shadowInterval) == 0u;
        if (updateShadow)
        {
            m_GpuTimer->BeginPass(GpuPass::Shadow, context);
            m_ShadowMap->Begin(m_Camera);
            for (auto& o : m_ObjectManager.GetAllObjects())
            {
                if (o->IsDestroy() || !o->CastsShadow())
                {
                    continue;
                }

                if (!IsRelevantToShadowMap(*o, m_Camera))
                {
                    ++stats.ShadowCulled;
                    continue;
                }

                ++stats.ShadowDrawn;
                o->DrawShadow();
            }
            m_ShadowMap->End();
            m_GpuTimer->EndPass(GpuPass::Shadow, context);
        }
        // 描き直さないフレームは、前に描いたシャドウマップをそのまま使っている
        else
        {
            m_GpuTimer->SkipPass(GpuPass::Shadow);
            m_ShadowMap->Bind();
        }
    }

    // 揺れる裸電球の影（2面）：カメラに一番近い電球から6方向を撮り、電球からの距離をキューブマップへ書いている。
    // 電球は毎フレーム揺れるので、6面とも毎フレーム描き直している。描く物は懐中電灯の影と同じく、影を落とす物だけで、
    // 光の届く範囲の外の物は省いている。物のDrawShadowが使うシェーダーを、この間だけ距離を書くシェーダーに差し替えている。
    void Game::DrawBulbShadowPass(ID3D11DeviceContext* context, int quality)
    {
        const HangingBulb* shadowBulb = nullptr;
        float nearestDistanceSquared = 0.0f;
        for (const HangingBulb* bulb : GetObjects<HangingBulb>())
        {
            const float distanceSquared = DirectX::SimpleMath::Vector3::DistanceSquared(
                bulb->GetBulbPosition(), m_Camera.GetPosition());
            if (shadowBulb == nullptr || distanceSquared < nearestDistanceSquared)
            {
                shadowBulb = bulb;
                nearestDistanceSquared = distanceSquared;
            }
        }

        if (shadowBulb == nullptr || quality < 2 || !Tools::TechDemo::UseBulbShadow)
        {
            m_ShadowMap->SetPointShadow(DirectX::SimpleMath::Vector3::Zero, 0.0f, false);
            m_PointShadowMap->Unbind();
            m_GpuTimer->SkipPass(GpuPass::BulbShadow);
            return;
        }

        m_GpuTimer->BeginPass(GpuPass::BulbShadow, context);
        const DirectX::SimpleMath::Vector3 lightPosition = shadowBulb->GetBulbPosition();
        const float lightRange = shadowBulb->GetLightRange();
        m_ShadowMap->SetPointShadow(lightPosition, lightRange, true);
        m_ShadowMap->SetShaderOverride(&m_PointShadowMap->GetShader());
        for (int face = 0; face < 6; ++face)
        {
            m_PointShadowMap->BeginFace(face, lightPosition);
            for (auto& o : m_ObjectManager.GetAllObjects())
            {
                if (o->IsDestroy() || !o->CastsShadow())
                {
                    continue;
                }
                const WorldBoundingSphere bounds = GetConservativeCullingBounds(*o);
                const float reach = lightRange + bounds.Radius;
                if (DirectX::SimpleMath::Vector3::DistanceSquared(bounds.Center, lightPosition) > reach * reach)
                {
                    continue;
                }
                o->DrawShadow();
            }
        }
        m_PointShadowMap->End();
        m_ShadowMap->SetShaderOverride(nullptr);
        // 面ごとに差し替えたビュー・射影の行列を、プレイヤーの視点に戻している
        m_Camera.SetCamera();
        m_GpuTimer->EndPass(GpuPass::BulbShadow, context);
    }

    // 濡れた床（水面の反射・環境マップ・SSAOなどを使う床）のある場面か
    bool Game::HasWetFloor() const
    {
        return m_CurrentScene == SceneName::Stage ||
            m_CurrentScene == SceneName::Stage2 ||
            m_CurrentScene == SceneName::Stage3 ||
            m_CurrentScene == SceneName::Forest ||
            m_CurrentScene == SceneName::Title ||
            m_CurrentScene == SceneName::Intro;
    }

    // 水面の反射で、景色を描いて作る物は、技術デモでキューブマップを選んでいる間だけ（濡れた床のある場面）。
    // カメラの位置から6方向のうち1面だけを毎フレーム撮り直している（最初の1フレームだけは6面とも撮る）。
    // ふだんのレイ反射とSSRは、本描画の床のシェーダーで光線を飛ばすので、ここでは何も描かない。
    // 技術デモで「水面の反射なし」を選んでいるときは、床も反射を混ぜない（Ground::Draw）
    void Game::DrawReflectionProbe(ID3D11DeviceContext* context, bool useCubeMap, bool hasWetFloor, DrawStats& stats)
    {
        if (!useCubeMap)
        {
            m_ReflectionProbe->Invalidate();
        }
        if (!hasWetFloor || !useCubeMap)
        {
            m_GpuTimer->SkipPass(GpuPass::Reflection);
            return;
        }

        m_GpuTimer->BeginPass(GpuPass::Reflection, context);
        const int faceCount = m_ReflectionProbe->HasAllFaces() ? 1 : 6;
        for (int face = 0; face < faceCount; ++face)
        {
            m_ReflectionProbe->BeginFace(m_Camera, m_Camera.GetPosition());
            for (auto& o : m_ObjectManager.GetAllObjects())
            {
                if (o->IsDestroy() || !o->ContributesToReflection() ||
                    !IsNearEnoughForReflection(*o, m_Camera))
                {
                    continue;
                }
                ++stats.ReflectionDrawn;
                o->Draw(&m_Camera);
            }
            m_ReflectionProbe->EndFace(m_Camera);
        }
        m_GpuTimer->EndPass(GpuPass::Reflection, context);
    }

    // PBRの環境マップ：金属や濡れた面に映す景色を、カメラの位置から1フレームに1面ずつ撮り直している（最初だけ6面）。
    // 懐中電灯の向きは、目の前を向くビュー空間で決めている。面ごとに視点が違うので、懐中電灯の向きを
    // その面の視点へ回して渡し、プレイヤーが照らしている壁が、そのまま金属に映るようにしている。
    void Game::DrawEnvironmentProbe(ID3D11DeviceContext* context, int quality, bool hasWetFloor)
    {
        const bool updateEnvironment = quality >= 2 || !m_EnvironmentProbe->HasAllFaces() ||
            (m_EnvironmentQualityFrame % 4u) == 0u;
        // PBRを止めているとき（技術デモ）は映り込みを使わないので、環境マップも撮らない
        const bool usesEnvironment = hasWetFloor && quality >= 1 && Tools::TechDemo::UsePbr;
        if (usesEnvironment && updateEnvironment)
        {
            m_GpuTimer->BeginPass(GpuPass::EnvironmentProbe, context);
            const LIGHT sceneLight = Renderer::GetLight();
            DirectX::SimpleMath::Matrix mainView;
            DirectX::SimpleMath::Matrix mainProjection;
            m_Camera.GetMainMatrices(mainView, mainProjection);
            const DirectX::SimpleMath::Vector3 flashlightWorldDirection =
                DirectX::SimpleMath::Vector3::TransformNormal(
                    DirectX::SimpleMath::Vector3(
                        sceneLight.Direction.x, sceneLight.Direction.y, sceneLight.Direction.z),
                    mainView.Invert());
            // 別の視点から描くので、プレイヤー視点のタイルのリストではなく、全部の光源を順に計算させている
            m_TiledLighting->BindAllLights();
            const int faceCount = m_EnvironmentProbe->HasAllFaces() ? 1 : 6;
            for (int face = 0; face < faceCount; ++face)
            {
                m_EnvironmentProbe->BeginFace(m_Camera, m_Camera.GetPosition());
                LIGHT faceLight = sceneLight;
                DirectX::SimpleMath::Vector3 faceDirection =
                    DirectX::SimpleMath::Vector3::TransformNormal(
                        flashlightWorldDirection, m_EnvironmentProbe->GetFaceView());
                faceDirection.Normalize();
                faceLight.Direction = DirectX::SimpleMath::Vector4(
                    faceDirection.x, faceDirection.y, faceDirection.z, 0.0f);
                Renderer::SetLight(faceLight);
                for (auto& o : m_ObjectManager.GetAllObjects())
                {
                    if (o->IsDestroy() || !o->ContributesToReflection() ||
                        !IsNearEnoughForReflection(*o, m_Camera))
                    {
                        continue;
                    }
                    o->Draw(&m_Camera);
                }
                m_EnvironmentProbe->EndFace(m_Camera);
            }
            Renderer::SetLight(sceneLight);
            m_GpuTimer->EndPass(GpuPass::EnvironmentProbe, context);
        }
        else if (usesEnvironment)
        {
            // 更新を休むだけでキャッシュを破棄すると、次回に6面を描き直して逆に重くなる。
            m_EnvironmentProbe->Bind();
            m_GpuTimer->SkipPass(GpuPass::EnvironmentProbe);
        }
        else
        {
            m_EnvironmentProbe->Invalidate();
            m_EnvironmentProbe->Unbind();
            m_GpuTimer->SkipPass(GpuPass::EnvironmentProbe);
        }
        // 水たまりのレイ反射は、画面の外の物の色を、この環境マップ（本物の景色）から読んでいる。
        // 撮っていないとき（PBRを止めた技術デモ・最低の画質）は、これまでどおり部屋の形と模様で映す
        // 撮った位置も渡し、シェーダーはその位置から撮った景色として読む（今の目の位置として読むと、歩くたびに映り込みがずれて跳ねるため）
        Renderer::SetReflectionEnvironment(usesEnvironment && m_EnvironmentProbe->HasAllFaces(),
            m_EnvironmentProbe->GetCaptureCenter());
    }

    // 深度プリパス：不透明な壁・床・扉の深度だけを、本描画の深度バッファへ先に描いている。
    // ・本描画では、奥に隠れた画素が深度の判定で先に捨てられ、重いピクセルシェーダーを動かさずに済む。
    // ・タイルベースライティングでは、タイルごとの一番奥の深度より奥の光源を外せる。
    // 本描画と同じカリングの判定を使い、本描画で描かない物の深度は書かないようにしている。
    // 技術デモでは、深度プリパスとタイルベースライティングを、使わない方式と見比べられるようにしている。
    void Game::DrawDepthPrepass()
    {
        for (auto& o : m_ObjectManager.GetAllObjects())
        {
            if (o->IsDestroy() || !o->WritesDepthPrepass() ||
                !IsVisibleToCamera(*o, m_Camera, true))
            {
                continue;
            }
            o->DrawDepthPrepass(&m_Camera);
        }
    }

    // 画面空間の環境遮蔽（SSAO）：深度プリパスの深度から、物と物の隙間や壁の根元など光が届きにくい所を求めている（1面・2面）。
    // 結果は t15 に設定し、ライトリストの準備で、本描画のときだけ使う印をシェーダーへ送っている。
    void Game::ComputeAmbientOcclusion(ID3D11DeviceContext* context, bool enabled)
    {
        if (!enabled)
        {
            m_AmbientOcclusion->Unbind();
            m_GpuTimer->SkipPass(GpuPass::AmbientOcclusion);
            return;
        }
        m_GpuTimer->BeginPass(GpuPass::AmbientOcclusion, context);
        DirectX::SimpleMath::Matrix occlusionView;
        DirectX::SimpleMath::Matrix occlusionProjection;
        m_Camera.GetMainMatrices(occlusionView, occlusionProjection);
        UINT viewportCount = 1;
        D3D11_VIEWPORT viewport{};
        context->RSGetViewports(&viewportCount, &viewport);
        m_AmbientOcclusion->Compute(occlusionView, occlusionProjection, viewport,
            m_TiledLighting->GetLightSRV(), m_TiledLighting->GetLightCount());
        m_TiledLighting->SetAmbientOcclusionActive(true);
        m_GpuTimer->EndPass(GpuPass::AmbientOcclusion, context);
    }

    // DDGI の間接光：部屋に並べたプローブから光線を飛ばして跳ね返った光を更新し、画面の各画素の間接光を t18 に設定している。
    // 壁と床のシェーダーは、SSAO と同じく本描画のときだけ読む（環境遮蔽を求めたフレームだけ使う）
    void Game::ComputeGlobalIllumination(ID3D11DeviceContext* context, bool enabled)
    {
        if (!enabled)
        {
            m_GlobalIllumination->Unbind();
            m_GpuTimer->SkipPass(GpuPass::GlobalIllumination);
            return;
        }
        m_GpuTimer->BeginPass(GpuPass::GlobalIllumination, context);
        DirectX::SimpleMath::Matrix giView;
        DirectX::SimpleMath::Matrix giProjection;
        m_Camera.GetMainMatrices(giView, giProjection);
        UINT viewportCount = 1;
        D3D11_VIEWPORT viewport{};
        context->RSGetViewports(&viewportCount, &viewport);
        const int giUpdateOverride = Tools::CaptureMode::GetGIUpdateFramesOverride();
        const int giUpdateFrames = giUpdateOverride > 0 ? giUpdateOverride : Debug::UI::GetGIUpdateFrames();
        if (m_GlobalIllumination->Update(
                m_TiledLighting->GetLightSRV(), m_TiledLighting->GetLightCount(),
                Renderer::GetLight(), m_Camera.GetPosition(), giView, static_cast<uint32_t>(giUpdateFrames)))
        {
            m_GlobalIllumination->Apply(giView, giProjection, viewport);
        }
        m_GpuTimer->EndPass(GpuPass::GlobalIllumination, context);
    }

    // 天井の照明の下の光の筋を、半分の解像度で求めている。求められたら true を返している
    bool Game::ComputeLightShafts(ID3D11DeviceContext* context, bool enabled)
    {
        if (!enabled)
        {
            return false;
        }
        DirectX::SimpleMath::Matrix view, projection;
        m_Camera.GetMainMatrices(view, projection);
        UINT count = 1;
        D3D11_VIEWPORT viewport{};
        context->RSGetViewports(&count, &viewport);
        return m_LightShafts->Compute(view, projection, viewport,
            m_TiledLighting->GetLights(), Application::GetDeltaTime());
    }

    // 本描画：カメラに映る物だけを描き、描いた数と省いた数を数えている
    void Game::DrawMainSceneObjects(DrawStats& stats)
    {
        for (auto& o : m_ObjectManager.GetAllObjects())
        {
            if (o->IsDestroy())
            {
                continue;
            }
            if (!IsVisibleToCamera(*o, m_Camera, true))
            {
                ++stats.MainCulled;
                continue;
            }
            ++stats.MainDrawn;
            o->Draw(&m_Camera);
        }
    }
}
