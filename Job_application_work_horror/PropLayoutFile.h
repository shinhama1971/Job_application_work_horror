// ============================================================================
// ファイルの役割: ステージの小物（棚・配管・木箱・警告灯など、進行に関わらない箱）の配置を、テキストファイルから読んでいる。
// 主な技術: 1行1つの簡単な書式の読み込み、色の名前の表、間違いの行番号の報告、読み直したときの置き換え
//
// ・配置をコードに直接書くと、1つ動かすたびにビルドし直す必要がある。ファイルにすると、Debug版ではゲームを
//   動かしたまま F5 で読み直せる（StageScene）。扉・アイテムなど、進行のコードが使う物はコードに残している。
// ・書式（# から行の終わりまではコメント）
//     color 名前 r g b [metal 粗さ [金属らしさ]] [rough 粗さ]
//     prop 名前 x y z 幅 高さ 奥行き 色 光る色 光沢 当たり判定(0/1) [noshadow] [weathering] [glow 範囲 強さ]
//   色と光る色は color で付けた名前（光らないときは none）。noshadow は影を落とさない、weathering は壁の古さを描く、
//   glow は自分の光る色で周りを照らす点光源にする。
//   色の後ろの metal は金属（色がそのまま映り込みの色になる。金属らしさを続けて書ける。省くと1）、rough は金属でない物の粗さ（0.045〜1）。PBRの仕上げで、その色の小物すべてに付く。
// ・Debug版では、デバッグ画面（F1）の配置エディタ（PropLayoutEditor）で、画面を見ながら動かして Save でこのファイルへ書き戻せる。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <cmath>
#include <filesystem>
#include <functional>

#include <string>
#include <vector>
#include <utility>

class SceneLayoutBuilder;
class Wall;

// PBRの表面の仕上げ（color の行の metal・rough で、色ごとに決めている）
struct PropSurfaceFinish
{
    float Metallic = 0.0f;
    // 0は指定なし（光沢の強さから換算する）
    float Roughness = 0.0f;
};

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
    // 部屋の形として、部屋の角の暗がりとレイ反射に使う壁か（room）
    bool Room = false;
    float GlowRange = 0.0f;
    float GlowStrength = 0.0f;
    // 見た目に使うモデルの名前（空なら箱。Wall::SetVisualModel）
    std::string Model;
    // PBRの仕上げ（色の名前から決まる）
    PropSurfaceFinish Finish;
    // 色の名前（保存するときに書き戻す）と、ファイルの何行目にあったか（0は、まだファイルにない）
    std::string DiffuseName = "none";
    std::string EmissionName = "none";
    int Line = 0;
};

// 進行のコードが作る物（扉・照明・電池・端末など）の位置・向き・大きさ（place 行）。
// 物の種類や動き（鍵がかかっているか、非常灯か）はコードが決め、置き場所だけをファイルから読んでいる
struct PropPlacement
{
    std::string Name;
    DirectX::SimpleMath::Vector3 Position;
    // 水平の向き（ラジアン。ファイルには度で書く）と、書いてあったか
    float Yaw = 0.0f;
    bool HasYaw = false;
    // 大きさ（size。書いてなければ、その物の決まった大きさのまま）
    DirectX::SimpleMath::Vector3 Size = DirectX::SimpleMath::Vector3::One;
    bool HasSize = false;
    int Line = 0;
};

// 読み込んだ結果（読めなかったときは、Errorに理由、ErrorLineに行番号が入る）
struct PropLayout
{
    std::vector<PropDescription> Props;
    // place 行（ファイルの順）
    std::vector<PropPlacement> Placements;
    // spot 行（候補の場所。プレイごとに1つを選ぶ物の置き場所など）を、組の名前ごとにファイルの順で
    std::vector<std::pair<std::string, std::vector<DirectX::SimpleMath::Vector3>>> Spots;
    // color で付けた名前と色（ファイルの順。先頭は光らないことを表す none）
    std::vector<std::pair<std::string, DirectX::SimpleMath::Color>> Colors;
    // 色の名前ごとのPBRの仕上げ（metal・rough を付けた色だけ）
    std::vector<std::pair<std::string, PropSurfaceFinish>> Finishes;
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
    // place 行の置き場所を、名前で返している（ファイルに書いていない名前なら、理由を出して終了している）
    const PropPlacement& GetPlacement(const PropLayout& layout, const std::string& name);

    // 置き場所を置き直す方法（名前と、その名前の place 行を受け取って物を置き直す関数）。配置を作るときに覚えておき、
    // 配置ファイルを読み直したときに ReapplyPlacements で置き直している
    using PlacementBindings = std::vector<std::pair<std::string, std::function<void(const PropPlacement&)>>>;
    // 覚えておいた物を、読み直した place 行で置き直し、置き直した数を返している（ファイルから消えた名前は、そのまま残す）
    int ReapplyPlacements(const PropLayout& layout, const PlacementBindings& bindings);

    // 配置ファイルが書き換わったかを、0.5秒ごとに更新時刻で調べている（Unity などで書き出した直後に、自動で読み直すため）
    class FileChangeWatcher
    {
    public:
        bool HasChanged(const std::string& path, float deltaTime);

    private:
        std::filesystem::file_time_type m_LastWrite{};
        bool m_HasLastWrite = false;
        float m_Timer = 0.0f;
    };

    // spot 行の候補の場所を、組の名前で返している（1つもなければ、理由を出して終了している）
    const std::vector<DirectX::SimpleMath::Vector3>& GetSpots(const PropLayout& layout, const std::string& group);

    // place の向き（yaw）を、真上から見た水平の向き（+Z が0度）の単位ベクトルにしている（壁の文字の面の向きなど）
    inline DirectX::SimpleMath::Vector3 GetFacing(const PropPlacement& placement)
    {
        return DirectX::SimpleMath::Vector3(std::sin(placement.Yaw), 0.0f, std::cos(placement.Yaw));
    }

    // 物を指定した位置へ動かしている（float 3つの SetPosition があれば、そちらで動きの基準も覚えさせる）
    template<typename T>
    void MoveTo(T& object, const DirectX::SimpleMath::Vector3& position)
    {
        if constexpr (requires { object.SetPosition(0.0f, 0.0f, 0.0f); })
        {
            object.SetPosition(position.x, position.y, position.z);
        }
        else
        {
            object.SetPosition(position);
        }
    }

    // 置き場所を物に当てはめている。扉・壁・電池・照明は float 3つの SetPosition を持ち、その中で動きの基準（開く前の位置、
    // 浮く動きの中心の高さ）も覚えるので、あればそちらを呼んでいる
    template<typename T>
    void ApplyPlacement(T& object, const PropPlacement& placement)
    {
        MoveTo(object, placement.Position);
        if (placement.HasYaw)
        {
            object.SetRotation(DirectX::SimpleMath::Vector3(0.0f, placement.Yaw, 0.0f));
        }
        if (placement.HasSize)
        {
            const DirectX::SimpleMath::Vector3& size = placement.Size;
            if constexpr (requires { object.SetScale(0.0f, 0.0f, 0.0f); })
            {
                object.SetScale(size.x, size.y, size.z);
            }
            else
            {
                object.SetScale(size);
            }
        }
    }

    // room を付けた壁を上から見た長方形（x,y = 中心のx・z、z,w = 幅と奥行きの半分）を、ファイルの順に返している
    std::vector<DirectX::SimpleMath::Vector4> CollectRoomFootprints(const PropLayout& layout);
    // 小物を作り（builderで名前を記録する）、作った小物を返している
    SpawnedProps Spawn(SceneLayoutBuilder& builder, const PropLayout& layout);
    // ファイルを読み直した結果で、今の小物を置き直している（同じ名前は置き直し、新しい名前は作り、ファイルから消えた物は隠す）
    void Reload(SceneLayoutBuilder& builder, const PropLayout& layout, SpawnedProps& props);
    // 1つのWallに、設定（位置・大きさ・色・当たり判定・影・古さ・光）を当てはめている（作るときと読み直すときの両方で使う）
    void Apply(Wall& wall, const PropDescription& description);
    // 色の名前から色を探している（見つからなければnullptr）
    const DirectX::SimpleMath::Color* FindColor(const PropLayout& layout, const std::string& name);
    // 色の名前からPBRの仕上げを探している（付けていない色は、金属でなく粗さは光沢から換算する仕上げを返す）
    PropSurfaceFinish FindFinish(const PropLayout& layout, const std::string& name);
    // 小物1つ分を、ファイルの prop の行の書式にしている
    std::string FormatProp(const PropDescription& description);
    // 今の配置をファイルへ書き戻している。変わった小物の行だけを書き換え、コメント・色の行・並びはそのまま残す。
    // ファイルから消した小物の行は消し、新しい小物は、元にした小物の行の後ろ（なければファイルの最後）に足す。
    // 書けなかったときはfalseを返し、errorに理由を入れる
    bool Save(const std::string& path, const PropLayout& layout, std::string& error);
}
