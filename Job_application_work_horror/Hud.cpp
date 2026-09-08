// ============================================================================
// ファイルの役割: 目的、操作ヒント、電池残量などのゲーム内UIを描画します。
// ============================================================================

#include "Hud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <windows.h>

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

namespace
{
    size_t CountDisplayedCharacters(std::string_view text)
    {
        size_t count = 0;
        for (unsigned char byte : text)
        {
            if ((byte & 0xC0u) != 0x80u)
            {
                ++count;
            }
        }
        return count;
    }
}

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
    const bool hasInteractionTarget = !interactionPrompt.empty();
    const bool interactionLocked = hasInteractionTarget && (
        interactionPrompt.find("Requires") != std::string_view::npos ||
        interactionPrompt.find("no power") != std::string_view::npos ||
        interactionPrompt.find("Locked") != std::string_view::npos ||
        interactionPrompt.find("locked") != std::string_view::npos ||
        interactionPrompt.find("必要") != std::string_view::npos ||
        interactionPrompt.find("開かない") != std::string_view::npos);
    const Color reticleColor = interactionLocked
        ? Color(0.92f, 0.28f, 0.20f, 0.96f)
        : (hasInteractionTarget
            ? Color(0.62f, 0.92f, 0.70f, 0.98f)
            : white);

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());

    if (!objectiveText.empty())
    {
        const float pixelSize = 3.0f;
        const float objectiveWidth =
            static_cast<float>(CountDisplayedCharacters(objectiveText)) *
            18.0f + 24.0f;
        const bool isSafeObjective =
            objectiveText == "ESCAPE" ||
            objectiveText == "ESCAPED" ||
            objectiveText == "POWER RESTORED" ||
            objectiveText == "OPEN THE DOOR" ||
            objectiveText == "LEAVE";
        const Color objectiveColor = isSafeObjective
            ? Color(0.52f, 0.92f, 0.58f, 0.95f)
            : Color(0.88f, 0.82f, 0.62f, 0.95f);

        AddRectangle(34.0f, 28.0f, objectiveWidth, 39.0f, dark);
        AddRectangle(34.0f, 28.0f, 4.0f, 39.0f, objectiveColor);
        AddText(48.0f, 37.0f, objectiveText, pixelSize, objectiveColor);
    }

    // Keep optional exploration visible without competing with the current
    // mandatory objective.  This also makes the result-screen bonus legible
    // before the player reaches the end of the game.
    Core::Game* game = Core::Game::GetInstance();
    const int evidenceCount = game != nullptr
        ? (std::clamp)(game->GetEvidenceCollected(), 0, 3)
        : 0;
    const std::string evidenceText =
        "記録 " + std::to_string(evidenceCount) + " / 3";
    constexpr float evidencePixelSize = 2.0f;
    const float evidenceWidth =
        static_cast<float>(CountDisplayedCharacters(evidenceText)) *
        evidencePixelSize * 6.0f;
    const float evidenceX = screenWidth - evidenceWidth - 52.0f;
    const Color evidenceColor = evidenceCount >= 3
        ? Color(0.92f, 0.78f, 0.34f, 0.96f)
        : Color(0.62f, 0.78f, 0.70f, 0.88f);
    AddRectangle(evidenceX - 14.0f, 74.0f,
        evidenceWidth + 28.0f, 34.0f, dark);
    AddText(evidenceX, 82.0f,
        evidenceText, evidencePixelSize, evidenceColor);

    // Minimal center reticle.
    const float reticleExtent = hasInteractionTarget ? 11.0f : 9.0f;
    AddRectangle(screenWidth * 0.5f - reticleExtent, screenHeight * 0.5f - 1.0f, reticleExtent - 2.0f, 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f + 2.0f, screenHeight * 0.5f - 1.0f, reticleExtent - 2.0f, 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f - reticleExtent, 2.0f, reticleExtent - 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f + 2.0f, 2.0f, reticleExtent - 2.0f, reticleColor);

    // A negative count lets non-fuse stages keep the shared battery HUD.
    if (fuseCount >= 0)
    {
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
    }

    // Flashlight battery frame and fill.
    const float batteryRate = std::clamp(
        player.GetBattery() / 100.0f, 0.0f, 1.0f);
    Color batteryColor(0.42f, 0.82f, 0.48f, 0.95f);
    if (batteryRate <= 0.2f)
    {
        batteryColor = Color(0.92f, 0.18f, 0.12f, 0.95f);
    }
    else if (batteryRate <= 0.5f)
    {
        batteryColor = Color(0.92f, 0.68f, 0.16f, 0.95f);
    }

    constexpr float barFillX = 104.0f;
    constexpr float barFillWidth = 152.0f;
    AddRectangle(34.0f, screenHeight - 66.0f, 230.0f, 30.0f, dark);
    AddText(43.0f, screenHeight - 58.0f,
        "ライト", 1.5f, white);
    AddRectangle(barFillX, screenHeight - 57.0f,
        barFillWidth, 12.0f, inactive);
    if (batteryRate > 0.0f)
    {
        AddRectangle(barFillX, screenHeight - 57.0f,
            barFillWidth * batteryRate, 12.0f, batteryColor);
    }

    const float staminaRate = std::clamp(
        player.GetStamina() / 100.0f, 0.0f, 1.0f);
    Color staminaColor(0.34f, 0.70f, 0.86f, 0.95f);
    if (staminaRate <= 0.20f)
    {
        staminaColor = Color(0.90f, 0.25f, 0.16f, 0.95f);
    }
    else if (staminaRate <= 0.40f)
    {
        staminaColor = Color(0.90f, 0.62f, 0.16f, 0.95f);
    }

    AddRectangle(34.0f, screenHeight - 32.0f, 230.0f, 24.0f, dark);
    AddText(43.0f, screenHeight - 26.0f,
        "スタミナ", 1.5f, white);
    AddRectangle(barFillX, screenHeight - 25.0f,
        barFillWidth, 10.0f, inactive);
    if (staminaRate > 0.0f)
    {
        AddRectangle(barFillX, screenHeight - 25.0f,
            barFillWidth * staminaRate, 10.0f, staminaColor);
    }

    // Show both the input and the selected action.  The old button-only
    // prompt did not tell the player whether E/A would collect, open or leave.
    if (!interactionPrompt.empty())
    {
        const Color promptColor = interactionLocked
            ? Color(0.85f, 0.22f, 0.18f, 0.95f)
            : white;
        constexpr float promptPixelSize = 2.0f;
        const float promptTextWidth =
            static_cast<float>(CountDisplayedCharacters(interactionPrompt)) *
            promptPixelSize * 6.0f;
        const float panelWidth = 88.0f + promptTextWidth;
        const float panelHeight = 58.0f;
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = screenHeight - 106.0f;

        AddRectangle(panelX, panelY, panelWidth, panelHeight, dark);
        AddRectangle(panelX + 10.0f, panelY + 10.0f,
            44.0f, 38.0f, inactive);
        if (Input::IsControllerConnected())
        {
            AddLetterA(panelX + 20.0f, panelY + 17.0f,
                24.0f, promptColor);
        }
        else
        {
            AddLetterE(panelX + 20.0f, panelY + 17.0f,
                24.0f, promptColor);
        }
        AddText(panelX + 68.0f, panelY + 22.0f,
            interactionPrompt, promptPixelSize, promptColor);
    }

    std::string_view batteryNotice;
    Color batteryNoticeColor(0.52f, 0.92f, 0.58f, 0.96f);
    if (player.GetBatteryNoticeTimer() > 0.0f)
    {
        batteryNotice = "電池を回復した";
    }
    else if (player.GetBattery() <= 20.0f)
    {
        batteryNotice = "電池残量が少ない";
        batteryNoticeColor = Color(0.94f, 0.30f, 0.18f, 0.96f);
    }
    if (!batteryNotice.empty())
    {
        constexpr float noticePixelSize = 2.0f;
        const float noticeWidth =
            static_cast<float>(CountDisplayedCharacters(batteryNotice)) *
            noticePixelSize * 6.0f;
        const float noticeX = screenWidth - noticeWidth - 52.0f;
        AddRectangle(noticeX - 14.0f, 28.0f,
            noticeWidth + 28.0f, 34.0f, dark);
        AddText(noticeX, 36.0f,
            batteryNotice, noticePixelSize, batteryNoticeColor);
    }

    Flush();
}


void Hud::DrawBlink(float opacity)
{
    const float blinkOpacity = (std::clamp)(opacity, 0.0f, 0.90f);
    if (blinkOpacity <= 0.001f)
    {
        return;
    }

    m_Vertices.clear();
    AddRectangle(
        0.0f,
        0.0f,
        static_cast<float>(Application::GetWidth()),
        static_cast<float>(Application::GetHeight()),
        Color(0.0f, 0.0f, 0.0f, blinkOpacity));
    Flush();
}

void Hud::Uninit()
{
    m_Vertices.clear();
}

void Hud::Flush()
{
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
    struct RasterGlyph
    {
        unsigned int width = 0;
        unsigned int height = 0;
        int advance = 0;
        std::vector<unsigned char> bitmap;
    };

    class JapaneseGlyphCache
    {
    public:
        JapaneseGlyphCache()
        {
            m_DC = CreateCompatibleDC(nullptr);
            m_Font = CreateFontW(
                -18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                L"Yu Gothic UI");
            if (m_DC != nullptr && m_Font != nullptr)
            {
                m_OldFont = SelectObject(m_DC, m_Font);
            }
        }

        ~JapaneseGlyphCache()
        {
            if (m_DC != nullptr && m_OldFont != nullptr)
            {
                SelectObject(m_DC, m_OldFont);
            }
            if (m_Font != nullptr)
            {
                DeleteObject(m_Font);
            }
            if (m_DC != nullptr)
            {
                DeleteDC(m_DC);
            }
        }

        const RasterGlyph& Get(wchar_t character)
        {
            const auto found = m_Glyphs.find(character);
            if (found != m_Glyphs.end())
            {
                return found->second;
            }

            RasterGlyph glyph;
            if (m_DC == nullptr || m_Font == nullptr)
            {
                return m_Glyphs.emplace(character, std::move(glyph)).first->second;
            }

            MAT2 transform{};
            transform.eM11.value = 1;
            transform.eM22.value = 1;
            GLYPHMETRICS metrics{};
            const DWORD size = GetGlyphOutlineW(
                m_DC, character, GGO_BITMAP, &metrics,
                0, nullptr, &transform);
            glyph.advance = metrics.gmCellIncX > 0
                ? metrics.gmCellIncX
                : 18;
            if (size != GDI_ERROR && size > 0)
            {
                glyph.width = metrics.gmBlackBoxX;
                glyph.height = metrics.gmBlackBoxY;
                const unsigned int pitch = ((glyph.width + 31u) / 32u) * 4u;
                std::vector<unsigned char> packed(size);
                if (GetGlyphOutlineW(
                    m_DC, character, GGO_BITMAP, &metrics,
                    size, packed.data(), &transform) != GDI_ERROR)
                {
                    glyph.bitmap.resize(glyph.width * glyph.height, 0);
                    for (unsigned int row = 0; row < glyph.height; ++row)
                    {
                        for (unsigned int column = 0; column < glyph.width; ++column)
                        {
                            const unsigned char byte = packed[row * pitch + column / 8u];
                            glyph.bitmap[row * glyph.width + column] =
                                (byte & (0x80u >> (column % 8u))) != 0 ? 1 : 0;
                        }
                    }
                }
            }
            return m_Glyphs.emplace(character, std::move(glyph)).first->second;
        }

    private:
        HDC m_DC = nullptr;
        HFONT m_Font = nullptr;
        HGDIOBJ m_OldFont = nullptr;
        std::unordered_map<wchar_t, RasterGlyph> m_Glyphs;
    };

    const auto getRows = [](char character)
    {
        using Glyph = std::array<std::uint8_t, 7>;

        switch (character)
        {
        case 'A': return Glyph{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'B': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
        case 'C': return Glyph{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D': return Glyph{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
        case 'E': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'G': return Glyph{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
        case 'H': return Glyph{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'I': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'J': return Glyph{ 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
        case 'K': return Glyph{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
        case 'L': return Glyph{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
        case 'M': return Glyph{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
        case 'N': return Glyph{ 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 };
        case 'O': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'Q': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
        case 'R': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return Glyph{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'V': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
        case 'W': return Glyph{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
        case 'X': return Glyph{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
        case 'Y': return Glyph{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
        case 'Z': return Glyph{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
        case '0': return Glyph{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
        case '1': return Glyph{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case '2': return Glyph{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
        case '3': return Glyph{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
        case '4': return Glyph{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
        case '5': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
        case '6': return Glyph{ 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E };
        case '7': return Glyph{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
        case '8': return Glyph{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
        case '9': return Glyph{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
        case '-': return Glyph{ 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
        case '\'': return Glyph{ 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 };
        default: return Glyph{};
        }
    };

    static JapaneseGlyphCache japaneseGlyphs;
    float cursorX = x;
    for (size_t textIndex = 0; textIndex < text.size();)
    {
        const unsigned char firstByte =
            static_cast<unsigned char>(text[textIndex]);
        if (firstByte >= 0x80u)
        {
            int sequenceLength = 0;
            if ((firstByte & 0xE0u) == 0xC0u) sequenceLength = 2;
            else if ((firstByte & 0xF0u) == 0xE0u) sequenceLength = 3;
            else sequenceLength = 4;

            if (textIndex + static_cast<size_t>(sequenceLength) > text.size())
            {
                break;
            }

            unsigned int codePoint = firstByte &
                (sequenceLength == 2 ? 0x1Fu :
                 sequenceLength == 3 ? 0x0Fu : 0x07u);
            for (int byteIndex = 1; byteIndex < sequenceLength; ++byteIndex)
            {
                codePoint = (codePoint << 6u) |
                    (static_cast<unsigned char>(text[textIndex + byteIndex]) & 0x3Fu);
            }
            textIndex += static_cast<size_t>(sequenceLength);

            const RasterGlyph& glyph = japaneseGlyphs.Get(
                static_cast<wchar_t>(codePoint));
            // Match the 5x7 Latin font's visual height and advance so mixed
            // Japanese/ASCII text stays inside the existing HUD panels.
            const float glyphPixelSize = pixelSize * 0.34f;
            for (unsigned int row = 0; row < glyph.height; ++row)
            {
                unsigned int column = 0;
                while (column < glyph.width)
                {
                    while (column < glyph.width &&
                        glyph.bitmap[row * glyph.width + column] == 0)
                    {
                        ++column;
                    }
                    const unsigned int runStart = column;
                    while (column < glyph.width &&
                        glyph.bitmap[row * glyph.width + column] != 0)
                    {
                        ++column;
                    }
                    if (column > runStart)
                    {
                        AddRectangle(
                            cursorX + static_cast<float>(runStart) * glyphPixelSize,
                            y + static_cast<float>(row) * glyphPixelSize,
                            static_cast<float>(column - runStart) * glyphPixelSize,
                            glyphPixelSize,
                            color);
                    }
                }
            }
            cursorX += static_cast<float>(glyph.advance) * glyphPixelSize;
            continue;
        }

        char character = static_cast<char>(firstByte);
        ++textIndex;
        if (character >= 'a' && character <= 'z')
        {
            character = static_cast<char>(character - 'a' + 'A');
        }
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
