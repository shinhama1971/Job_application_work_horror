// ============================================================================
// ファイルの役割: 目的、操作ヒント、電池残量などのゲーム内UIを描画します。
// 主な技術: 2Dスプライト、ベクターフォント、アンカー配置、状態に応じたUI
// ============================================================================

#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "Shader.h"
#include "VertexBuffer.h"

class Player;
class Camera;

class Hud
{
public:
    void Init();
    void Draw(
        const Player& player,
        int fuseCount,
        std::string_view interactionPrompt,
        std::string_view objectiveText);
    void DrawTitle(
        float time,
        bool hasClearRecord,
        float bestClearTimeSeconds,
        int bestCaughtCount);
    void DrawResult(
        float revealAmount,
        float clearTimeSeconds,
        int caughtCount,
        int anomaliesHandled,
        int puzzleMistakes,
        int chargersUsed,
        int evidenceCollected,
        bool newBestTime,
        bool newBestCaught);
    void DrawChapterCard(
        std::string_view chapter,
        std::string_view subtitle,
        float elapsedSeconds);
    void DrawObjectiveGuide(
        const Camera& camera,
        const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& target);
    void DrawStage2Status(
        int completedLoops,
        float threatRate,
        bool exitReady,
        int signalStep,
        bool signalActive);
    void DrawSurveillanceFeed(
        ID3D11ShaderResourceView* feed,
        Shader& textureShader,
        float elapsedSeconds,
        std::string_view cameraLabel,
        int cameraIndex,
        int cameraCount,
        int roundsCleared,
        int requiredRounds,
        int mistakes,
        int mistakesUntilCaught,
        bool zoomed,
        bool showingReference,
        bool reportReady,
        bool wrongReportVisible);
    // 暗証番号の入力画面。wrongRateは不正解の直後に1から0へ下がり、枠を赤く点滅させます。
    void DrawKeypad(
        const std::array<int, 4>& entered,
        int cursor,
        float wrongRate,
        int mistakes);
    // ロッカーに隠れている間の視界。扉の横長の隙間以外を暗くし、出る操作を表示します。
    // dangerRateは影の近さ（0〜1）で、近いほど隙間の縁が赤くにじみます。
    void DrawHidingView(float elapsedSeconds, float dangerRate);
    void DrawQuietRecovery(float progressRate, float cooldown, bool success, bool tooClose);
    void DrawPause(
        int brightnessLevel,
        int effectLevel,
        int lookSensitivityLevel,
        int volumeLevel,
        bool guideEnabled,
        int resolutionLevel,
        int selectedSetting,
        int floorNumber,
        float runTimeSeconds,
        int caughtCount);
    void DrawBlink(float opacity);
    void Uninit();

private:
    static constexpr size_t MaxVertices = 65536;

    // HUDの座標は「縦864の画面」を基準に画素単位で決めてあります。実際の描画解像度との比で
    // キャンバスの大きさを決め、画面の大きさが変わってもHUDが画面に対して同じ大きさに見えるようにします。
    static constexpr float ReferenceHeight = 864.0f;
    static float GetCanvasScale();
    static float GetCanvasWidth();
    static float GetCanvasHeight();

    void AddRectangle(float x, float y, float width, float height, const DirectX::SimpleMath::Color& color);
    void AddLetterE(float x, float y, float size, const DirectX::SimpleMath::Color& color);
    void AddLetterA(float x, float y, float size, const DirectX::SimpleMath::Color& color);
    void AddText(
        float x,
        float y,
        std::string_view text,
        float pixelSize,
        const DirectX::SimpleMath::Color& color);
    void Flush();
    void DrawTextureRectangle(
        ID3D11ShaderResourceView* texture,
        Shader& textureShader,
        float x,
        float y,
        float width,
        float height);

    Shader m_Shader;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    std::vector<VERTEX_3D> m_Vertices;
};
