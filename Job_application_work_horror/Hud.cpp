#include "Hud.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "Application.h"
#include "Input.h"
#include "Player.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

void Hud::Init()
{
    m_Shader.Create("shader/hudVS.hlsl", "shader/hudPS.hlsl");

    std::vector<VERTEX_3D> initialVertices(MaxVertices);
    m_VertexBuffer.Create(initialVertices);
    m_Vertices.reserve(MaxVertices);
}

void Hud::Draw(
    const Player& player,
    int fuseCount,
    std::string_view interactionPrompt,
    std::string_view objectiveText)
{
    m_Vertices.clear();

    const Color white(0.88f, 0.92f, 0.90f, 0.85f);
    const Color dark(0.015f, 0.02f, 0.02f, 0.72f);
    const Color inactive(0.16f, 0.19f, 0.18f, 0.85f);
    const Color active(0.72f, 0.86f, 0.66f, 0.95f);

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());

    if (!objectiveText.empty())
    {
        const float pixelSize = 3.0f;
        const float objectiveWidth =
            static_cast<float>(objectiveText.size()) * 18.0f + 24.0f;
        const Color objectiveColor = objectiveText == "ESCAPE"
            ? Color(0.52f, 0.92f, 0.58f, 0.95f)
            : Color(0.88f, 0.82f, 0.62f, 0.95f);

        AddRectangle(34.0f, 28.0f, objectiveWidth, 39.0f, dark);
        AddRectangle(34.0f, 28.0f, 4.0f, 39.0f, objectiveColor);
        AddText(48.0f, 37.0f, objectiveText, pixelSize, objectiveColor);
    }

    // Minimal center reticle.
    AddRectangle(screenWidth * 0.5f - 9.0f, screenHeight * 0.5f - 1.0f, 7.0f, 2.0f, white);
    AddRectangle(screenWidth * 0.5f + 2.0f, screenHeight * 0.5f - 1.0f, 7.0f, 2.0f, white);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f - 9.0f, 2.0f, 7.0f, white);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f + 2.0f, 2.0f, 7.0f, white);

    // Three fuse indicators.
    AddRectangle(34.0f, screenHeight - 112.0f, 112.0f, 36.0f, dark);
    const int clampedFuseCount = std::clamp(fuseCount, 0, 3);
    for (int i = 0; i < 3; ++i)
    {
        AddRectangle(
            46.0f + static_cast<float>(i) * 32.0f,
            screenHeight - 101.0f,
            20.0f,
            14.0f,
            i < clampedFuseCount ? active : inactive
        );
    }

    // Flashlight battery frame and fill.
    const float batteryRate = std::clamp(player.GetBattery() / 100.0f, 0.0f, 1.0f);
    Color batteryColor(0.42f, 0.82f, 0.48f, 0.95f);
    if (batteryRate <= 0.2f)
    {
        batteryColor = Color(0.92f, 0.18f, 0.12f, 0.95f);
    }
    else if (batteryRate <= 0.5f)
    {
        batteryColor = Color(0.92f, 0.68f, 0.16f, 0.95f);
    }

    AddRectangle(34.0f, screenHeight - 62.0f, 190.0f, 28.0f, dark);
    AddRectangle(42.0f, screenHeight - 54.0f, 174.0f, 12.0f, inactive);
    if (batteryRate > 0.0f)
    {
        AddRectangle(42.0f, screenHeight - 54.0f, 174.0f * batteryRate, 12.0f, batteryColor);
    }

    // A compact, code-rendered E prompt appears only for the selected target.
    if (!interactionPrompt.empty())
    {
        const bool locked = interactionPrompt.find("Requires") != std::string_view::npos;
        const Color promptColor = locked
            ? Color(0.85f, 0.22f, 0.18f, 0.95f)
            : white;

        AddRectangle(screenWidth * 0.5f - 48.0f, screenHeight - 106.0f, 96.0f, 58.0f, dark);
        AddRectangle(screenWidth * 0.5f - 22.0f, screenHeight - 96.0f, 44.0f, 38.0f, inactive);
        if (Input::IsControllerConnected())
        {
            AddLetterA(screenWidth * 0.5f - 10.0f, screenHeight - 89.0f, 24.0f, promptColor);
        }
        else
        {
            AddLetterE(screenWidth * 0.5f - 10.0f, screenHeight - 89.0f, 24.0f, promptColor);
        }
    }

    if (m_Vertices.empty())
    {
        return;
    }

    m_VertexBuffer.Modify(m_Vertices);

    Renderer::SetDepthEnable(false);
    Renderer::SetBlendState(BS_ALPHABLEND);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(static_cast<UINT>(m_Vertices.size()), 0);

    Renderer::SetDepthEnable(true);
}

void Hud::Uninit()
{
    m_Vertices.clear();
}

void Hud::AddRectangle(float x, float y, float width, float height, const Color& color)
{
    if (m_Vertices.size() + 6 > MaxVertices)
    {
        return;
    }

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());

    const float left = x / screenWidth * 2.0f - 1.0f;
    const float right = (x + width) / screenWidth * 2.0f - 1.0f;
    const float top = 1.0f - y / screenHeight * 2.0f;
    const float bottom = 1.0f - (y + height) / screenHeight * 2.0f;

    const auto makeVertex = [&color](float px, float py)
    {
        VERTEX_3D vertex{};
        vertex.position = Vector3(px, py, 0.0f);
        vertex.color = color;
        return vertex;
    };

    m_Vertices.push_back(makeVertex(left, top));
    m_Vertices.push_back(makeVertex(right, top));
    m_Vertices.push_back(makeVertex(left, bottom));
    m_Vertices.push_back(makeVertex(left, bottom));
    m_Vertices.push_back(makeVertex(right, top));
    m_Vertices.push_back(makeVertex(right, bottom));
}

void Hud::AddLetterE(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(2.0f, size * 0.18f);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size * 0.82f, stroke, color);
    AddRectangle(x, y + size - stroke, size, stroke, color);
}

void Hud::AddLetterA(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(2.0f, size * 0.18f);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x + size - stroke, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size, stroke, color);
}

void Hud::AddText(
    float x,
    float y,
    std::string_view text,
    float pixelSize,
    const Color& color)
{
    const auto getRows = [](char character)
    {
        using Glyph = std::array<std::uint8_t, 7>;

        switch (character)
        {
        case 'A': return Glyph{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'C': return Glyph{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D': return Glyph{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
        case 'E': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'I': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'N': return Glyph{ 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 };
        case 'O': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'R': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return Glyph{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'W': return Glyph{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
        case '3': return Glyph{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
        default: return Glyph{};
        }
    };

    float cursorX = x;
    for (const char character : text)
    {
        const std::array<std::uint8_t, 7> rows = getRows(character);
        for (size_t row = 0; row < rows.size(); ++row)
        {
            for (int column = 0; column < 5; ++column)
            {
                if ((rows[row] & (1u << (4 - column))) != 0)
                {
                    AddRectangle(
                        cursorX + static_cast<float>(column) * pixelSize,
                        y + static_cast<float>(row) * pixelSize,
                        pixelSize,
                        pixelSize,
                        color);
                }
            }
        }

        cursorX += pixelSize * 6.0f;
    }
}
