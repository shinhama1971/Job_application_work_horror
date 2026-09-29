// ============================================================================
// ファイルの役割: 1面の監視カメラ巡回（映像の確認・報告・現地での対処・捕獲）を進めます。
// 主な技術: 有限状態機械、RenderTextureによる別視点描画、入力と演出の同期
// 巡回の正誤や制限時間はSurveillancePatrol（描画・入力に依存しない状態クラス）が判定し、
// このクラスはそれに入力を渡し、映像・異常の見た目・通知・捕獲演出を担当します。
// StageSceneはこのクラスを呼ぶだけで、巡回の中身を知りません。
// ============================================================================

#pragma once

#include "CaughtSequence.h"
#include "RenderTexture.h"
#include "Shader.h"
#include "Stage1Layout.h"
#include "Stage1Objective.h"
#include "StageSurveillanceCameras.h"
#include "SurveillancePatrol.h"

#include <array>
#include <random>

class Hud;
class Player;

class StageSurveillanceController final
{
public:
    // 映像の描画先を用意します。objectsは配置で作ったObjectで、Sceneの終了まで有効なものを渡します。
    void Init(const StageObjects& objects);
    void Uninit();

    // 巡回を1フレーム進めます。巡回をすべて終えたフレームだけtrueを返します。
    bool Update(Player& player, float deltaTime);

    // 映像を見ている間だけ、選んだ監視カメラの視点をRenderTextureへ描きます（本描画の前に呼びます）。
    void RenderFeeds();

    // 映像を見ている間は、通常のHUDの代わりに監視映像の画面を出します。
    bool IsViewing() const
    {
        return m_Patrol.GetState() == SurveillancePatrol::State::Viewing;
    }
    void DrawFeed(Hud& hud);

    // 捕獲された直後の暗転の濃さです。
    bool IsCaughtActive() const { return m_Caught.IsActive(); }
    float GetCaughtFadeRate() const { return m_Caught.GetFadeRate(); }

    // 1面の目的表示に、現地確認の残り時間や通知を書き込みます。
    void FillObjectiveInput(Stage1ObjectiveInput& input, const Player& player) const;

private:
    void UpdateState(Player& player, float deltaTime);
    void UpdateViewing(Player& player);
    void UpdateDispatch(Player& player, float deltaTime);
    SurveillancePatrol::Anomaly ChooseAnomaly();
    void SetAnomalyVisible(const SurveillancePatrol::Anomaly& anomaly, bool visible);
    bool IsIlluminatingAnomaly(const Player& player) const;
    void EndViewing(Player& player);
    void StartCaught(Player& player);
    void UpdateCaught(Player& player, float deltaTime);
    void Complete(Player& player);
    void ShowNotice(const char* text, float seconds);

    const StageObjects* m_Objects = nullptr;

    SurveillancePatrol m_Patrol;
    CaughtSequence m_Caught;
    std::mt19937 m_Random{ std::random_device{}() };

    // 選んでいるカメラの映像と、端末を開いた瞬間に記録した各カメラの基準映像です。
    Graphics::RenderTexture m_Feed;
    std::array<Graphics::RenderTexture, StageSurveillanceCameraCount> m_References;
    Shader m_Shader;

    // 映像を開いてからの経過秒。開いた直後の誤入力を防ぐために使います。
    float m_ViewTimer = 0.0f;
    bool m_Zoomed = false;
    bool m_ShowReference = false;
    // 基準映像を1フレームに1台ずつ記録している途中か、と次に記録するカメラ番号です。
    bool m_ReferenceCapturePending = false;
    int m_ReferenceCaptureIndex = 0;
    float m_WrongTimer = 0.0f;
    float m_WarningCooldown = 0.0f;
    float m_NoticeTimer = 0.0f;
    const char* m_NoticeText = "";
    bool m_CompletedThisFrame = false;
};
