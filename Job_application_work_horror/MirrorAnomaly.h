// ============================================================================
// ファイルの役割: 2面の「鏡」の異変に固有の、鏡に映った影を見たか・振り返ったか・見つけたかを管理している。
// 主な技術: 有限状態機械（鏡で見る → 振り返る）、鏡の面で反転した点への線と、鏡の四角との交差判定
// 影の表示（鏡のシェーダー mirrorPS が、鏡の中にだけ描く）と、音・照明の演出は Stage2Scene が担当している。
//
// ・廊下の左の壁に、古い鏡が掛かっている（どの周回にもある）。この異変の周回だけ、鏡をのぞくと、
//   自分のすぐ後ろに黒い人影が立っているのが映る。振り返ると、そこには誰もいない（見つけたことになる）。
// ・影は物として置かず、鏡の中にだけ描いている（Renderer::SetMirrorFigure）。鏡に映る景色は、鏡で反射した光線を
//   シェーダーの中で飛ばして求めているため、影も本物の位置（プレイヤーの後ろ）に立たせ、その光線と交差させている。
// ・ほかの異変（照らす・ライトを消す・見つめる・音を探す・光でたどる）と違い、「鏡で見て、振り返る」操作にしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

class MirrorAnomaly final
{
public:
    // 鏡の面（左の壁から廊下の側へ向いた面）の位置と大きさ。Stage2Layout の鏡の箱と合わせている
    static constexpr float SurfaceX = -39.3f;
    static constexpr float CenterY = -68.0f;
    static constexpr float CenterZ = 37.0f;
    static constexpr float HalfHeight = 14.0f;
    static constexpr float HalfWidth = 7.0f;
    // 影が鏡に映っているのを見続ける秒数、見てから振り返るまでの猶予の秒数
    static constexpr float RequiredSeenSeconds = 0.5f;
    static constexpr float TurnWindowSeconds = 8.0f;
    // 影の大きさ（プレイヤーと同じ背の高さにし、鏡の中で自分の顔のすぐ横に顔が並ぶようにしている）と、頭と胸の高さ
    static constexpr float FigureScaleXZ = 9.0f;
    static constexpr float FigureScaleY = 21.0f;
    static constexpr float FigureHeadY = -60.0f;
    static constexpr float FigureChestY = -73.0f;

    // Updateの結果（何も起きていない・鏡の中の影を見た・振り返って見つけた・振り返らずに離れた）
    enum class Event
    {
        None,
        Seen,
        Found,
        Lost
    };

private:
    // 影の立つ位置、影を見たか、見つけたか、鏡の中の影を見続けている秒数、振り返るまでの残り秒数、通知の残り秒数
    DirectX::SimpleMath::Vector3 m_FigurePosition = DirectX::SimpleMath::Vector3(16.0f, -99.0f, 33.0f);
    bool m_Seen = false;
    bool m_Found = false;
    float m_SeenSeconds = 0.0f;
    float m_TurnTimer = 0.0f;
    float m_NoticeTimer = 0.0f;

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        ResetProgressForLoop();
        m_NoticeTimer = 0.0f;
    }

    // 周回の始まりに、最初からにしている
    void ResetProgressForLoop() noexcept
    {
        m_Seen = false;
        m_Found = false;
        m_SeenSeconds = 0.0f;
        m_TurnTimer = 0.0f;
    }

    // 影の立つ位置：鏡に映したとき、プレイヤーのすぐ後ろ（鏡と反対の側）で、鏡に映る自分の肩の横から覗く位置
    static DirectX::SimpleMath::Vector3 FigurePositionBehind(const DirectX::SimpleMath::Vector3& playerPosition) noexcept
    {
        return DirectX::SimpleMath::Vector3(
            (std::min)(playerPosition.x + 16.0f, 34.0f), -99.0f, playerPosition.z - 11.5f);
    }

    // 目の位置eyeから見て、pointが鏡に映っているかを調べている。
    // pointを鏡の面で反転した点へ目から線を引き、その線が鏡の四角の中を通れば映っている（通る位置をmirrorHitに返す）。
    static bool IsReflectedInMirror(
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& point,
        DirectX::SimpleMath::Vector3& mirrorHit) noexcept
    {
        if (eye.x <= SurfaceX + 0.5f || point.x <= SurfaceX)
        {
            return false;
        }
        const DirectX::SimpleMath::Vector3 mirrored(SurfaceX * 2.0f - point.x, point.y, point.z);
        const float t = (eye.x - SurfaceX) / (eye.x - mirrored.x);
        mirrorHit = eye + (mirrored - eye) * t;
        // 縁の剥げた所に半分だけ映っても見えたことにしないよう、縁から1だけ内側で判定している
        return std::abs(mirrorHit.y - CenterY) <= HalfHeight - 1.0f &&
            std::abs(mirrorHit.z - CenterZ) <= HalfWidth - 1.0f;
    }

    // 1フレーム分進めている。activeはこの周回が鏡の異変か、eyeとforwardはカメラの位置と向き
    Event Update(
        float deltaTime,
        bool active,
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& forward,
        const DirectX::SimpleMath::Vector3& playerPosition) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
        if (!active || m_Found)
        {
            return Event::None;
        }

        if (!m_Seen)
        {
            // まだ見ていない間は、影をいつもプレイヤーのすぐ後ろに立たせている（直接は見えないので、ついて来ても気づかない）
            m_FigurePosition = FigurePositionBehind(playerPosition);
            // 鏡をのぞき込み（正面から内積0.94超、80未満の距離）、映った影の頭か胸が鏡の中に見えているか
            bool visible = false;
            for (const float height : { FigureHeadY, FigureChestY })
            {
                DirectX::SimpleMath::Vector3 point = m_FigurePosition;
                point.y = height;
                DirectX::SimpleMath::Vector3 hit;
                if (!IsReflectedInMirror(eye, point, hit))
                {
                    continue;
                }
                DirectX::SimpleMath::Vector3 toHit = hit - eye;
                const float distance = toHit.Length();
                if (distance > 0.001f && distance < 80.0f && forward.Dot(toHit / distance) > 0.94f)
                {
                    visible = true;
                    break;
                }
            }
            m_SeenSeconds = visible ? m_SeenSeconds + deltaTime : 0.0f;
            if (m_SeenSeconds < RequiredSeenSeconds)
            {
                return Event::None;
            }
            // 見た：影はその場に止まり、振り返るのを待つ
            m_Seen = true;
            m_TurnTimer = TurnWindowSeconds;
            return Event::Seen;
        }

        // 見た後：振り返って、影が立っていた所をおおよそ正面（水平の向きで内積0.70超）に見ると、そこには誰もいない（見つけた）。
        // 影はすぐ近くに立つため、上下の向きまで比べると、目の高さとの差で正面を向いていても外れてしまう。水平の向きだけで比べている
        DirectX::SimpleMath::Vector3 toFigure = m_FigurePosition - eye;
        toFigure.y = 0.0f;
        DirectX::SimpleMath::Vector3 flatForward = forward;
        flatForward.y = 0.0f;
        const float distance = toFigure.Length();
        const float forwardLength = flatForward.Length();
        if (distance > 0.001f && forwardLength > 0.001f &&
            (flatForward / forwardLength).Dot(toFigure / distance) > 0.70f)
        {
            m_Found = true;
            m_NoticeTimer = 2.6f;
            return Event::Found;
        }
        // 振り返らずに時間が過ぎるか、遠くへ離れたら、また鏡の中で後ろについて来る
        m_TurnTimer -= deltaTime;
        if (m_TurnTimer <= 0.0f || distance > 90.0f)
        {
            m_Seen = false;
            m_SeenSeconds = 0.0f;
            return Event::Lost;
        }
        return Event::None;
    }

    // 影の立つ位置、鏡の中に影を描くか、見つけたか、振り返るのを待っている途中か、通知の残り秒数
    DirectX::SimpleMath::Vector3 GetFigurePosition() const noexcept { return m_FigurePosition; }
    bool IsFigureVisible() const noexcept { return !m_Found; }
    bool WasFound() const noexcept { return m_Found; }
    bool IsWaitingForTurn() const noexcept { return m_Seen && !m_Found; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
