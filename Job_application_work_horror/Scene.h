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
