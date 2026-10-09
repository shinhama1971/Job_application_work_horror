// ============================================================================
// ファイルの役割: 1面の「懐中電灯で照らすと浮かぶ壁の文字」の進み具合を管理している。
// 主な技術: 視線による読んだかの判定、見ていない間に書き換える演出（P.T.のような手法）、見た目と状態の分離
// 文字の見た目は FlashlightWriting（Object）が担当し、ここでは「読んだか・いつ書き換えるか」だけを扱っている。
//
// ・どの文字も、ライトの中心で少しの間照らすと「読んだ」ことになる。
// ・ループ廊下の「ふりかえるな」は、読んだ後に目を離すと「ふりかえったな」に書き換わる。
//   書き換わった文字を読んだ瞬間を Update の戻り値で知らせ、Sceneが物音の演出を起こしている。
// ・電力が戻ると、すべての文字が消えていく（「でんきが つくと みえなくなる」）。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include <array>

class FlashlightWriting;

class Stage1WallWritings final
{
public:
    // 壁の文字の番号（読んだ数を数える対象）
    enum Index
    {
        Warning,    // あかりを けすな（開始地点の奥の壁）
        Three,      // みっつ もどせば でられる（左の倉庫）
        Power,      // でんきが つくと みえなくなる（配電盤の近く）
        Turn,       // ふりかえるな → ふりかえったな（ループ廊下の突き当たり）
        // 文字の数
        Count
    };
    // 各文字のObjectの並び
    using Writings = std::array<FlashlightWriting*, Count>;

    // 文字のObjectを受け取り、どれも読んでいない状態から始めている
    void Init(const Writings& writings);

    // 戻り値は、書き換わった文字を初めて読んだフレームだけtrue。
    bool Update(
        float deltaTime,
        const DirectX::SimpleMath::Vector3& cameraPosition,
        const DirectX::SimpleMath::Vector3& cameraForward,
        bool flashlightOn,
        bool powerRestored);

    // 読んだ文字の数、ループ廊下の文字が書き換わったかを返している
    int GetReadCount() const;
    bool HasTurnChanged() const { return m_TurnChanged; }

private:
    // ライトを当てて、この秒数だけ読み続けると読んだことにしている（一瞬なでただけでは読んだことにしない）。
    static constexpr float ReadHoldSeconds = 0.45f;
    // 書き換えは、文字から目を離しているときだけ行っている（視線との内積・距離）。
    static constexpr float LookAwayFacing = 0.55f;
    static constexpr float LookAwayDistance = 112.0f;

    // 文字のObject、各文字を照らし続けた秒数、読んだか
    Writings m_Writings{};
    std::array<float, Count> m_ReadTimers{};
    std::array<bool, Count> m_Read{};
    // 書き換わったか、書き換わった文字を読んだか、それを照らし続けた秒数、電力が戻って消し始めたか
    bool m_TurnChanged = false;
    bool m_TurnChangedRead = false;
    float m_TurnChangedReadTimer = 0.0f;
    bool m_FadedByPower = false;
};
