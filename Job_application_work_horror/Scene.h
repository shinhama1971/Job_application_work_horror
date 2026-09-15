// ============================================================================
// ファイルの役割: 各画面が実装する初期化、更新、描画、終了処理の基底クラスです。
// 主な技術: 抽象基底クラス、仮想関数、シーンパターン
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
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
