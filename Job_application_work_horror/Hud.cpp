// ============================================================================
// ファイルの役割: 目的・操作のヒント・電池の残りなど、ゲーム画面に重ねるUI（HUD）を描いている。
// 主な技術: 四角形を並べた2D描画、ドット絵の英数字フォントとGDIで作る日本語の文字、画面の端を基準にした配置、状態に応じた表示
// ============================================================================

#include "Hud.h"
#include "StoryText.h"

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
    // --- 遊んでいる間の HUD の並べ方（キャンバスの単位。キャンバスは高さ864）
    // 画面の左・右・上の余白、右上の知らせの枠の内側の余白
    constexpr float LeftMargin = 34.0f;
    constexpr float RightMargin = 52.0f;
    constexpr float TopMargin = 28.0f;
    constexpr float NoticePadding = 14.0f;
    // 左下のゲージの枠の幅と、名前の文字を置く x
    constexpr float GaugePanelWidth = 230.0f;
    constexpr float GaugeLabelX = 43.0f;
    // ヒューズの数（左下の3つの四角）
    constexpr int FuseSlotCount = Core::GameState::RequiredFuseCount;
    // 電池とスタミナの最大値（%）と、色を変える割合（電池：半分以下で黄、2割以下で赤。スタミナ：4割以下で黄、2割以下で赤）
    constexpr float GaugeMaximum = 100.0f;
    constexpr float BatteryWarningRate = 0.5f;
    constexpr float BatteryDangerRate = 0.2f;
    constexpr float StaminaWarningRate = 0.40f;
    constexpr float StaminaDangerRate = 0.20f;
    // 右上に「電池残量が少ない」と出す残量（%）
    constexpr float LowBatteryNoticePercent = 20.0f;
    // 背景と通常の文字色は複数の表示で共通にし、ヒューズとゲージの色を揃える。
    namespace HudPalette
    {
        const Color Text = Color(0.88f, 0.92f, 0.90f, 0.85f);
        const Color Panel = Color(0.015f, 0.02f, 0.02f, 0.72f);
        const Color Inactive = Color(0.16f, 0.19f, 0.18f, 0.85f);
        const Color Collected = Color(0.72f, 0.86f, 0.66f, 0.95f);
    }

    // 操作できるときだけ十字を広げ、操作できない対象は赤で知らせる。
    namespace ReticleLayout
    {
        constexpr float TargetExtent = 11.0f;
        constexpr float CenterGap = 2.0f;
        constexpr float StrokeWidth = 2.0f;
        constexpr float IdleExtent = 9.0f;
        const Color LockedColor = Color(0.92f, 0.28f, 0.20f, 0.96f);
        const Color TargetColor = Color(0.62f, 0.92f, 0.70f, 0.98f);
    }

    // 左上に目的を置き、必要な幅だけ文字の後ろを暗くする。
    namespace ObjectiveLayout
    {
        constexpr float TextPixelSize = 3.0f;
        constexpr float CharacterWidth = 18.0f;
        constexpr float PanelExtraWidth = 24.0f;
        constexpr float PanelHeight = 39.0f;
        constexpr float AccentWidth = 4.0f;
        constexpr float TextX = 48.0f;
        constexpr float TextY = 37.0f;
        const Color SafeColor = Color(0.52f, 0.92f, 0.58f, 0.95f);
        const Color DefaultColor = Color(0.88f, 0.82f, 0.62f, 0.95f);
    }

    // 記録と電池の知らせの高さ、文字サイズ、上下の間隔を揃える。
    namespace TopRightNoticeLayout
    {
        constexpr float EvidencePanelY = 74.0f;
        constexpr float EvidenceTextY = 82.0f;
        constexpr float PanelHeight = 34.0f;
        constexpr float TextPixelSize = 2.0f;
        const Color EvidenceCompleteColor = Color(0.92f, 0.78f, 0.34f, 0.96f);
        const Color EvidenceProgressColor = Color(0.62f, 0.78f, 0.70f, 0.88f);
    }

    // ヒューズは固定間隔のスロットで取得済みと未取得を見分ける。
    namespace FuseLayout
    {
        constexpr float PanelBottomOffset = 112.0f;
        constexpr float PanelWidth = 112.0f;
        constexpr float PanelHeight = 36.0f;
        constexpr float FirstSlotX = 46.0f;
        constexpr float SlotSpacing = 32.0f;
        constexpr float SlotBottomOffset = 101.0f;
        constexpr float SlotWidth = 20.0f;
        constexpr float SlotHeight = 14.0f;
    }

    // 電池とスタミナで横位置と文字の大きさを揃え、縦位置と高さを分ける。
    namespace GaugeLayout
    {
        constexpr float BatteryFillBottomOffset = 57.0f;
        constexpr float BatteryPanelBottomOffset = 66.0f;
        constexpr float BatteryLabelBottomOffset = 58.0f;
        constexpr float BatteryPanelHeight = 30.0f;
        constexpr float BatteryFillHeight = 12.0f;
        constexpr float StaminaPanelBottomOffset = 32.0f;
        constexpr float StaminaLabelBottomOffset = 26.0f;
        constexpr float StaminaFillBottomOffset = 25.0f;
        constexpr float StaminaPanelHeight = 24.0f;
        constexpr float StaminaFillHeight = 10.0f;
        constexpr float LabelPixelSize = 1.5f;
        constexpr float FillX = 104.0f;
        constexpr float FillWidth = 152.0f;
        const Color BatteryNormalColor = Color(0.42f, 0.82f, 0.48f, 0.95f);
        const Color StaminaNormalColor = Color(0.34f, 0.70f, 0.86f, 0.95f);
        const Color BatteryDangerColor = Color(0.92f, 0.18f, 0.12f, 0.95f);
        const Color BatteryWarningColor = Color(0.92f, 0.68f, 0.16f, 0.95f);
        const Color StaminaDangerColor = Color(0.90f, 0.25f, 0.16f, 0.95f);
        const Color StaminaWarningColor = Color(0.90f, 0.62f, 0.16f, 0.95f);
    }

    // 操作キーの枠を固定し、操作名の長さに合わせて全体の幅を広げる。
    namespace InteractionPromptLayout
    {
        constexpr float PanelExtraWidth = 88.0f;
        constexpr float PanelHeight = 58.0f;
        constexpr float PanelBottomOffset = 106.0f;
        constexpr float KeyPanelInset = 10.0f;
        constexpr float KeyPanelWidth = 44.0f;
        constexpr float KeyPanelHeight = 38.0f;
        constexpr float KeyGlyphX = 20.0f;
        constexpr float KeyGlyphY = 17.0f;
        constexpr float KeyGlyphSize = 24.0f;
        constexpr float TextX = 68.0f;
        constexpr float TextY = 22.0f;
        constexpr float TextPixelSize = 2.0f;
        const Color LockedTextColor = Color(0.85f, 0.22f, 0.18f, 0.95f);
    }

    // 電池の回復と残量警告を、記録の表示より上に置く。
    namespace BatteryNoticeLayout
    {
        constexpr float TextY = 36.0f;
        const Color RecoveryColor = Color(0.52f, 0.92f, 0.58f, 0.96f);
        const Color LowColor = Color(0.94f, 0.30f, 0.18f, 0.96f);
    }

    // 透明に近い暗転は四角形を描かずに済ませる。
    constexpr float BlinkDrawThreshold = 0.001f;

    // ボタンの印は小さくしても線がつぶれない太さに保つ。
    constexpr float KeyGlyphMinStroke = 2.0f;
    constexpr float KeyGlyphStrokeRatio = 0.18f;
    constexpr float KeyGlyphEMiddleWidthRatio = 0.82f;

    // UTF-8の文字列で、画面に出る文字の数を数えている（2バイト目以降のバイトは数えない）
    size_t CountDisplayedCharacters(std::string_view text)
    {
        size_t count = 0;
        for (unsigned char byte : text)
        {
            if (!StoryText::IsUtf8ContinuationByte(byte))
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
    m_Shader.CreateRequired("shader/hudVS.hlsl", "shader/hudPS.hlsl");
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
    // 前の頂点で今回の件数を描かないよう、転送できたときだけ描画する。
    if (!m_VertexBuffer.Modify(m_Vertices))
    {
        return;
    }

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
        ? ReticleLayout::LockedColor
        : (hasInteractionTarget
            ? ReticleLayout::TargetColor
            : HudPalette::Text);

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();

    // 左上：今の目的。逃げる・電力が戻ったなどの安全な目的は緑、それ以外は黄色で出している
    if (!objectiveText.empty())
    {
        const float objectiveWidth =
            static_cast<float>(CountDisplayedCharacters(objectiveText)) *
            ObjectiveLayout::CharacterWidth + ObjectiveLayout::PanelExtraWidth;
        const bool isSafeObjective =
            objectiveText == "ESCAPE" ||
            objectiveText == "ESCAPED" ||
            objectiveText == "POWER RESTORED" ||
            objectiveText == "OPEN THE DOOR" ||
            objectiveText == "LEAVE";
        const Color objectiveColor = isSafeObjective
            ? ObjectiveLayout::SafeColor
            : ObjectiveLayout::DefaultColor;

        AddRectangle(LeftMargin, TopMargin, objectiveWidth, ObjectiveLayout::PanelHeight, HudPalette::Panel);
        AddRectangle(LeftMargin, TopMargin, ObjectiveLayout::AccentWidth, ObjectiveLayout::PanelHeight, objectiveColor);
        AddText(ObjectiveLayout::TextX, ObjectiveLayout::TextY, objectiveText, ObjectiveLayout::TextPixelSize, objectiveColor);
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
    const float evidenceWidth =
        static_cast<float>(CountDisplayedCharacters(evidenceText)) *
        TopRightNoticeLayout::TextPixelSize * GlyphAdvance;
    const float evidenceX = screenWidth - evidenceWidth - RightMargin;
    const Color evidenceColor = evidenceCount >= Core::GameState::TotalEvidenceCount
        ? TopRightNoticeLayout::EvidenceCompleteColor
        : TopRightNoticeLayout::EvidenceProgressColor;
    if (m_EvidenceVisible)
    {
        AddRectangle(evidenceX - NoticePadding, TopRightNoticeLayout::EvidencePanelY + m_TopRightOffset,
            evidenceWidth + NoticePadding * 2.0f, TopRightNoticeLayout::PanelHeight, HudPalette::Panel);
        AddText(evidenceX, TopRightNoticeLayout::EvidenceTextY + m_TopRightOffset,
            evidenceText, TopRightNoticeLayout::TextPixelSize, evidenceColor);
    }

    // 視線の位置だけを伝える、小さな十字の照準を画面の中央に描いている。
    const float reticleExtent = hasInteractionTarget ? ReticleLayout::TargetExtent : ReticleLayout::IdleExtent;
    const float centerX = screenWidth * 0.5f;
    const float centerY = screenHeight * 0.5f;
    const float stroke = ReticleLayout::StrokeWidth;
    const float gap = ReticleLayout::CenterGap;
    const float armLength = reticleExtent - gap;
    AddRectangle(centerX - reticleExtent, centerY - stroke * 0.5f, armLength, stroke, reticleColor);
    AddRectangle(centerX + gap, centerY - stroke * 0.5f, armLength, stroke, reticleColor);
    AddRectangle(centerX - stroke * 0.5f, centerY - reticleExtent, stroke, armLength, reticleColor);
    AddRectangle(centerX - stroke * 0.5f, centerY + gap, stroke, armLength, reticleColor);

    // 負の個数を「ヒューズの表示なし」として扱い、ヒューズのない面でも電池のHUDを共用している。
    if (fuseCount >= 0)
    {
        AddRectangle(LeftMargin, screenHeight - FuseLayout::PanelBottomOffset, FuseLayout::PanelWidth, FuseLayout::PanelHeight, HudPalette::Panel);
        const int clampedFuseCount = std::clamp(fuseCount, 0, FuseSlotCount);
        for (int i = 0; i < FuseSlotCount; ++i)
        {
            AddRectangle(
                FuseLayout::FirstSlotX + static_cast<float>(i) * FuseLayout::SlotSpacing,
                screenHeight - FuseLayout::SlotBottomOffset,
                FuseLayout::SlotWidth,
                FuseLayout::SlotHeight,
                i < clampedFuseCount ? HudPalette::Collected : HudPalette::Inactive
            );
        }
    }

    // 懐中電灯の電池の枠と残りのゲージを描いている（50%以下で黄色、20%以下で赤）。
    const float batteryRate = std::clamp(
        player.GetBattery() / GaugeMaximum, 0.0f, 1.0f);
    Color batteryColor = GaugeLayout::BatteryNormalColor;
    if (batteryRate <= BatteryDangerRate)
    {
        batteryColor = GaugeLayout::BatteryDangerColor;
    }
    else if (batteryRate <= BatteryWarningRate)
    {
        batteryColor = GaugeLayout::BatteryWarningColor;
    }

    AddRectangle(LeftMargin, screenHeight - GaugeLayout::BatteryPanelBottomOffset, GaugePanelWidth, GaugeLayout::BatteryPanelHeight, HudPalette::Panel);
    AddText(GaugeLabelX, screenHeight - GaugeLayout::BatteryLabelBottomOffset,
        "ライト", GaugeLayout::LabelPixelSize, HudPalette::Text);
    AddRectangle(GaugeLayout::FillX, screenHeight - GaugeLayout::BatteryFillBottomOffset,
        GaugeLayout::FillWidth, GaugeLayout::BatteryFillHeight, HudPalette::Inactive);
    if (batteryRate > 0.0f)
    {
        AddRectangle(GaugeLayout::FillX, screenHeight - GaugeLayout::BatteryFillBottomOffset,
            GaugeLayout::FillWidth * batteryRate, GaugeLayout::BatteryFillHeight, batteryColor);
    }

    // スタミナのゲージの色（40%以下で黄色、20%以下で赤）
    const float staminaRate = std::clamp(
        player.GetStamina() / GaugeMaximum, 0.0f, 1.0f);
    Color staminaColor = GaugeLayout::StaminaNormalColor;
    if (staminaRate <= StaminaDangerRate)
    {
        staminaColor = GaugeLayout::StaminaDangerColor;
    }
    else if (staminaRate <= StaminaWarningRate)
    {
        staminaColor = GaugeLayout::StaminaWarningColor;
    }

    // 走れない間はスタミナを使わないため、ゲージ自体を出していない。
    if (player.IsSprintAllowed())
    {
        AddRectangle(LeftMargin, screenHeight - GaugeLayout::StaminaPanelBottomOffset, GaugePanelWidth, GaugeLayout::StaminaPanelHeight, HudPalette::Panel);
        AddText(GaugeLabelX, screenHeight - GaugeLayout::StaminaLabelBottomOffset,
            "スタミナ", GaugeLayout::LabelPixelSize, HudPalette::Text);
        AddRectangle(GaugeLayout::FillX, screenHeight - GaugeLayout::StaminaFillBottomOffset,
            GaugeLayout::FillWidth, GaugeLayout::StaminaFillHeight, HudPalette::Inactive);
        if (staminaRate > 0.0f)
        {
            AddRectangle(GaugeLayout::FillX, screenHeight - GaugeLayout::StaminaFillBottomOffset,
                GaugeLayout::FillWidth * staminaRate, GaugeLayout::StaminaFillHeight, staminaColor);
        }
    }

    // 押すキーと、実行される操作の名前を一緒に表示している。
    // ボタン名だけでは、拾う・開ける・脱出するのどれが起きるか分からなかったためである。
    if (!interactionPrompt.empty())
    {
        const Color promptColor = interactionLocked
            ? InteractionPromptLayout::LockedTextColor
            : HudPalette::Text;
        const float promptTextWidth =
            static_cast<float>(CountDisplayedCharacters(interactionPrompt)) *
            InteractionPromptLayout::TextPixelSize * GlyphAdvance;
        const float panelWidth = InteractionPromptLayout::PanelExtraWidth + promptTextWidth;
        const float panelHeight = InteractionPromptLayout::PanelHeight;
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = screenHeight - InteractionPromptLayout::PanelBottomOffset;

        AddRectangle(panelX, panelY, panelWidth, panelHeight, HudPalette::Panel);
        AddRectangle(panelX + InteractionPromptLayout::KeyPanelInset, panelY + InteractionPromptLayout::KeyPanelInset,
            InteractionPromptLayout::KeyPanelWidth, InteractionPromptLayout::KeyPanelHeight, HudPalette::Inactive);
        // コントローラーがつながっていればA、なければEの印を出している
        if (Input::IsControllerConnected())
        {
            AddLetterA(panelX + InteractionPromptLayout::KeyGlyphX, panelY + InteractionPromptLayout::KeyGlyphY,
                InteractionPromptLayout::KeyGlyphSize, promptColor);
        }
        else
        {
            AddLetterE(panelX + InteractionPromptLayout::KeyGlyphX, panelY + InteractionPromptLayout::KeyGlyphY,
                InteractionPromptLayout::KeyGlyphSize, promptColor);
        }
        AddText(panelX + InteractionPromptLayout::TextX, panelY + InteractionPromptLayout::TextY,
            interactionPrompt, InteractionPromptLayout::TextPixelSize, promptColor);
    }

    // 右上：電池を拾った直後は「回復した」、20%以下なら「残量が少ない」と出している
    std::string_view batteryNotice;
    Color batteryNoticeColor = BatteryNoticeLayout::RecoveryColor;
    if (player.GetBatteryNoticeTimer() > 0.0f)
    {
        batteryNotice = "電池を回復した";
    }
    else if (player.GetBattery() <= LowBatteryNoticePercent)
    {
        batteryNotice = "電池残量が少ない";
        batteryNoticeColor = BatteryNoticeLayout::LowColor;
    }
    if (!batteryNotice.empty())
    {
        const float noticeWidth =
            static_cast<float>(CountDisplayedCharacters(batteryNotice)) *
            TopRightNoticeLayout::TextPixelSize * GlyphAdvance;
        const float noticeX = screenWidth - noticeWidth - RightMargin;
        AddRectangle(noticeX - NoticePadding, TopMargin + m_TopRightOffset,
            noticeWidth + NoticePadding * 2.0f, TopRightNoticeLayout::PanelHeight, HudPalette::Panel);
        AddText(noticeX, BatteryNoticeLayout::TextY + m_TopRightOffset,
            batteryNotice, TopRightNoticeLayout::TextPixelSize, batteryNoticeColor);
    }

    Flush();
}


// 画面全体を黒で覆う。まばたきでは最大90%、玄関から入る暗転では完全に黒くする。
void Hud::DrawBlink(float opacity, float maximumOpacity)
{
    const float blinkOpacity = (std::clamp)(opacity, 0.0f, maximumOpacity);
    if (blinkOpacity <= BlinkDrawThreshold)
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

    // 前の頂点で今回の件数を描かないよう、転送できたときだけ描画する。
    if (!m_VertexBuffer.Modify(m_Vertices))
    {
        return;
    }

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
    // 四角形1つ（三角形2枚）の頂点の数
    constexpr size_t QuadVertexCount = 6;
    if (m_Vertices.size() + QuadVertexCount > MaxVertices)
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
    const float stroke = (std::max)(KeyGlyphMinStroke, size * KeyGlyphStrokeRatio);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size * KeyGlyphEMiddleWidthRatio, stroke, color);
    AddRectangle(x, y + size - stroke, size, stroke, color);
}

// 操作ボタンの印「A」を、左右の縦線と2本の横線で組み立てている（角ばった形）
void Hud::AddLetterA(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(KeyGlyphMinStroke, size * KeyGlyphStrokeRatio);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x + size - stroke, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size, stroke, color);
}

namespace
{
    // 英数字のドット絵の大きさ（横5点・縦7点）
    constexpr int AsciiGlyphColumnCount = 5;
    constexpr std::size_t AsciiGlyphRowCount = 7;
    // 日本語の文字の大きさ：基準の18pxのフォントを pixelSize*0.34 倍し、5x7の英字と見た目の高さと送り幅をそろえる。小さくなりすぎないよう、この画素の高さより小さくしない
    constexpr float JapaneseFontReferencePixels = 18.0f;
    constexpr float JapaneseFontScaleToAscii = 0.34f;
    constexpr int MinimumJapaneseFontHeight = 6;

    // GDIの白黒画像は32ビット境界で行をそろえ、各バイトを上位ビットから読む。
    constexpr unsigned int GlyphBitmapBitsPerByte = 8;
    constexpr unsigned int GlyphBitmapRowAlignmentBytes = 4;
    constexpr unsigned int GlyphBitmapRowAlignmentBits = GlyphBitmapRowAlignmentBytes * GlyphBitmapBitsPerByte;
    constexpr unsigned int GlyphBitmapLeftmostBitMask = 0x80u;
    // キャッシュキーの下位32ビットを文字、上位をフォントの高さに使う。
    constexpr unsigned int GlyphCacheFontHeightShift = 32u;

    // UTF-8の先頭バイトは上位の印で長さを、残りのビットで文字番号の一部を表す。
    constexpr unsigned int Utf8FirstNonAsciiByte = 0x80u;
    constexpr unsigned int Utf8TwoBytePrefixMask = 0xE0u;
    constexpr unsigned int Utf8TwoBytePrefix = 0xC0u;
    constexpr unsigned int Utf8ThreeBytePrefixMask = 0xF0u;
    constexpr unsigned int Utf8ThreeBytePrefix = 0xE0u;
    constexpr unsigned int Utf8FourBytePrefixMask = 0xF8u;
    constexpr unsigned int Utf8FourBytePrefix = 0xF0u;
    constexpr int Utf8TwoByteLength = 2;
    constexpr int Utf8ThreeByteLength = 3;
    constexpr int Utf8FourByteLength = 4;
    constexpr unsigned int Utf8TwoByteLeadPayloadMask = 0x1Fu;
    constexpr unsigned int Utf8ThreeByteLeadPayloadMask = 0x0Fu;
    constexpr unsigned int Utf8FourByteLeadPayloadMask = 0x07u;
    // 続きのバイトからは印の上位2ビットを除き、下位6ビットだけをつなぐ。
    constexpr unsigned int Utf8ContinuationPayloadBits = 6u;
    constexpr unsigned int Utf8ContinuationPayloadMask = 0x3Fu;
    // 正規の長さより短い値や、UTF-16で予約されている値は文字として表示しない。
    constexpr unsigned int Utf8TwoByteMinimum = 0x80u;
    constexpr unsigned int Utf8ThreeByteMinimum = 0x800u;
    constexpr unsigned int Utf8FourByteMinimum = 0x10000u;
    constexpr unsigned int UnicodeSurrogateStart = 0xD800u;
    constexpr unsigned int UnicodeSurrogateEnd = 0xDFFFu;
    constexpr unsigned int UnicodeMaximum = 0x10FFFFu;
    constexpr char UnsupportedTextGlyph = '?';

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
                (static_cast<std::uint64_t>(fontHeight) << GlyphCacheFontHeightShift) | static_cast<std::uint64_t>(character);
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
                const unsigned int pitch = ((glyph.width + GlyphBitmapRowAlignmentBits - 1u) / GlyphBitmapRowAlignmentBits) * GlyphBitmapRowAlignmentBytes;
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
                            const unsigned char byte = packed[row * pitch + column / GlyphBitmapBitsPerByte];
                            glyph.bitmap[row * glyph.width + column] =
                                (byte & (GlyphBitmapLeftmostBitMask >> (column % GlyphBitmapBitsPerByte))) != 0 ? 1 : 0;
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
    std::array<std::uint8_t, AsciiGlyphRowCount> GetAsciiGlyphRows(char character)
    {
        using Glyph = std::array<std::uint8_t, AsciiGlyphRowCount>;

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
    }

    // 文字の画像は一度作ったら使い回す（初めて使うときに作り、プログラムの終わりに片付ける）
    JapaneseGlyphCache& GetJapaneseGlyphs()
    {
        static JapaneseGlyphCache japaneseGlyphs;
        return japaneseGlyphs;
    }
}

// 文字列を1文字ずつ、四角形の集まりとしてためている（日本語はフォントの画像、英数字は5x7のドット絵）
void Hud::AddText(
    float x,
    float y,
    std::string_view text,
    float pixelSize,
    const Color& color)
{
    float cursorX = x;
    for (size_t textIndex = 0; textIndex < text.size();)
    {
        const unsigned char firstByte =
            static_cast<unsigned char>(text[textIndex]);
        // 先頭のバイトが0x80以上なら、UTF-8の2〜4バイトの文字としてコードポイントを取り出している
        if (firstByte >= Utf8FirstNonAsciiByte)
        {
            int sequenceLength = 0;
            if ((firstByte & Utf8TwoBytePrefixMask) == Utf8TwoBytePrefix) sequenceLength = Utf8TwoByteLength;
            else if ((firstByte & Utf8ThreeBytePrefixMask) == Utf8ThreeBytePrefix) sequenceLength = Utf8ThreeByteLength;
            else if ((firstByte & Utf8FourBytePrefixMask) == Utf8FourBytePrefix) sequenceLength = Utf8FourByteLength;

            unsigned int codePoint = firstByte &
                (sequenceLength == Utf8TwoByteLength ? Utf8TwoByteLeadPayloadMask :
                 sequenceLength == Utf8ThreeByteLength ? Utf8ThreeByteLeadPayloadMask : Utf8FourByteLeadPayloadMask);
            bool valid = sequenceLength > 0 && static_cast<size_t>(sequenceLength) <= text.size() - textIndex;
            for (int byteIndex = 1; valid && byteIndex < sequenceLength; ++byteIndex)
            {
                const auto byte = static_cast<unsigned char>(text[textIndex + byteIndex]);
                valid = StoryText::IsUtf8ContinuationByte(byte);
                codePoint = (codePoint << Utf8ContinuationPayloadBits) | (byte & Utf8ContinuationPayloadMask);
            }
            const unsigned int minimum = sequenceLength == Utf8TwoByteLength ? Utf8TwoByteMinimum :
                sequenceLength == Utf8ThreeByteLength ? Utf8ThreeByteMinimum : Utf8FourByteMinimum;
            valid = valid && codePoint >= minimum && codePoint <= UnicodeMaximum &&
                (codePoint < UnicodeSurrogateStart || codePoint > UnicodeSurrogateEnd);
            // 壊れた並びは1バイトだけ進める。4バイト文字は1文字分を読み、GDIの1文字描画では代替表示にする。
            textIndex += valid ? static_cast<size_t>(sequenceLength) : 1;
            if (valid && sequenceLength != Utf8FourByteLength)
            {
                cursorX += AddJapaneseCharacter(cursorX, y, static_cast<wchar_t>(codePoint), pixelSize, color);
            }
            else
            {
                AddAsciiCharacter(cursorX, y, UnsupportedTextGlyph, pixelSize, color);
                cursorX += pixelSize * GlyphAdvance;
            }
            continue;
        }

        AddAsciiCharacter(cursorX, y, static_cast<char>(firstByte), pixelSize, color);
        ++textIndex;
        cursorX += pixelSize * GlyphAdvance;
    }
}

// 日本語の1文字を、画面に出る大きさのフォントの白黒の画像から、横に続く点をまとめた四角形で描いている。進む幅を返している
float Hud::AddJapaneseCharacter(float x, float y, wchar_t character, float pixelSize, const Color& color)
{
    // 5x7の英字フォントの見た目の高さと送り幅に合わせた大きさにしている（日本語とASCIIが混ざっても、今あるHUDのパネルの中へ収めるため）。
    // その大きさを実際の画素に直したフォントで画像を作り、1ドットを1画素（キャンバス単位では1/scale）で描いている。
    const float canvasScale = GetCanvasScale();
    const int fontHeight = (std::max)(MinimumJapaneseFontHeight,
        static_cast<int>(std::lround(JapaneseFontReferencePixels * pixelSize * JapaneseFontScaleToAscii * canvasScale)));
    const RasterGlyph& glyph = GetJapaneseGlyphs().Get(character, fontHeight);
    const float glyphPixelSize = 1.0f / canvasScale;
    // 文字の位置を画素の境目にそろえ、ドットが2画素にまたがってにじまないようにしている。
    const float glyphX = std::round(x * canvasScale) / canvasScale +
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
    return static_cast<float>(glyph.advance) * glyphPixelSize;
}

// 英数字の1文字：小文字は大文字として扱い、5x7のドット絵の点を四角形で描いている
void Hud::AddAsciiCharacter(float x, float y, char character, float pixelSize, const Color& color)
{
    if (character >= 'a' && character <= 'z')
    {
        character = static_cast<char>(character - 'a' + 'A');
    }
    const std::array<std::uint8_t, AsciiGlyphRowCount> rows = GetAsciiGlyphRows(character);
    for (size_t row = 0; row < rows.size(); ++row)
    {
        for (int column = 0; column < AsciiGlyphColumnCount; ++column)
        {
            if ((rows[row] & (1u << (AsciiGlyphColumnCount - 1 - column))) != 0)
            {
                AddRectangle(
                    x + static_cast<float>(column) * pixelSize,
                    y + static_cast<float>(row) * pixelSize,
                    pixelSize,
                    pixelSize,
                    color);
            }
        }
    }
}
