#include "pch.h"
#include "gdInternal.h"
#include <d3d12.h>
#include <d3dx12.h>

using Microsoft::WRL::ComPtr;

namespace gd {

class GraphicsDeviceD3D12 : public GraphicsDevice
{
public:
    GraphicsDeviceD3D12(void *device);
    ~GraphicsDeviceD3D12();
    void release() override;

    void* getDevicePtr() override;
    DeviceType getDeviceType() override;
    void sync() override;

    Error createTexture2D(void **dst_tex, int width, int height, TextureFormat format, const void *data, ResourceFlags flags) override;
    void  releaseTexture2D(void *tex) override;
    Error readTexture2D(void *o_buf, size_t bufsize, void *tex, int width, int height, TextureFormat format) override;
    Error writeTexture2D(void *o_tex, int width, int height, TextureFormat format, const void *buf, size_t bufsize) override;

    Error createBuffer(void **dst_buf, size_t size, BufferType type, const void *data, ResourceFlags flags) override;
    void  releaseBuffer(void *buf) override;
    Error readBuffer(void *dst, const void *src_buf, size_t read_size, BufferType type) override;
    Error writeBuffer(void *dst_buf, const void *src, size_t write_size, BufferType type) override;

private:
    enum class BufferFlags {
        Upload,
        Readback,
    };
    ID3D12Resource* createStagingBuffer(size_t size, BufferFlags flags);

    // Body: [](ID3D12GraphicsCommandList *clist) -> void
    template<class Body> HRESULT executeCommands(const Body& body);

private:
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_cqueue;
    ComPtr<ID3D12CommandAllocator> m_calloc;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fence_event;
};


GraphicsDevice* CreateGraphicsDeviceD3D12(void *device)
{
    if (!device) { return nullptr; }
    return new GraphicsDeviceD3D12(device);
}


GraphicsDeviceD3D12::GraphicsDeviceD3D12(void *device)
    : m_device((ID3D12Device*)device)
{
    // create command queue
    {
        D3D12_COMMAND_QUEUE_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        auto hr = m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_cqueue));
    }

    // create command allocator & list
    {
        auto hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_calloc));
        if (SUCCEEDED(hr)) {
        }
    }

    // create signal
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fence->Signal(1);

    m_fence_event = CreateEvent(nullptr, false, false, nullptr);
}

GraphicsDeviceD3D12::~GraphicsDeviceD3D12()
{
    if (m_fence_event) {
        CloseHandle(m_fence_event);
    }
}

void GraphicsDeviceD3D12::release()
{
    delete this;
}

void* GraphicsDeviceD3D12::getDevicePtr()
{
    return nullptr;
}

DeviceType GraphicsDeviceD3D12::getDeviceType()
{
    return DeviceType::D3D12;
}

// Body: [](ID3D12GraphicsCommandList *clist) -> void
template<class Body>
HRESULT GraphicsDeviceD3D12::executeCommands(const Body& body)
{
    HRESULT hr;

    ComPtr<ID3D12GraphicsCommandList> clist;
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_calloc.Get(), nullptr, IID_PPV_ARGS(&clist));
    if (FAILED(hr)) { return hr; }

    body(clist.Get());

    hr = clist->Close();
    if (FAILED(hr)) { return hr; }

    m_cqueue->ExecuteCommandLists(1, (ID3D12CommandList**)clist.GetAddressOf());
    hr = m_cqueue->Signal(m_fence.Get(), 1);
    if (FAILED(hr)) { return hr; }

    sync();
    return S_OK;
}


void GraphicsDeviceD3D12::sync()
{
    m_fence->SetEventOnCompletion(1, m_fence_event);
    WaitForSingleObject(m_fence_event, INFINITE);
}

static DXGI_FORMAT GetInternalFormatD3D12(TextureFormat fmt)
{
    switch (fmt)
    {
    case TextureFormat::RGBAu8:  return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case TextureFormat::RGBAf16: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::RGf16:   return DXGI_FORMAT_R16G16_FLOAT;
    case TextureFormat::Rf16:    return DXGI_FORMAT_R16_FLOAT;

    case TextureFormat::RGBAf32: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case TextureFormat::RGf32:   return DXGI_FORMAT_R32G32_FLOAT;
    case TextureFormat::Rf32:    return DXGI_FORMAT_R32_FLOAT;

    case TextureFormat::RGBAi32: return DXGI_FORMAT_R32G32B32A32_SINT;
    case TextureFormat::RGi32:   return DXGI_FORMAT_R32G32_SINT;
    case TextureFormat::Ri32:    return DXGI_FORMAT_R32_SINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static Error TranslateReturnCode(HRESULT hr)
{
    switch (hr) {
    case S_OK: return Error::OK;
    case E_OUTOFMEMORY: return Error::OutOfMemory;
    case E_INVALIDARG: return Error::InvalidParameter;
    }
    return Error::Unknown;
}

ID3D12Resource* GraphicsDeviceD3D12::createStagingBuffer(size_t size, BufferFlags flags)
{
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;

    D3D12_HEAP_PROPERTIES heap;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 0;
    heap.VisibleNodeMask = 0;
    if (flags == BufferFlags::Upload) {
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    }
    if (flags == BufferFlags::Readback) {
        state = D3D12_RESOURCE_STATE_COPY_DEST;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
    }

    D3D12_RESOURCE_DESC desc;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource *ret = nullptr;
    m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&ret));
    return ret;
}

Error GraphicsDeviceD3D12::createTexture2D(void **dst_tex, int width, int height, TextureFormat format, const void *data, ResourceFlags flags)
{
    D3D12_HEAP_PROPERTIES heap  = {};
    heap.Type                   = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty        = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference   = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask       = 1;
    heap.VisibleNodeMask        = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment          = 0;
    desc.Width              = (UINT64)width;
    desc.Height             = (UINT)height;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = GetInternalFormatD3D12(format);
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource *tex = nullptr;
    auto hr = m_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex) );
    if ( FAILED( hr ) )
    {
        return TranslateReturnCode(hr);
    }
    *dst_tex = tex;
    return Error::OK;
}

void GraphicsDeviceD3D12::releaseTexture2D(void *tex_)
{
    if (!tex_) { return; }

    auto *tex = (ID3D12Resource*)tex_;
    tex->Release();
}

Error GraphicsDeviceD3D12::readTexture2D(void *dst, size_t read_size, void *src_tex_, int width, int height, TextureFormat format)
{
    if (read_size == 0) { return Error::OK; }
    if (!dst || !src_tex_) { return Error::InvalidParameter; }

    auto *src_tex = (ID3D12Resource*)src_tex_;

    D3D12_RESOURCE_DESC src_desc = src_tex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT src_layout;
    UINT src_num_rows;
    UINT64 src_row_size;
    UINT64 src_required_size;
    m_device->GetCopyableFootprints(&src_desc, 0, 1, 0, &src_layout, &src_num_rows, &src_row_size, &src_required_size);

    ComPtr<ID3D12Resource> staging = createStagingBuffer(src_required_size, BufferFlags::Readback);
    if (!staging) { return Error::OutOfMemory; }

    D3D12_TEXTURE_COPY_LOCATION src_region;
    src_region.pResource = src_tex;
    src_region.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_region.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst_region;
    dst_region.pResource = staging.Get();
    dst_region.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_region.PlacedFootprint.Offset = 0;
    dst_region.PlacedFootprint.Footprint = src_layout.Footprint;

    auto hr = executeCommands([&](ID3D12GraphicsCommandList *clist) {
        clist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(src_tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
        clist->CopyTextureRegion(&dst_region, 0, 0, 0, &src_region, nullptr);
        clist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(src_tex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    });
    if (FAILED(hr)) { return TranslateReturnCode(hr); }


    char *mapped_data = nullptr;
    hr = staging->Map(0, nullptr, (void**)&mapped_data);
    if (FAILED(hr)) { return TranslateReturnCode(hr); }
    {
        int dst_pitch = width * GetTexelSize(format);
        int src_pitch = src_layout.Footprint.RowPitch;
        int num_rows = std::min<int>(height, (int)src_layout.Footprint.Height);
        CopyRegion(dst, dst_pitch, mapped_data, src_pitch, num_rows);
    }
    staging->Unmap(0, nullptr);

    return Error::OK;
}

Error GraphicsDeviceD3D12::writeTexture2D(void *dst_tex_, int width, int height, TextureFormat format, const void *src, size_t write_size)
{
    if (write_size == 0) { return Error::OK; }
    if (!dst_tex_ || !src) { return Error::InvalidParameter; }

    auto *dst_tex = (ID3D12Resource*)dst_tex_;

    auto staging_size = GetRequiredIntermediateSize(dst_tex, 0, 1);
    ComPtr<ID3D12Resource> staging = createStagingBuffer(staging_size, BufferFlags::Upload);
    if (!staging) { return Error::OutOfMemory; }

    size_t texel_size = GetTexelSize(format);
    D3D12_SUBRESOURCE_DATA subr = {
        src,
        width * (UINT)texel_size,
        width * height * (UINT)texel_size,
    };

    UINT64 ret;
    executeCommands([&](auto *clist) {
        ret = UpdateSubresources(clist, dst_tex, staging.Get(), 0, 0, 1, &subr);
    });

    sync();

    return ret != 0 ? Error::OK : Error::Unknown;
}



Error GraphicsDeviceD3D12::createBuffer(void **dst_buf, size_t size, BufferType type, const void *data, ResourceFlags flags)
{
    if (!dst_buf) { return Error::InvalidParameter; }
    
    D3D12_HEAP_PROPERTIES heap = {};
     heap.Type                 = D3D12_HEAP_TYPE_UPLOAD;
     heap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
     heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
     heap.CreationNodeMask     = 1;
     heap.VisibleNodeMask      = 1;

     D3D12_RESOURCE_DESC desc = {};
     desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
     desc.Alignment          = 0;
     desc.Width              = (UINT64)size;
     desc.Height             = 1;
     desc.DepthOrArraySize   = 1;
     desc.MipLevels          = 1;
     desc.Format             = DXGI_FORMAT_UNKNOWN;
     desc.SampleDesc.Count   = 1;
     desc.SampleDesc.Quality = 0;
     desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
     desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

     D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;

     ID3D12Resource *buf = nullptr;
     auto hr = m_device->CreateCommittedResource(
         &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&buf) );
     if (FAILED(hr))
     {
         return TranslateReturnCode(hr);
     }
     *dst_buf = buf;
     return Error::OK;
}

void GraphicsDeviceD3D12::releaseBuffer(void *buf_)
{
    if (!buf_) { return; }

    auto *buf = (ID3D12Resource*)buf_;
    buf->Release();
}

Error GraphicsDeviceD3D12::readBuffer(void *dst, const void *src_buf, size_t read_size, BufferType type)
{
    if (read_size == 0) { return Error::OK; }
    if (!dst || !src_buf) { return Error::InvalidParameter; }

    auto *buf = (ID3D12Resource*)src_buf;
    void *mapped_data = nullptr;
    D3D12_RANGE range = { 0, read_size };

    auto hr = buf->Map(0, &range, &mapped_data);
    if (FAILED(hr)) {
        return TranslateReturnCode(hr);
    }

    memcpy(dst, mapped_data, read_size);
    buf->Unmap(0, nullptr);
    return Error::OK;
}

Error GraphicsDeviceD3D12::writeBuffer(void *dst_buf, const void *src, size_t write_size, BufferType type)
{
    if (write_size == 0) { return Error::OK; }
    if (!dst_buf || !src) { return Error::InvalidParameter; }

    auto *buf = (ID3D12Resource*)dst_buf;
    void *mapped_data = nullptr;
    D3D12_RANGE range = { 0, write_size };

    auto hr = buf->Map(0, nullptr, &mapped_data);
    if (FAILED(hr)) {
        return TranslateReturnCode(hr);
    }

    memcpy(mapped_data, src, write_size);
    buf->Unmap(0, &range);
    return Error::OK;
}

} // namespace gd
