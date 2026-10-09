// ============================================================================
// ファイルの役割: 目的・操作のヒント・電池の残りなど、ゲーム画面に重ねるUI（HUD）を描いている。
// 主な技術: 四角形を並べた2D描画、ドット絵の英数字フォントとGDIで作る日本語の文字、画面の端を基準にした配置、状態に応じた表示
// ============================================================================

#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "Shader.h"
#include "VertexBuffer.h"

class Player;
class Camera;

// HUDの描画をまとめたクラス。各画面の描画関数が四角形の頂点をためて、Flushでまとめて1回で描いている。
class Hud
{
public:
    // シェーダーと、頂点をためるバッファを作っている
    void Init();
    // 遊んでいる間のHUD（目的・記録の数・照準・ヒューズ・電池・スタミナ・調べる操作・電池の通知）を描いている
    void Draw(
        const Player& player,
        int fuseCount,
        std::string_view interactionPrompt,
        std::string_view objectiveText);
    // タイトル画面を描いている（ベスト記録があれば表示している）
    void DrawTitle(
        float time,
        bool hasClearRecord,
        float bestClearTimeSeconds,
        int bestCaughtCount);
    // リザルト画面を描いている（revealAmountで項目を順に表示している）
    void DrawResult(
        float revealAmount,
        float clearTimeSeconds,
        int caughtCount,
        int anomaliesHandled,
        int puzzleMistakes,
        int chargersUsed,
        int evidenceCollected,
        int wallWritingsRead,
        bool hiddenRoomEscaped,
        std::string_view stage2Anomalies,
        bool newBestTime,
        bool newBestCaught);
    // 章の始まりに出す、章の名前と副題のカードを描いている
    void DrawChapterCard(
        std::string_view chapter,
        std::string_view subtitle,
        float elapsedSeconds);
    // 目的地の方向を示す矢印を描いている
    void DrawObjectiveGuide(
        const Camera& camera,
        const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& target);
    // 2面の右上の状態パネル（周回・危険度・信号盤の段階）を描いている
    void DrawStage2Status(
        int completedLoops,
        float threatRate,
        bool exitReady,
        int signalStep,
        bool signalActive);
    // 監視カメラの映像を見ている画面を描いている（映像・カメラの名前・巡回の進み具合・失敗の回数など）
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
    // 暗証番号の入力画面。wrongRateは不正解の直後に1から0へ下がり、枠を赤く点滅させている。
    void DrawKeypad(
        const std::array<int, 4>& entered,
        int cursor,
        float wrongRate,
        int mistakes);
    // ロッカーに隠れている間の視界。扉の横長のすき間以外を暗くし、出る操作を表示している。
    // dangerRateは影の近さ（0〜1）で、近いほどすき間の縁が赤くにじむ。
    void DrawHidingView(float elapsedSeconds, float dangerRate);
    // 息を殺して影をやり過ごす操作の進み具合を描いている
    void DrawQuietRecovery(float progressRate, float cooldown, bool success, bool tooClose);
    // ポーズメニューを描いている（設定の各段階・選んでいる項目・今の階・経過時間・捕まった回数）
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
    // まばたきのように画面全体を暗くしている
    void DrawBlink(float opacity);
    // ためた頂点を捨てている
    void Uninit();

    // 右上の電池の通知と「記録」を、この高さだけ下げて描いている。
    // 2面の状態パネル（DrawStage2Status）が右上にあるとき、重ならないように使っている。
    void SetTopRightOffset(float offset) { m_TopRightOffset = offset; }
    // 2面の右上に縦に並ぶ「状態パネル（高さ58）」と「危険ゲージ（高さ40）」に、すき間（8ずつ）を足した高さ。
    // 危険ゲージは出たり消えたりするため、出ていないときも場所を空けておき、下の表示が上下に動かないようにしている。
    static constexpr float Stage2StatusReservedHeight = 58.0f + 8.0f + 40.0f + 8.0f;

private:
    // 1回にためられる頂点の数の上限
    static constexpr size_t MaxVertices = 65536;

    // HUDの座標は「縦864の画面」を基準に、画素単位で決めてある。実際の描画解像度との比で
    // キャンバスの大きさを決め、画面の大きさが変わってもHUDが画面に対して同じ大きさに見えるようにしている。
    static constexpr float ReferenceHeight = 864.0f;
    // キャンバスの拡大率（描画解像度の高さ÷864）、キャンバスの幅（画面の縦横比に合わせて変わる）、高さ（常に864）
    static float GetCanvasScale();
    static float GetCanvasWidth();
    static float GetCanvasHeight();

    // 色付きの四角形を1つためている（キャンバス座標）
    void AddRectangle(float x, float y, float width, float height, const DirectX::SimpleMath::Color& color);
    // 操作ボタンの印として、「E」と「A」の形を四角形で組み立てている
    void AddLetterE(float x, float y, float size, const DirectX::SimpleMath::Color& color);
    void AddLetterA(float x, float y, float size, const DirectX::SimpleMath::Color& color);
    // 文字列をためている。英数字は5x7のドット絵、日本語はGDIで作った文字の画像を使っている
    void AddText(
        float x,
        float y,
        std::string_view text,
        float pixelSize,
        const DirectX::SimpleMath::Color& color);
    // ためた頂点をGPUへ送り、まとめて描いている
    void Flush();
    // テクスチャ（監視カメラの映像など）を四角形に貼ってすぐに描いている
    void DrawTextureRectangle(
        ID3D11ShaderResourceView* texture,
        Shader& textureShader,
        float x,
        float y,
        float width,
        float height);

    // HUD用のシェーダー、頂点バッファ、ためている頂点、右上の表示を下げる高さ
    Shader m_Shader;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    std::vector<VERTEX_3D> m_Vertices;
    float m_TopRightOffset = 0.0f;
};
