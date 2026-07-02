#include "Game.h"
#include "Renderer.h"
#include "Input.h"
#include "Scene.h"
#include "TitleScene.h"
#include "StageScene.h"
#include "ResultScene.h"


namespace Core
{
    Game* Game::m_Instance = nullptr;

    Game::Game()
    {
        m_Scene = nullptr;
    }

    Game::~Game()
    {
        delete m_Scene;
        m_Scene = nullptr;

        DeleteAllObject();
    }

    void Game::Init()
    {
        m_Instance = new Game;

        Renderer::Init();

        Input::Create();

        m_Instance->m_Camera.Init();

        m_Instance->m_Scene = new StageScene;
        m_Instance->m_PostProcess.Init();
    }

    void Game::Update()
    {
        Input::Update();

        if (m_Instance->m_Scene != nullptr)
        {
            m_Instance->m_Scene->Update();
        }

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
    }

    void Game::Draw()
    {
        Renderer::DrawStart();

        for (auto& o : m_Instance->m_Objects)
        {
            if (!o->IsDestroy())
            {
                o->Draw(&m_Instance->m_Camera);
            }
        }

        Renderer::DrawEnd();
    }
    void Game::Uninit()
    {
        if (m_Instance == nullptr) return;

        m_Instance->m_Camera.Uninit();

        if (m_Instance->m_Scene != nullptr)
        {
            delete m_Instance->m_Scene;
            m_Instance->m_Scene = nullptr;
        }

        for (auto& o : m_Instance->m_Objects)
        {
            o->Uninit();
        }

        m_Instance->m_Objects.clear();
        m_Instance->m_NamedObjects.clear();
        m_Instance->m_PostProcess.Uninit();

        Input::Release();

        Renderer::Uninit();

        delete m_Instance;
        m_Instance = nullptr;
    }

    Game* Core::Game::GetInstance()
    {
        return m_Instance;
    }

    void Game::ChangeScene(SceneName sName)
    {
        if (m_Scene != nullptr)
        {
            delete m_Scene;
            m_Scene = nullptr;
        }

        DeleteAllObject();

        switch (sName)
        {
        case TITLE:
            m_Scene = new TitleScene;
            break;

        case STAGE:
            m_Scene = new StageScene;
            break;

        case RESULT:
            m_Scene = new ResultScene;
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