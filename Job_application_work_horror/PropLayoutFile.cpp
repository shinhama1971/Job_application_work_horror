// ============================================================================
// ファイルの役割: ステージの小物の配置を、テキストファイルから読んでいる（書式は PropLayoutFile.h を参照）。
// 主な技術: 1行1つの簡単な書式の読み込み、色の名前の表、間違いの行番号の報告、コメントを残したままの書き戻し
// ============================================================================

#include "PropLayoutFile.h"

#include "SceneLayoutBuilder.h"
#include "Wall.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace DirectX::SimpleMath;

namespace
{
    // 数を短く書いている（小数3桁まで。後ろの0と小数点は省く）
    std::string FormatNumber(float value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.3f", value);
        std::string result(text);
        result.erase(result.find_last_not_of('0') + 1);
        if (!result.empty() && result.back() == '.')
        {
            result.pop_back();
        }
        return result == "-0" ? "0" : result;
    }

    // ファイルに書く値（位置・大きさ・色の名前・光沢・当たり判定・影・古さ・光）が同じか
    bool IsSameProp(const PropDescription& a, const PropDescription& b)
    {
        return a.Position == b.Position && a.Scale == b.Scale &&
            a.DiffuseName == b.DiffuseName && a.EmissionName == b.EmissionName &&
            a.Shininess == b.Shininess && a.Collision == b.Collision &&
            a.CastsShadow == b.CastsShadow && a.Weathering == b.Weathering &&
            a.GlowRange == b.GlowRange && a.GlowStrength == b.GlowStrength;
    }

    // prop の行なら小物の名前を、そうでなければ空の文字列を返している
    std::string PropNameOfLine(const std::string& text)
    {
        std::istringstream line(text.substr(0, text.find('#')));
        std::string command, name;
        if ((line >> command >> name) && command == "prop")
        {
            return name;
        }
        return {};
    }
}

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
        layout.Colors.emplace_back("none", Color(0.0f, 0.0f, 0.0f, 1.0f));
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
                const auto existing = std::find_if(layout.Colors.begin(), layout.Colors.end(),
                    [&name](const auto& color) { return color.first == name; });
                if (existing != layout.Colors.end())
                {
                    existing->second = Color(r, g, b, 1.0f);
                }
                else
                {
                    layout.Colors.emplace_back(name, Color(r, g, b, 1.0f));
                }
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
                const Color* diffuse = FindColor(layout, diffuseName);
                const Color* emission = FindColor(layout, emissionName);
                if (diffuse == nullptr || emission == nullptr)
                {
                    return fail(lineNumber, "color で決めていない色の名前: " +
                        (diffuse == nullptr ? diffuseName : emissionName));
                }
                prop.Diffuse = *diffuse;
                prop.Emission = *emission;
                prop.DiffuseName = diffuseName;
                prop.EmissionName = emissionName;
                prop.Collision = collision != 0;
                prop.Line = lineNumber;

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

    const Color* FindColor(const PropLayout& layout, const std::string& name)
    {
        for (const auto& [colorName, color] : layout.Colors)
        {
            if (colorName == name)
            {
                return &color;
            }
        }
        return nullptr;
    }

    std::string FormatProp(const PropDescription& description)
    {
        // 名前の後ろをそろえ、位置・大きさ・見た目の間を広めに空けて、手で書いた行と見た目を合わせている
        std::string text = "prop " + description.Name;
        text.append(description.Name.size() < 24 ? 24 - description.Name.size() : 1, ' ');
        text += FormatNumber(description.Position.x) + " " + FormatNumber(description.Position.y) + " " +
            FormatNumber(description.Position.z) + "   ";
        text += FormatNumber(description.Scale.x) + " " + FormatNumber(description.Scale.y) + " " +
            FormatNumber(description.Scale.z) + "   ";
        text += description.DiffuseName + " " + description.EmissionName + " " +
            FormatNumber(description.Shininess) + " " + (description.Collision ? "1" : "0");
        if (!description.CastsShadow)
        {
            text += " noshadow";
        }
        if (description.Weathering)
        {
            text += " weathering";
        }
        if (description.GlowRange > 0.0f)
        {
            text += " glow " + FormatNumber(description.GlowRange) + " " + FormatNumber(description.GlowStrength);
        }
        return text;
    }

    bool Save(const std::string& path, const PropLayout& layout, std::string& error)
    {
        // 今のファイルを読み直し、書き換える前の値と行を知っている（読めないファイルには上書きしない）
        const PropLayout current = Load(path);
        if (!current.IsValid())
        {
            error = "保存する前のファイルの" + std::to_string(current.ErrorLine) + "行目が読めない " + current.Error;
            return false;
        }
        std::vector<std::string> lines;
        {
            std::ifstream file(path);
            std::string text;
            while (std::getline(file, text))
            {
                if (!text.empty() && text.back() == '\r')
                {
                    text.pop_back();
                }
                lines.push_back(text);
            }
        }

        std::unordered_map<std::string, const PropDescription*> before;
        for (const PropDescription& prop : current.Props)
        {
            before[prop.Name] = &prop;
        }
        std::unordered_map<std::string, const PropDescription*> after;
        // 新しい小物は、元にした小物の行（Line）の後ろへ足す。行が分からなければファイルの最後へ足す
        std::vector<std::vector<std::string>> insertedAfterLine(lines.size() + 1);
        std::vector<std::string> appended;
        for (const PropDescription& prop : layout.Props)
        {
            after[prop.Name] = &prop;
            if (before.count(prop.Name) != 0)
            {
                continue;
            }
            if (prop.Line > 0 && static_cast<size_t>(prop.Line) <= lines.size())
            {
                insertedAfterLine[static_cast<size_t>(prop.Line)].push_back(FormatProp(prop));
            }
            else
            {
                appended.push_back(FormatProp(prop));
            }
        }

        std::ostringstream output;
        for (size_t index = 0; index < lines.size(); ++index)
        {
            const std::string& text = lines[index];
            const std::string name = PropNameOfLine(text);
            const auto edited = after.find(name);
            if (name.empty())
            {
                output << text << "\r\n";
            }
            else if (edited != after.end())
            {
                const auto original = before.find(name);
                if (original != before.end() && IsSameProp(*original->second, *edited->second))
                {
                    output << text << "\r\n";
                }
                else
                {
                    // 値が変わった行だけを書き直し、行の後ろのコメントは残している
                    output << FormatProp(*edited->second);
                    const size_t comment = text.find('#');
                    if (comment != std::string::npos)
                    {
                        output << "   " << text.substr(comment);
                    }
                    output << "\r\n";
                }
            }
            // （ファイルから消した小物の行は書かない）
            for (const std::string& added : insertedAfterLine[index + 1])
            {
                output << added << "\r\n";
            }
        }
        if (!appended.empty())
        {
            output << "\r\n# --- 配置エディタで足した小物\r\n";
            for (const std::string& added : appended)
            {
                output << added << "\r\n";
            }
        }

        const std::string text = output.str();
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size())))
        {
            error = "ファイルに書けない: " + path;
            return false;
        }
        return true;
    }
}
