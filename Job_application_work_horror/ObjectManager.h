// ============================================================================
// ファイルの役割: Objectの所有、検索、追加・削除のライフサイクルを管理します。
// 主な技術: unique_ptr、型検索、イテレーション安全性、遅延キュー
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
        // その場で生成してInitまで行います。UpdateAllの走査中に呼ぶとm_Objectsが再配置されるため、
        // SceneのInitなど更新ループの外でだけ使います。更新中はRequestAddObjectを使います。
        template<typename T>
        T* AddObject()
        {
            auto object = std::make_unique<T>();
            T* result = object.get();
            m_Objects.emplace_back(std::move(object));
            result->Init();
            return result;
        }

        // 更新ループ中に生成したいときの予約です。FlushPendingCommandsで走査後に生成し、
        // setupで位置などを設定します。シーンを切り替えるフレームの予約は破棄されます。
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

        // 名前付きで生成します。SceneはInitでこの名前をRequireObjに渡し、ポインタを保持します。
        template<typename T>
        T* CreateNamedObject(const std::string& name)
        {
            T* result = AddObject<T>();
            m_NamedObjects[name] = result;
            return result;
        }

        // 名前が見つからない、または型が違う場合はnullptrを返します。
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

        // 破棄予約されていない、指定型のObjectをすべて返します。全Objectを走査するため、
        // 毎フレーム多数回呼ぶ用途ではSceneで結果を保持してください。
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
        // Destroy済みのObjectをUninitして実体を解放します。UpdateAllの後に呼びます。
        void RemoveDestroyed();
        // 破棄を予約し、名前検索からも外します。実体はRemoveDestroyedで解放されます。
        void DeleteObject(Object* object);
        // 名前で指定して破棄を予約します（DeleteObjectの名前版）。
        void DestroyNamedObject(const std::string& name);
        // 全Objectをその場でUninitして解放します。シーン切り替え時だけ使います。
        void DeleteAll();
        void ClearPendingCommands();
        // RequestAddObjectで予約された生成を実行します。
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
