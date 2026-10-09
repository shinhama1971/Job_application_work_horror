// ============================================================================
// ファイルの役割: Objectの所有と検索、追加・削除の流れを管理している。
// 主な技術: unique_ptr、型による検索、順に処理している途中の安全性、後回しにする処理の列
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
    // Gameが1つ持ち、全Objectの実体を所有している。名前による検索と、型による検索を提供している。
    class ObjectManager final
    {
    private:
        // 全Object（実体の持ち主）
        std::vector<std::unique_ptr<Object>> m_Objects;
        // m_Objectsが実体を所有しているため、このMapのポインタは所有していない。
        std::unordered_map<std::string, Object*> m_NamedObjects;
        // 更新ループの後に実行する予定の処理（Objectの追加）
        std::vector<std::function<void()>> m_PendingCommands;

        // Objectを指定の型に変換している（違う型ならnullptr）
        template<typename T>
        static T* CastObject(Object* object)
        {
            if (object == nullptr)
            {
                return nullptr;
            }

            // 作ったときと同じ具体的な型を求める普通の検索では、RTTIによるキャストを省いている。
            // Interactableなど別の基底クラスへの検索は、前と同じdynamic_castへ戻すため、
            // 多重継承を含む今までの検索結果と、型の安全性は変わらない。
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
        // その場で生成してInitまで行っている。UpdateAllで順に処理している途中で呼ぶとm_Objectsが並べ直されるため、
        // SceneのInitなど、更新ループの外でだけ使っている。更新中はRequestAddObjectを使っている。
        template<typename T>
        T* AddObject()
        {
            auto object = std::make_unique<T>();
            T* result = object.get();
            m_Objects.emplace_back(std::move(object));
            result->Init();
            return result;
        }

        // 更新ループの中で生成したいときの予約。FlushPendingCommandsで処理の後に生成し、
        // setupで位置などを設定している。シーンを切り替えるフレームの予約は捨てている。
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

        // 名前付きで生成している。SceneはInitでこの名前をRequireObjに渡し、ポインタを持ち続けている。
        template<typename T>
        T* CreateNamedObject(const std::string& name)
        {
            T* result = AddObject<T>();
            m_NamedObjects[name] = result;
            return result;
        }

        // 名前が見つからない、または型が違う場合はnullptrを返している。
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

        // 破棄を予約されていない、指定の型のObjectをすべて返している。全Objectを調べるため、
        // 毎フレーム何度も呼ぶ使い方では、Sceneで結果を覚えておいて使っている。
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

        // 破棄を予約されていない全Objectを更新している
        void UpdateAll();
        // Destroy済みのObjectをUninitして、実体を解放している。UpdateAllの後に呼んでいる。
        void RemoveDestroyed();
        // 破棄を予約し、名前の検索からも外している。実体はRemoveDestroyedで解放される。
        void DeleteObject(Object* object);
        // 名前で指定して破棄を予約している（DeleteObjectの名前版）。
        void DestroyNamedObject(const std::string& name);
        // 全Objectをその場でUninitして解放している。シーンの切り替え時だけ使っている。
        void DeleteAll();
        // 予約された追加を捨てている（シーンを切り替えるフレームで使っている）
        void ClearPendingCommands();
        // RequestAddObjectで予約された生成を実行している。
        void FlushPendingCommands();

        // 全Objectの一覧を返している（描画で順に処理するのに使っている）
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
