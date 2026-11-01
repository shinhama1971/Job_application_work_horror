// ============================================================================
// ファイルの役割: 1面の西棟（浸水した広間）で一度だけ起きる「水面にだけ映る、天井の人影」の演出の状態を管理している。
// 主な技術: 有限状態機械（待つ → 水面に映っている → 見た → 見上げると消える）、水面で上下に反転した点への線と水面の交差判定
// 人影の表示（床のシェーダーのレイ反射で、逆さまに吊り下がったカプセルと反射の光線を交差させる）と、音・照明の演出は StageScene が担当している。
//
// ・西棟に入ってしばらくすると、プレイヤーの少し先の天井に、黒い人影が逆さまに張り付く。目では見えず、
//   足元の水面をのぞいたときだけ、水面に映った天井にその人影が見える（2面の「水たまり」の異変と同じ描き方を、上下逆にしている）。
// ・水面の人影を見た後に見上げると、天井には何もいない。その真下の水面に滴が落ちて波紋が広がる。1回のプレイで一度だけ起きる。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

class CeilingReflectionScare final
{
public:
    // 天井の下面の高さ（人影の足を付けている所）と、水面の高さ
    static constexpr float CeilingY = -49.0f;
    static constexpr float WaterY = -100.0f;
    // 人影の胸と頭の高さ（天井から下へ。シェーダーの人影の形と合わせている）
    static constexpr float ChestDrop = 24.0f;
    static constexpr float HeadDrop = 36.0f;
    // 西棟に入ってから人影が現れるまでの秒数、プレイヤーの前のどれだけ先に張り付くか
    static constexpr float ArmSeconds = 20.0f;
    static constexpr float AheadDistance = 28.0f;
    // 水面の人影を見続ける秒数、見た後に見上げるのを待つ秒数（過ぎたら黙って消える）
    static constexpr float RequiredSeenSeconds = 0.5f;
    static constexpr float LookUpWindowSeconds = 7.0f;

    // Updateの結果（何も起きていない・水面の人影を見た・見上げると消えていた）
    enum class Event
    {
        None,
        Seen,
        Vanished
    };

private:
    enum class State
    {
        Waiting,    // 西棟で過ごした時間を数えている
        Reflected,  // 天井に張り付き、水面にだけ映っている
        Seen,       // 水面の人影を見た（見上げるのを待つ）
        Done        // 消えた（このプレイではもう起きない）
    };
    State m_State = State::Waiting;
    // 人影の足を付けた天井の位置、西棟で過ごした秒数、水面の人影を見続けている秒数、見上げるのを待つ残り秒数、見え方（0〜1）
    DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, CeilingY, 0.0f);
    float m_InsideSeconds = 0.0f;
    float m_SeenSeconds = 0.0f;
    float m_LookUpTimer = 0.0f;
    float m_Visibility = 0.0f;

public:
    // 最初の状態に戻している
    void Reset() noexcept
    {
        m_State = State::Waiting;
        m_InsideSeconds = 0.0f;
        m_SeenSeconds = 0.0f;
        m_LookUpTimer = 0.0f;
        m_Visibility = 0.0f;
    }

    // すぐに天井へ張り付かせている（デバッグ画面と自動撮影で、試したいときに使っている）
    void ForceArm() noexcept
    {
        if (m_State == State::Waiting || m_State == State::Done)
        {
            m_State = State::Reflected;
            m_SeenSeconds = 0.0f;
        }
    }

    // 1フレーム分進めている。inside は西棟の中にいるか、minimum・maximum は人影を置いてよい範囲（西棟の壁の内側）、
    // isInWater は床の点が水の中かを返す関数（水の引いた所では水面に映らないため、見たことにしない）
    template<typename WaterTest>
    Event Update(
        float deltaTime,
        bool inside,
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& forward,
        const DirectX::SimpleMath::Vector2& minimum,
        const DirectX::SimpleMath::Vector2& maximum,
        const WaterTest& isInWater)
    {
        const bool shown = m_State == State::Reflected || m_State == State::Seen;
        m_Visibility = shown
            ? (std::min)(1.0f, m_Visibility + deltaTime / 0.8f)
            : (std::max)(0.0f, m_Visibility - deltaTime / 0.2f);

        if (m_State == State::Waiting)
        {
            m_InsideSeconds += inside ? deltaTime : 0.0f;
            if (m_InsideSeconds >= ArmSeconds)
            {
                m_State = State::Reflected;
            }
            return Event::None;
        }
        if (m_State == State::Done)
        {
            return Event::None;
        }
        // 西棟の外へ出たら、黙って消えている
        if (!inside)
        {
            m_State = State::Done;
            return Event::None;
        }

        const bool lookingDown = forward.y < -0.42f;
        if (m_State == State::Reflected)
        {
            // 見ていない間は、プレイヤーの少し先の天井へ張り付き直している（のぞいている最中は動かさない）
            if (m_SeenSeconds <= 0.0f)
            {
                DirectX::SimpleMath::Vector3 flat(forward.x, 0.0f, forward.z);
                if (flat.LengthSquared() > 1e-4f)
                {
                    flat.Normalize();
                }
                const DirectX::SimpleMath::Vector3 ahead = eye + flat * AheadDistance;
                m_Position = DirectX::SimpleMath::Vector3(
                    (std::clamp)(ahead.x, minimum.x + 10.0f, maximum.x - 10.0f), CeilingY,
                    (std::clamp)(ahead.z, minimum.y + 10.0f, maximum.y - 10.0f));
            }
            // 足元をのぞき込み、水面に映った人影の胸か頭が、視線の中心の近くに見えているか
            bool visible = false;
            if (lookingDown)
            {
                for (const float drop : { ChestDrop, HeadDrop })
                {
                    const DirectX::SimpleMath::Vector3 point(m_Position.x, CeilingY - drop, m_Position.z);
                    const DirectX::SimpleMath::Vector3 mirrored(point.x, WaterY * 2.0f - point.y, point.z);
                    const float t = (eye.y - WaterY) / (eye.y - mirrored.y);
                    const DirectX::SimpleMath::Vector3 waterHit = eye + (mirrored - eye) * t;
                    const DirectX::SimpleMath::Vector3 toHit = waterHit - eye;
                    const float distance = toHit.Length();
                    if (isInWater(waterHit) && distance > 0.001f && distance < 90.0f &&
                        forward.Dot(toHit / distance) > 0.88f)
                    {
                        visible = true;
                        break;
                    }
                }
            }
            m_SeenSeconds = visible ? m_SeenSeconds + deltaTime : 0.0f;
            if (m_SeenSeconds >= RequiredSeenSeconds)
            {
                m_State = State::Seen;
                m_LookUpTimer = LookUpWindowSeconds;
                return Event::Seen;
            }
            return Event::None;
        }

        // 見た後：見上げる（上を向く）と、天井には何もいない
        if (forward.y > 0.15f)
        {
            m_State = State::Done;
            return Event::Vanished;
        }
        m_LookUpTimer -= deltaTime;
        if (m_LookUpTimer <= 0.0f)
        {
            m_State = State::Done;
        }
        return Event::None;
    }

    // 人影の足を付けた天井の位置、シェーダーへ渡す見え方（0〜1）
    DirectX::SimpleMath::Vector3 GetPosition() const noexcept { return m_Position; }
    float GetVisibility() const noexcept { return m_Visibility; }
};
