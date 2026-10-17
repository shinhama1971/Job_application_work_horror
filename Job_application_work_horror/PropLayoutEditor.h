// ============================================================================
// ファイルの役割: Debug版のデバッグ画面（F1）に出す「配置エディタ」。ステージの小物を画面で選び、位置・大きさ・色・
//                 当たり判定・影・光をその場で変え、配置のファイル（PropLayoutFile）へ書き戻している。
// 主な技術: Dear ImGui、画面のクリック位置から伸ばした光線と箱の当たり判定（スラブ法）での選択、変えた行だけの書き戻し
//
// ・テキストを書き換えて F5 で読み直すより速く、見ながら置ける。Save で書き戻すので、置いた結果はファイルに残る。
// ・選んだ小物は黄色く明滅させ、どれを動かしているかが分かるようにしている（閉じると元の色に戻す）。
// ・Release版では窓を出さない（Drawは何もしない）。
// ============================================================================

#pragma once

#include "PropLayoutFile.h"

#include <string>
#include <utility>

class Camera;

class PropLayoutEditor final
{
public:
    // Drawの結果（何もしない・書き戻した・書き戻せなかった・ファイルを読み直してほしい）
    enum class Action
    {
        None,
        Saved,
        SaveFailed,
        ReloadFile
    };

    explicit PropLayoutEditor(std::string path) : m_Path(std::move(path)) {}

    // ファイルから読んだ配置を受け取っている（作ったときと、F5・Reloadで読み直したとき）
    void SetLayout(const PropLayout& layout);

    // デバッグ画面を開いている間、窓を描いて小物を編集している。新しく足した小物はbuilderで作り、propsに加える
    Action Draw(SceneLayoutBuilder& builder, PropLayoutFile::SpawnedProps& props, const Camera& camera);

    // 書き戻せなかったときの理由（画面の左上に出すための日本語）
    const std::string& GetError() const { return m_Error; }

private:
    // 名前から、配置の設定とWallを探している
    PropDescription* FindDescription(const std::string& name);
    static Wall* FindWall(PropLayoutFile::SpawnedProps& props, const std::string& name);
    // 光線（originから向きdirection）に一番近くで当たる小物を選んでいる
    void PickAlongRay(
        const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& direction);
    // 明滅させていた小物の色を元に戻している
    void RestoreHighlight(PropLayoutFile::SpawnedProps& props);

    std::string m_Path;
    PropLayout m_Layout;
    bool m_Loaded = false;
    // 未保存の変更があるか
    bool m_Dirty = false;
    // 選んでいる小物と、今明滅させている小物の名前
    std::string m_Selected;
    std::string m_Highlighted;
    // 一覧の絞り込みの文字
    char m_Filter[64] = {};
    // 天井の板のような、古さを描く大きな面を選ばないか（見上げたときに天井ばかり選ばれないように）
    bool m_IgnoreWeatheredSurfaces = true;
    // 窓に出す英語の状態と、HUDに出す日本語の理由
    std::string m_Status;
    std::string m_Error;
};
