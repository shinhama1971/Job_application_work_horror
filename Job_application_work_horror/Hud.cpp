// ============================================================================
// ファイルの役割: 目的・操作のヒント・電池の残りなど、ゲーム画面に重ねるUI（HUD）を描いている。
// 主な技術: 四角形を並べた2D描画、画面の端を基準にした配置、状態に応じた表示
// ============================================================================

#include "Hud.h"
#include "HudText.h"

#include <algorithm>
#include <string>

#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;
using HudText::CountDisplayedCharacters;

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

    DrawObjective(objectiveText);
    DrawEvidenceCount();
    DrawReticle(hasInteractionTarget, reticleColor);
    DrawFuseSlots(fuseCount);
    DrawGauges(player);
    DrawInteractionPrompt(interactionPrompt, interactionLocked);
    DrawBatteryNotice(player);

    Flush();
}

void Hud::DrawObjective(std::string_view objectiveText)
{
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
}

void Hud::DrawEvidenceCount()
{
    const float screenWidth = GetCanvasWidth();

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
}

void Hud::DrawReticle(bool hasInteractionTarget, const Color& reticleColor)
{
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();

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
}

void Hud::DrawFuseSlots(int fuseCount)
{
    const float screenHeight = GetCanvasHeight();

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
}

void Hud::DrawGauges(const Player& player)
{
    const float screenHeight = GetCanvasHeight();

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
}

void Hud::DrawInteractionPrompt(std::string_view interactionPrompt, bool interactionLocked)
{
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();

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
}

void Hud::DrawBatteryNotice(const Player& player)
{
    const float screenWidth = GetCanvasWidth();

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
