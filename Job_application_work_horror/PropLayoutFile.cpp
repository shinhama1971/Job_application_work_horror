// ============================================================================
// ファイルの役割: ステージの小物の配置を、テキストファイルから読んでいる（書式は PropLayoutFile.h を参照）。
// 主な技術: 1行1つの簡単な書式の読み込み、色の名前の表、間違いの行番号の報告
// ============================================================================

#include "PropLayoutFile.h"

#include "SceneLayoutBuilder.h"
#include "Wall.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace DirectX::SimpleMath;

namespace PropLayoutFile
{
    PropLayout Load(const std::string& path)
    {
        PropLayout layout;
        std::ifstream file(path);
        if (!file)
        {
            layout.Error = "ファイルを開けない: " + path;
            return layout;
        }

        // 色の名前の表（none は光らないことを表す黒）
        std::unordered_map<std::string, Color> colors;
        colors["none"] = Color(0.0f, 0.0f, 0.0f, 1.0f);
        const auto fail = [&layout](int line, const std::string& message)
        {
            layout.Props.clear();
            layout.Error = message;
            layout.ErrorLine = line;
            return layout;
        };

        std::string text;
        int lineNumber = 0;
        while (std::getline(file, text))
        {
            ++lineNumber;
            // # から行の終わりまでは読まない
            const size_t comment = text.find('#');
            if (comment != std::string::npos)
            {
                text.erase(comment);
            }
            std::istringstream line(text);
            std::string command;
            if (!(line >> command))
            {
                continue;
            }

            if (command == "color")
            {
                std::string name;
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (!(line >> name >> r >> g >> b))
                {
                    return fail(lineNumber, "color の後に 名前 r g b が必要");
                }
                colors[name] = Color(r, g, b, 1.0f);
            }
            else if (command == "prop")
            {
                PropDescription prop;
                std::string diffuseName, emissionName;
                int collision = 0;
                if (!(line >> prop.Name >>
                    prop.Position.x >> prop.Position.y >> prop.Position.z >>
                    prop.Scale.x >> prop.Scale.y >> prop.Scale.z >>
                    diffuseName >> emissionName >> prop.Shininess >> collision))
                {
                    return fail(lineNumber, "prop の値が足りない（名前 位置3つ 大きさ3つ 色 光る色 光沢 当たり判定）");
                }
                const auto diffuse = colors.find(diffuseName);
                const auto emission = colors.find(emissionName);
                if (diffuse == colors.end() || emission == colors.end())
                {
                    return fail(lineNumber, "color で決めていない色の名前: " +
                        (diffuse == colors.end() ? diffuseName : emissionName));
                }
                prop.Diffuse = diffuse->second;
                prop.Emission = emission->second;
                prop.Collision = collision != 0;

                // 後ろに付ける指定（影なし・壁の古さ・光る範囲）
                std::string option;
                while (line >> option)
                {
                    if (option == "noshadow")
                    {
                        prop.CastsShadow = false;
                    }
                    else if (option == "weathering")
                    {
                        prop.Weathering = true;
                    }
                    else if (option == "glow")
                    {
                        if (!(line >> prop.GlowRange >> prop.GlowStrength))
                        {
                            return fail(lineNumber, "glow の後に 範囲 強さ が必要");
                        }
                    }
                    else
                    {
                        return fail(lineNumber, "知らない指定: " + option);
                    }
                }
                for (const PropDescription& existing : layout.Props)
                {
                    if (existing.Name == prop.Name)
                    {
                        return fail(lineNumber, "同じ名前が2回ある: " + prop.Name);
                    }
                }
                layout.Props.push_back(prop);
            }
            else
            {
                return fail(lineNumber, "知らない行: " + command);
            }
        }
        return layout;
    }

    void Apply(Wall& wall, const PropDescription& description)
    {
        wall.SetPosition(description.Position.x, description.Position.y, description.Position.z);
        wall.SetScale(description.Scale.x, description.Scale.y, description.Scale.z);
        wall.SetAppearance(description.Diffuse, description.Emission, description.Shininess);
        wall.SetCollisionEnabled(description.Collision);
        wall.SetCastsShadow(description.CastsShadow);
        wall.SetWeatheringSurface(description.Weathering);
        wall.SetGlowLight(description.GlowRange, description.GlowStrength);
        wall.SetVisible(true);
    }

    SpawnedProps Spawn(SceneLayoutBuilder& builder, const PropLayout& layout)
    {
        SpawnedProps props;
        props.reserve(layout.Props.size());
        for (const PropDescription& description : layout.Props)
        {
            Wall* wall = builder.Create<Wall>(description.Name);
            Apply(*wall, description);
            props.emplace_back(description.Name, wall);
        }
        return props;
    }

    void Reload(SceneLayoutBuilder& builder, const PropLayout& layout, SpawnedProps& props)
    {
        // いったん全部を隠し、ファイルにある名前の物だけを置き直して表示している（ファイルから消えた物は隠れたまま）
        for (auto& [name, wall] : props)
        {
            wall->SetVisible(false);
        }
        for (const PropDescription& description : layout.Props)
        {
            const auto found = std::find_if(props.begin(), props.end(),
                [&description](const auto& prop) { return prop.first == description.Name; });
            if (found != props.end())
            {
                Apply(*found->second, description);
            }
            else
            {
                // 新しく足した名前は、ここで作っている
                Wall* wall = builder.Create<Wall>(description.Name);
                Apply(*wall, description);
                props.emplace_back(description.Name, wall);
            }
        }
    }
}
