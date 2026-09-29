// ============================================================================
// ファイルの役割: ステージの配置処理（Stage1Layout / Stage2Layout）で、名前付きObjectを
//                 生成しながら、その名前を一覧へ記録します。
// 主な技術: テンプレート、名前の一元管理
// 名前を書くのは生成するこの1か所だけにし、Sceneの終了時はこの一覧で破棄します。
// ============================================================================

#pragma once

#include "Game.h"

#include <string>
#include <vector>

class SceneLayoutBuilder final
{
public:
    SceneLayoutBuilder(Core::Game& game, std::vector<std::string>& createdNames)
        : m_Game(game), m_CreatedNames(createdNames)
    {
    }

    template<typename T>
    T* Create(const std::string& name)
    {
        m_CreatedNames.emplace_back(name);
        return m_Game.CreateObj<T>(name);
    }

private:
    Core::Game& m_Game;
    std::vector<std::string>& m_CreatedNames;
};
