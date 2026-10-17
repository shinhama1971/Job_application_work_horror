// ============================================================================
// ファイルの役割: Debug版の配置エディタ（小物を画面で選んで動かし、配置のファイルへ書き戻す）。説明は PropLayoutEditor.h を参照。
// 主な技術: Dear ImGui、画面のクリック位置から伸ばした光線と箱の当たり判定（スラブ法）での選択
// ============================================================================

#include "PropLayoutEditor.h"

#include "Camera.h"
#include "DebugUI.h"
#include "SceneLayoutBuilder.h"
#include "Wall.h"

#if defined(ENABLE_IMGUI)
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX::SimpleMath;

namespace
{
    // 床の高さ（全ステージ共通）
    constexpr float FloorY = -100.0f;

    // 光線が箱（中心と大きさ）に当たるかを、軸ごとの出入りの範囲（スラブ法）で調べ、当たった距離を返している
    bool RayHitsBox(const Vector3& origin, const Vector3& direction, const Vector3& center, const Vector3& size, float& distance)
    {
        const float origins[3] = { origin.x, origin.y, origin.z };
        const float directions[3] = { direction.x, direction.y, direction.z };
        const float centers[3] = { center.x, center.y, center.z };
        const float halves[3] = { std::abs(size.x) * 0.5f, std::abs(size.y) * 0.5f, std::abs(size.z) * 0.5f };
        float enter = 0.0f;
        float leave = 1.0e9f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float minimum = centers[axis] - halves[axis];
            const float maximum = centers[axis] + halves[axis];
            if (std::abs(directions[axis]) < 1.0e-6f)
            {
                // この軸に平行な光線は、範囲の外なら当たらない
                if (origins[axis] < minimum || origins[axis] > maximum)
                {
                    return false;
                }
                continue;
            }
            float axisEnter = (minimum - origins[axis]) / directions[axis];
            float axisLeave = (maximum - origins[axis]) / directions[axis];
            if (axisEnter > axisLeave)
            {
                std::swap(axisEnter, axisLeave);
            }
            enter = (std::max)(enter, axisEnter);
            leave = (std::min)(leave, axisLeave);
            if (enter > leave)
            {
                return false;
            }
        }
        distance = enter;
        return true;
    }
}

void PropLayoutEditor::SetLayout(const PropLayout& layout)
{
    m_Layout = layout;
    m_Loaded = true;
    m_Dirty = false;
    if (FindDescription(m_Selected) == nullptr)
    {
        m_Selected.clear();
    }
}

PropDescription* PropLayoutEditor::FindDescription(const std::string& name)
{
    for (PropDescription& description : m_Layout.Props)
    {
        if (description.Name == name)
        {
            return &description;
        }
    }
    return nullptr;
}

Wall* PropLayoutEditor::FindWall(PropLayoutFile::SpawnedProps& props, const std::string& name)
{
    for (auto& [propName, wall] : props)
    {
        if (propName == name)
        {
            return wall;
        }
    }
    return nullptr;
}

void PropLayoutEditor::PickAlongRay(const Vector3& origin, const Vector3& direction)
{
    float nearest = 1.0e9f;
    for (const PropDescription& description : m_Layout.Props)
    {
        if (m_IgnoreWeatheredSurfaces && description.Weathering)
        {
            continue;
        }
        float distance = 0.0f;
        if (RayHitsBox(origin, direction, description.Position, description.Scale, distance) && distance < nearest)
        {
            nearest = distance;
            m_Selected = description.Name;
        }
    }
}

void PropLayoutEditor::RestoreHighlight(PropLayoutFile::SpawnedProps& props)
{
    if (m_Highlighted.empty())
    {
        return;
    }
    // 消した小物は隠れたままにするため、表示は変えずに色だけを戻している
    const PropDescription* description = FindDescription(m_Highlighted);
    Wall* wall = FindWall(props, m_Highlighted);
    if (description != nullptr && wall != nullptr)
    {
        wall->SetAppearance(description->Diffuse, description->Emission, description->Shininess);
    }
    m_Highlighted.clear();
}

#if defined(ENABLE_IMGUI)

PropLayoutEditor::Action PropLayoutEditor::Draw(
    SceneLayoutBuilder& builder, PropLayoutFile::SpawnedProps& props, const Camera& camera)
{
    if (!Debug::UI::IsVisible())
    {
        RestoreHighlight(props);
        return Action::None;
    }
    if (!m_Loaded)
    {
        SetLayout(PropLayoutFile::Load(m_Path));
    }

    Action action = Action::None;
    const ImGuiIO& io = ImGui::GetIO();

    // 窓の外（景色）をクリックしたら、カーソルの位置から奥へ伸ばした光線で小物を選んでいる
    if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
    {
        Matrix view;
        Matrix projection;
        camera.GetMainMatrices(view, projection);
        const Matrix inverse = (view * projection).Invert();
        const float x = io.MousePos.x / io.DisplaySize.x * 2.0f - 1.0f;
        const float y = 1.0f - io.MousePos.y / io.DisplaySize.y * 2.0f;
        const Vector3 nearPoint = DirectX::XMVector3TransformCoord(Vector3(x, y, 0.0f), inverse);
        const Vector3 farPoint = DirectX::XMVector3TransformCoord(Vector3(x, y, 1.0f), inverse);
        Vector3 direction = farPoint - nearPoint;
        direction.Normalize();
        PickAlongRay(nearPoint, direction);
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 440.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Prop Layout Editor");
    ImGui::TextUnformatted(m_Path.c_str());
    if (m_Dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.76f, 0.25f, 1.0f), "(unsaved)");
    }
    if (ImGui::Button("Save to file"))
    {
        RestoreHighlight(props);
        if (PropLayoutFile::Save(m_Path, m_Layout, m_Error))
        {
            m_Dirty = false;
            m_Status = "Saved.";
            action = Action::Saved;
        }
        else
        {
            m_Status = "Save failed (reason shown on the HUD).";
            action = Action::SaveFailed;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload file (discard)"))
    {
        m_Status = "Reloaded.";
        action = Action::ReloadFile;
    }
    ImGui::SameLine();
    if (ImGui::Button("Pick in view"))
    {
        PickAlongRay(camera.GetPosition(), camera.GetForward());
    }
    ImGui::Checkbox("Ignore weathered surfaces (ceiling)", &m_IgnoreWeatheredSurfaces);
    ImGui::TextDisabled("Click a prop in the scene to select it.");
    if (!m_Status.empty())
    {
        ImGui::TextUnformatted(m_Status.c_str());
    }

    // 小物の一覧（名前の一部で絞り込める）
    ImGui::InputText("Filter", m_Filter, sizeof(m_Filter));
    if (ImGui::BeginListBox("##props", ImVec2(-1.0f, 170.0f)))
    {
        for (const PropDescription& description : m_Layout.Props)
        {
            if (m_Filter[0] != '\0' && description.Name.find(m_Filter) == std::string::npos)
            {
                continue;
            }
            const bool selected = description.Name == m_Selected;
            if (ImGui::Selectable(description.Name.c_str(), selected))
            {
                m_Selected = description.Name;
            }
            if (selected && ImGui::IsWindowAppearing())
            {
                ImGui::SetScrollHereY();
            }
        }
        ImGui::EndListBox();
    }

    // 選んだ小物の値（変えたらすぐWallへ当てはめている）
    PropDescription* description = FindDescription(m_Selected);
    Wall* wall = description != nullptr ? FindWall(props, description->Name) : nullptr;
    if (description != nullptr && wall != nullptr)
    {
        ImGui::SeparatorText(description->Name.c_str());
        bool changed = false;
        changed |= ImGui::DragFloat3("Position", &description->Position.x, 0.25f);
        changed |= ImGui::DragFloat3("Size", &description->Scale.x, 0.1f, 0.1f, 1000.0f);

        // 色は color で付けた名前から選ぶ（ファイルに名前で書き戻すため）
        const auto colorCombo = [this, &changed](const char* label, std::string& name, Color& color)
        {
            if (ImGui::BeginCombo(label, name.c_str()))
            {
                for (const auto& [colorName, value] : m_Layout.Colors)
                {
                    if (ImGui::Selectable(colorName.c_str(), colorName == name))
                    {
                        name = colorName;
                        color = value;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        };
        colorCombo("Color", description->DiffuseName, description->Diffuse);
        colorCombo("Emission", description->EmissionName, description->Emission);
        changed |= ImGui::DragFloat("Shininess", &description->Shininess, 0.2f, 0.0f, 128.0f);
        changed |= ImGui::Checkbox("Collision", &description->Collision);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Shadow", &description->CastsShadow);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Weathering", &description->Weathering);
        changed |= ImGui::DragFloat("Glow range", &description->GlowRange, 0.5f, 0.0f, 300.0f);
        changed |= ImGui::DragFloat("Glow strength", &description->GlowStrength, 0.05f, 0.0f, 10.0f);

        if (ImGui::Button("Snap to floor"))
        {
            description->Position.y = FloorY + std::abs(description->Scale.y) * 0.5f;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bring in front"))
        {
            // 目の前（水平に40）へ持ってくる。高さはそのまま
            Vector3 forward = camera.GetForward();
            forward.y = 0.0f;
            forward.Normalize();
            description->Position.x = camera.GetPosition().x + forward.x * 40.0f;
            description->Position.z = camera.GetPosition().z + forward.z * 40.0f;
            changed = true;
        }
        if (changed)
        {
            PropLayoutFile::Apply(*wall, *description);
            m_Dirty = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Duplicate"))
        {
            // 名前の後ろに番号を付けて複製し、少し横へずらしている。ファイルでは元の行の後ろに足される
            PropDescription copy = *description;
            int number = 2;
            do
            {
                copy.Name = description->Name + "_" + std::to_string(number++);
            } while (FindDescription(copy.Name) != nullptr);
            copy.Position.x += std::abs(copy.Scale.x) + 2.0f;
            // 前に消した同じ名前の小物があれば、それを使い直している
            Wall* copyWall = FindWall(props, copy.Name);
            if (copyWall == nullptr)
            {
                copyWall = builder.Create<Wall>(copy.Name);
                props.emplace_back(copy.Name, copyWall);
            }
            PropLayoutFile::Apply(*copyWall, copy);
            RestoreHighlight(props);
            const auto source = std::find_if(m_Layout.Props.begin(), m_Layout.Props.end(),
                [this](const PropDescription& prop) { return prop.Name == m_Selected; });
            m_Selected = copy.Name;
            m_Layout.Props.insert(source + 1, copy);
            m_Dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            RestoreHighlight(props);
            wall->SetVisible(false);
            m_Layout.Props.erase(std::remove_if(m_Layout.Props.begin(), m_Layout.Props.end(),
                [this](const PropDescription& prop) { return prop.Name == m_Selected; }), m_Layout.Props.end());
            m_Selected.clear();
            m_Dirty = true;
        }
    }
    else
    {
        ImGui::TextDisabled("No prop selected.");
    }
    ImGui::End();

    // 選んだ小物を黄色く明滅させている（選び直したら、前の小物の色を戻す）
    if (m_Highlighted != m_Selected)
    {
        RestoreHighlight(props);
    }
    description = FindDescription(m_Selected);
    wall = description != nullptr ? FindWall(props, m_Selected) : nullptr;
    if (description != nullptr && wall != nullptr)
    {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        wall->SetAppearance(description->Diffuse,
            description->Emission + Color(0.55f, 0.42f, 0.05f, 0.0f) * pulse,
            description->Shininess);
        m_Highlighted = m_Selected;
    }
    return action;
}

#else

PropLayoutEditor::Action PropLayoutEditor::Draw(
    SceneLayoutBuilder& builder, PropLayoutFile::SpawnedProps& props, const Camera& camera)
{
    (void)builder;
    (void)props;
    (void)camera;
    return Action::None;
}

#endif
