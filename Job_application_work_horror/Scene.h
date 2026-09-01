// ============================================================================
// ファイルの役割: 各画面が実装する初期化、更新、描画、終了処理の基底クラスです。
// ============================================================================

#pragma once

class Camera;

class Scene
{
public:
    Scene();
    virtual ~Scene();

    virtual void Update() = 0;
    virtual void Draw(Camera* camera) { (void)camera; }
};
