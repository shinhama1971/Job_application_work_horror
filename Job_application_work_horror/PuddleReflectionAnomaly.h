// ============================================================================
// ファイルの役割: 2面の「水たまり」の異変に固有の、水面に映った人影を見たか・顔を上げたか・見つけたかを管理している。
// 主な技術: 有限状態機械（水面で見る → 顔を上げる）、水面で上下に反転した点への線と、水たまりとの交差判定
// 人影の表示（床のシェーダーのレイ反射で、反射の光線と人影の形を交差させる）と、音・照明の演出は Stage2Scene が担当している。
//
// ・廊下の中央の大きな水たまり（どの周回にもある）。この異変の周回だけ、足元の水たまりをのぞくと、
//   水たまりの向こう側に黒い人影が立っているのが、水面にだけ逆さまに映る。顔を上げると、そこには誰もいない（見つけたことになる）。
// ・人影は物として置かず、床のシェーダーのレイ反射（shader/wetFloorPS.hlsl の TracePuddleFigure）だけで描いている。
//   反射の光線が人影の形（脚・胴・頭のカプセル）のそばを通った所を黒く沈めるので、カメラをもう1台使わずに、水面にだけ映せる。
// ・水面をのぞき続けると、人影は水を踏みながら一歩ずつ近づいて来る（水たまりに波紋が広がり、水音が鳴る）。
//   すぐそばまで来られると、驚かされて最初からやり直しになる。
// ・鏡の異変（鏡で見て振り返る）と違い、「下を見て、顔を上げる」操作にしている。
// ・2周目に出たときは慣れさせないよう、人影は水たまりの手前寄り（すぐ近く）に立ち、速い間隔で近づいて来る（SetBoldness）。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

class PuddleReflectionAnomaly final
{
public:
    // 廊下の中央の大きな水たまりの中心（Stage2Layout の水たまりのマス目 (0, 0) と同じ）と、人影を立たせる奥行きのずれ
    // （1周目は水たまりの向こう側、2周目は手前寄りに立たせる）
    static constexpr float PuddleCenterX = 9.0f;
    static constexpr float PuddleCenterZ = 9.0f;
    static constexpr float FigureDepthOffset = 13.0f;
    static constexpr float BoldFigureDepthOffset = 2.0f;
    // 床（水面）の高さ。人影の足もとの高さにもしている
    static constexpr float FloorY = -100.0f;
    // 人影を見たことにする、映った頭か胸の高さ（足もとからの高さ。シェーダーの人影の形と合わせている）
    static constexpr float FigureHeadHeight = 36.0f;
    static constexpr float FigureChestHeight = 24.0f;
    // 水面に映った人影を見続ける秒数、見てから顔を上げるまでの猶予の秒数
    static constexpr float RequiredSeenSeconds = 0.5f;
    static constexpr float LookUpWindowSeconds = 9.0f;
    // 水面をのぞき続けている間に近づく一歩の長さと間隔（1周目・2周目）、ここまで近づかれたら驚かされる距離
    static constexpr float StepLength = 4.5f;
    static constexpr float StepSeconds = 0.85f;
    static constexpr float BoldStepSeconds = 0.55f;
    static constexpr float CatchDistance = 10.0f;

    // Updateの結果（何も起きていない・水面の人影を見た・一歩近づいた・顔を上げて見つけた・すぐそばまで来られた・見失った）
    enum class Event
    {
        None,
        Seen,
        Step,
        Found,
        Caught,
        Lost
    };

private:
    // 人影の立つ足もとの位置、見たか、見つけたか、水面の人影を見続けている秒数、顔を上げるまでの残り秒数、
    // 次の一歩までの秒数、通知の残り秒数、シェーダーへ渡す見え方（0〜1。現れるときと消えるときに滑らかに変えている）
    DirectX::SimpleMath::Vector3 m_FigurePosition = DirectX::SimpleMath::Vector3(PuddleCenterX, FloorY, PuddleCenterZ);
    bool m_Seen = false;
    bool m_Found = false;
    float m_SeenSeconds = 0.0f;
    float m_LookUpTimer = 0.0f;
    float m_StepTimer = 0.0f;
    float m_NoticeTimer = 0.0f;
    float m_Visibility = 0.0f;
    // 出方の大胆さ（0 = 1周目の出方、1 = 2周目の出方）
    float m_Boldness = 0.0f;

    // 足もとをのぞき込んでいるか（下へ向けた度合い）。顔を上げたかの判定にも使っている
    static bool IsLookingDown(const DirectX::SimpleMath::Vector3& forward) noexcept
    {
        return forward.y < -0.42f;
    }

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        ResetProgressForLoop();
        m_NoticeTimer = 0.0f;
        m_Visibility = 0.0f;
        m_Boldness = 0.0f;
    }

    // 周回の始まりに、最初からにしている
    void ResetProgressForLoop() noexcept
    {
        m_Seen = false;
        m_Found = false;
        m_SeenSeconds = 0.0f;
        m_LookUpTimer = 0.0f;
        m_StepTimer = 0.0f;
    }

    // 出方の大胆さ（0〜1）を決めている。Stage2Scene が周回の始まりに、何周目かに合わせて設定している
    void SetBoldness(float boldness) noexcept { m_Boldness = (std::clamp)(boldness, 0.0f, 1.0f); }

    // 人影の立つ位置：プレイヤーから見て、中央の水たまりの向こう側（奥行きは水たまりの中心を挟んだ反対側）。
    // 大胆な出方では、水たまりの手前寄り（プレイヤーのすぐ近く）に立たせている
    static DirectX::SimpleMath::Vector3 FigurePositionAcross(
        const DirectX::SimpleMath::Vector3& playerPosition, float boldness = 0.0f) noexcept
    {
        const float side = playerPosition.z < PuddleCenterZ ? 1.0f : -1.0f;
        const float depth = FigureDepthOffset + (BoldFigureDepthOffset - FigureDepthOffset) * boldness;
        return DirectX::SimpleMath::Vector3(
            (std::clamp)(playerPosition.x, -12.0f, 30.0f), FloorY, PuddleCenterZ + depth * side);
    }

    // 目の位置eyeから見て、pointが水面に映っているかを調べている。
    // pointを水面で上下に反転した点へ目から線を引き、その線が水面と交わる点（puddleHit）が水たまりの中なら映っている
    template<typename PuddleTest>
    static bool IsReflectedInPuddle(
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& point,
        const PuddleTest& isInsidePuddle,
        DirectX::SimpleMath::Vector3& puddleHit)
    {
        if (eye.y <= FloorY + 0.5f || point.y <= FloorY)
        {
            return false;
        }
        const DirectX::SimpleMath::Vector3 mirrored(point.x, FloorY * 2.0f - point.y, point.z);
        const float t = (eye.y - FloorY) / (eye.y - mirrored.y);
        puddleHit = eye + (mirrored - eye) * t;
        return isInsidePuddle(puddleHit);
    }

    // 1フレーム分進めている。activeはこの周回が水たまりの異変か、eyeとforwardはカメラの位置と向き。
    // isInsidePuddle は、床の点が水たまりの中かを返す関数（Ground::IsInsidePuddle）
    template<typename PuddleTest>
    Event Update(
        float deltaTime,
        bool active,
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& forward,
        const DirectX::SimpleMath::Vector3& playerPosition,
        const PuddleTest& isInsidePuddle)
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
        // 見え方は、映っている間は0.6秒で現れ、見つけた後や異変でない周回は0.25秒で消している
        const bool shown = active && !m_Found;
        m_Visibility = shown
            ? (std::min)(1.0f, m_Visibility + deltaTime / 0.6f)
            : (std::max)(0.0f, m_Visibility - deltaTime / 0.25f);
        if (!shown)
        {
            return Event::None;
        }

        if (!m_Seen)
        {
            // まだ見ていない間は、目を向けていないときだけ、人影を水たまりの向こう側へ立たせ直している
            // （のぞいている最中に位置が滑らないように、見続けている秒数が0のときだけ動かしている）
            if (m_SeenSeconds <= 0.0f)
            {
                m_FigurePosition = FigurePositionAcross(playerPosition, m_Boldness);
            }
            // 足もとをのぞき込み（下へ向け、正面から内積0.90超、70未満の距離）、映った人影の頭か胸が水たまりの中に見えているか
            bool visible = false;
            if (IsLookingDown(forward))
            {
                for (const float height : { FigureHeadHeight, FigureChestHeight })
                {
                    DirectX::SimpleMath::Vector3 point = m_FigurePosition;
                    point.y = FloorY + height;
                    DirectX::SimpleMath::Vector3 hit;
                    if (!IsReflectedInPuddle(eye, point, isInsidePuddle, hit))
                    {
                        continue;
                    }
                    const DirectX::SimpleMath::Vector3 toHit = hit - eye;
                    const float distance = toHit.Length();
                    if (distance > 0.001f && distance < 70.0f && forward.Dot(toHit / distance) > 0.90f)
                    {
                        visible = true;
                        break;
                    }
                }
            }
            m_SeenSeconds = visible ? m_SeenSeconds + deltaTime : 0.0f;
            if (m_SeenSeconds < RequiredSeenSeconds)
            {
                return Event::None;
            }
            // 見た：人影は顔を上げるのを待ち、のぞき続けると近づいて来る
            m_Seen = true;
            m_LookUpTimer = LookUpWindowSeconds;
            m_StepTimer = GetStepSeconds() * 1.6f;
            return Event::Seen;
        }

        // 見た後：顔を上げて、人影が立っていた方をおおよそ正面（水平の向きで内積0.60超）に見ると、そこには誰もいない（見つけた）
        DirectX::SimpleMath::Vector3 toFigure = m_FigurePosition - playerPosition;
        toFigure.y = 0.0f;
        const float distance = toFigure.Length();
        if (!IsLookingDown(forward))
        {
            DirectX::SimpleMath::Vector3 flatForward = forward;
            flatForward.y = 0.0f;
            const float forwardLength = flatForward.Length();
            if (distance > 0.001f && forwardLength > 0.001f &&
                (flatForward / forwardLength).Dot(toFigure / distance) > 0.60f)
            {
                m_Found = true;
                m_NoticeTimer = 2.6f;
                return Event::Found;
            }
        }
        else
        {
            // 水面をのぞき続けている間は、水を踏みながら一歩ずつ近づいて来る
            m_StepTimer -= deltaTime;
            if (m_StepTimer <= 0.0f && distance > 0.001f)
            {
                m_StepTimer = GetStepSeconds();
                const float step = (std::min)(StepLength, distance);
                m_FigurePosition -= toFigure / distance * step;
                if (distance - step <= CatchDistance)
                {
                    // すぐそばまで来られた：驚かされて、最初からやり直し
                    m_Seen = false;
                    m_SeenSeconds = 0.0f;
                    return Event::Caught;
                }
                return Event::Step;
            }
        }
        // 顔を上げずに時間が過ぎるか、遠くへ離れたら、また水たまりの向こうに立ち直す
        m_LookUpTimer -= deltaTime;
        if (m_LookUpTimer <= 0.0f || distance > 80.0f)
        {
            m_Seen = false;
            m_SeenSeconds = 0.0f;
            return Event::Lost;
        }
        return Event::None;
    }

    // 一歩近づく間隔（大胆な出方ほど短い）
    float GetStepSeconds() const noexcept { return StepSeconds + (BoldStepSeconds - StepSeconds) * m_Boldness; }
    // 人影の足もとの位置、シェーダーへ渡す見え方（0〜1）、見つけたか、顔を上げるのを待っている途中か、通知の残り秒数
    DirectX::SimpleMath::Vector3 GetFigurePosition() const noexcept { return m_FigurePosition; }
    float GetVisibility() const noexcept { return m_Visibility; }
    bool WasFound() const noexcept { return m_Found; }
    bool IsWaitingForLookUp() const noexcept { return m_Seen && !m_Found; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
