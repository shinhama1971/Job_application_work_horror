// ============================================================================
// ファイルの役割: 各画面が実装する初期化、更新、描画、終了処理の基底クラスです。
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
    virtual bool TryGetDebugInfo(SceneDebugInfo&) const { return false; }
    virtual void RequestDebugAction(SceneDebugAction) {}
};
