// ============================================================================
// ファイルの役割: Objectの更新、破棄、予約した追加を、安全な順番で実行している。
// 主な技術: unique_ptr、型による検索、順に処理している途中の安全性、後回しにする処理の列
// ============================================================================

#include "ObjectManager.h"

#include <algorithm>

namespace Core
{
    // 破棄を予約されていないObjectを、追加した順に更新している
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

    // 名前のMapは所有していないポインタなので、実体を壊す前に参照を外して、
    // 壊れた先を指すポインタ（dangling pointer）を残さないようにしている。
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

        // 破棄を予約されたObjectだけUninitしてから、配列から取り除いている（unique_ptrが実体を解放する）
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

    // 破棄を予約し、同じObjectを指している名前をすべて外している
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

    // 名前で探して破棄を予約し、名前を外している
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

    // 全ObjectをUninitしてから、まとめて解放している
    void ObjectManager::DeleteAll()
    {
        for (auto& object : m_Objects)
        {
            object->Uninit();
        }
        m_Objects.clear();
        m_NamedObjects.clear();
    }

    // 予約された追加を捨てている
    void ObjectManager::ClearPendingCommands()
    {
        m_PendingCommands.clear();
    }

    // 更新中に予約された追加を処理の後に実行し、m_Objectsが並べ直されて
    // イテレーターが壊れるのを防いでいる。
    void ObjectManager::FlushPendingCommands()
    {
        // 実行中に新しい予約が入っても壊れないよう、列を取り出してから実行している
        auto pendingCommands = std::move(m_PendingCommands);
        m_PendingCommands.clear();
        for (auto& command : pendingCommands)
        {
            command();
        }
    }
}
