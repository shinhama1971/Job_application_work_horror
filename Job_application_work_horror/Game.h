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
    Stage2,
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
        bool m_IsPaused = false;
        unsigned int m_ReflectionFrameIndex = 0;
        unsigned int m_ShadowFrameIndex = 0;
        float m_RunTimeSeconds = 0.0f;
        float m_LastClearTimeSeconds = 0.0f;
        int m_CaughtCount = 0;
        int m_AnomaliesHandled = 0;
        int m_PuzzleMistakes = 0;
        int m_ChargersUsed = 0;
        int m_EvidenceCollected = 0;
        int m_BrightnessLevel = 2;
        int m_EffectLevel = 1;
        int m_LookSensitivityLevel = 2;
        int m_PauseSettingIndex = 0;
        float m_BestClearTimeSeconds = 0.0f;
        int m_BestCaughtCount = 0;
        bool m_HasClearRecord = false;
        bool m_LastRunBestTime = false;
        bool m_LastRunBestCaught = false;

        void ChangeScene(SceneName sName);
        void LoadBestRecord();
        void SaveBestRecord() const;
        void LoadSettings();
        void SaveSettings() const;

    public:
        Game();
        ~Game();

        static void Init();
        static void Update();
        static void Draw();
        static void Uninit();

        static Game* GetInstance();

        Scene* GetScene() const
        {
            return m_Scene.get();
        }

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
                if (o->IsDestroy())
                {
                    continue;
                }

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

        bool IsPaused() const
        {
            return m_IsPaused;
        }

        int GetBrightnessLevel() const
        {
            return m_BrightnessLevel;
        }

        int GetEffectLevel() const
        {
            return m_EffectLevel;
        }

        int GetLookSensitivityLevel() const
        {
            return m_LookSensitivityLevel;
        }

        int GetPauseSettingIndex() const
        {
            return m_PauseSettingIndex;
        }

        float GetLastClearTimeSeconds() const
        {
            return m_LastClearTimeSeconds;
        }

        float GetRunTimeSeconds() const
        {
            return m_RunTimeSeconds;
        }

        bool HasClearRecord() const
        {
            return m_HasClearRecord;
        }

        float GetBestClearTimeSeconds() const
        {
            return m_BestClearTimeSeconds;
        }

        int GetBestCaughtCount() const
        {
            return m_BestCaughtCount;
        }

        int GetCaughtCount() const
        {
            return m_CaughtCount;
        }

        bool IsLastRunBestTime() const
        {
            return m_LastRunBestTime;
        }

        bool IsLastRunBestCaught() const
        {
            return m_LastRunBestCaught;
        }

        void RegisterCaught()
        {
            ++m_CaughtCount;
        }

        void RegisterAnomalyHandled() { ++m_AnomaliesHandled; }
        void RegisterPuzzleMistake() { ++m_PuzzleMistakes; }
        void RegisterChargerUsed() { ++m_ChargersUsed; }
        int GetAnomaliesHandled() const { return m_AnomaliesHandled; }
        int GetPuzzleMistakes() const { return m_PuzzleMistakes; }
        int GetChargersUsed() const { return m_ChargersUsed; }
        void RegisterEvidenceCollected() { ++m_EvidenceCollected; }
        int GetEvidenceCollected() const { return m_EvidenceCollected; }



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
