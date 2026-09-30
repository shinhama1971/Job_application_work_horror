// ============================================================================
// ファイルの役割: 各画面が実装する初期化、更新、描画、終了処理の基底クラスです。
// 主な技術: 抽象基底クラス、仮想関数、シーンパターン
// ============================================================================

#pragma once

class Camera;

// デバッグUIが具体的なScene型へ依存せずに進行状態を確認するための読み取り専用情報です。
struct SceneDebugInfo
{
    int progressionStep = 0;
    int puzzleStep = 0;
    int puzzleMistakeCount = 0;
    float threatLevel = 0.0f;
    bool finalSequenceArmed = false;
    bool exitReady = false;
    // 1周目・2周目に探す異変の名前（2面のみ。文字列リテラルを指します）。
    const char* firstAnomaly = "";
    const char* secondAnomaly = "";
};

enum class SceneDebugAction
{
    AdvanceProgression,
    PlayFinalSequence,
    PlayLightingEvent
};

class Scene
{
public:
    Scene();
    virtual ~Scene();

    virtual void Update() = 0;
    virtual void Draw(Camera* camera) { (void)camera; }
    // 本描画の前に、監視映像などのオフスクリーン描画が必要なSceneだけが実装します。
    virtual void RenderOffscreen() {}
    virtual bool TryGetDebugInfo(SceneDebugInfo&) const { return false; }
    virtual void RequestDebugAction(SceneDebugAction) {}
};
