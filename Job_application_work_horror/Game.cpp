#include "Game.h"
#include "Renderer.h"
#include "Input.h"
#include "Scene.h"
#include "TitleScene.h"
#include "StageScene.h"
#include "Stage2Scene.h"
#include "ResultScene.h"
#include "Ground.h"
#include "Texture2D.h"
#include "ScreenDustOverlay.h"
#include "Shader.h"

#include "DebugUI.h"
#include "Application.h"

namespace Core
{
    std::unique_ptr<Game> Game::m_Instance;

    Game::Game()
    {
    }

    Game::~Game()
    {
        DeleteAllObject();
    }

    void Game::Init()
    {
        if (m_Instance)
        {
            return;
        }

        m_Instance = std::make_unique<Game>();

        Renderer::Init();
        Debug::UI::Init(Application::GetWindow());


        Input::Create();

        m_Instance->m_Camera.Init();

        m_Instance->m_PlanarReflection.Init();
        m_Instance->m_ShadowMap.Init();
        m_Instance->m_PostProcess.Init();
        m_Instance->ChangeScene(SceneName::Title);
    }

    void Game::Update()
    {
        Input::Update();
        if (Debug::UI::ShouldPauseGameplay())
        {
            Debug::UI::ApplyTuning(m_Instance->m_PostProcess);
            m_Instance->m_PostProcess.Update();
            return;
        }


        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Update();
        }

        Debug::UI::ApplyTuning(m_Instance->m_PostProcess);

        m_Instance->m_Camera.Update();
        m_Instance->m_PostProcess.Update();


        for (auto& o : m_Instance->m_Objects)
        {
            if (!o->IsDestroy())
            {
                o->Update();
            }
        }

        std::erase_if(
            m_Instance->m_Objects,
            [](const std::unique_ptr<Object>& o)
            {
                if (o->IsDestroy())
                {
                    o->Uninit();
                    return true;
                }

                return false;
            }
        );

        if (m_Instance->m_PendingScene.has_value())
        {
            m_Instance->m_PendingObjectCommands.clear();

            const SceneName nextScene =
                m_Instance->m_PendingScene.value();

            m_Instance->m_PendingScene.reset();
            m_Instance->ChangeScene(nextScene);
            return;
        }

        auto pendingCommands =
            std::move(m_Instance->m_PendingObjectCommands);

        m_Instance->m_PendingObjectCommands.clear();

        for (auto& command : pendingCommands)
        {
            command();
        }
    }

    void Game::Draw()
    {
        Debug::UI::BeginFrame();
        m_Instance->m_ShadowMap.Begin(m_Instance->m_Camera);
        for (auto& o : m_Instance->m_Objects)
        {
            if (!o->IsDestroy())
            {
                o->DrawShadow();
            }
        }
        m_Instance->m_ShadowMap.End();

        if (m_Instance->m_CurrentScene == SceneName::Stage)
        {
            // A puddle reflection is naturally soft, so updating it at 30 Hz
            // is difficult to notice while removing half of the extra scene
            // passes. The main view and input still update at 60 Hz.
            const unsigned int reflectionInterval =
                Debug::UI::GetReflectionUpdateInterval();
            const bool updateReflection =
                (m_Instance->m_ReflectionFrameIndex++ %
                    reflectionInterval) == 0u;
            if (updateReflection)
            {
                m_Instance->m_PlanarReflection.Begin(
                    m_Instance->m_Camera,
                    -99.5f);

                for (auto& o : m_Instance->m_Objects)
                {
                    if (o->IsDestroy() ||
                        dynamic_cast<Ground*>(o.get()) != nullptr ||
                        dynamic_cast<Texture2D*>(o.get()) != nullptr ||
                        dynamic_cast<ScreenDustOverlay*>(o.get()) != nullptr)
                    {
                        continue;
                    }

                    o->Draw(&m_Instance->m_Camera);
                }

                m_Instance->m_PlanarReflection.End(
                    m_Instance->m_Camera);
            }
            else
            {
                m_Instance->m_PlanarReflection.Bind();
            }
        }

        Renderer::DrawStart();

        for (auto& o : m_Instance->m_Objects)
        {
            if (!o->IsDestroy())
            {
                o->Draw(&m_Instance->m_Camera);
            }
        }

        m_Instance->m_PostProcess.CaptureBackBuffer();
        m_Instance->m_PostProcess.Draw();

        // Draw HUD and scene overlays after bloom so text stays sharp.
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Draw(&m_Instance->m_Camera);
        }
        Debug::UI::Draw(m_Instance->m_PostProcess);


        Renderer::DrawEnd();
    }
    void Game::Uninit()
    {
        if (m_Instance == nullptr) return;

        m_Instance->m_Camera.Uninit();

        m_Instance->m_Scene.reset();

        for (auto& o : m_Instance->m_Objects)
        {
            o->Uninit();
        }

        m_Instance->m_Objects.clear();
        m_Instance->m_NamedObjects.clear();
        m_Instance->m_PostProcess.Uninit();
        m_Instance->m_ShadowMap.Uninit();
        m_Instance->m_PlanarReflection.Uninit();

        Input::Release();
        Debug::UI::Uninit();

        // Release cached and member-owned GPU objects while the D3D device is
        // still valid. Renderer::Uninit can then report genuine leaks instead
        // of resources retained by the still-alive Game singleton.
        Shader::ClearCache();
        m_Instance.reset();

        Renderer::Uninit();
    }

    Game* Core::Game::GetInstance()
    {
        return m_Instance.get();
    }

    void Game::RequestSceneChange(SceneName sName)
    {
        if (m_PendingScene.has_value())
        {
            return;
        }

        m_PendingScene = sName;
    }

    void Game::ChangeScene(SceneName sName)
    {
        m_CurrentScene = sName;
        m_Scene.reset();

        DeleteAllObject();

        switch (sName)
        {
        case SceneName::Title:
            m_Scene = std::make_unique<TitleScene>();
            break;

            m_ReflectionFrameIndex = 0;
        case SceneName::Stage:
            m_ItemCount = 0;
            m_PowerRestored = false;
            m_Scene = std::make_unique<StageScene>();
            break;

        case SceneName::Stage2:
            m_ItemCount = 3;
            m_PowerRestored = true;
            m_Scene = std::make_unique<Stage2Scene>();
            break;

        case SceneName::Result:
            m_Scene = std::make_unique<ResultScene>();
            break;
        }
    }

    void Game::DeleteObject(Object* pt)
    {
        if (pt == nullptr) return;

        pt->Destroy();

        for (auto it = m_NamedObjects.begin(); it != m_NamedObjects.end();)
        {
            if (it->second == pt)
            {
                it = m_NamedObjects.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Game::DestroyObj(const std::string& name)
    {
        auto it = m_NamedObjects.find(name);

        if (it == m_NamedObjects.end())
        {
            return;
        }

        if (it->second != nullptr)
        {
            it->second->Destroy();
        }

        m_NamedObjects.erase(it);
    }

    void Game::DeleteAllObject()
    {
        for (auto& o : m_Objects)
        {
            o->Uninit();
        }

        m_Objects.clear();
        m_Objects.shrink_to_fit();

        m_NamedObjects.clear();
    }
}
