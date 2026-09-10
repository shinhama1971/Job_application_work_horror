// ============================================================================
// ファイルの役割: Objectの所有、検索、追加・削除のライフサイクルを管理します。
// ============================================================================

#pragma once

#include "Object.h"

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core
{
    class ObjectManager final
    {
    private:
        std::vector<std::unique_ptr<Object>> m_Objects;
        // m_Objectsが実体を所有するため、このMapのポインタは非所有です。
        std::unordered_map<std::string, Object*> m_NamedObjects;
        std::vector<std::function<void()>> m_PendingCommands;

        template<typename T>
        static T* CastObject(Object* object)
        {
            if (object == nullptr)
            {
                return nullptr;
            }

            // 生成時と同じ具象型を要求する通常検索ではRTTIキャストを省略します。
            // Interactableなど別基底への検索は従来のdynamic_castへ戻すため、
            // 多重継承を含む既存の検索結果と型安全性は変わりません。
            if constexpr (std::is_base_of_v<Object, T>)
            {
                if (typeid(*object) == typeid(T))
                {
                    return static_cast<T*>(object);
                }
            }
            return dynamic_cast<T*>(object);
        }

    public:
        template<typename T>
        T* AddObject()
        {
            auto object = std::make_unique<T>();
            T* result = object.get();
            m_Objects.emplace_back(std::move(object));
            result->Init();
            return result;
        }

        template<typename T, typename Setup>
        void RequestAddObject(Setup&& setup)
        {
            m_PendingCommands.emplace_back(
                [this, setup = std::forward<Setup>(setup)]() mutable
                {
                    T* object = AddObject<T>();
                    setup(*object);
                });
        }

        template<typename T>
        T* CreateNamedObject(const std::string& name)
        {
            T* result = AddObject<T>();
            m_NamedObjects[name] = result;
            return result;
        }

        template<typename T>
        T* FindNamedObject(const std::string& name)
        {
            const auto it = m_NamedObjects.find(name);
            if (it == m_NamedObjects.end())
            {
                return nullptr;
            }
            return CastObject<T>(it->second);
        }

        template<typename T>
        std::vector<T*> FindObjects()
        {
            std::vector<T*> result;
            for (auto& object : m_Objects)
            {
                if (object->IsDestroy())
                {
                    continue;
                }
                if (T* typedObject = CastObject<T>(object.get()))
                {
                    result.emplace_back(typedObject);
                }
            }
            return result;
        }

        void UpdateAll();
        void RemoveDestroyed();
        void DeleteObject(Object* object);
        void DestroyNamedObject(const std::string& name);
        void DeleteAll();
        void ClearPendingCommands();
        void FlushPendingCommands();

        std::vector<std::unique_ptr<Object>>& GetAllObjects()
        {
            return m_Objects;
        }

        const std::vector<std::unique_ptr<Object>>& GetAllObjects() const
        {
            return m_Objects;
        }
    };
}
