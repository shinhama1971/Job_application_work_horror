// ============================================================================
// ファイルの役割: 1面の「懐中電灯で照らすと浮かぶ壁の文字」の進行を管理します。
// 主な技術: 視線による既読判定、見ていない間に書き換える演出（P.T.型）、状態の分離
// 文字の見た目は FlashlightWriting（Object）、ここでは「読んだか・いつ書き換えるか」だけを扱います。
//
// ・どの文字も、ライトの中心で少しの間照らすと「読んだ」ことになります。
// ・ループ廊下の「ふりかえるな」は、読んだ後に目を離すと「ふりかえったな」に書き換わります。
//   書き換わった文字を読んだ瞬間を Update の戻り値で知らせ、Sceneが物音の演出を起こします。
// ・電力が戻ると、すべての文字が消えていきます（「でんきが つくと みえなくなる」）。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include <array>

class FlashlightWriting;

class Stage1WallWritings final
{
public:
    enum Index
    {
        Warning,    // あかりを けすな（開始地点の奥の壁）
        Three,      // みっつ もどせば でられる（左の倉庫）
        Power,      // でんきが つくと みえなくなる（配電盤の近く）
        Turn,       // ふりかえるな → ふりかえったな（ループ廊下の突き当たり）
        Count
    };
    using Writings = std::array<FlashlightWriting*, Count>;

    void Init(const Writings& writings);

    // 戻り値は、書き換わった文字を初めて読んだフレームだけtrueです。
    bool Update(
        float deltaTime,
        const DirectX::SimpleMath::Vector3& cameraPosition,
        const DirectX::SimpleMath::Vector3& cameraForward,
        bool flashlightOn,
        bool powerRestored);

    int GetReadCount() const;
    bool HasTurnChanged() const { return m_TurnChanged; }

private:
    // ライトを当てて、この秒数だけ読み続けると既読になります（一瞬なでただけでは読んだことにしません）。
    static constexpr float ReadHoldSeconds = 0.45f;
    // 書き換えは、文字から目を離しているときだけ行います（視線との内積・距離）。
    static constexpr float LookAwayFacing = 0.55f;
    static constexpr float LookAwayDistance = 112.0f;

    Writings m_Writings{};
    std::array<float, Count> m_ReadTimers{};
    std::array<bool, Count> m_Read{};
    bool m_TurnChanged = false;
    bool m_TurnChangedRead = false;
    float m_TurnChangedReadTimer = 0.0f;
    bool m_FadedByPower = false;
};
