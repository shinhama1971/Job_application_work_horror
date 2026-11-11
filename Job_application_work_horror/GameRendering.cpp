// ============================================================================
// ファイルの役割: 1フレームの描画の順番（影 → 水面の反射 → 監視映像 → 本描画 → 画面効果 → HUD）と、描かない物の省き方を管理している。
// 主な技術: 何段階にも分けた描画、シャドウマップ、視錐台カリング、ポストプロセス、描き直す頻度の調整
// ============================================================================

#include "Game.h"
#include "Application.h"
#include "Scene.h"
#include "Renderer.h"
#include "ScreenDustOverlay.h"
#include "Player.h"
#include "DebugUI.h"
#include "TechDemo.h"
#include "HangingBulb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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
    // GPU時間は描画の段階ごとにGpuTimerで測っている。
    void Game::Draw()
    {
        Debug::UI::BeginFrame();
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        m_Instance->m_GpuTimer.BeginFrame(context);
        const int quality = m_Instance->m_Settings.GetEffectLevel();
        ++m_Instance->m_EnvironmentQualityFrame;
        // 技術デモ（F2）で切り替えた効果も、画質設定と同じように止めている
        m_Instance->m_TiledLighting.SetLightShaftsDisabled(quality == 0 || !Tools::TechDemo::UsesLightShafts());
        // PBRを止めたとき、壁と小物のシェーダーは拡散反射だけで描く（技術デモの比較用。印はb8で渡している）
        m_Instance->m_ShadowMap.SetPbrEnabled(Tools::TechDemo::UsesPbr());
        // 画面空間の環境遮蔽は本描画の直前に作るまで使わせない（反射・環境マップ・影など、先に描くほかの視点では使わない）
        m_Instance->m_TiledLighting.SetAmbientOcclusionActive(false);
        m_Instance->m_TiledLighting.SetLightShaftsActive(false);
        m_Instance->m_LightShafts.Unbind();

        // 水面の光の揺らぎ（壁のピクセルシェーダー）で使う、プレイヤーの視点のビュー行列を渡している。
        // 懐中電灯はこの視点から照らしているため、反射を描くときも同じ行列を使う。
        // 水面の反射の方式（0=なし、1=キューブマップ、2=SSR、3=レイ反射（普段）。技術デモ F2 で切り替える）も、ここで床のシェーダーへ渡している。
        // SSRとレイ反射は、深度プリパスの深度と前のフレームの画面を使う（レイ反射は、使えないときは部屋の形との交差だけで映す）。
        // 2面の壁の鏡は、方式に関係なく、いつもレイ反射で映している（mirrorPS）
        const int reflectionMethod = Tools::TechDemo::GetReflectionMethod();
        const bool usesScreenSpaceRays = reflectionMethod >= 2 && Tools::TechDemo::UsesDepthPrepass();
        DirectX::SimpleMath::Matrix reflectionView, reflectionProjection;
        m_Instance->m_Camera.GetMainMatrices(reflectionView, reflectionProjection);
        Renderer::SetWaterCausticsView(reflectionView);
        Renderer::SetReflectionMode(reflectionMethod, reflectionProjection);
        // 画面の外の小物（棚・ロッカー・柱・2面の廊下の壁）も、水面のレイ反射と壁の鏡に映すため、
        // カメラに近い小物の箱を選んで床と鏡のシェーダーへ渡している
        {
            struct ReflectionPropCandidate
            {
                float DistanceSquared;
                DirectX::SimpleMath::Vector4 Box[3];
            };
            std::vector<ReflectionPropCandidate> candidates;
            {
                const DirectX::SimpleMath::Vector3 eye = m_Instance->m_Camera.GetPosition();
                for (const auto& object : m_Instance->m_ObjectManager.GetAllObjects())
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
                    if (distanceSquared > 120.0f * 120.0f)
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
        m_Instance->m_GpuTimer.BeginPass(GpuPass::WaterRipples, context);
        for (const auto& object : m_Instance->m_ObjectManager.GetAllObjects())
        {
            if (!object->IsDestroy())
            {
                object->RunGpuSimulation();
            }
        }
        m_Instance->m_GpuTimer.EndPass(GpuPass::WaterRipples, context);

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

        // 揺れる裸電球の影（2面）：カメラに一番近い電球から6方向を撮り、電球からの距離をキューブマップへ書いている。
        // 電球は毎フレーム揺れるので、6面とも毎フレーム描き直している。描く物は懐中電灯の影と同じく、影を落とす物だけで、
        // 光の届く範囲の外の物は省いている。物のDrawShadowが使うシェーダーを、この間だけ距離を書くシェーダーに差し替えている。
        {
            const HangingBulb* shadowBulb = nullptr;
            float nearestDistanceSquared = 0.0f;
            for (const HangingBulb* bulb : m_Instance->GetObjects<HangingBulb>())
            {
                const float distanceSquared = DirectX::SimpleMath::Vector3::DistanceSquared(
                    bulb->GetBulbPosition(), m_Instance->m_Camera.GetPosition());
                if (shadowBulb == nullptr || distanceSquared < nearestDistanceSquared)
                {
                    shadowBulb = bulb;
                    nearestDistanceSquared = distanceSquared;
                }
            }

            if (shadowBulb != nullptr && quality >= 2 && Tools::TechDemo::UsesBulbShadow())
            {
                m_Instance->m_GpuTimer.BeginPass(GpuPass::BulbShadow, context);
                const DirectX::SimpleMath::Vector3 lightPosition = shadowBulb->GetBulbPosition();
                const float lightRange = shadowBulb->GetLightRange();
                m_Instance->m_ShadowMap.SetPointShadow(lightPosition, lightRange, true);
                m_Instance->m_ShadowMap.SetShaderOverride(&m_Instance->m_PointShadowMap.GetShader());
                for (int face = 0; face < 6; ++face)
                {
                    m_Instance->m_PointShadowMap.BeginFace(face, lightPosition);
                    for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
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
                m_Instance->m_PointShadowMap.End();
                m_Instance->m_ShadowMap.SetShaderOverride(nullptr);
                // 面ごとに差し替えたビュー・射影の行列を、プレイヤーの視点に戻している
                m_Instance->m_Camera.SetCamera();
                m_Instance->m_GpuTimer.EndPass(GpuPass::BulbShadow, context);
            }
            else
            {
                m_Instance->m_ShadowMap.SetPointShadow(DirectX::SimpleMath::Vector3::Zero, 0.0f, false);
                m_Instance->m_PointShadowMap.Unbind();
                m_Instance->m_GpuTimer.SkipPass(GpuPass::BulbShadow);
            }
        }

        // 水面の反射で、景色を描いて作る物は、技術デモでキューブマップを選んでいる間だけ（濡れた床のある1面と2面）。
        // カメラの位置から6方向のうち1面だけを毎フレーム撮り直している（最初の1フレームだけは6面とも撮る）。
        // ふだんのレイ反射とSSRは、本描画の床のシェーダーで光線を飛ばすので、ここでは何も描かない。
        // 技術デモで「水面の反射なし」を選んでいるときは、床も反射を混ぜない（Ground::Draw）
        const bool hasWetFloor =
            m_Instance->m_CurrentScene == SceneName::Stage ||
            m_Instance->m_CurrentScene == SceneName::Stage2 ||
            m_Instance->m_CurrentScene == SceneName::Forest ||
            m_Instance->m_CurrentScene == SceneName::Title ||
            m_Instance->m_CurrentScene == SceneName::Intro;
        if (reflectionMethod != 1)
        {
            m_Instance->m_ReflectionProbe.Invalidate();
        }
        if (hasWetFloor && reflectionMethod == 1)
        {
            m_Instance->m_GpuTimer.BeginPass(GpuPass::Reflection, context);
            const int faceCount = m_Instance->m_ReflectionProbe.HasAllFaces() ? 1 : 6;
            for (int face = 0; face < faceCount; ++face)
            {
                m_Instance->m_ReflectionProbe.BeginFace(
                    m_Instance->m_Camera, m_Instance->m_Camera.GetPosition());
                for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
                {
                    if (o->IsDestroy() || !o->ContributesToReflection() ||
                        !IsNearEnoughForReflection(*o, m_Instance->m_Camera))
                    {
                        continue;
                    }
                    ++reflectionDrawn;
                    o->Draw(&m_Instance->m_Camera);
                }
                m_Instance->m_ReflectionProbe.EndFace(m_Instance->m_Camera);
            }
            m_Instance->m_GpuTimer.EndPass(GpuPass::Reflection, context);
        }
        else
        {
            m_Instance->m_GpuTimer.SkipPass(GpuPass::Reflection);
        }

        // PBRの環境マップ：金属や濡れた面に映す景色を、カメラの位置から1フレームに1面ずつ撮り直している（最初だけ6面）。
        // 懐中電灯の向きは、目の前を向くビュー空間で決めている。面ごとに視点が違うので、懐中電灯の向きを
        // その面の視点へ回して渡し、プレイヤーが照らしている壁が、そのまま金属に映るようにしている。
        const bool updateEnvironment = quality >= 2 || !m_Instance->m_EnvironmentProbe.HasAllFaces() ||
            (m_Instance->m_EnvironmentQualityFrame % 4u) == 0u;
        // PBRを止めているとき（技術デモ）は映り込みを使わないので、環境マップも撮らない
        const bool usesEnvironment = hasWetFloor && quality >= 1 && Tools::TechDemo::UsesPbr();
        if (usesEnvironment && updateEnvironment)
        {
            m_Instance->m_GpuTimer.BeginPass(GpuPass::EnvironmentProbe, context);
            const LIGHT sceneLight = Renderer::GetLight();
            DirectX::SimpleMath::Matrix mainView;
            DirectX::SimpleMath::Matrix mainProjection;
            m_Instance->m_Camera.GetMainMatrices(mainView, mainProjection);
            const DirectX::SimpleMath::Vector3 flashlightWorldDirection =
                DirectX::SimpleMath::Vector3::TransformNormal(
                    DirectX::SimpleMath::Vector3(
                        sceneLight.Direction.x, sceneLight.Direction.y, sceneLight.Direction.z),
                    mainView.Invert());
            // 別の視点から描くので、プレイヤー視点のタイルのリストではなく、全部の光源を順に計算させている
            m_Instance->m_TiledLighting.BindAllLights();
            const int faceCount = m_Instance->m_EnvironmentProbe.HasAllFaces() ? 1 : 6;
            for (int face = 0; face < faceCount; ++face)
            {
                m_Instance->m_EnvironmentProbe.BeginFace(
                    m_Instance->m_Camera, m_Instance->m_Camera.GetPosition());
                LIGHT faceLight = sceneLight;
                DirectX::SimpleMath::Vector3 faceDirection =
                    DirectX::SimpleMath::Vector3::TransformNormal(
                        flashlightWorldDirection, m_Instance->m_EnvironmentProbe.GetFaceView());
                faceDirection.Normalize();
                faceLight.Direction = DirectX::SimpleMath::Vector4(
                    faceDirection.x, faceDirection.y, faceDirection.z, 0.0f);
                Renderer::SetLight(faceLight);
                for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
                {
                    if (o->IsDestroy() || !o->ContributesToReflection() ||
                        !IsNearEnoughForReflection(*o, m_Instance->m_Camera))
                    {
                        continue;
                    }
                    o->Draw(&m_Instance->m_Camera);
                }
                m_Instance->m_EnvironmentProbe.EndFace(m_Instance->m_Camera);
            }
            Renderer::SetLight(sceneLight);
            m_Instance->m_GpuTimer.EndPass(GpuPass::EnvironmentProbe, context);
        }
        else if (usesEnvironment)
        {
            // 更新を休むだけでキャッシュを破棄すると、次回に6面を描き直して逆に重くなる。
            m_Instance->m_EnvironmentProbe.Bind();
            m_Instance->m_GpuTimer.SkipPass(GpuPass::EnvironmentProbe);
        }
        else
        {
            m_Instance->m_EnvironmentProbe.Invalidate();
            m_Instance->m_EnvironmentProbe.Unbind();
            m_Instance->m_GpuTimer.SkipPass(GpuPass::EnvironmentProbe);
        }
        // 水たまりのレイ反射は、画面の外の物の色を、この環境マップ（本物の景色）から読んでいる。
        // 撮っていないとき（PBRを止めた技術デモ・最低の画質）は、これまでどおり部屋の形と模様で映す
        // 撮った位置も渡し、シェーダーはその位置から撮った景色として読む（今の目の位置として読むと、歩くたびに映り込みがずれて跳ねるため）
        Renderer::SetReflectionEnvironment(usesEnvironment && m_Instance->m_EnvironmentProbe.HasAllFaces(),
            m_Instance->m_EnvironmentProbe.GetCaptureCenter());

        // 監視映像など、Sceneが持つ補助カメラの映像は、本描画の前に描いておいている。
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->RenderOffscreen();
        }

        // 鏡と水たまりのシェーダーが光線を通す、人の形の距離場（t6・t13・s2）
        m_Instance->m_FigureVolumes.Bind();
        // 本描画：レンダーターゲットを消してから、カメラに映る物だけを描いている
        m_Instance->m_GpuTimer.BeginPass(GpuPass::MainScene, context);
        Renderer::DrawStart();
        // 深度プリパス：不透明な壁・床・扉の深度だけを、本描画の深度バッファへ先に描いている。
        // ・本描画では、奥に隠れた画素が深度の判定で先に捨てられ、重いピクセルシェーダーを動かさずに済む。
        // ・タイルベースライティングでは、タイルごとの一番奥の深度より奥の光源を外せる。
        // 本描画と同じカリングの判定を使い、本描画で描かない物の深度は書かないようにしている。
        // 技術デモでは、深度プリパスとタイルベースライティングを、使わない方式と見比べられるようにしている。
        Renderer::SetDepthEnable(true);
        const bool useDepthPrepass = Tools::TechDemo::UsesDepthPrepass();
        if (useDepthPrepass)
        {
            for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
            {
                if (o->IsDestroy() || !o->WritesDepthPrepass() ||
                    !IsVisibleToCamera(*o, m_Instance->m_Camera, true))
                {
                    continue;
                }
                o->DrawDepthPrepass(&m_Instance->m_Camera);
            }
        }
        // 画面空間の環境遮蔽（SSAO）：深度プリパスの深度から、物と物の隙間や壁の根元など光が届きにくい所を求めている（1面・2面）。
        // 結果は t15 に設定し、下のライトリストの準備で、本描画のときだけ使う印をシェーダーへ送っている。
        if (useDepthPrepass && hasWetFloor && quality >= 1 && Tools::TechDemo::UsesAmbientOcclusion())
        {
            m_Instance->m_GpuTimer.BeginPass(GpuPass::AmbientOcclusion, context);
            DirectX::SimpleMath::Matrix occlusionView;
            DirectX::SimpleMath::Matrix occlusionProjection;
            m_Instance->m_Camera.GetMainMatrices(occlusionView, occlusionProjection);
            UINT viewportCount = 1;
            D3D11_VIEWPORT viewport{};
            context->RSGetViewports(&viewportCount, &viewport);
            m_Instance->m_AmbientOcclusion.Compute(occlusionProjection, viewport);
            m_Instance->m_TiledLighting.SetAmbientOcclusionActive(true);
            m_Instance->m_GpuTimer.EndPass(GpuPass::AmbientOcclusion, context);
        }
        else
        {
            m_Instance->m_AmbientOcclusion.Unbind();
            m_Instance->m_GpuTimer.SkipPass(GpuPass::AmbientOcclusion);
        }
        bool halfShafts = false;
        if (useDepthPrepass && hasWetFloor && quality >= 1 && Tools::TechDemo::UsesLightShafts())
        {
            DirectX::SimpleMath::Matrix view, projection;
            m_Instance->m_Camera.GetMainMatrices(view, projection);
            UINT count = 1; D3D11_VIEWPORT viewport{};
            context->RSGetViewports(&count, &viewport);
            halfShafts = m_Instance->m_LightShafts.Compute(view, projection, viewport,
                m_Instance->m_TiledLighting.GetLights(), Application::GetDeltaTime());
        }
        m_Instance->m_TiledLighting.SetLightShaftsActive(halfShafts);
        // プレイヤー視点のタイルごとのライトリストを、Compute Shaderで作ってから描いている。
        // 使わない方式では、全部の光源を画素ごとに順に計算させている。
        if (Tools::TechDemo::UsesTiledLighting())
        {
            m_Instance->m_TiledLighting.BuildTiles(m_Instance->m_Camera, useDepthPrepass);
        }
        else
        {
            m_Instance->m_TiledLighting.BindAllLights();
        }
        // SSRとレイ反射のときは、深度プリパスの深度を写し取り、前のフレームの画面と一緒に床のシェーダーへ渡している。
        // （反射の光線をたどる計算は、本描画の床のピクセルシェーダーで行う）
        const bool useScreenSpaceReflection = hasWetFloor && usesScreenSpaceRays;
        if (useScreenSpaceReflection)
        {
            m_Instance->m_ScreenSpaceReflection.Prepare(
                m_Instance->m_PostProcess.GetCapturedSceneSRV());
        }

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
        if (useScreenSpaceReflection)
        {
            m_Instance->m_ScreenSpaceReflection.Unbind();
        }

        // デバッグ画面に表示する、描いた数と省いた数を渡している
        Debug::UI::SetCullingStats(
            mainDrawn,
            mainCulled,
            shadowDrawn,
            shadowCulled,
            reflectionDrawn);

        // 描き終えた画面を取り込み、露出・ブルーム・光の筋・ノイズなどの画面効果を重ねている
        m_Instance->m_GpuTimer.BeginPass(GpuPass::PostProcess, context);
        const bool captured = m_Instance->m_PostProcess.CaptureBackBuffer();
        m_Instance->m_ScreenSpaceReflection.FinishFrame(
            reflectionView, reflectionProjection, useScreenSpaceReflection && captured);
        m_Instance->m_PostProcess.Draw(&m_Instance->m_GpuTimer);
        m_Instance->m_GpuTimer.EndPass(GpuPass::PostProcess, context);

        // ブルームの後にHUDと画面の文字を描き、文字の輪郭がぼけないようにしている。
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Draw(&m_Instance->m_Camera);
        }
        // 技術デモの操作パネル（ポーズ中は、ポーズメニューと重ならないように出さない）
        if (!m_Instance->m_PauseMenu.IsOpen())
        {
            Tools::TechDemo::Draw(m_Instance->m_GpuTimer);
        }
        Debug::UI::Draw(m_Instance->m_PostProcess);

        // GPU時間の計測を締めくくり、画面に表示している（Present）
        m_Instance->m_GpuTimer.EndFrame(context);
        Renderer::DrawEnd();
    }
}
