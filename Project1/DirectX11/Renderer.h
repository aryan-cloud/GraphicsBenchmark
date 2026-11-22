#pragma once

#include "../CubeScene.h"

#include <windows.h>
#include <wrl/client.h>

#include <array>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11Buffer;
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Query;
struct FrameSample;
struct FrameDiagnostics;

namespace DirectX11
{
    enum class RenderMode { Separate, Instanced };

    struct FrameInfo
    {
        wchar_t adapter[128]{};
        UINT width = 0;
        UINT height = 0;
    };

    struct PresentationInfo
    {
        HRESULT descriptionResult = E_FAIL;
        UINT swapEffect = 0;
        UINT bufferCount = 0;
        UINT swapChainFlags = 0;
        HRESULT tearingCheckResult = E_FAIL;
        bool tearingSupported = false;
        UINT presentInterval = 0;
        UINT presentFlags = 0;
    };

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        // Initialize once. The window must outlive the renderer.
        HRESULT Initialize(HWND window, UINT cubeCount, UINT vsync, RenderMode mode);
        HRESULT InitializeTiming();

        // Match the back buffer to the window's drawable client area.
        HRESULT Resize();
        // A sample must stay at a stable address through final query collection.
        HRESULT RenderFrame(const std::array<float, 4>& clearColor, FrameSample* sample = nullptr,
            FrameDiagnostics* diagnostics = nullptr);
        void FinishTiming();
        PresentationInfo ReadPresentationInfo() const;
        const FrameInfo& GetFrameInfo() const { return frameInfo_; }
        LONGLONG GetTimingFrequency() const { return qpcFrequency_; }

    private:
        struct QuerySet
        {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> start;
            Microsoft::WRL::ComPtr<ID3D11Query> end;
            FrameSample* sample = nullptr;
            UINT issuedFrameId = 0;
            bool retired = false;
        };

        HRESULT CreateDevice();
        HRESULT CreateSwapChain(HWND window);
        HRESULT CreateCubeBuffers();
        HRESULT CreateCubePipeline();
        HRESULT CreateFrameTargets();
        void UpdateDrawTransforms(UINT width, UINT height);
        UINT SubmitScene(const std::array<float, 4>& clearColor, bool countDraws) const;
        template<bool CountDraws>
        UINT SubmitDraws() const;
        QuerySet* BeginGpuSample(FrameSample& sample);
        bool CollectGpuSamples();
        HRESULT ReadGpuSample(QuerySet& queries) const;

        CubeScene scene_;
        std::vector<DirectX::XMFLOAT4X4> drawTransforms_;
        RenderMode mode_ = RenderMode::Separate;
        FrameInfo frameInfo_;
        HRESULT tearingCheckResult_ = E_FAIL;
        bool tearingSupported_ = false;
        UINT swapChainFlags_ = 0;
        UINT presentInterval_ = 1;
        UINT presentFlags_ = 0;

        // The context's bindings are cleared before these owning references release.
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthTarget_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> transformBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> instanceBuffer_;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;

        std::array<QuerySet, 16> queryRing_;
        std::size_t nextQuery_ = 0;
        LONGLONG qpcFrequency_ = 0;
    };
}
