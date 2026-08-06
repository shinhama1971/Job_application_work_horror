#pragma once

#include <string_view>
#include <vector>

#include "Shader.h"
#include "VertexBuffer.h"

class Player;

class Hud
{
public:
    void Init();
    void Draw(
        const Player& player,
        int fuseCount,
        std::string_view interactionPrompt,
        std::string_view objectiveText);
    void Uninit();

private:
    static constexpr size_t MaxVertices = 4096;

    void AddRectangle(float x, float y, float width, float height, const DirectX::SimpleMath::Color& color);
    void AddLetterE(float x, float y, float size, const DirectX::SimpleMath::Color& color);
    void AddLetterA(float x, float y, float size, const DirectX::SimpleMath::Color& color);
    void AddText(
        float x,
        float y,
        std::string_view text,
        float pixelSize,
        const DirectX::SimpleMath::Color& color);

    Shader m_Shader;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    std::vector<VERTEX_3D> m_Vertices;
};
