#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <optional>
#include <functional>
#include <utility>

#include "Camera.h"
#include "Renderer.h"
#include "Object.h"
#include"PostProcess.h"
#include "ShadowMap.h"
#include "PlanarReflection.h"
enum class SceneName
{
    Title,
    Stage,
    Result
};

class Scene;

namespace Core
{
    class Game
    {
    private:
        static std::unique_ptr<Game> m_Instance;

        std::unique_ptr<Scene> m_Scene;
        Camera m_Camera;
        Effect::PostProcess m_PostProcess;
        Effect::ShadowMap m_ShadowMap;
        Effect::PlanarReflection m_PlanarReflection;

        std::vector<std::unique_ptr<Object>> m_Objects;
        std::unordered_map<std::string, Object*> m_NamedObjects;

        std::optional<SceneName> m_PendingScene;
        std::vector<std::function<void()>> m_PendingObjectCommands;

        int m_ItemCount = 0;
        bool m_PowerRestored = false;
        SceneName m_CurrentScene = SceneName::Title;

        void ChangeScene(SceneName sName);

    public:
        Game();
        ~Game();

        static void Init();
        static void Update();
        static void Draw();
        static void Uninit();

        static Game* GetInstance();

        void RequestSceneChange(SceneName sName);

        void DeleteObject(Object* pt);
        void DestroyObj(const std::string& name);
        void DeleteAllObject();

        template<typename T>
        T* AddObject()
        {
            auto object = std::make_unique<T>();
            T* pt = object.get();
            m_Objects.emplace_back(std::move(object));
            pt->Init();
            return pt;
        }

        template<typename T, typename Setup>
        void RequestAddObject(Setup&& setup)
        {
            m_PendingObjectCommands.emplace_back(
                [this, setup = std::forward<Setup>(setup)]() mutable
                {
                    T* object = AddObject<T>();
                    setup(*object);
                }
            );
        }

        template<typename T>
        T* CreateObj(const std::string& name)
        {
            T* pt = AddObject<T>();
            m_NamedObjects[name] = pt;
            return pt;
        }

        template<typename T>
        T* GetObj(const std::string& name)
        {
            auto it = m_NamedObjects.find(name);

            if (it == m_NamedObjects.end())
            {
                return nullptr;
            }

            return dynamic_cast<T*>(it->second);
        }

        template<typename T>
        std::vector<T*> GetObjects()
        {
            std::vector<T*> res;

            for (auto& o : m_Objects)
            {
                if (T* derivedObj = dynamic_cast<T*>(o.get()))
                {
                    res.emplace_back(derivedObj);
                }
            }

            return res;
        }


        void AddItemCount()
        {
            m_ItemCount++;
        }

        int GetItemCount() const
        {
            return m_ItemCount;
        }

        void SetPowerRestored(bool restored)
        {
            m_PowerRestored = restored;
        }

        bool IsPowerRestored() const
        {
            return m_PowerRestored;
        }



        Camera* GetCamera()
        {
            return &m_Camera;
        }

        Effect::PostProcess* GetPostProcess()
        {
            return &m_PostProcess;
        }

        Effect::ShadowMap* GetShadowMap()
        {
            return &m_ShadowMap;
        }
    };
}
