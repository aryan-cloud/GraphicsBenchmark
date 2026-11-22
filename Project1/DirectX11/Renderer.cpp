#include "Renderer.h"
#include "CubeGeometry.h"
#include "../Benchmark.h"

#include <d3d11.h>
#include <dxgi1_5.h>
#include <DirectXMath.h>

#include <cassert>
#include <cstddef>
#include <new>

namespace
{
    #include "CubeVS.bytecode.h"
    #include "CubeInstancedVS.bytecode.h"
    #include "CubePS.bytecode.h"

    static_assert(sizeof(DirectX::XMFLOAT4X4) == 64,
        "CubeTransforms must match the HLSL constant buffer.");
}

namespace DirectX11
{
    using Microsoft::WRL::ComPtr;

    Renderer::Renderer() = default;
    Renderer::~Renderer()
    {
        if (context_)
            context_->ClearState();
    }

    HRESULT Renderer::Initialize(HWND window, UINT cubeCount, UINT vsync, RenderMode mode)
    {
        presentInterval_ = vsync;
        mode_ = mode;
        try
        {
            scene_ = CreateCubeScene(cubeCount);
            drawTransforms_.resize(scene_.positions.size());
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        HRESULT result = CreateDevice();
        if (FAILED(result))
            return result;

        result = CreateSwapChain(window);
        if (FAILED(result))
            return result;

        result = CreateCubeBuffers();
        if (FAILED(result))
            return result;

        result = CreateCubePipeline();
        if (FAILED(result))
            return result;

        return CreateFrameTargets();
    }

    HRESULT Renderer::Resize()
    {
        // Drop references to the old back buffer before DXGI replaces it.
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        renderTarget_.Reset();
        depthTarget_.Reset();
        const HRESULT result = swapChain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, swapChainFlags_);
        if (FAILED(result))
            return result;

        return CreateFrameTargets();
    }

    HRESULT Renderer::InitializeTiming()
    {
        LARGE_INTEGER frequency;
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            return E_FAIL;
        qpcFrequency_ = frequency.QuadPart;

        for (auto& queries : queryRing_)
        {
            D3D11_QUERY_DESC description{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
            HRESULT result = device_->CreateQuery(&description, queries.disjoint.GetAddressOf());
            if (FAILED(result))
                return result;
            description.Query = D3D11_QUERY_TIMESTAMP;
            result = device_->CreateQuery(&description, queries.start.GetAddressOf());
            if (FAILED(result))
                return result;
            result = device_->CreateQuery(&description, queries.end.GetAddressOf());
            if (FAILED(result))
                return result;
        }
        return S_OK;
    }

    HRESULT Renderer::RenderFrame(const std::array<float, 4>& clearColor, FrameSample* sample,
        FrameDiagnostics* diagnostics)
    {
        assert(renderTarget_ && depthTarget_);
        const UINT presentInterval = presentInterval_;
        const UINT presentFlags = presentFlags_;
        if (diagnostics != nullptr)
        {
            diagnostics->presentInterval = presentInterval;
            diagnostics->presentFlags = presentFlags;
        }
        QuerySet* queries = nullptr;
        LARGE_INTEGER submitStart, submitEnd, presentStart, presentEnd;
        if (sample != nullptr)
        {
            assert(qpcFrequency_ > 0);
            CollectGpuSamples();
            queries = BeginGpuSample(*sample);
            QueryPerformanceCounter(&submitStart);
        }

        const UINT drawCount = SubmitScene(clearColor, diagnostics != nullptr);
        if (sample == nullptr)
        {
            const HRESULT result = swapChain_->Present(presentInterval, presentFlags);
            if (diagnostics != nullptr)
                diagnostics->drawSubmissions = drawCount;
            return result;
        }

        QueryPerformanceCounter(&submitEnd);
        if (queries != nullptr)
        {
            context_->End(queries->end.Get());
            context_->End(queries->disjoint.Get());
        }

        QueryPerformanceCounter(&presentStart);
        const HRESULT result = swapChain_->Present(presentInterval, presentFlags);
        QueryPerformanceCounter(&presentEnd);

        if (diagnostics != nullptr)
            diagnostics->drawSubmissions = drawCount;
        sample->cpuSubmitMs = (submitEnd.QuadPart - submitStart.QuadPart) * 1000.0 / qpcFrequency_;
        sample->presentMs = (presentEnd.QuadPart - presentStart.QuadPart) * 1000.0 / qpcFrequency_;
        return result;
    }

    UINT Renderer::SubmitScene(const std::array<float, 4>& clearColor, bool countDraws) const
    {
        // Present unbinds the flip-model back buffer. Bind both targets each frame.
        ID3D11RenderTargetView* renderTarget = renderTarget_.Get();
        context_->OMSetRenderTargets(1, &renderTarget, depthTarget_.Get());
        context_->ClearRenderTargetView(renderTarget_.Get(), clearColor.data());
        context_->ClearDepthStencilView(depthTarget_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        return countDraws ? SubmitDraws<true>() : SubmitDraws<false>();
    }

    template<bool CountDraws>
    UINT Renderer::SubmitDraws() const
    {
        if (mode_ == RenderMode::Instanced)
        {
            context_->DrawIndexedInstanced(ARRAYSIZE(CubeIndices), static_cast<UINT>(drawTransforms_.size()), 0, 0, 0);
            return CountDraws ? 1 : 0;
        }

        UINT count = 0;
        for (const auto& transform : drawTransforms_)
        {
            context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr, &transform, 0, 0);
            context_->DrawIndexed(ARRAYSIZE(CubeIndices), 0, 0);
            if (CountDraws)
                ++count;
        }
        return count;
    }

    Renderer::QuerySet* Renderer::BeginGpuSample(FrameSample& sample)
    {
        sample.gpuStatus = GpuStatus::QueryRingDisabled;
        for (std::size_t offset = 0; offset < queryRing_.size(); ++offset)
        {
            const std::size_t index = (nextQuery_ + offset) % queryRing_.size();
            auto& queries = queryRing_[index];
            if (queries.retired)
                continue;
            if (queries.sample != nullptr)
            {
                sample.gpuStatus = GpuStatus::RingFull;
                continue;
            }

            sample.gpuStatus = GpuStatus::Pending;
            queries.sample = &sample;
            if (sample.diagnostics != nullptr)
            {
                queries.issuedFrameId = sample.frameId;
                sample.diagnostics->querySlot = static_cast<int>(index);
            }
            nextQuery_ = (index + 1) % queryRing_.size();
            context_->Begin(queries.disjoint.Get());
            context_->End(queries.start.Get());
            return &queries;
        }
        return nullptr;
    }

    bool Renderer::CollectGpuSamples()
    {
        bool pending = false;
        for (auto& queries : queryRing_)
        {
            if (queries.sample == nullptr)
                continue;

            const HRESULT result = ReadGpuSample(queries);
            if (result == S_FALSE)
            {
                pending = true;
                continue;
            }
            if (result != S_OK)
            {
                queries.sample->gpuStatus = GpuStatus::QueryError;
                queries.sample->queryResult = result;
                // The driver did not confirm completion. Never issue this set again.
                queries.retired = true;
            }
            if (queries.sample->diagnostics != nullptr)
            {
                queries.sample->diagnostics->collectedSlot = static_cast<int>(&queries - queryRing_.data());
                queries.sample->diagnostics->collectedFrameId = queries.issuedFrameId;
            }
            queries.sample = nullptr;
        }
        return pending;
    }

    HRESULT Renderer::ReadGpuSample(QuerySet& queries) const
    {
        constexpr UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        HRESULT result = context_->GetData(queries.disjoint.Get(), &disjoint, sizeof(disjoint), flags);
        if (result != S_OK)
            return result;
        UINT64 start = 0;
        result = context_->GetData(queries.start.Get(), &start, sizeof(start), flags);
        if (result != S_OK)
            return result;
        UINT64 end = 0;
        result = context_->GetData(queries.end.Get(), &end, sizeof(end), flags);
        if (result != S_OK)
            return result;

        auto& sample = *queries.sample;
        if (sample.diagnostics != nullptr)
        {
            auto& diagnostics = *sample.diagnostics;
            diagnostics.startTicks = start;
            diagnostics.endTicks = end;
            diagnostics.frequencyHz = disjoint.Frequency;
            diagnostics.disjoint = disjoint.Disjoint;
            diagnostics.timestampsRead = true;
        }
        if (disjoint.Disjoint)
            sample.gpuStatus = GpuStatus::Disjoint;
        else if (disjoint.Frequency == 0)
            sample.gpuStatus = GpuStatus::InvalidFrequency;
        else if (end < start)
            sample.gpuStatus = GpuStatus::InvalidTimestamps;
        else
        {
            sample.gpuElapsedMs = (end - start) * 1000.0 / disjoint.Frequency;
            sample.gpuStatus = GpuStatus::Valid;
        }
        return S_OK;
    }

    void Renderer::FinishTiming()
    {
        if (!CollectGpuSamples())
            return;

        // Submit any buffered work once, after the last measured frame.
        context_->Flush();
        const ULONGLONG deadline = GetTickCount64() + 2000;
        do
        {
            Sleep(1);
            if (!CollectGpuSamples())
                return;
        } while (GetTickCount64() < deadline);

        for (auto& queries : queryRing_)
        {
            if (queries.sample != nullptr)
            {
                queries.sample->gpuStatus = GpuStatus::Timeout;
                queries.sample = nullptr;
                queries.retired = true;
            }
        }
    }

    PresentationInfo Renderer::ReadPresentationInfo() const
    {
        PresentationInfo info;
        info.tearingCheckResult = tearingCheckResult_;
        info.tearingSupported = tearingSupported_;
        info.swapChainFlags = swapChainFlags_;
        info.presentInterval = presentInterval_;
        info.presentFlags = presentFlags_;

        DXGI_SWAP_CHAIN_DESC1 description{};
        info.descriptionResult = swapChain_->GetDesc1(&description);
        if (info.descriptionResult == S_OK)
        {
            info.swapEffect = description.SwapEffect;
            info.bufferCount = description.BufferCount;
            info.swapChainFlags = description.Flags;
        }
        return info;
    }

    HRESULT Renderer::CreateDevice()
    {
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        constexpr D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, &featureLevel, 1, D3D11_SDK_VERSION, device_.GetAddressOf(),
            nullptr, context_.GetAddressOf());
    }

    HRESULT Renderer::CreateSwapChain(HWND window)
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT result = device_.As(&dxgiDevice);
        if (FAILED(result))
            return result;

        ComPtr<IDXGIAdapter> adapter;
        result = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        if (FAILED(result))
            return result;

        DXGI_ADAPTER_DESC adapterDescription{};
        result = adapter->GetDesc(&adapterDescription);
        if (FAILED(result))
            return result;
        wcscpy_s(frameInfo_.adapter, adapterDescription.Description);

        ComPtr<IDXGIFactory2> factory;
        result = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(result))
            return result;

        ComPtr<IDXGIFactory5> factory5;
        BOOL allowTearing = FALSE;
        tearingCheckResult_ = factory.As(&factory5);
        if (tearingCheckResult_ == S_OK)
        {
            tearingCheckResult_ = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing));
        }
        tearingSupported_ = tearingCheckResult_ == S_OK && allowTearing != FALSE;

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        result = factory->CreateSwapChainForHwnd(device_.Get(), window, &description,
            nullptr, nullptr, swapChain_.GetAddressOf());
        if (FAILED(result))
            return result;

        swapChainFlags_ = description.Flags;
        result = factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(result))
            return result;

        // The app creates a windowed chain and has no fullscreen transitions.
        BOOL fullscreen = TRUE;
        if (tearingSupported_ && (swapChainFlags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0 &&
            presentInterval_ == 0 && swapChain_->GetFullscreenState(&fullscreen, nullptr) == S_OK && !fullscreen)
            presentFlags_ = DXGI_PRESENT_ALLOW_TEARING;
        return S_OK;
    }

    HRESULT Renderer::CreateCubeBuffers()
    {
        D3D11_BUFFER_DESC description{};
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.ByteWidth = sizeof(CubeVertices);
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = CubeVertices;
        HRESULT result = device_->CreateBuffer(&description, &data, vertexBuffer_.GetAddressOf());
        if (FAILED(result))
            return result;

        description.ByteWidth = sizeof(CubeIndices);
        description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        data.pSysMem = CubeIndices;
        result = device_->CreateBuffer(&description, &data, indexBuffer_.GetAddressOf());
        if (FAILED(result))
            return result;

        description.Usage = D3D11_USAGE_DEFAULT;
        if (mode_ == RenderMode::Instanced)
        {
            description.ByteWidth = static_cast<UINT>(drawTransforms_.size() * sizeof(DirectX::XMFLOAT4X4));
            description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            return device_->CreateBuffer(&description, nullptr, instanceBuffer_.GetAddressOf());
        }
        description.ByteWidth = sizeof(DirectX::XMFLOAT4X4);
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return device_->CreateBuffer(&description, nullptr, transformBuffer_.GetAddressOf());
    }

    HRESULT Renderer::CreateCubePipeline()
    {
        const bool instanced = mode_ == RenderMode::Instanced;
        const BYTE* vertexBytecode = instanced ? CubeInstancedVertexShaderBytecode : CubeVertexShaderBytecode;
        const SIZE_T vertexBytecodeSize = instanced ? sizeof(CubeInstancedVertexShaderBytecode) :
            sizeof(CubeVertexShaderBytecode);
        HRESULT result = device_->CreateVertexShader(vertexBytecode, vertexBytecodeSize, nullptr,
            vertexShader_.GetAddressOf());
        if (FAILED(result))
            return result;

        result = device_->CreatePixelShader(CubePixelShaderBytecode,
            sizeof(CubePixelShaderBytecode), nullptr, pixelShader_.GetAddressOf());
        if (FAILED(result))
            return result;

        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(CubeVertex, position),
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(CubeVertex, color),
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "INSTANCE_TRANSFORM", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
                D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "INSTANCE_TRANSFORM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16,
                D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "INSTANCE_TRANSFORM", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32,
                D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "INSTANCE_TRANSFORM", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48,
                D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };
        result = device_->CreateInputLayout(elements, instanced ? ARRAYSIZE(elements) : 2,
            vertexBytecode, vertexBytecodeSize, inputLayout_.GetAddressOf());
        if (FAILED(result))
            return result;

        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS;
        result = device_->CreateDepthStencilState(&depth, depthState_.GetAddressOf());
        if (FAILED(result))
            return result;

        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_BACK;
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthClipEnable = TRUE;
        result = device_->CreateRasterizerState(&rasterizer, rasterizerState_.GetAddressOf());
        if (FAILED(result))
            return result;

        ID3D11Buffer* buffers[] = { vertexBuffer_.Get(), instanceBuffer_.Get() };
        const UINT strides[] = { sizeof(CubeVertex), sizeof(DirectX::XMFLOAT4X4) };
        const UINT offsets[] = { 0, 0 };
        context_->IASetVertexBuffers(0, instanced ? ARRAYSIZE(buffers) : 1, buffers, strides, offsets);
        context_->IASetIndexBuffer(indexBuffer_.Get(), DXGI_FORMAT_R16_UINT, 0);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        if (!instanced)
            context_->VSSetConstantBuffers(0, 1, transformBuffer_.GetAddressOf());
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        context_->OMSetDepthStencilState(depthState_.Get(), 0);
        context_->RSSetState(rasterizerState_.Get());
        return S_OK;
    }

    HRESULT Renderer::CreateFrameTargets()
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT result = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(result))
            return result;

        result = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTarget_.GetAddressOf());
        if (FAILED(result))
            return result;

        D3D11_TEXTURE2D_DESC color{};
        backBuffer->GetDesc(&color);

        D3D11_TEXTURE2D_DESC depth{};
        depth.Width = color.Width;
        depth.Height = color.Height;
        depth.MipLevels = 1;
        depth.ArraySize = 1;
        depth.Format = DXGI_FORMAT_D32_FLOAT;
        depth.SampleDesc = color.SampleDesc;
        depth.Usage = D3D11_USAGE_DEFAULT;
        depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> depthTexture;
        result = device_->CreateTexture2D(&depth, nullptr, depthTexture.GetAddressOf());
        if (FAILED(result))
            return result;

        result = device_->CreateDepthStencilView(depthTexture.Get(), nullptr, depthTarget_.GetAddressOf());
        if (FAILED(result))
            return result;

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(color.Width);
        viewport.Height = static_cast<float>(color.Height);
        viewport.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &viewport);
        if (color.Width != frameInfo_.width || color.Height != frameInfo_.height)
            UpdateDrawTransforms(color.Width, color.Height);
        frameInfo_.width = color.Width;
        frameInfo_.height = color.Height;
        return S_OK;
    }

    void Renderer::UpdateDrawTransforms(UINT width, UINT height)
    {
        using namespace DirectX;

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const XMMATRIX viewProjection = CubeViewProjection(scene_, aspect);

        for (std::size_t index = 0; index < scene_.positions.size(); ++index)
        {
            const auto& position = scene_.positions[index];
            const XMMATRIX world = XMMatrixTranslation(position.x, position.y, position.z);
            XMStoreFloat4x4(&drawTransforms_[index], world * viewProjection);
        }

        if (instanceBuffer_)
            context_->UpdateSubresource(instanceBuffer_.Get(), 0, nullptr, drawTransforms_.data(), 0, 0);
    }
}
