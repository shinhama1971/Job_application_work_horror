// ============================================================================
// ファイルの役割: ステージの小物（棚・配管・木箱・警告灯など、進行に関わらない箱）の配置を、テキストファイルから読んでいる。
// 主な技術: 1行1つの簡単な書式の読み込み、色の名前の表、間違いの行番号の報告、読み直したときの置き換え
//
// ・配置をコードに直接書くと、1つ動かすたびにビルドし直す必要がある。ファイルにすると、Debug版ではゲームを
//   動かしたまま F5 で読み直せる（StageScene）。扉・アイテムなど、進行のコードが使う物はコードに残している。
// ・書式（# から行の終わりまではコメント）
//     color 名前 r g b
//     prop 名前 x y z 幅 高さ 奥行き 色 光る色 光沢 当たり判定(0/1) [noshadow] [weathering] [glow 範囲 強さ]
//   色と光る色は color で付けた名前（光らないときは none）。noshadow は影を落とさない、weathering は壁の古さを描く、
//   glow は自分の光る色で周りを照らす点光源にする。
// ・Debug版では、デバッグ画面（F1）の配置エディタ（PropLayoutEditor）で、画面を見ながら動かして Save でこのファイルへ書き戻せる。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <string>
#include <vector>
#include <utility>

class SceneLayoutBuilder;
class Wall;

// 小物1つ分の設定
struct PropDescription
{
    std::string Name;
    DirectX::SimpleMath::Vector3 Position;
    DirectX::SimpleMath::Vector3 Scale;
    DirectX::SimpleMath::Color Diffuse;
    DirectX::SimpleMath::Color Emission;
    float Shininess = 8.0f;
    bool Collision = false;
    bool CastsShadow = true;
    bool Weathering = false;
    float GlowRange = 0.0f;
    float GlowStrength = 0.0f;
    // 色の名前（保存するときに書き戻す）と、ファイルの何行目にあったか（0は、まだファイルにない）
    std::string DiffuseName = "none";
    std::string EmissionName = "none";
    int Line = 0;
};

// 読み込んだ結果（読めなかったときは、Errorに理由、ErrorLineに行番号が入る）
struct PropLayout
{
    std::vector<PropDescription> Props;
    // color で付けた名前と色（ファイルの順。先頭は光らないことを表す none）
    std::vector<std::pair<std::string, DirectX::SimpleMath::Color>> Colors;
    std::string Error;
    int ErrorLine = 0;
    bool IsValid() const { return Error.empty(); }
};

namespace PropLayoutFile
{
    // ファイルを読み、小物の一覧を返している
    PropLayout Load(const std::string& path);
    // 作った小物（名前とWall）。読み直すときは、同じ名前の物を置き直している
    using SpawnedProps = std::vector<std::pair<std::string, Wall*>>;
    // 小物を作り（builderで名前を記録する）、作った小物を返している
    SpawnedProps Spawn(SceneLayoutBuilder& builder, const PropLayout& layout);
    // ファイルを読み直した結果で、今の小物を置き直している（同じ名前は置き直し、新しい名前は作り、ファイルから消えた物は隠す）
    void Reload(SceneLayoutBuilder& builder, const PropLayout& layout, SpawnedProps& props);
    // 1つのWallに、設定（位置・大きさ・色・当たり判定・影・古さ・光）を当てはめている（作るときと読み直すときの両方で使う）
    void Apply(Wall& wall, const PropDescription& description);
    // 色の名前から色を探している（見つからなければnullptr）
    const DirectX::SimpleMath::Color* FindColor(const PropLayout& layout, const std::string& name);
    // 小物1つ分を、ファイルの prop の行の書式にしている
    std::string FormatProp(const PropDescription& description);
    // 今の配置をファイルへ書き戻している。変わった小物の行だけを書き換え、コメント・色の行・並びはそのまま残す。
    // ファイルから消した小物の行は消し、新しい小物は、元にした小物の行の後ろ（なければファイルの最後）に足す。
    // 書けなかったときはfalseを返し、errorに理由を入れる
    bool Save(const std::string& path, const PropLayout& layout, std::string& error);
}
