#ifdef BOREALIS_USE_DEKO3D

#include "core/io/deko3d_renderer.hpp"
#include "core/io/bitmap_font.hpp"
#include "crypto/libnx/gmac.h"
#include <borealis.hpp>
#include <borealis/platforms/switch/switch_platform.hpp>
#include <array>
#include <cstdio>
#include <cstring>

extern "C"
{
#include <libavutil/pixfmt.h>
#include <libavutil/hwcontext_nvtegra.h>
}

namespace
{
    static constexpr unsigned StaticCmdSize = 0x10000;

    struct Vertex
    {
        float position[3];
        float uv[2];
    };

    // Text vertex with color
    struct TextVertex
    {
        float position[3];
        float uv[2];
        float color[4];
    };

    constexpr std::array VertexAttribState =
    {
        DkVtxAttribState{ 0, 0, offsetof(Vertex, position), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState{ 0, 0, offsetof(Vertex, uv), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
    };

    constexpr std::array VertexBufferState =
    {
        DkVtxBufferState{ sizeof(Vertex), 0 },
    };

    // Text vertex attributes
    constexpr std::array TextVertexAttribState =
    {
        DkVtxAttribState{ 0, 0, offsetof(TextVertex, position), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState{ 0, 0, offsetof(TextVertex, uv), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState{ 0, 0, offsetof(TextVertex, color), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0 },
    };

    constexpr std::array TextVertexBufferState =
    {
        DkVtxBufferState{ sizeof(TextVertex), 0 },
    };

    // Full-screen quad vertices (NDC coordinates)
    constexpr std::array QuadVertexData =
    {
        Vertex{ { -1.0f, +1.0f, 0.0f }, { 0.0f, 0.0f } },
        Vertex{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
        Vertex{ { +1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        Vertex{ { +1.0f, +1.0f, 0.0f }, { 1.0f, 0.0f } },
    };

    // Helper to convert pixel coords to NDC
    inline void pixelToNDC(float px, float py, int screenW, int screenH, float& ndcX, float& ndcY)
    {
        ndcX = (px / screenW) * 2.0f - 1.0f;
        ndcY = 1.0f - (py / screenH) * 2.0f;
    }
}

Deko3dRenderer::Deko3dRenderer()
{
}

Deko3dRenderer::~Deko3dRenderer()
{
    cleanup();

    if (m_font_memblock) {
        dkMemBlockDestroy(m_font_memblock);
        m_font_memblock = nullptr;
    }
    m_text_vertex_buffer.destroy();

    m_vertex_shader.destroy();
    m_fragment_shader.destroy();
    m_text_vertex_shader.destroy();
    m_text_fragment_shader.destroy();
}

bool Deko3dRenderer::initialize(int frame_width, int frame_height, int screen_width, int screen_height, ChiakiLog* log)
{
    if (m_initialized)
        return true;

    m_log = log;
    m_frame_width = frame_width;
    m_frame_height = frame_height;
    m_screen_width = screen_width;
    m_screen_height = screen_height;

    brls::Logger::info("Deko3dRenderer::initialize: frame={}x{}, screen={}x{}",
                frame_width, frame_height, screen_width, screen_height);

    // Get deko3d context from borealis
    m_vctx = (brls::SwitchVideoContext*)brls::Application::getPlatform()->getVideoContext();
    if (!m_vctx)
    {
        brls::Logger::error("Failed to get SwitchVideoContext");
        return false;
    }

    m_device = m_vctx->getDeko3dDevice();
    m_queue = m_vctx->getQueue();

    // Create memory pools
    m_pool_code.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
    m_pool_data.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

    // Create command buffer
    m_cmdbuf = dk::CmdBufMaker{m_device}.create();
    CMemPool::Handle cmdmem = m_pool_data->allocate(StaticCmdSize);
    m_cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

    // Get image descriptor set from borealis
    m_image_descriptor_set = m_vctx->getImageDescriptor();
    if (!m_image_descriptor_set)
    {
        brls::Logger::error("Failed to get image descriptor set from borealis");
        return false;
    }

    // Load shaders
    brls::Logger::info("Loading vertex shader...");
    m_vertex_shader.load(*m_pool_code, "romfs:/shaders/video_vsh.dksh");
    brls::Logger::info("Loading fragment shader...");
    m_fragment_shader.load(*m_pool_code, "romfs:/shaders/video_fsh.dksh");
    brls::Logger::info("Shaders loaded successfully");

    // Create vertex buffer
    m_vertex_buffer = m_pool_data->allocate(sizeof(QuadVertexData), alignof(Vertex));
    memcpy(m_vertex_buffer.getCpuAddr(), QuadVertexData.data(), m_vertex_buffer.getSize());

    brls::Logger::info("Deko3dRenderer: shaders and vertex buffer initialized");

    // Initialize text rendering for stats overlay (with full error handling)
    initTextRendering();

    // Register post-render callback with borealis
    // This ensures our video renders AFTER NanoVG flush (after UI is drawn)
    registerCallback();

    m_initialized = true;
    return true;
}

bool Deko3dRenderer::setupTextures(AVFrame* frame)
{
    if (m_textures_initialized)
        return true;

    brls::Logger::info("Deko3dRenderer::setupTextures: setting up NV12 texture resources");
    brls::Logger::info("Frame info: format={}, width={}, height={}, linesize[0]={}, linesize[1]={}",
        static_cast<int>(frame->format), frame->width, frame->height, frame->linesize[0], frame->linesize[1]);

    // Check if this is a hardware frame
    if (frame->format != AV_PIX_FMT_NVTEGRA)
    {
        brls::Logger::error("Frame is not a hardware frame (format={}, expected NVTEGRA)", static_cast<int>(frame->format));
        return false;
    }

    // Get GPU memory map
    AVNVTegraMap* map = av_nvtegra_frame_get_fbuf_map(frame);
    if (!map)
    {
        brls::Logger::error("Failed to get NVTEGRA map from frame");
        return false;
    }

    void* map_addr = av_nvtegra_map_get_addr(map);
    size_t map_size = av_nvtegra_map_get_size(map);

    brls::Logger::info("NVTEGRA map: addr={}, size={}", map_addr, map_size);
    brls::Logger::info("Frame data: data[0]={}, data[1]={}", (void*)frame->data[0], (void*)frame->data[1]);

    // Allocate image indexes from borealis
    m_luma_texture_id = m_vctx->allocateImageIndex();
    m_chroma_texture_id = m_vctx->allocateImageIndex();

    brls::Logger::info("Allocated texture IDs: luma={}, chroma={}", m_luma_texture_id, m_chroma_texture_id);

    brls::Logger::info("Creating image layouts: luma={}x{}, chroma={}x{}",
        m_frame_width, m_frame_height, m_frame_width / 2, m_frame_height / 2);

    // Luma: actual frame dimensions
    dk::ImageLayoutMaker{m_device}
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_R8_Unorm)
        .setDimensions(m_frame_width, m_frame_height, 1)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
        .initialize(m_luma_layout);

    // Chroma: half dimensions (NV12)
    dk::ImageLayoutMaker{m_device}
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_RG8_Unorm)
        .setDimensions(m_frame_width / 2, m_frame_height / 2, 1)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
        .initialize(m_chroma_layout);

    // Create memory block from GPU memory
    m_mapping_memblock = dk::MemBlockMaker{m_device, map_size}
        .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .setStorage(map_addr)
        .create();

    // Initialize images with correct offsets from map base
    // data[0] may not be at start of map - need to calculate offset
    ptrdiff_t luma_offset = frame->data[0] - (uint8_t*)map_addr;
    ptrdiff_t chroma_offset = frame->data[1] - (uint8_t*)map_addr;

    brls::Logger::info("Offsets from map: luma={}, chroma={}", luma_offset, chroma_offset);

    m_luma.initialize(m_luma_layout, m_mapping_memblock, luma_offset);
    m_chroma.initialize(m_chroma_layout, m_mapping_memblock, chroma_offset);

    brls::Logger::info("Images initialized: luma_offset={}, chroma_offset={}", luma_offset, chroma_offset);

    // Create image descriptors
    m_luma_desc.initialize(m_luma);
    m_chroma_desc.initialize(m_chroma);

    // Update image descriptors and submit 
    m_image_descriptor_set->update(m_cmdbuf, m_luma_texture_id, m_luma_desc);
    m_image_descriptor_set->update(m_cmdbuf, m_chroma_texture_id, m_chroma_desc);

    // Submit and flush to kick GPU execution
    DkCmdList list = m_cmdbuf.finishList();
    if (!list)
    {
        brls::Logger::error("setupTextures: finishList returned NULL!");
        return false;
    }
    m_queue.submitCommands(list);
    m_queue.flush();  
    m_queue.waitIdle(); 

    brls::Logger::info("Image descriptors updated");

    // Track the current map address to detect changes
    m_current_map_addr = map_addr;

    m_textures_initialized = true;
    brls::Logger::info("Deko3dRenderer: texture setup complete");
    return true;
}

void Deko3dRenderer::draw(AVFrame* frame)
{
    static int call = 0;
    if (++call % 100 == 1)
        brls::Logger::info("draw #{}, init={}, tex={}, buf={}", call, m_initialized, m_textures_initialized, m_buffers_initialized);

    if (!m_initialized)
        return;

    if (!frame || frame->format != AV_PIX_FMT_NVTEGRA)
        return;

    if (!m_textures_initialized)
    {
        brls::Logger::info("draw: First NVTEGRA frame, calling setupTextures");
        if (!setupTextures(frame))
        {
            brls::Logger::error("draw: setupTextures failed");
            return;
        }
    }

    if (!m_buffers_initialized)
    {
        if (!initPersistentBuffers(frame))
        {
            brls::Logger::error("draw: initPersistentBuffers failed");
            return;
        }
    }

    if (m_paused)
        return;

    AVNVTegraMap* map = av_nvtegra_frame_get_fbuf_map(frame);
    if (map)
    {
        void* map_addr = av_nvtegra_map_get_addr(map);
        if (map_addr != m_current_map_addr)
        {
            updateTextureBindings(frame, map);
            m_current_map_addr = map_addr;
        }
    }

    copyFrameToBuffer();

    m_cmdbuf.clear();
    m_image_descriptor_set->update(m_cmdbuf, m_luma_texture_id, m_luma_descs[m_current_buffer]);
    m_image_descriptor_set->update(m_cmdbuf, m_chroma_texture_id, m_chroma_descs[m_current_buffer]);
    DkCmdList list = m_cmdbuf.finishList();
    if (list)
    {
        m_queue.submitCommands(list);
        m_queue.flush();
    }
}

void Deko3dRenderer::renderVideo(AVFrame* frame)
{
    if (!m_initialized || !m_textures_initialized || !m_buffers_initialized)
        return;

    // Get current framebuffer from borealis
    dk::Image* framebuffer = m_vctx->getFramebuffer();
    if (!framebuffer)
    {
        brls::Logger::error("renderVideo: Failed to get framebuffer from borealis");
        return;
    }

    // Build command list fresh each frame
    m_cmdbuf.clear();

    // Bind render target 
    dk::ImageView colorTarget { *framebuffer };
    m_cmdbuf.bindRenderTargets(&colorTarget);

    // Set viewport and scissors to match screen dimensions
    m_cmdbuf.setViewports(0, { { 0.0f, 0.0f, (float)m_screen_width, (float)m_screen_height, 0.0f, 1.0f } });
    m_cmdbuf.setScissors(0, { { 0, 0, (uint32_t)m_screen_width, (uint32_t)m_screen_height } });

    // Set up GPU render stat
    // disable depth/stencil, no face culling
    m_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_cmdbuf.bindColorState(dk::ColorState{});
    m_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

    // Bind shaders
    m_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_vertex_shader, m_fragment_shader });

    // Bind textures (using sampler 0 from NanoVG's pre-initialized samplers)
    m_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_luma_texture_id, 0));
    m_cmdbuf.bindTextures(DkStage_Fragment, 1, dkMakeTextureHandle(m_chroma_texture_id, 0));

    // Bind vertex buffer and state
    m_cmdbuf.bindVtxBuffer(0, m_vertex_buffer.getGpuAddr(), m_vertex_buffer.getSize());
    m_cmdbuf.bindVtxAttribState(VertexAttribState);
    m_cmdbuf.bindVtxBufferState(VertexBufferState);

    // Draw quad
    m_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);

    // Finish command list and verify it's not empty
    DkCmdList list = m_cmdbuf.finishList();
    if (!list)
    {
        brls::Logger::error("renderVideo: finishList returned NULL - no commands!");
        return;
    }

    // Submit video commands and FLUSH to kick GPU execution
    m_queue.submitCommands(list);
    m_queue.flush();  // KEY: This actually sends commands to GPU

    // Wait for video to complete before overlaying stats (only when stats enabled)
    // Using waitIdle instead of fencing here - proper fence sync would require
    // tracking fence values and resetting between frames, not worth the complexity
    // for this use case since we're at end of frame anyway
    if (m_show_stats)
        m_queue.waitIdle();

    // Render stats overlay on top of video (if enabled)
    renderStatsOverlay();
}

void Deko3dRenderer::registerCallback()
{
    if (m_callback_registered)
        return;

    brls::Application::setPostRenderCallback([this]() {
        if (m_buffers_initialized && !m_paused)
        {
            renderVideo(nullptr);
        }
    });

    m_callback_registered = true;
    brls::Logger::info("Deko3dRenderer: post-render callback registered");
}

void Deko3dRenderer::unregisterCallback()
{
    if (!m_callback_registered)
        return;

    brls::Application::setPostRenderCallback(nullptr);
    m_callback_registered = false;
    m_pending_frame = nullptr;
    brls::Logger::info("Deko3dRenderer: post-render callback unregistered");
}

void Deko3dRenderer::updateTextureBindings(AVFrame* frame, AVNVTegraMap* map)
{
    void* map_addr = av_nvtegra_map_get_addr(map);
    size_t map_size = av_nvtegra_map_get_size(map);

    static uint32_t update_count = 0;
    if (update_count++ % 300 == 0)
        brls::Logger::info("updateTextureBindings #{}, addr={}", update_count, map_addr);

    if (m_mapping_memblock)
    {
        m_queue.waitIdle();
        dkMemBlockDestroy(m_mapping_memblock);
        m_mapping_memblock = nullptr;
    }

    m_mapping_memblock = dk::MemBlockMaker{m_device, map_size}
        .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .setStorage(map_addr)
        .create();

    m_luma.initialize(m_luma_layout, m_mapping_memblock, 0);
    m_chroma.initialize(m_chroma_layout, m_mapping_memblock, frame->data[1] - frame->data[0]);

    m_luma_desc.initialize(m_luma);
    m_chroma_desc.initialize(m_chroma);
}

bool Deko3dRenderer::initPersistentBuffers(AVFrame* frame)
{
    if (m_buffers_initialized)
        return true;

    AVNVTegraMap* map = av_nvtegra_frame_get_fbuf_map(frame);
    size_t frame_size = av_nvtegra_map_get_size(map);
    frame_size = (frame_size + 0xFFF) & ~0xFFF;

    brls::Logger::info("initPersistentBuffers: creating {} GPU buffers of size {}", GPU_BUFFER_COUNT, frame_size);

    for (int i = 0; i < GPU_BUFFER_COUNT; i++)
    {
        m_frame_buffers[i] = dk::MemBlockMaker{m_device, frame_size}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .create();

        m_luma_buffers[i].initialize(m_luma_layout, m_frame_buffers[i], 0);

        size_t chroma_offset = m_luma_layout.getSize();
        m_chroma_buffers[i].initialize(m_chroma_layout, m_frame_buffers[i], chroma_offset);

        m_luma_descs[i].initialize(m_luma_buffers[i]);
        m_chroma_descs[i].initialize(m_chroma_buffers[i]);
    }

    m_buffers_initialized = true;
    brls::Logger::info("initPersistentBuffers: GPU circular buffer initialized");
    return true;
}

void Deko3dRenderer::copyFrameToBuffer()
{
    dk::ImageView srcLuma{m_luma};
    dk::ImageView srcChroma{m_chroma};

    dk::ImageView dstLuma{m_luma_buffers[m_next_buffer]};
    dk::ImageView dstChroma{m_chroma_buffers[m_next_buffer]};

    DkImageRect lumaRect = {0, 0, 0, (uint32_t)m_frame_width, (uint32_t)m_frame_height, 1};
    DkImageRect chromaRect = {0, 0, 0, (uint32_t)(m_frame_width / 2), (uint32_t)(m_frame_height / 2), 1};

    m_cmdbuf.clear();
    m_cmdbuf.copyImage(srcLuma, lumaRect, dstLuma, lumaRect);
    m_cmdbuf.copyImage(srcChroma, chromaRect, dstChroma, chromaRect);

    DkCmdList list = m_cmdbuf.finishList();
    m_queue.submitCommands(list);
    m_queue.waitIdle();

    m_current_buffer = m_next_buffer;
    m_next_buffer = (m_next_buffer + 1) % GPU_BUFFER_COUNT;
}

void Deko3dRenderer::cleanup()
{
    if (!m_initialized)
        return;

    brls::Logger::info("Deko3dRenderer::cleanup");

    unregisterCallback();

    m_queue.waitIdle();

    cleanupTextRendering();

    m_vertex_buffer.destroy();

    if (m_luma_texture_id) {
        m_vctx->freeImageIndex(m_luma_texture_id);
        m_luma_texture_id = 0;
    }
    if (m_chroma_texture_id) {
        m_vctx->freeImageIndex(m_chroma_texture_id);
        m_chroma_texture_id = 0;
    }

    if (m_mapping_memblock)
    {
        dkMemBlockDestroy(m_mapping_memblock);
        m_mapping_memblock = nullptr;
    }

    if (m_buffers_initialized)
    {
        for (int i = 0; i < GPU_BUFFER_COUNT; i++)
        {
            if (m_frame_buffers[i])
            {
                dkMemBlockDestroy(m_frame_buffers[i]);
                m_frame_buffers[i] = nullptr;
            }
            m_luma_buffers[i] = dk::Image{};
            m_chroma_buffers[i] = dk::Image{};
            m_luma_descs[i] = dk::ImageDescriptor{};
            m_chroma_descs[i] = dk::ImageDescriptor{};
        }
        m_buffers_initialized = false;
    }

    m_luma = dk::Image{};
    m_chroma = dk::Image{};
    m_luma_desc = dk::ImageDescriptor{};
    m_chroma_desc = dk::ImageDescriptor{};
    m_luma_layout = dk::ImageLayout{};
    m_chroma_layout = dk::ImageLayout{};

    m_current_map_addr = nullptr;
    m_current_buffer = 0;
    m_next_buffer = 0;

    m_initialized = false;
    m_textures_initialized = false;
    m_first_frame = true;
    m_ready_fence = {};
    m_done_fence = {};
}

void Deko3dRenderer::waitIdle()
{
    if (m_initialized)
    {
        m_queue.waitIdle();
    }
}

void Deko3dRenderer::initTextRendering()
{
    if (m_text_initialized)
        return;

    brls::Logger::info("Deko3dRenderer: initializing text rendering for stats overlay");

    // Load text shaders with error handling
    brls::Logger::info("Loading text vertex shader from romfs:/shaders/text_vsh.dksh");
    if (!m_text_vertex_shader.load(*m_pool_code, "romfs:/shaders/text_vsh.dksh"))
    {
        brls::Logger::error("Failed to load text vertex shader - file may not exist in romfs");
        brls::Logger::warning("Stats overlay will be disabled");
        return;
    }
    brls::Logger::info("Text vertex shader loaded successfully");

    brls::Logger::info("Loading text fragment shader from romfs:/shaders/text_fsh.dksh");
    if (!m_text_fragment_shader.load(*m_pool_code, "romfs:/shaders/text_fsh.dksh"))
    {
        brls::Logger::error("Failed to load text fragment shader - file may not exist in romfs");
        brls::Logger::warning("Stats overlay will be disabled");
        return;
    }
    brls::Logger::info("Text fragment shader loaded successfully");

    // Create font texture from embedded data
    brls::Logger::info("Creating font texture...");
    uint8_t* fontAtlas = nullptr;

    try
    {
        brls::Logger::info("Generating font atlas ({}x{})...", BitmapFont::ATLAS_WIDTH, BitmapFont::ATLAS_HEIGHT);
        fontAtlas = BitmapFont::generateAtlasTexture();
        brls::Logger::info("Font atlas generated");

        brls::Logger::info("Creating font image layout...");
        dk::ImageLayoutMaker{m_device}
            .setType(DkImageType_2D)
            .setFormat(DkImageFormat_R8_Unorm)
            .setDimensions(BitmapFont::ATLAS_WIDTH, BitmapFont::ATLAS_HEIGHT, 1)
            .setFlags(DkImageFlags_Usage2DEngine)
            .initialize(m_font_layout);
        brls::Logger::info("Font image layout created");

        size_t fontSize = m_font_layout.getSize();
        size_t fontAlign = m_font_layout.getAlignment();
        // Memory blocks must be page-aligned (DK_MEMBLOCK_ALIGNMENT = 0x1000)
        size_t alignedSize = (fontSize + 0xFFF) & ~0xFFF;
        brls::Logger::info("Creating font memory block: size={}, alignedSize={}, align={}", fontSize, alignedSize, fontAlign);

        m_font_memblock = dk::MemBlockMaker{m_device, alignedSize}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .create();

        if (!m_font_memblock)
        {
            brls::Logger::error("Failed to create font memory block!");
            if (fontAtlas) delete[] fontAtlas;
            return;
        }
        brls::Logger::info("Font memory block created");

        // Initialize font image
        brls::Logger::info("Initializing font image...");
        m_font_image.initialize(m_font_layout, m_font_memblock, 0);
        brls::Logger::info("Font image initialized");

        // Copy font data to staging and upload
        // Create a staging buffer
        size_t atlasSize = BitmapFont::ATLAS_WIDTH * BitmapFont::ATLAS_HEIGHT;
        brls::Logger::info("Allocating staging buffer: size={}", atlasSize);
        CMemPool::Handle stagingBuffer = m_pool_data->allocate(atlasSize, 1);
        if (!stagingBuffer)
        {
            brls::Logger::error("Failed to allocate staging buffer!");
            if (fontAtlas) delete[] fontAtlas;
            return;
        }
        brls::Logger::info("Copying font data to staging buffer...");
        memcpy(stagingBuffer.getCpuAddr(), fontAtlas, atlasSize);
        brls::Logger::info("Font data copied");

        // Build command to copy from staging to texture
        brls::Logger::info("Building copy command...");
        m_cmdbuf.clear();

        // Create image view for copy destination
        dk::ImageView dstView{m_font_image};

        // Copy from buffer to image
        m_cmdbuf.copyBufferToImage(
            { stagingBuffer.getGpuAddr(), BitmapFont::ATLAS_WIDTH, BitmapFont::ATLAS_HEIGHT },
            dstView,
            { 0, 0, 0, BitmapFont::ATLAS_WIDTH, BitmapFont::ATLAS_HEIGHT, 1 }
        );
        brls::Logger::info("Copy command built");

        // Submit and wait
        brls::Logger::info("Submitting copy command...");
        DkCmdList list = m_cmdbuf.finishList();
        if (list)
        {
            m_queue.submitCommands(list);
            m_queue.waitIdle();
        }
        brls::Logger::info("Copy command completed");

        // Free staging buffer
        stagingBuffer.destroy();

        // Create image descriptor for font
        m_font_desc.initialize(m_font_image);

        // Allocate texture ID
        m_font_texture_id = m_vctx->allocateImageIndex();

        // Update descriptor
        m_cmdbuf.clear();
        m_image_descriptor_set->update(m_cmdbuf, m_font_texture_id, m_font_desc);
        list = m_cmdbuf.finishList();
        if (list)
        {
            m_queue.submitCommands(list);
            m_queue.waitIdle();
        }

        // Allocate text vertex buffer
        m_text_vertex_buffer = m_pool_data->allocate(MAX_TEXT_VERTICES * sizeof(TextVertex), alignof(TextVertex));

        // Free the generated atlas data
        delete[] fontAtlas;
        fontAtlas = nullptr;

        m_text_initialized = true;
        brls::Logger::info("Deko3dRenderer: text rendering initialized, font texture ID={}", m_font_texture_id);
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to initialize font texture: {}", e.what());
        if (fontAtlas) delete[] fontAtlas;
        return;
    }
    catch (...)
    {
        brls::Logger::error("Failed to initialize font texture: unknown error");
        if (fontAtlas) delete[] fontAtlas;
        return;
    }
}

void Deko3dRenderer::cleanupTextRendering()
{
    if (!m_text_initialized)
        return;

    if (m_font_texture_id) {
        m_vctx->freeImageIndex(m_font_texture_id);
        m_font_texture_id = 0;
    }
    m_text_initialized = false;
}

void Deko3dRenderer::renderStatsOverlay()
{
    // Debug logging to verify function is called
    static uint32_t call_count = 0;
    if (++call_count % 60 == 1)  // Log every ~1 second at 60fps
        brls::Logger::info("renderStatsOverlay #{}: show={}, text_init={}, font_id={}",
            call_count, m_show_stats, m_text_initialized, m_font_texture_id);

    if (!m_show_stats)
        return;

    if (!m_text_initialized)
    {
        // Text rendering failed to initialize - log once and disable stats
        static bool warned = false;
        if (!warned)
        {
            brls::Logger::warning("Stats overlay requested but text rendering not initialized - disabling");
            warned = true;
        }
        m_show_stats = false;
        return;
    }

    uint64_t mins = m_stats.stream_duration_seconds / 60;
    uint64_t secs = m_stats.stream_duration_seconds % 60;

    const char* ghashMode = (chiaki_libnx_get_ghash_mode() == CHIAKI_LIBNX_GHASH_PMULL) ? "PMULL" : "TABLE";

    char statsText[512];
    snprintf(statsText, sizeof(statsText),
        "=== Requested ===\n"
        "%dx%d @ %dfps\n"
        "Bitrate: %d kbps\n"
        "Codec: %s\n"
        "\n"
        "=== Rendered ===\n"
        "%dx%d @ %.0ffps\n"
        "Decoder: %s (%s)\n"
        "Dropped: %zu  Faked: %zu\n"
        "Queue: %zu\n"
        "\n"
        "=== Network ===\n"
        "Packet Loss (Live): %.1f%%\n"
        "Frame Loss: %zu (Rec: %zu)\n"
        "Duration: %lum%02lus\n"
        "GHASH: %s",
        m_stats.requested_width,
        m_stats.requested_height,
        m_stats.requested_fps,
        m_stats.requested_bitrate,
        m_stats.requested_hevc ? "HEVC" : "H.264",
        m_stats.video_width,
        m_stats.video_height,
        m_stats.fps,
        m_stats.is_hevc ? "HEVC" : "H.264",
        m_stats.is_hardware_decoder ? "NVTEGRA" : "SW",
        m_stats.dropped_frames,
        m_stats.faked_frames,
        m_stats.queue_size,
        m_stats.packet_loss_percent,
        m_stats.network_frames_lost,
        m_stats.frames_recovered,
        mins,
        secs,
        ghashMode
    );

    // Calculate overlay dimensions
    constexpr float MARGIN = 10.0f;
    constexpr float PADDING = 8.0f;
    constexpr float CHAR_SCALE = 2.0f;  // Scale up the 8x8 font
    float charW = BitmapFont::CHAR_WIDTH * CHAR_SCALE;
    float charH = BitmapFont::CHAR_HEIGHT * CHAR_SCALE;

    // Count lines and max line width
    int numLines = 1;
    int maxLineLen = 0;
    int currentLineLen = 0;
    for (const char* p = statsText; *p; p++)
    {
        if (*p == '\n')
        {
            numLines++;
            if (currentLineLen > maxLineLen)
                maxLineLen = currentLineLen;
            currentLineLen = 0;
        }
        else
        {
            currentLineLen++;
        }
    }
    if (currentLineLen > maxLineLen)
        maxLineLen = currentLineLen;

    float boxWidth = maxLineLen * charW + PADDING * 2;
    float boxHeight = numLines * charH + PADDING * 2;

    // Build vertex buffer
    std::vector<TextVertex> vertices;
    vertices.reserve(6 + 6 * strlen(statsText));  // Background quad + text quads

    // Background color (semi-transparent black)
    float bgR = 0.0f, bgG = 0.0f, bgB = 0.0f, bgA = 0.7f;

    // Text color (green)
    float txR = 0.0f, txG = 1.0f, txB = 0.0f, txA = 1.0f;

    // Background quad (NDC coordinates)
    float bgX1 = MARGIN;
    float bgY1 = MARGIN;
    float bgX2 = MARGIN + boxWidth;
    float bgY2 = MARGIN + boxHeight;

    float ndcX1, ndcY1, ndcX2, ndcY2;
    pixelToNDC(bgX1, bgY1, m_screen_width, m_screen_height, ndcX1, ndcY1);
    pixelToNDC(bgX2, bgY2, m_screen_width, m_screen_height, ndcX2, ndcY2);

    // Background quad (two triangles) - use UV outside font range for solid color
    vertices.push_back({{ ndcX1, ndcY1, 0.0f }, { -1.0f, -1.0f }, { bgR, bgG, bgB, bgA }});
    vertices.push_back({{ ndcX2, ndcY1, 0.0f }, { -1.0f, -1.0f }, { bgR, bgG, bgB, bgA }});
    vertices.push_back({{ ndcX1, ndcY2, 0.0f }, { -1.0f, -1.0f }, { bgR, bgG, bgB, bgA }});

    vertices.push_back({{ ndcX2, ndcY1, 0.0f }, { -1.0f, -1.0f }, { bgR, bgG, bgB, bgA }});
    vertices.push_back({{ ndcX2, ndcY2, 0.0f }, { -1.0f, -1.0f }, { bgR, bgG, bgB, bgA }});
    vertices.push_back({{ ndcX1, ndcY2, 0.0f }, { -1.0f, -1.0f }, { bgR, bgG, bgB, bgA }});

    // Text characters
    float cursorX = MARGIN + PADDING;
    float cursorY = MARGIN + PADDING;

    for (const char* p = statsText; *p; p++)
    {
        if (*p == '\n')
        {
            cursorX = MARGIN + PADDING;
            cursorY += charH;
            continue;
        }

        // Get UV coordinates for this character
        float u1, v1, u2, v2;
        BitmapFont::getCharUV(*p, u1, v1, u2, v2);

        // Character quad corners
        float cx1 = cursorX;
        float cy1 = cursorY;
        float cx2 = cursorX + charW;
        float cy2 = cursorY + charH;

        pixelToNDC(cx1, cy1, m_screen_width, m_screen_height, ndcX1, ndcY1);
        pixelToNDC(cx2, cy2, m_screen_width, m_screen_height, ndcX2, ndcY2);

        // Two triangles for the character quad
        vertices.push_back({{ ndcX1, ndcY1, 0.0f }, { u1, v1 }, { txR, txG, txB, txA }});
        vertices.push_back({{ ndcX2, ndcY1, 0.0f }, { u2, v1 }, { txR, txG, txB, txA }});
        vertices.push_back({{ ndcX1, ndcY2, 0.0f }, { u1, v2 }, { txR, txG, txB, txA }});

        vertices.push_back({{ ndcX2, ndcY1, 0.0f }, { u2, v1 }, { txR, txG, txB, txA }});
        vertices.push_back({{ ndcX2, ndcY2, 0.0f }, { u2, v2 }, { txR, txG, txB, txA }});
        vertices.push_back({{ ndcX1, ndcY2, 0.0f }, { u1, v2 }, { txR, txG, txB, txA }});

        cursorX += charW;
    }

    // Check if we exceed max vertices
    if (vertices.size() > MAX_TEXT_VERTICES)
    {
        brls::Logger::warning("Stats overlay exceeded max vertices: {}", vertices.size());
        return;
    }

    // log vertex count periodically
    static uint32_t draw_count = 0;
    if (++draw_count % 60 == 1)
        brls::Logger::info("Stats overlay drawing {} vertices (bg=6, text={})",
            vertices.size(), vertices.size() - 6);

    // Copy to GPU buffer
    memcpy(m_text_vertex_buffer.getCpuAddr(), vertices.data(), vertices.size() * sizeof(TextVertex));

    // Get current framebuffer
    dk::Image* framebuffer = m_vctx->getFramebuffer();
    if (!framebuffer)
        return;

    // Build and submit render commands
    m_cmdbuf.clear();

    dk::ImageView colorTarget{*framebuffer};
    m_cmdbuf.bindRenderTargets(&colorTarget);

    m_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)m_screen_width, (float)m_screen_height, 0.0f, 1.0f }});
    m_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)m_screen_width, (uint32_t)m_screen_height }});

    // Enable alpha blending
    m_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_cmdbuf.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    m_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

    // Set blend function for alpha blending
    m_cmdbuf.bindBlendStates(0, dk::BlendState{}
        .setColorBlendOp(DkBlendOp_Add)
        .setSrcColorBlendFactor(DkBlendFactor_SrcAlpha)
        .setDstColorBlendFactor(DkBlendFactor_InvSrcAlpha)
        .setAlphaBlendOp(DkBlendOp_Add)
        .setSrcAlphaBlendFactor(DkBlendFactor_One)
        .setDstAlphaBlendFactor(DkBlendFactor_InvSrcAlpha));

    // Bind text shaders
    m_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_text_vertex_shader, m_text_fragment_shader });

    m_image_descriptor_set->bindForImages(m_cmdbuf);

    // Bind font texture using NanoVG's pre-existing sampler index 2 (Nearest filter, ClampToEdge)
    // NanoVG creates 16 samplers at startup with bit-flag indices:
    //   Bit 0 (1): MipFilter, Bit 1 (2): Nearest, Bit 2 (4): RepeatX, Bit 3 (8): RepeatY
    // Index 2 = SamplerType_Nearest = nearest filter, clamp to edge (what we need for font)
    m_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_font_texture_id, 2));

    // Bind vertex buffer
    m_cmdbuf.bindVtxBuffer(0, m_text_vertex_buffer.getGpuAddr(), vertices.size() * sizeof(TextVertex));
    m_cmdbuf.bindVtxAttribState(TextVertexAttribState);
    m_cmdbuf.bindVtxBufferState(TextVertexBufferState);

    // Draw
    m_cmdbuf.draw(DkPrimitive_Triangles, vertices.size(), 1, 0, 0);

    // Submit
    DkCmdList list = m_cmdbuf.finishList();
    if (list)
    {
        m_queue.submitCommands(list);
        m_queue.flush();
    }
}

#endif // BOREALIS_USE_DEKO3D
