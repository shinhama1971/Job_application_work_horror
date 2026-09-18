// ============================================================================
// ファイルの役割: Objectの更新、破棄、遅延追加を安全な順序で実行します。
// 主な技術: unique_ptr、型検索、イテレーション安全性、遅延キュー
// ============================================================================

#include "ObjectManager.h"

#include <algorithm>

namespace Core
{
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

    // 名前Mapは非所有ポインタなので、実体を破棄する前に参照を外して
    // dangling pointerを残さないようにします。
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

    void ObjectManager::DeleteAll()
    {
        for (auto& object : m_Objects)
        {
            object->Uninit();
        }
        m_Objects.clear();
        m_NamedObjects.clear();
    }

    void ObjectManager::ClearPendingCommands()
    {
        m_PendingCommands.clear();
    }

    // 更新中に予約された追加を走査後に実行し、m_Objectsの再配置で
    // iteratorが壊れることを防ぎます。
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
