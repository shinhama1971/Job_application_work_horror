// ============================================================================
// ファイルの役割: ステージの配置処理（Stage1Layout / Stage2Layout）で、名前付きのObjectを
//                 作りながら、その名前を一覧へ記録している。
// 主な技術: テンプレート、名前の一元管理
// 名前を書くのは作るこの1か所だけにし、Sceneの終了時はこの一覧を使って破棄している。
// ============================================================================

#pragma once

#include "Game.h"

#include <string>
#include <vector>

// 配置処理に渡す道具。Createを呼ぶたびにObjectを作り、名前を一覧に足している。
class SceneLayoutBuilder final
{
public:
    // Objectを作るGameと、作った名前を記録する一覧を受け取っている
    SceneLayoutBuilder(Core::Game& game, std::vector<std::string>& createdNames)
        : m_Game(game), m_CreatedNames(createdNames)
    {
    }

    // 名前付きでObjectを作り、名前を一覧に記録している
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
