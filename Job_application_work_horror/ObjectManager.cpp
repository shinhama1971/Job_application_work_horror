// ============================================================================
// ファイルの役割: Objectの更新、破棄、遅延追加を安全な順序で実行します。
// 主な技術: unique_ptr、型検索、イテレーション安全性、遅延キュー
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "ObjectManager.h"

#include <algorithm>

namespace Core
{
    // 処理内容: ObjectManagerの「UpdateAll」処理を担当します。
    void ObjectManager::UpdateAll()
    {
        for (auto& object : m_Objects)
        {
            if (!object->IsDestroy())
            {
                object->Update();
            }
        }
    }

    // 処理内容: ObjectManagerの「RemoveDestroyed」処理を担当します。
    void ObjectManager::RemoveDestroyed()
    {
        for (auto it = m_NamedObjects.begin();
            it != m_NamedObjects.end();)
        {
            Object* object = it->second;
            if (object == nullptr || object->IsDestroy())
            {
                it = m_NamedObjects.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // 処理内容: stdの「erase_if」処理を担当します。
        std::erase_if(
            m_Objects,
            [](const std::unique_ptr<Object>& object)
            {
                if (!object->IsDestroy())
                {
                    return false;
                }
                object->Uninit();
                return true;
            });
    }

    // 処理内容: ObjectManagerの「DeleteObject」処理を担当します。
    void ObjectManager::DeleteObject(Object* object)
    {
        if (object == nullptr)
        {
            return;
        }
        object->Destroy();
        for (auto it = m_NamedObjects.begin();
            it != m_NamedObjects.end();)
        {
            if (it->second == object)
            {
                it = m_NamedObjects.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // 処理内容: ObjectManagerの「DestroyNamedObject」処理を担当します。
    void ObjectManager::DestroyNamedObject(const std::string& name)
    {
        const auto it = m_NamedObjects.find(name);
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

    // 処理内容: ObjectManagerの「DeleteAll」処理を担当します。
    void ObjectManager::DeleteAll()
    {
        for (auto& object : m_Objects)
        {
            object->Uninit();
        }
        m_Objects.clear();
        m_NamedObjects.clear();
    }

    // 処理内容: ObjectManagerの「ClearPendingCommands」処理を担当します。
    void ObjectManager::ClearPendingCommands()
    {
        m_PendingCommands.clear();
    }

    // 処理内容: ObjectManagerの「FlushPendingCommands」処理を担当します。
    void ObjectManager::FlushPendingCommands()
    {
        auto pendingCommands = std::move(m_PendingCommands);
        m_PendingCommands.clear();
        for (auto& command : pendingCommands)
        {
            command();
        }
    }
}
