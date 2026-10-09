// ============================================================================
// ファイルの役割: 各画面（タイトル・1面・2面・リザルト）が実装する、更新・描画などの基底クラス。
// 主な技術: 抽象基底クラス、仮想関数、シーンを切り替える設計（Stateパターンに近い形）
// ============================================================================

#pragma once

class Camera;

// デバッグ画面が具体的なSceneの型に依存せず、進行の状態を確かめるための読み取り専用の情報。
struct SceneDebugInfo
{
    // 2面の周回の段階、信号盤の段階、間違えた回数、足音の危険度、最後のイベントの準備ができたか、出口が開いたか
    int progressionStep = 0;
    int puzzleStep = 0;
    int puzzleMistakeCount = 0;
    float threatLevel = 0.0f;
    bool finalSequenceArmed = false;
    bool exitReady = false;
    // 1周目・2周目に探す異変の名前（2面のみ。文字列リテラルを指している）。
    const char* firstAnomaly = "";
    const char* secondAnomaly = "";
};

// デバッグ画面のボタンから頼む操作（周回を進める・最後の停電を再生する・照明の演出を再生する）
enum class SceneDebugAction
{
    AdvanceProgression,
    PlayFinalSequence,
    PlayLightingEvent
};

// シーンの基底クラス。Gameが今のシーンを1つだけunique_ptrで持ち、毎フレーム更新・描画している。
class Scene
{
public:
    Scene();
    virtual ~Scene();

    // 毎フレームの更新（Objectの更新より先に呼ばれる）
    virtual void Update() = 0;
    // HUDなど、3Dの本描画と画面効果の後に重ねる描画
    virtual void Draw(Camera* camera) { (void)camera; }
    // 本描画の前に、監視映像など画面以外への描画が必要なSceneだけが実装している。
    virtual void RenderOffscreen() {}
    // デバッグ画面に出す情報を返している（対応しているSceneだけtrue）
    virtual bool TryGetDebugInfo(SceneDebugInfo&) const { return false; }
    // デバッグ画面のボタンの操作を受け取っている（次のフレームの更新の始めに実行している）
    virtual void RequestDebugAction(SceneDebugAction) {}
};
