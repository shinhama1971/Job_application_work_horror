#pragma once

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
        bool exitReady);
    void DrawPause(
        int brightnessLevel,
        int effectLevel,
        int lookSensitivityLevel,
        int selectedSetting,
        int floorNumber,
        float runTimeSeconds,
        int caughtCount);
    void DrawBlink(float opacity);
    void Uninit();

private:
    static constexpr size_t MaxVertices = 32768;

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

    Shader m_Shader;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    std::vector<VERTEX_3D> m_Vertices;
};
