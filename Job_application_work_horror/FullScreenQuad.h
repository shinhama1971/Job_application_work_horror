#pragma once

#include "Renderer.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

namespace Graphics
{
    class FullScreenQuad
    {
    private:
        struct TimeBuffer
        {
            float time;
            float dummy1;
            float dummy2;
            float dummy3;
        };

        std::vector<VERTEX_3D> m_Vertices;
        std::vector<unsigned int> m_Indices;

        VertexBuffer<VERTEX_3D> m_VertexBuffer;
        IndexBuffer m_IndexBuffer;

        Shader m_Shader;
        std::unique_ptr<Material> m_Material;

        ID3D11Buffer* m_TimeBuffer = nullptr;

    public:
        void Init();
        void Uninit();

        void Draw(ID3D11ShaderResourceView* srv, float time);
    };
}