// 呼吸する壁の格子・描画情報と、膨らみに合わせた当たり判定。
#include "Wall.h"
#include "Renderer.h"
#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "shader/breathingWallShape.hlsli"

using namespace DirectX::SimpleMath;

void Wall::EnableBreathing(const Vector3& faceNormal, int columns, int rows, float edgeWidth)
{
    columns = (std::max)(columns, 1);
    rows = (std::max)(rows, 1);
    const bool normalAlongZ = faceNormal.z != 0.0f;

    // 箱の6面（各4頂点・12インデックス）のうち、動かす面だけを格子に置き換えている
    constexpr std::size_t VerticesPerFace = 4;
    constexpr std::size_t IndicesPerFace = 12;
    std::vector<VERTEX_3D> vertices;
    std::vector<unsigned int> indices;
    const std::size_t faceCount = m_BoxVertices.size() / VerticesPerFace;
    for (std::size_t face = 0; face < faceCount; ++face)
    {
        const VERTEX_3D& first = m_BoxVertices[face * VerticesPerFace];
        const unsigned int base = static_cast<unsigned int>(vertices.size());
        if (first.normal.Dot(faceNormal) <= 0.5f)
        {
            // ほかの面はそのまま写している（インデックスは新しい並びの番号に直す）
            const unsigned int oldBase = static_cast<unsigned int>(face * VerticesPerFace);
            vertices.insert(vertices.end(),
                m_BoxVertices.begin() + face * VerticesPerFace, m_BoxVertices.begin() + (face + 1) * VerticesPerFace);
            for (std::size_t index = face * IndicesPerFace; index < (face + 1) * IndicesPerFace; ++index)
            {
                indices.push_back(m_BoxIndices[index] - oldBase + base);
            }
            continue;
        }
        // 横の格子は x または z の小さい側から並べ、当たり判定の補間と対角線をそろえる。
        for (int column = 0; column <= columns; ++column)
        {
            for (int row = 0; row <= rows; ++row)
            {
                const float x = -0.5f + static_cast<float>(column) / static_cast<float>(columns);
                const float y = -0.5f + static_cast<float>(row) / static_cast<float>(rows);
                VERTEX_3D vertex = first;
                vertex.position = normalAlongZ ? Vector3(x, y, 0.5f * faceNormal.z)
                    : Vector3(0.5f * faceNormal.x, y, x);
                const bool uvIncreases = normalAlongZ ? faceNormal.z > 0.0f : faceNormal.x < 0.0f;
                vertex.uv = Vector2(uvIncreases ? x + 0.5f : 0.5f - x, 0.5f - y);
                vertices.push_back(vertex);
            }
        }
        // 1マスを2つの三角形にし、箱と同じく表と裏の両方の順番を持たせている
        const auto at = [base, rows](int column, int row)
        {
            return base + static_cast<unsigned int>(column * (rows + 1) + row);
        };
        for (int column = 0; column < columns; ++column)
        {
            for (int row = 0; row < rows; ++row)
            {
                const unsigned int a = at(column, row);
                const unsigned int b = at(column, row + 1);
                const unsigned int c = at(column + 1, row);
                const unsigned int d = at(column + 1, row + 1);
                const unsigned int cell[] = { a, b, c, c, b, d, c, b, a, d, b, c };
                indices.insert(indices.end(), std::begin(cell), std::end(cell));
            }
        }
    }

    m_Vertices = std::move(vertices);
    m_Indices = std::move(indices);
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/breathingWallVS.hlsl", "shader/breathingWallPS.hlsl");


    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.ByteWidth = sizeof(BREATHING_WALL_BUFFER);
    if (FAILED(Renderer::GetDevice()->CreateBuffer(&desc, nullptr, m_BreathBuffer.ReleaseAndGetAddressOf())))
    {
        Utility::ReportFatalError("呼吸する壁の定数バッファの作成に失敗しました。");
    }
    m_BreathFaceNormal = faceNormal;
    m_BreathColumns = columns;
    m_BreathRows = rows;
    m_BreathEdgeWidth = edgeWidth;
    m_Breathing = true;
}

void Wall::SetBreathing(float time, float amplitude, bool wholeFace)
{
    m_BreathTime = time;
    m_BreathAmplitude = amplitude;
    m_BreathWholeFace = wholeFace;
}

void Wall::BindBreathing()
{
    BREATHING_WALL_BUFFER buffer{};
    buffer.Params = Vector4(m_BreathTime, m_BreathAmplitude, m_BreathEdgeWidth, m_BreathWholeFace ? 1.0f : 0.0f);
    buffer.FaceNormal = Vector4(m_BreathFaceNormal.x, m_BreathFaceNormal.y, m_BreathFaceNormal.z, 0.0f);
    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->UpdateSubresource(m_BreathBuffer.Get(), 0, nullptr, &buffer, 0, 0);
    ID3D11Buffer* breathBuffer = m_BreathBuffer.Get();
    context->VSSetConstantBuffers(SIGNAL_LOST_OBJECT_DRAW_BUFFER_SLOT, 1, &breathBuffer);
}

// 格子の2つの三角形と同じ対角線で、その位置の押し出し量を補間する。
float Wall::SampleBreathingFace(float along, float height) const
{
    using namespace BreathingWallShape;
    const bool normalAlongZ = m_BreathFaceNormal.z != 0.0f;
    const float centerAlong = normalAlongZ ? m_Position.x : m_Position.z;
    const float width = std::abs(normalAlongZ ? m_Scale.x : m_Scale.z);
    const float wallHeight = std::abs(m_Scale.y);
    const float halfDepth = std::abs(normalAlongZ ? m_Scale.z : m_Scale.x) * 0.5f;
    const float gridX = (along - centerAlong + width * 0.5f) / width * m_BreathColumns;
    const float gridY = (height - m_Position.y + wallHeight * 0.5f) / wallHeight * m_BreathRows;
    if (gridX <= 0.0f || gridX >= m_BreathColumns || gridY <= 0.0f || gridY >= m_BreathRows)
    {
        return 0.0f;
    }

    const int column = static_cast<int>(gridX);
    const int row = static_cast<int>(gridY);
    const float tx = gridX - column;
    const float ty = gridY - row;
    const auto offsetAt = [&](int x, int y)
    {
        const float localAlong = static_cast<float>(x) / m_BreathColumns - 0.5f;
        const float localY = static_cast<float>(y) / m_BreathRows - 0.5f;
        Vector3 point = m_Position + m_BreathFaceNormal * halfDepth;
        point.y += localY * wallHeight;
        (normalAlongZ ? point.x : point.z) += localAlong * width;
        const float planeAlong = normalAlongZ ? point.x : point.z;
        return BreathOffset(m_BreathTime, m_BreathAmplitude, m_BreathEdgeWidth, m_BreathWholeFace,
            (0.5f - std::abs(localAlong)) * width, (0.5f - std::abs(localY)) * wallHeight,
            planeAlong, point.y);
    };

    const float leftTop = offsetAt(column, row + 1);
    const float rightBottom = offsetAt(column + 1, row);
    if (tx + ty <= 1.0f)
    {
        return offsetAt(column, row) * (1.0f - tx - ty) + leftTop * ty + rightBottom * tx;
    }
    return offsetAt(column + 1, row + 1) * (tx + ty - 1.0f) + leftTop * (1.0f - tx) + rightBottom * (1.0f - ty);
}

float Wall::GetBreathingOffsetLimit() const
{
    const float maximumRatio = m_BreathWholeFace ? BreathingWallShape::BreathWholeFaceMaximumRatio
        : BreathingWallShape::BreathLocalMaximumRatio;
    return m_BreathAmplitude * maximumRatio;
}

void Wall::ResolveBreathingCollision(Vector3& position, float radius) const
{
    const bool normalAlongZ = m_BreathFaceNormal.z != 0.0f;
    const float width = std::abs(normalAlongZ ? m_Scale.x : m_Scale.z);
    const float depth = std::abs(normalAlongZ ? m_Scale.z : m_Scale.x);
    const float along = normalAlongZ ? position.x : position.z;
    const float centerAlong = normalAlongZ ? m_Position.x : m_Position.z;
    const float signedDistance = (position - m_Position).Dot(m_BreathFaceNormal);
    const float faceDistance = signedDistance - depth * 0.5f;
    const float maximumOffset = GetBreathingOffsetLimit();
    if (signedDistance < 0.0f || faceDistance >= maximumOffset + radius ||
        std::abs(along - centerAlong) >= width * 0.5f + radius)
    {
        return;
    }

    // 平面の円を胴体の3つの高さで調べ、横に張り出した面にも肩が先に当たるようにする。
    constexpr float BodySampleHeights[] = { 12.0f, 24.0f, 39.0f };
    constexpr float ShoulderSampleRadiusRatio = 0.7f;
    const float shoulderOffset = radius * ShoulderSampleRadiusRatio;
    const float horizontalOffsets[] = { -shoulderOffset, 0.0f, shoulderOffset };
    float closestAllowedDistance = faceDistance;
    for (float horizontalOffset : horizontalOffsets)
    {
        const float contactAlong = along + horizontalOffset;
        if (std::abs(contactAlong - centerAlong) >= width * 0.5f)
        {
            continue;
        }
        const float circleReach = std::sqrt(radius * radius - horizontalOffset * horizontalOffset);
        for (float bodyHeight : BodySampleHeights)
        {
            closestAllowedDistance = (std::max)(closestAllowedDistance,
                SampleBreathingFace(contactAlong, position.y + bodyHeight) + circleReach);
        }
    }
    position += m_BreathFaceNormal * (closestAllowedDistance - faceDistance);
}
