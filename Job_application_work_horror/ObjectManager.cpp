// ============================================================================
// ファイルの役割: Objectの更新、破棄、予約した追加を、安全な順番で実行している。
// 主な技術: unique_ptr、型による検索、順に処理している途中の安全性、後回しにする処理の列
// ============================================================================

#include "ObjectManager.h"
#include "utility.h"

#include <algorithm>

namespace Core
{
    void ObjectManager::CheckNameAvailable(const std::string& name) const
    {
        const auto existing = m_NamedObjects.find(name);
        if (existing != m_NamedObjects.end() && !existing->second->IsDestroy())
        {
            utility::ReportFatalError("同じ名前の Object を登録できない: " + name);
        }
    }

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

        // 一覧の並び替えを終えてから Uninit を呼ぶ。Uninit から検索しても、移動途中の要素に触れない。
        std::vector<std::unique_ptr<Object>> destroyed;
        for (auto& object : m_Objects)
        {
            if (object->IsDestroy())
            {
                destroyed.emplace_back(std::move(object));
            }
        }
        if (!destroyed.empty())
        {
            std::erase(m_Objects, nullptr);
            for (auto& object : destroyed)
            {
                object->Uninit();
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
        m_PendingCommands.clear();
        m_NamedObjects.clear();
        m_Objects.clear();
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
