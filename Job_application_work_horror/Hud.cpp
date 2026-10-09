// ============================================================================
// ファイルの役割: 目的・操作のヒント・電池の残りなど、ゲーム画面に重ねるUI（HUD）を描いている。
// 主な技術: 四角形を並べた2D描画、ドット絵の英数字フォントとGDIで作る日本語の文字、画面の端を基準にした配置、状態に応じた表示
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
    // UTF-8の文字列で、画面に出る文字の数を数えている（2バイト目以降のバイトは数えない）
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

// キャンバスの拡大率：描画解像度の高さ÷864
float Hud::GetCanvasScale()
{
    return (std::max)(static_cast<float>(Application::GetHeight()), 1.0f) / ReferenceHeight;
}

// キャンバスの幅：画面の縦横比に合わせて、864を基準にした幅にしている
float Hud::GetCanvasWidth()
{
    return static_cast<float>(Application::GetWidth()) / GetCanvasScale();
}

float Hud::GetCanvasHeight()
{
    return ReferenceHeight;
}

// HUD用のシェーダーと、最大の頂点数分のバッファを作っている（毎フレーム中身を書き換えて使っている）
void Hud::Init()
{
    m_Shader.Create("shader/hudVS.hlsl", "shader/hudPS.hlsl");
    std::vector<VERTEX_3D> initialVertices(MaxVertices);
    m_VertexBuffer.Create(initialVertices);
    m_Vertices.reserve(MaxVertices);
}

// テクスチャを四角形に貼り、ためている頂点とは別に、その場で描いている
void Hud::DrawTextureRectangle(
    ID3D11ShaderResourceView* texture,
    Shader& textureShader,
    float x,
    float y,
    float width,
    float height)
{
    if (texture == nullptr)
    {
        return;
    }

    const float right = x + width;
    const float bottom = y + height;
    const auto makeVertex = [](float px, float py, float u, float v)
    {
        VERTEX_3D vertex{};
        vertex.position = Vector3(px, py, 0.0f);
        vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
        vertex.uv = Vector2(u, v);
        return vertex;
    };

    m_Vertices.clear();
    m_Vertices.push_back(makeVertex(x, y, 0.0f, 0.0f));
    m_Vertices.push_back(makeVertex(right, y, 1.0f, 0.0f));
    m_Vertices.push_back(makeVertex(x, bottom, 0.0f, 1.0f));
    m_Vertices.push_back(makeVertex(x, bottom, 0.0f, 1.0f));
    m_Vertices.push_back(makeVertex(right, y, 1.0f, 0.0f));
    m_Vertices.push_back(makeVertex(right, bottom, 1.0f, 1.0f));
    m_VertexBuffer.Modify(m_Vertices);

    // 座標はHUDのキャンバス単位なので、キャンバス全体が画面全体になる行列にしている。
    Renderer::SetWorldViewProjection2D(GetCanvasWidth(), GetCanvasHeight());
    Renderer::SetDepthEnable(false);
    Renderer::SetBlendState(BS_NONE);
    Renderer::SetUV(0.0f, 0.0f, 1.0f, 1.0f);
    textureShader.SetGPU();
    m_VertexBuffer.SetGPU();

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->PSSetShaderResources(0, 1, &texture);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(static_cast<UINT>(m_Vertices.size()), 0);

    // 使い終わったテクスチャを外している
    ID3D11ShaderResourceView* nullResource = nullptr;
    context->PSSetShaderResources(0, 1, &nullResource);
    Renderer::SetDepthEnable(true);
}

// 遊んでいる間のHUDを描いている。fuseCountが負なら、ヒューズの表示を出していない
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
    // 調べる対象がある・開かない対象のときは、照準の色を緑・赤に変えている
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

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();

    // 左上：今の目的。逃げる・電力が戻ったなどの安全な目的は緑、それ以外は黄色で出している
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

    // 任意で探せる情報（記録の数）は、必ず進める目的より控えめに表示し、両方を同時に分かるようにしている。
    // リザルト画面で数える探索の要素も、クリアする前から分かる表示にしている。
    Core::Game* game = Core::Game::GetInstance();
    const int evidenceCount = game != nullptr
        ? (std::clamp)(game->GetEvidenceCollected(), 0, Core::GameState::TotalEvidenceCount)
        : 0;
    const std::string evidenceText =
        "記録 " + std::to_string(evidenceCount) + " / " +
        std::to_string(Core::GameState::TotalEvidenceCount);
    constexpr float evidencePixelSize = 2.0f;
    const float evidenceWidth =
        static_cast<float>(CountDisplayedCharacters(evidenceText)) *
        evidencePixelSize * 6.0f;
    const float evidenceX = screenWidth - evidenceWidth - 52.0f;
    const Color evidenceColor = evidenceCount >= Core::GameState::TotalEvidenceCount
        ? Color(0.92f, 0.78f, 0.34f, 0.96f)
        : Color(0.62f, 0.78f, 0.70f, 0.88f);
    AddRectangle(evidenceX - 14.0f, 74.0f + m_TopRightOffset,
        evidenceWidth + 28.0f, 34.0f, dark);
    AddText(evidenceX, 82.0f + m_TopRightOffset,
        evidenceText, evidencePixelSize, evidenceColor);

    // 視線の位置だけを伝える、小さな十字の照準を画面の中央に描いている。
    const float reticleExtent = hasInteractionTarget ? 11.0f : 9.0f;
    AddRectangle(screenWidth * 0.5f - reticleExtent, screenHeight * 0.5f - 1.0f, reticleExtent - 2.0f, 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f + 2.0f, screenHeight * 0.5f - 1.0f, reticleExtent - 2.0f, 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f - reticleExtent, 2.0f, reticleExtent - 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f + 2.0f, 2.0f, reticleExtent - 2.0f, reticleColor);

    // 負の個数を「ヒューズの表示なし」として扱い、ヒューズのない面でも電池のHUDを共用している。
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

    // 懐中電灯の電池の枠と残りのゲージを描いている（50%以下で黄色、20%以下で赤）。
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

    // スタミナのゲージの色（40%以下で黄色、20%以下で赤）
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

    // 走れない間はスタミナを使わないため、ゲージ自体を出していない。
    if (player.IsSprintAllowed())
    {
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
    }

    // 押すキーと、実行される操作の名前を一緒に表示している。
    // ボタン名だけでは、拾う・開ける・脱出するのどれが起きるか分からなかったためである。
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
        // コントローラーがつながっていればA、なければEの印を出している
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

    // 右上：電池を拾った直後は「回復した」、20%以下なら「残量が少ない」と出している
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
        AddRectangle(noticeX - 14.0f, 28.0f + m_TopRightOffset,
            noticeWidth + 28.0f, 34.0f, dark);
        AddText(noticeX, 36.0f + m_TopRightOffset,
            batteryNotice, noticePixelSize, batteryNoticeColor);
    }

    Flush();
}


// まばたきのように、画面全体を黒で覆っている（最大90%の濃さ）
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
        GetCanvasWidth(),
        GetCanvasHeight(),
        Color(0.0f, 0.0f, 0.0f, blinkOpacity));
    Flush();
}

// ためた頂点を捨てている
void Hud::Uninit()
{
    m_Vertices.clear();
}

// ためた頂点をGPUへ送り、アルファブレンドで1回で描いている
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

// 四角形を1つためている。キャンバス座標（左上が原点、下向きが+y）を、-1〜1の画面の座標に変換している
void Hud::AddRectangle(float x, float y, float width, float height, const Color& color)
{
    if (m_Vertices.size() + 6 > MaxVertices)
    {
        return;
    }

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();

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

// 操作ボタンの印「E」を、縦線と3本の横線で組み立てている
void Hud::AddLetterE(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(2.0f, size * 0.18f);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size * 0.82f, stroke, color);
    AddRectangle(x, y + size - stroke, size, stroke, color);
}

// 操作ボタンの印「A」を、左右の縦線と2本の横線で組み立てている（角ばった形）
void Hud::AddLetterA(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(2.0f, size * 0.18f);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x + size - stroke, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size, stroke, color);
}

// 文字列を1文字ずつ、四角形の集まりとしてためている
void Hud::AddText(
    float x,
    float y,
    std::string_view text,
    float pixelSize,
    const Color& color)
{
    // GDIで作った1文字分の画像（幅・高さ・次の文字までの送り幅・ずれ・白黒の画素）
    struct RasterGlyph
    {
        unsigned int width = 0;
        unsigned int height = 0;
        int advance = 0;
        int offsetX = 0;    // 文字の位置から画像の左上までのずれ（画素）
        int offsetY = 0;
        std::vector<unsigned char> bitmap;
    };

    // 日本語の文字は、画面に出る大きさ（画素の高さ）のフォントで白黒の画像にし、1ドット＝1画素で描いている。
    // 小さいフォントの画像を縮めて描くと1ドットが1画素より細くなり、線が消えたり太ったりして字が崩れるためである。
    class JapaneseGlyphCache
    {
    public:
        // 文字の画像を作るための、画面に出さないデバイスコンテキストを作っている
        JapaneseGlyphCache()
        {
            m_DC = CreateCompatibleDC(nullptr);
        }

        // 選んでいたフォントを戻し、作ったフォントとデバイスコンテキストを解放している
        ~JapaneseGlyphCache()
        {
            if (m_DC != nullptr && m_OldFont != nullptr)
            {
                SelectObject(m_DC, m_OldFont);
            }
            for (const auto& [height, font] : m_Fonts)
            {
                DeleteObject(font);
            }
            if (m_DC != nullptr)
            {
                DeleteDC(m_DC);
            }
        }

        // fontHeightは文字の高さ（画素）。大きさと文字の組み合わせごとに、一度だけ画像を作っている。
        const RasterGlyph& Get(wchar_t character, int fontHeight)
        {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(fontHeight) << 32u) | static_cast<std::uint64_t>(character);
            const auto found = m_Glyphs.find(key);
            if (found != m_Glyphs.end())
            {
                return found->second;
            }

            RasterGlyph glyph;
            if (m_DC == nullptr || !SelectFont(fontHeight))
            {
                return m_Glyphs.emplace(key, std::move(glyph)).first->second;
            }

            // 拡大・回転なしの変換で、GGO_BITMAP（1画素1ビットの白黒画像）として文字の形を取り出している
            MAT2 transform{};
            transform.eM11.value = 1;
            transform.eM22.value = 1;
            GLYPHMETRICS metrics{};
            const DWORD size = GetGlyphOutlineW(
                m_DC, character, GGO_BITMAP, &metrics,
                0, nullptr, &transform);
            glyph.advance = metrics.gmCellIncX > 0
                ? metrics.gmCellIncX
                : fontHeight;
            if (size != GDI_ERROR && size > 0)
            {
                glyph.width = metrics.gmBlackBoxX;
                glyph.height = metrics.gmBlackBoxY;
                // 画像は字の形を囲む最小の四角なので、基準線からの位置で上下左右をずらしている。
                // ずらさないと「ュ」「ー」のような小さい字や横線が、上の端に寄って描かれる。
                glyph.offsetX = metrics.gmptGlyphOrigin.x;
                glyph.offsetY = m_Ascent - metrics.gmptGlyphOrigin.y - m_TopInset;
                // GDIの白黒画像は1行が4バイト単位にそろえられているので、その幅でビットを1画素ずつ取り出している
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
            return m_Glyphs.emplace(key, std::move(glyph)).first->second;
        }

    private:
        // デバイスコンテキスト、元のフォント、今選んでいる高さ
        HDC m_DC = nullptr;
        HGDIOBJ m_OldFont = nullptr;
        int m_SelectedHeight = 0;
        int m_Ascent = 0;       // 基準線から文字の枠の上端までの高さ
        int m_TopInset = 0;     // 文字の枠の上端から、漢字の上端までのすき間
        // 高さごとのフォントと、作った文字の画像
        std::unordered_map<int, HFONT> m_Fonts;
        std::unordered_map<std::uint64_t, RasterGlyph> m_Glyphs;

        // 指定した高さのフォントを使える状態にしている（初めての高さなら作っている）。
        bool SelectFont(int fontHeight)
        {
            if (m_SelectedHeight == fontHeight)
            {
                return true;
            }
            HFONT font = nullptr;
            const auto found = m_Fonts.find(fontHeight);
            if (found != m_Fonts.end())
            {
                font = found->second;
            }
            else
            {
                // Yu Gothic UIの太字を、アンチエイリアスなし（白黒）で作っている
                font = CreateFontW(
                    -fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                    L"Yu Gothic UI");
                if (font == nullptr)
                {
                    return false;
                }
                m_Fonts.emplace(fontHeight, font);
            }

            const HGDIOBJ previous = SelectObject(m_DC, font);
            if (m_OldFont == nullptr)
            {
                m_OldFont = previous;
            }
            m_SelectedHeight = fontHeight;

            TEXTMETRICW textMetrics{};
            GetTextMetricsW(m_DC, &textMetrics);
            m_Ascent = textMetrics.tmAscent;
            // 漢字の上端を文字の位置（y）にそろえ、以前と同じ高さに並ぶようにしている。
            MAT2 transform{};
            transform.eM11.value = 1;
            transform.eM22.value = 1;
            GLYPHMETRICS reference{};
            m_TopInset = GetGlyphOutlineW(
                m_DC, L'国', GGO_METRICS, &reference, 0, nullptr, &transform) != GDI_ERROR
                ? m_Ascent - reference.gmptGlyphOrigin.y
                : 0;
            return true;
        }
    };

    // 英数字と記号の5x7のドット絵。各行の下位5ビットが左から右の点を表している
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
        // 記号。HUDの「記録 0 / 4」「C / Y」「100%」「受信中...」「記録:」などで使っている（無いと空白になる）。
        case '/': return Glyph{ 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
        case '%': return Glyph{ 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
        case '.': return Glyph{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
        case ',': return Glyph{ 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 };
        case ':': return Glyph{ 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
        case '?': return Glyph{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
        case '!': return Glyph{ 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
        case '(': return Glyph{ 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
        case ')': return Glyph{ 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
        case '+': return Glyph{ 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
        default: return Glyph{};
        }
    };

    // 文字の画像は一度作ったら使い回すため、関数の中のstaticにしている
    static JapaneseGlyphCache japaneseGlyphs;
    float cursorX = x;
    for (size_t textIndex = 0; textIndex < text.size();)
    {
        const unsigned char firstByte =
            static_cast<unsigned char>(text[textIndex]);
        // 先頭のバイトが0x80以上なら、UTF-8の2〜4バイトの文字としてコードポイントを取り出している
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

            // 5x7の英字フォントの見た目の高さと送り幅に合わせ、18pxのフォントを pixelSize*0.34 倍した大きさにしている。
            // 日本語とASCIIが混ざっても、今あるHUDのパネルの中へ収めるためである。
            // その大きさを実際の画素に直したフォントで画像を作り、1ドットを1画素（キャンバス単位では1/scale）で描いている。
            const float canvasScale = GetCanvasScale();
            const int fontHeight = (std::max)(6,
                static_cast<int>(std::lround(18.0f * pixelSize * 0.34f * canvasScale)));
            const RasterGlyph& glyph = japaneseGlyphs.Get(
                static_cast<wchar_t>(codePoint), fontHeight);
            const float glyphPixelSize = 1.0f / canvasScale;
            // 文字の位置を画素の境目にそろえ、ドットが2画素にまたがってにじまないようにしている。
            const float glyphX = std::round(cursorX * canvasScale) / canvasScale +
                static_cast<float>(glyph.offsetX) * glyphPixelSize;
            const float glyphY = std::round(y * canvasScale) / canvasScale +
                static_cast<float>(glyph.offsetY) * glyphPixelSize;
            // 横に続く点はまとめて1つの横長の四角形にし、頂点の数を減らしている
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
                            glyphX + static_cast<float>(runStart) * glyphPixelSize,
                            glyphY + static_cast<float>(row) * glyphPixelSize,
                            static_cast<float>(column - runStart) * glyphPixelSize,
                            glyphPixelSize,
                            color);
                    }
                }
            }
            cursorX += static_cast<float>(glyph.advance) * glyphPixelSize;
            continue;
        }

        // 英数字：小文字は大文字として扱い、5x7のドット絵の点を四角形で描いている
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
