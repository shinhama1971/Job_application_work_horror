#pragma once
#include "Renderer.h"
#include "ComputeShader.h"
#include "utility.h"
#include <SimpleMath.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>
#include <cstdlib>
namespace Effect {
// 光の筋を縦横半分で計算し、本描画でのみt16から読む。反射の視点とは共有しない。
class LightShafts final {
    using Matrix=DirectX::SimpleMath::Matrix;
    using Vector4=DirectX::SimpleMath::Vector4;
    // figure は見えない影の人影（xyz = 足もと、w = 見え方）。光の筋の中にも、人の形の影を落とすために渡している
    struct Parameters { Matrix inverse; Vector4 eyeTime; Vector4 viewport; UINT count; float padding[3]; Vector4 figure; };
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv, lightSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> parameters, lights;
    ComputeShader shader;
    float time=0;
    bool enabled=true;
public:
    void Init(int width,int height) {
        // 比較時だけ環境変数を1にして、既存の全解像度の積分へ戻せる。
        char mode[8]{}; size_t length=0;
        getenv_s(&length,mode,sizeof(mode),"SIGNAL_LOST_FULLRES_SHAFTS"); enabled=mode[0]!='1';
        auto d=Renderer::GetDevice();
        D3D11_TEXTURE2D_DESC t{};t.Width=(width+1)/2;t.Height=(height+1)/2;t.MipLevels=1;t.ArraySize=1;
        t.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        D3D11_BUFFER_DESC l{};l.ByteWidth=sizeof(ENVIRONMENT_POINT_LIGHT)*256;l.Usage=D3D11_USAGE_DEFAULT;
        l.BindFlags=D3D11_BIND_SHADER_RESOURCE;l.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;l.StructureByteStride=sizeof(ENVIRONMENT_POINT_LIGHT);
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_UNKNOWN;v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=256;
        if(FAILED(d->CreateTexture2D(&t,nullptr,texture.GetAddressOf())) ||
           FAILED(d->CreateShaderResourceView(texture.Get(),nullptr,srv.GetAddressOf())) ||
           FAILED(d->CreateUnorderedAccessView(texture.Get(),nullptr,uav.GetAddressOf())) ||
           FAILED(d->CreateBuffer(&l,nullptr,lights.GetAddressOf())) ||
           FAILED(d->CreateShaderResourceView(lights.Get(),&v,lightSRV.GetAddressOf())) ||
           !Renderer::CreateConstantBuffer(sizeof(Parameters),parameters.GetAddressOf()) ||
           !shader.Create("shader/lightShaftsCS.hlsl")) utility::ReportFatalError("光の筋の初期化に失敗しました");
    }
    void Unbind() { ID3D11ShaderResourceView* empty=nullptr;Renderer::GetDeviceContext()->PSSetShaderResources(16,1,&empty); }
    bool Compute(const Matrix& view,const Matrix& projection,const D3D11_VIEWPORT& viewport,
                 const std::vector<ENVIRONMENT_POINT_LIGHT>& source,float dt) {
        Unbind(); if(!enabled)return false;
        auto c=Renderer::GetDeviceContext();time+=dt;
        ENVIRONMENT_POINT_LIGHT data[256]{};const UINT count=static_cast<UINT>((std::min)(source.size(),size_t(256)));
        std::copy_n(source.begin(),count,data);c->UpdateSubresource(lights.Get(),0,nullptr,data,0,0);
        Parameters p{};p.inverse=(view*projection).Invert().Transpose();
        const auto eye=view.Invert().Translation();p.eyeTime=Vector4(eye.x,eye.y,eye.z,time);
        p.viewport=Vector4(viewport.Width,viewport.Height,viewport.TopLeftX,viewport.TopLeftY);p.count=count;
        p.figure=Renderer::GetUnseenFigure();
        c->UpdateSubresource(parameters.Get(),0,nullptr,&p,0,0);
        ID3D11RenderTargetView* target=Renderer::GetBackBufferRTV();c->OMSetRenderTargets(1,&target,nullptr);
        ID3D11Buffer* cb=parameters.Get();c->CSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* inputs[]={Renderer::GetDepthShaderResourceView(),lightSRV.Get()};
        c->CSSetShaderResources(0,2,inputs);auto output=uav.Get();c->CSSetUnorderedAccessViews(0,1,&output,nullptr);
        shader.SetGPU();c->Dispatch((static_cast<UINT>(viewport.Width)+15)/16,(static_cast<UINT>(viewport.Height)+15)/16,1);
        ID3D11UnorderedAccessView* noUAV=nullptr;c->CSSetUnorderedAccessViews(0,1,&noUAV,nullptr);
        ID3D11ShaderResourceView* noSRV[2]{};c->CSSetShaderResources(0,2,noSRV);c->CSSetShader(nullptr,nullptr,0);
        c->OMSetRenderTargets(1,&target,Renderer::GetDepthStencilView());auto image=srv.Get();c->PSSetShaderResources(16,1,&image);
        return true;
    }
};
}
