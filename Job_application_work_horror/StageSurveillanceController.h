// ============================================================================
// ファイルの役割: 1面の監視カメラの巡回（映像の確認・報告・現地での対処・捕まる）を進めている。
// 主な技術: 有限状態機械、RenderTextureによる別の視点の描画、入力と演出のタイミング合わせ
// 巡回の正しい・間違いや制限時間はSurveillancePatrol（描画・入力に依存しない状態クラス）が判定し、
// このクラスはそれに入力を渡し、映像・異常の見た目・知らせ・捕まる演出を担当している。
// StageSceneはこのクラスを呼ぶだけで、巡回の中身を知らない。
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
    // 映像の描画先を用意している。objectsは配置で作ったObjectで、Sceneが終わるまで有効なものを渡している。
    void Init(const StageObjects& objects);
    // 描画先を解放している
    void Uninit();

    // 巡回を1フレーム進めている。巡回をすべて終えたフレームだけtrueを返している。
    bool Update(Player& player, float deltaTime);

    // 映像を見ている間だけ、選んだ監視カメラの視点をRenderTextureへ描いている（本描画の前に呼んでいる）。
    void RenderFeeds();

    // 映像を見ている間は、普段のHUDの代わりに監視映像の画面を出している。
    bool IsViewing() const
    {
        return m_Patrol.GetState() == SurveillancePatrol::State::Viewing;
    }
    // 監視映像の画面をHUDで描いている
    void DrawFeed(Hud& hud);

    // 捕まった直後の暗転の濃さを返している。
    bool IsCaughtActive() const { return m_Caught.IsActive(); }
    float GetCaughtFadeRate() const { return m_Caught.GetFadeRate(); }

    // 1面の目的表示に、現地確認の残り時間や知らせを書き込んでいる。
    void FillObjectiveInput(Stage1ObjectiveInput& input, const Player& player) const;

private:
    // 状態ごとの処理（端末を調べたら映像を開く・映像の操作・現地確認・捕まった後）
    void UpdateState(Player& player, float deltaTime);
    void UpdateViewing(Player& player);
    void UpdateDispatch(Player& player, float deltaTime);
    // 異常をランダムに選んでいる／異常の見た目を出す・消す／ライトで異常を照らしているか
    SurveillancePatrol::Anomaly ChooseAnomaly();
    void SetAnomalyVisible(const SurveillancePatrol::Anomaly& anomaly, bool visible);
    bool IsIlluminatingAnomaly(const Player& player) const;
    // 映像を閉じている／捕まる演出を始める・進める／巡回をすべて終えた／知らせを出している
    void EndViewing(Player& player);
    void StartCaught(Player& player);
    void UpdateCaught(Player& player, float deltaTime);
    void Complete(Player& player);
    void ShowNotice(const char* text, float seconds);

    // 配置で作ったObject（所有しない）
    const StageObjects* m_Objects = nullptr;

    // 巡回の状態、捕まった演出、乱数
    SurveillancePatrol m_Patrol;
    CaughtSequence m_Caught;
    std::mt19937 m_Random{ std::random_device{}() };

    // 選んでいるカメラの映像と、端末を開いた瞬間に記録した各カメラの基準映像。
    Graphics::RenderTexture m_Feed;
    std::array<Graphics::RenderTexture, StageSurveillanceCameraCount> m_References;
    // 映像をHUDに貼るためのシェーダー
    Shader m_Shader;

    // 映像を開いてからの経過秒。開いた直後の押し間違いを防ぐために使っている。
    float m_ViewTimer = 0.0f;
    // 拡大しているか、基準映像を見ているか
    bool m_Zoomed = false;
    bool m_ShowReference = false;
    // 基準映像を1フレームに1台ずつ記録している途中か、と、次に記録するカメラの番号。
    bool m_ReferenceCapturePending = false;
    int m_ReferenceCaptureIndex = 0;
    // 判定の間違いの表示の残り秒数、警告の間隔、知らせの残り秒数と文章、このフレームで巡回を終えたか
    float m_WrongTimer = 0.0f;
    float m_WarningCooldown = 0.0f;
    float m_NoticeTimer = 0.0f;
    const char* m_NoticeText = "";
    bool m_CompletedThisFrame = false;
};
