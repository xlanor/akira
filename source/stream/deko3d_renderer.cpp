#ifdef BOREALIS_USE_DEKO3D

#include "stream/deko3d_renderer.hpp"
#include "stream/bitmap_font.hpp"
#include "core/wireguard_manager.hpp"
#include "core/settings_manager.hpp"
#include "crypto/libnx/gmac.h"
#include <borealis.hpp>
#include <borealis/platforms/switch/switch_platform.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <format>
#include <uam.h>

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

static std::string loadShaderSource(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        brls::Logger::error("Failed to open shader source: {}", path);
        return "";
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    std::string source(size, '\0');
    fread(&source[0], 1, size, f);
    fclose(f);
    return source;
}

Deko3dRenderer::Deko3dRenderer()
{
    uam_init();
    m_uam_initialized = true;
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

    if (m_uam_initialized)
    {
        uam_deinit();
        m_uam_initialized = false;
    }
}

bool Deko3dRenderer::initialize(int frame_width, int frame_height, ChiakiLog* log)
{
    if (m_initialized)
        return true;

    m_log = log;
    m_frame_width = frame_width;
    m_frame_height = frame_height;
    m_display_width = brls::Application::windowWidth;
    m_display_height = brls::Application::windowHeight;

    brls::Logger::info("Deko3dRenderer::initialize: frame={}x{}", frame_width, frame_height);

    m_vctx = (brls::SwitchVideoContext*)brls::Application::getPlatform()->getVideoContext();
    if (!m_vctx)
    {
        brls::Logger::error("Failed to get SwitchVideoContext");
        return false;
    }

    m_device = m_vctx->getDeko3dDevice();
    m_queue = m_vctx->getQueue();

    m_pool_code.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
    m_pool_data.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

    m_cmdbuf = dk::CmdBufMaker{m_device}.create();
    CMemPool::Handle cmdmem = m_pool_data->allocate(StaticCmdSize);
    m_cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

    m_update_cmdbuf = dk::CmdBufMaker{m_device}.create();
    m_update_cmdmem = m_pool_data->allocate(UpdateCmdSliceSize * brls::FRAMEBUFFERS_COUNT, DK_CMDMEM_ALIGNMENT);

    m_overlay_cmdbuf = dk::CmdBufMaker{m_device}.create();
    m_overlay_cmdmem = m_pool_data->allocate(OverlayCmdSize);

    m_image_descriptor_set = m_vctx->getImageDescriptor();
    if (!m_image_descriptor_set)
    {
        brls::Logger::error("Failed to get image descriptor set from borealis");
        return false;
    }

    bool dithering = SettingsManager::getInstance()->getEnableDithering();
    if (!compileVideoShaders(dithering))
    {
        brls::Logger::error("Failed to compile video shaders");
        return false;
    }

    m_vertex_buffer = m_pool_data->allocate(sizeof(QuadVertexData), alignof(Vertex));
    memcpy(m_vertex_buffer.getCpuAddr(), QuadVertexData.data(), m_vertex_buffer.getSize());

    brls::Logger::info("Deko3dRenderer: shaders and vertex buffer initialized");

    initTextRendering();
    registerCallback();

    m_initialized = true;
    return true;
}

bool Deko3dRenderer::setupTextures(AVFrame* frame)
{
    if (m_textures_initialized)
        return true;

    brls::Logger::info("Deko3dRenderer::setupTextures: frame={}x{}, format={}",
        frame->width, frame->height, static_cast<int>(frame->format));

    if (frame->format != AV_PIX_FMT_NVTEGRA)
    {
        brls::Logger::error("Frame is not NVTEGRA (format={})", static_cast<int>(frame->format));
        return false;
    }

    m_frame_width = frame->width;
    m_frame_height = frame->height;

    m_luma_texture_id = m_vctx->allocateImageIndex();
    m_chroma_texture_id = m_vctx->allocateImageIndex();

    dk::ImageLayoutMaker{m_device}
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_R8_Unorm)
        .setDimensions(m_frame_width, m_frame_height, 1)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
        .initialize(m_luma_layout);

    dk::ImageLayoutMaker{m_device}
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_RG8_Unorm)
        .setDimensions(m_frame_width / 2, m_frame_height / 2, 1)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
        .initialize(m_chroma_layout);

    bool fsr_setting = SettingsManager::getInstance()->getFsrEnabled();
    int targetH = SettingsManager::getInstance()->getFsrTargetHeight();
    int targetW = (targetH * 16) / 9;
    m_fsr_pending = fsr_setting && (m_frame_width < targetW || m_frame_height < targetH);

    recordStaticVideoCommands();

    m_textures_initialized = true;
    brls::Logger::info("Deko3dRenderer::setupTextures: luma_id={}, chroma_id={}, fsr={}",
        m_luma_texture_id, m_chroma_texture_id, m_fsr_enabled);
    return true;
}

void Deko3dRenderer::recordStaticVideoCommands()
{
    m_cmdbuf.clear();

    m_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_cmdbuf.bindColorState(dk::ColorState{});
    m_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

    m_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_vertex_shader, m_fragment_shader });
    m_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_luma_texture_id, 0));
    m_cmdbuf.bindTextures(DkStage_Fragment, 1, dkMakeTextureHandle(m_chroma_texture_id, 0));

    m_cmdbuf.bindVtxBuffer(0, m_vertex_buffer.getGpuAddr(), m_vertex_buffer.getSize());
    m_cmdbuf.bindVtxAttribState(VertexAttribState);
    m_cmdbuf.bindVtxBufferState(VertexBufferState);

    m_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);

    m_video_cmdlist = m_cmdbuf.finishList();
    brls::Logger::info("Deko3dRenderer: static video command list recorded");
}

bool Deko3dRenderer::compileShaderFromSource(CShader& shader, const std::string& source, bool isVertex)
{
    uint8_t* dksh_out = nullptr;
    uint32_t dksh_size = 0;

    uam_pipeline_stage stage = isVertex ? uam_pipeline_stage_vertex : uam_pipeline_stage_fragment;

    brls::Logger::info("Deko3dRenderer: compiling {} shader ({} bytes)",
        isVertex ? "vertex" : "fragment", source.size());

    if (!uam_compileDksh(stage, source.c_str(), 3, &dksh_out, &dksh_size))
    {
        brls::Logger::error("Deko3dRenderer: failed to compile {} shader",
            isVertex ? "vertex" : "fragment");
        return false;
    }

    brls::Logger::info("Deko3dRenderer: {} shader compiled ({} bytes dksh)",
        isVertex ? "vertex" : "fragment", dksh_size);

    shader.destroy();
    bool result = shader.loadFromMemory(*m_pool_code, dksh_out, dksh_size);
    std::free(dksh_out);

    if (!result)
        brls::Logger::error("Deko3dRenderer: failed to load compiled {} shader into GPU memory",
            isVertex ? "vertex" : "fragment");

    return result;
}

bool Deko3dRenderer::compileVideoShaders(bool dithering)
{
    brls::Logger::info("Deko3dRenderer: compiling video shaders (dithering={})", dithering);

    std::string vsh_source = loadShaderSource("romfs:/shaders/video_vsh.glsl");
    if (vsh_source.empty())
        return false;

    if (!compileShaderFromSource(m_vertex_shader, vsh_source, true))
        return false;

    std::string fsh_source = loadShaderSource("romfs:/shaders/video_fsh.glsl");
    if (fsh_source.empty())
        return false;

    if (dithering)
    {
        float strength = SettingsManager::getInstance()->getDitheringStrength();
        auto pos = fsh_source.find('\n');
        if (pos != std::string::npos)
            fsh_source.insert(pos + 1,
                std::format("#define DITHER_NOISE\n#define DITHER_STRENGTH {:.1f}\n", strength));
    }

    if (!compileShaderFromSource(m_fragment_shader, fsh_source, false))
        return false;

    m_dithering_enabled = dithering;
    brls::Logger::info("Deko3dRenderer: video shaders compiled successfully");
    return true;
}

void Deko3dRenderer::setDithering(bool enabled)
{
    if (enabled == m_dithering_enabled)
        return;

    if (!m_initialized || !m_pool_code)
        return;

    brls::Logger::info("Deko3dRenderer: switching dithering to {}", enabled);

    if (compileVideoShaders(enabled) && m_textures_initialized)
        recordStaticVideoCommands();
}

void Deko3dRenderer::initFsr()
{
    m_queue.waitIdle();

    m_fsr_sharpness = SettingsManager::getInstance()->getFsrSharpness();

    int targetH = SettingsManager::getInstance()->getFsrTargetHeight();
    m_fsr_target_height = targetH;
    m_fsr_target_width = (targetH * 16) / 9;
    m_fsr_supersampling = (m_fsr_target_width > m_display_width || m_fsr_target_height > m_display_height);

    if (m_fsr_target_width <= m_frame_width && m_fsr_target_height <= m_frame_height)
    {
        brls::Logger::info("Deko3dRenderer::initFsr: target {}x{} <= frame {}x{}, skipping",
            m_fsr_target_width, m_fsr_target_height, m_frame_width, m_frame_height);
        return;
    }

    brls::Logger::info("Deko3dRenderer::initFsr: input={}x{} target={}x{} display={}x{} ss={} ratio={:.2f}x{:.2f}",
        m_frame_width, m_frame_height, m_fsr_target_width, m_fsr_target_height,
        m_display_width, m_display_height, m_fsr_supersampling,
        (float)m_fsr_target_width / m_frame_width, (float)m_fsr_target_height / m_frame_height);

    m_fsr_easu_shader.load(*m_pool_code, "romfs:/shaders/fsr_easu_fsh.dksh");
    m_fsr_rcas_shader.load(*m_pool_code, "romfs:/shaders/fsr_rcas_fsh.dksh");
    m_fsr_pass_shader.load(*m_pool_code, "romfs:/shaders/fsr_pass_fsh.dksh");

    CMemPool* imagesPool = m_vctx->getImagesPool();
    if (!imagesPool)
    {
        brls::Logger::error("initFsr: no images pool available");
        return;
    }

    dk::ImageLayoutMaker{m_device}
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(m_frame_width, m_frame_height, 1)
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine)
        .initialize(m_rt_yuv_layout);

    m_rt_yuv_handle = imagesPool->allocate(m_rt_yuv_layout.getSize(), m_rt_yuv_layout.getAlignment());
    m_rt_yuv_image.initialize(m_rt_yuv_layout, m_rt_yuv_handle.getMemBlock(), m_rt_yuv_handle.getOffset());
    m_rt_yuv_desc.initialize(m_rt_yuv_image, true);
    m_rt_yuv_texture_id = m_vctx->allocateImageIndex();

    dk::ImageLayoutMaker{m_device}
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(m_fsr_target_width, m_fsr_target_height, 1)
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine)
        .initialize(m_rt_easu_layout);

    m_rt_easu_handle = imagesPool->allocate(m_rt_easu_layout.getSize(), m_rt_easu_layout.getAlignment());
    m_rt_easu_image.initialize(m_rt_easu_layout, m_rt_easu_handle.getMemBlock(), m_rt_easu_handle.getOffset());
    m_rt_easu_desc.initialize(m_rt_easu_image, true);
    m_rt_easu_texture_id = m_vctx->allocateImageIndex();

    if (m_fsr_supersampling)
    {
        m_rt_rcas_handle = imagesPool->allocate(m_rt_easu_layout.getSize(), m_rt_easu_layout.getAlignment());
        m_rt_rcas_image.initialize(m_rt_easu_layout, m_rt_rcas_handle.getMemBlock(), m_rt_rcas_handle.getOffset());
        m_rt_rcas_desc.initialize(m_rt_rcas_image, true);
        m_rt_rcas_texture_id = m_vctx->allocateImageIndex();
    }

    m_fsr_uniform_buffer = m_pool_data->allocate(512, DK_UNIFORM_BUF_ALIGNMENT);
    computeFsrConstants();

    m_fsr_static_cmdbuf = dk::CmdBufMaker{m_device}.create();
    m_fsr_static_cmdmem = m_pool_data->allocate(FsrStaticCmdSize);
    m_fsr_static_cmdbuf.addMemory(m_fsr_static_cmdmem.getMemBlock(), m_fsr_static_cmdmem.getOffset(), m_fsr_static_cmdmem.getSize());

    m_fsr_rcas_cmdbuf = dk::CmdBufMaker{m_device}.create();
    m_fsr_rcas_cmdmem = m_pool_data->allocate(FsrRcasCmdSize);

    m_update_cmdbuf.clear();
    m_update_cmdbuf.addMemory(
        m_update_cmdmem.getMemBlock(),
        m_update_cmdmem.getOffset(),
        UpdateCmdSliceSize);
    m_image_descriptor_set->update(m_update_cmdbuf, m_rt_yuv_texture_id, m_rt_yuv_desc);
    m_image_descriptor_set->update(m_update_cmdbuf, m_rt_easu_texture_id, m_rt_easu_desc);
    if (m_rt_rcas_texture_id)
        m_image_descriptor_set->update(m_update_cmdbuf, m_rt_rcas_texture_id, m_rt_rcas_desc);
    m_queue.submitCommands(m_update_cmdbuf.finishList());
    m_queue.waitIdle();

    recordFsrCommands();

    m_fsr_enabled = true;
    brls::Logger::info("Deko3dRenderer::initFsr: initialized, sharpness={}", m_fsr_sharpness);
}

void Deko3dRenderer::computeFsrConstants()
{
    struct {
        uint32_t con0[4];
        uint32_t con1[4];
        uint32_t con2[4];
        uint32_t con3[4];
        uint32_t rcas_con0[4];
    } constants;

    float inputW = static_cast<float>(m_frame_width);
    float inputH = static_cast<float>(m_frame_height);
    float outputW = static_cast<float>(m_fsr_target_width);
    float outputH = static_cast<float>(m_fsr_target_height);

    auto f2u = [](float f) -> uint32_t {
        uint32_t u;
        memcpy(&u, &f, sizeof(u));
        return u;
    };

    constants.con0[0] = f2u(inputW / outputW);
    constants.con0[1] = f2u(inputH / outputH);
    constants.con0[2] = f2u(0.5f * inputW / outputW - 0.5f);
    constants.con0[3] = f2u(0.5f * inputH / outputH - 0.5f);

    constants.con1[0] = f2u(1.0f / inputW);
    constants.con1[1] = f2u(1.0f / inputH);
    constants.con1[2] = f2u(1.0f / inputW);
    constants.con1[3] = f2u(-1.0f / inputH);

    constants.con2[0] = f2u(-1.0f / inputW);
    constants.con2[1] = f2u(2.0f / inputH);
    constants.con2[2] = f2u(1.0f / inputW);
    constants.con2[3] = f2u(2.0f / inputH);

    constants.con3[0] = f2u(0.0f / inputW);
    constants.con3[1] = f2u(4.0f / inputH);
    constants.con3[2] = 0;
    constants.con3[3] = 0;

    memcpy(m_fsr_uniform_buffer.getCpuAddr(), &constants, sizeof(constants));

    struct { uint32_t con0[4]; } rcas_constants;
    float sharpnessLinear = exp2f(-m_fsr_sharpness);
    rcas_constants.con0[0] = f2u(sharpnessLinear);
    rcas_constants.con0[1] = 0;
    rcas_constants.con0[2] = 0;
    rcas_constants.con0[3] = 0;

    memcpy((uint8_t*)m_fsr_uniform_buffer.getCpuAddr() + 256, &rcas_constants, sizeof(rcas_constants));
}

void Deko3dRenderer::recordFsrCommands()
{
    m_fsr_static_cmdbuf.clear();
    m_fsr_static_cmdbuf.addMemory(m_fsr_static_cmdmem.getMemBlock(), m_fsr_static_cmdmem.getOffset(), m_fsr_static_cmdmem.getSize());

    dk::ImageView yuvTarget{m_rt_yuv_image};
    m_fsr_static_cmdbuf.bindRenderTargets(&yuvTarget);
    m_fsr_static_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)m_frame_width, (float)m_frame_height, 0.0f, 1.0f }});
    m_fsr_static_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)m_frame_width, (uint32_t)m_frame_height }});

    m_fsr_static_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_fsr_static_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_fsr_static_cmdbuf.bindColorState(dk::ColorState{});
    m_fsr_static_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

    m_image_descriptor_set->bindForImages(m_fsr_static_cmdbuf);
    m_fsr_static_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_vertex_shader, m_fragment_shader });
    m_fsr_static_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_luma_texture_id, 0));
    m_fsr_static_cmdbuf.bindTextures(DkStage_Fragment, 1, dkMakeTextureHandle(m_chroma_texture_id, 0));
    m_fsr_static_cmdbuf.bindVtxBuffer(0, m_vertex_buffer.getGpuAddr(), m_vertex_buffer.getSize());
    m_fsr_static_cmdbuf.bindVtxAttribState(VertexAttribState);
    m_fsr_static_cmdbuf.bindVtxBufferState(VertexBufferState);
    m_fsr_static_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);

    m_fsr_yuv_cmdlist = m_fsr_static_cmdbuf.finishList();

    m_fsr_static_cmdbuf.barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);

    dk::ImageView easuTarget{m_rt_easu_image};
    m_fsr_static_cmdbuf.bindRenderTargets(&easuTarget);
    m_fsr_static_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)m_fsr_target_width, (float)m_fsr_target_height, 0.0f, 1.0f }});
    m_fsr_static_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)m_fsr_target_width, (uint32_t)m_fsr_target_height }});

    m_fsr_static_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_fsr_static_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_fsr_static_cmdbuf.bindColorState(dk::ColorState{});
    m_fsr_static_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

    m_image_descriptor_set->bindForImages(m_fsr_static_cmdbuf);
    m_fsr_static_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_vertex_shader, m_fsr_easu_shader });
    m_fsr_static_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_rt_yuv_texture_id, 0));
    m_fsr_static_cmdbuf.bindUniformBuffer(DkStage_Fragment, 0,
        m_fsr_uniform_buffer.getGpuAddr(), 64);
    m_fsr_static_cmdbuf.bindVtxBuffer(0, m_vertex_buffer.getGpuAddr(), m_vertex_buffer.getSize());
    m_fsr_static_cmdbuf.bindVtxAttribState(VertexAttribState);
    m_fsr_static_cmdbuf.bindVtxBufferState(VertexBufferState);
    m_fsr_static_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);

    m_fsr_easu_cmdlist = m_fsr_static_cmdbuf.finishList();

    brls::Logger::info("Deko3dRenderer: FSR command lists recorded");
}

void Deko3dRenderer::cleanupFsr()
{
    if (!m_fsr_enabled)
        return;

    brls::Logger::info("Deko3dRenderer::cleanupFsr");

    m_fsr_yuv_cmdlist = 0;
    m_fsr_easu_cmdlist = 0;

    m_fsr_static_cmdmem.destroy();
    m_fsr_rcas_cmdmem.destroy();
    m_fsr_uniform_buffer.destroy();

    if (m_rt_yuv_texture_id) {
        m_vctx->freeImageIndex(m_rt_yuv_texture_id);
        m_rt_yuv_texture_id = 0;
    }
    if (m_rt_easu_texture_id) {
        m_vctx->freeImageIndex(m_rt_easu_texture_id);
        m_rt_easu_texture_id = 0;
    }
    if (m_rt_rcas_texture_id) {
        m_vctx->freeImageIndex(m_rt_rcas_texture_id);
        m_rt_rcas_texture_id = 0;
    }

    m_rt_yuv_handle.destroy();
    m_rt_easu_handle.destroy();
    m_rt_rcas_handle.destroy();
    m_rt_yuv_image = dk::Image{};
    m_rt_easu_image = dk::Image{};

    m_fsr_easu_shader.destroy();
    m_fsr_rcas_shader.destroy();
    m_fsr_pass_shader.destroy();

    m_fsr_enabled = false;
}

void Deko3dRenderer::draw(AVFrame* frame)
{
    if (!m_initialized)
        return;

    if (!frame || frame->format != AV_PIX_FMT_NVTEGRA)
        return;

    if (!m_textures_initialized)
    {
        if (!setupTextures(frame))
            return;
    }

    if (m_paused)
        return;

    AVFrame* new_ref = av_frame_alloc();
    if (!new_ref || av_frame_ref(new_ref, frame) < 0)
    {
        if (new_ref) av_frame_free(&new_ref);
        return;
    }

    if (m_current_frame)
        av_frame_free(&m_current_frame);

    m_current_frame = new_ref;
    m_frame_bound = true;
}

void Deko3dRenderer::renderVideo(AVFrame* frame)
{
    if (!m_initialized || !m_textures_initialized || !m_frame_bound || !m_current_frame)
        return;

    if (m_fsr_pending)
    {
        m_queue.waitIdle();
        initFsr();
        m_fsr_pending = false;
    }

    if (!m_fsr_enabled && !m_video_cmdlist)
        return;

    int oldest = m_frame_ring_index;
    if (m_frame_ring[oldest])
        av_frame_free(&m_frame_ring[oldest]);

    m_frame_ring[oldest] = m_current_frame;
    m_current_frame = nullptr;
    m_frame_ring_index = (m_frame_ring_index + 1) % FRAME_RING_SIZE;

    updateFrameBindings(m_frame_ring[oldest]);

    if (m_fsr_enabled)
    {
        m_queue.submitCommands(m_fsr_yuv_cmdlist);
        m_queue.waitIdle();
        m_queue.submitCommands(m_fsr_easu_cmdlist);

        dk::Image* framebuffer = m_vctx->getFramebuffer();
        if (framebuffer)
        {
            m_fsr_rcas_cmdbuf.clear();
            m_fsr_rcas_cmdbuf.addMemory(m_fsr_rcas_cmdmem.getMemBlock(), m_fsr_rcas_cmdmem.getOffset(), m_fsr_rcas_cmdmem.getSize());

            dk::ImageView colorTarget{*framebuffer};
            m_fsr_rcas_cmdbuf.bindRenderTargets(&colorTarget);
            m_fsr_rcas_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)m_display_width, (float)m_display_height, 0.0f, 1.0f }});
            m_fsr_rcas_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)m_display_width, (uint32_t)m_display_height }});

            m_fsr_rcas_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
            m_fsr_rcas_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
                .setDepthTestEnable(false)
                .setDepthWriteEnable(false)
                .setStencilTestEnable(false));
            m_fsr_rcas_cmdbuf.bindColorState(dk::ColorState{});
            m_fsr_rcas_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

            m_image_descriptor_set->bindForImages(m_fsr_rcas_cmdbuf);

            m_fsr_rcas_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_vertex_shader, m_fsr_rcas_shader });
            m_fsr_rcas_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_rt_easu_texture_id, 0));
            m_fsr_rcas_cmdbuf.bindUniformBuffer(DkStage_Fragment, 0,
                m_fsr_uniform_buffer.getGpuAddr() + 256, 16);
            m_fsr_rcas_cmdbuf.bindVtxBuffer(0, m_vertex_buffer.getGpuAddr(), m_vertex_buffer.getSize());
            m_fsr_rcas_cmdbuf.bindVtxAttribState(VertexAttribState);
            m_fsr_rcas_cmdbuf.bindVtxBufferState(VertexBufferState);

            if (m_fsr_supersampling)
            {
                dk::ImageView rcasTarget{m_rt_rcas_image};
                m_fsr_rcas_cmdbuf.bindRenderTargets(&rcasTarget);
                m_fsr_rcas_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)m_fsr_target_width, (float)m_fsr_target_height, 0.0f, 1.0f }});
                m_fsr_rcas_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)m_fsr_target_width, (uint32_t)m_fsr_target_height }});
                m_fsr_rcas_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);

                dk::ImageView finalTarget{*framebuffer};
                m_fsr_rcas_cmdbuf.bindRenderTargets(&finalTarget);
                m_fsr_rcas_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)m_display_width, (float)m_display_height, 0.0f, 1.0f }});
                m_fsr_rcas_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)m_display_width, (uint32_t)m_display_height }});
                m_fsr_rcas_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_vertex_shader, m_fsr_pass_shader });
                m_fsr_rcas_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_rt_rcas_texture_id, 0));
                m_fsr_rcas_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);
            }
            else
            {
                m_fsr_rcas_cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);
            }

            m_queue.submitCommands(m_fsr_rcas_cmdbuf.finishList());
        }
    }
    else
    {
        m_queue.submitCommands(m_video_cmdlist);
    }
    m_queue.flush();

    if (m_show_stats || m_border_flash_frames > 0)
        m_queue.waitIdle();

    renderStatsOverlay();

    if (m_border_flash_frames > 0)
        renderBorderFlash();
}

void Deko3dRenderer::registerCallback()
{
    if (m_callback_registered)
        return;

    brls::Application::setPostRenderCallback([this]() {
        if (m_tick_callback)
            m_tick_callback();

        if (m_frame_bound && !m_paused)
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

    brls::Application::setExclusiveRender(false);
    brls::Application::setPostRenderCallback(nullptr);
    m_callback_registered = false;
    brls::Logger::info("Deko3dRenderer: post-render callback unregistered");
}

void Deko3dRenderer::updateFrameBindings(AVFrame* frame)
{
    AVNVTegraMap* map = av_nvtegra_frame_get_fbuf_map(frame);
    if (!map)
        return;

    uint32_t handle = av_nvtegra_map_get_handle(map);
    void* cpuAddr = av_nvtegra_map_get_addr(map);
    uint32_t size = av_nvtegra_map_get_size(map);
    uint32_t chromaOffset = static_cast<uint32_t>(frame->data[1] - frame->data[0]);

    int mappingIndex = -1;
    for (size_t i = 0; i < m_frame_mappings.size(); ++i)
    {
        const auto& m = m_frame_mappings[i];
        if (m.handle == handle && m.cpuAddr == cpuAddr && m.size == size && m.chromaOffset == chromaOffset)
        {
            mappingIndex = static_cast<int>(i);
            break;
        }
    }

    if (mappingIndex < 0)
    {
        FrameMapping mapping;
        mapping.handle = handle;
        mapping.cpuAddr = cpuAddr;
        mapping.size = size;
        mapping.chromaOffset = chromaOffset;

        mapping.memblock = dk::MemBlockMaker{m_device, size}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .setStorage(cpuAddr)
            .create();

        mapping.luma.initialize(m_luma_layout, mapping.memblock, 0);
        mapping.chroma.initialize(m_chroma_layout, mapping.memblock, chromaOffset);

        mapping.lumaDesc.initialize(mapping.luma);
        mapping.chromaDesc.initialize(mapping.chroma);

        m_frame_mappings.emplace_back(std::move(mapping));
        mappingIndex = static_cast<int>(m_frame_mappings.size()) - 1;
    }

    if (mappingIndex == m_current_mapping_index)
        return;

    m_update_cmdbuf.clear();
    m_update_cmdbuf.addMemory(
        m_update_cmdmem.getMemBlock(),
        m_update_cmdmem.getOffset() + m_update_cmdmem_slice * UpdateCmdSliceSize,
        UpdateCmdSliceSize);
    m_update_cmdmem_slice = (m_update_cmdmem_slice + 1) % brls::FRAMEBUFFERS_COUNT;

    auto& active = m_frame_mappings[mappingIndex];
    m_image_descriptor_set->update(m_update_cmdbuf, m_luma_texture_id, active.lumaDesc);
    m_image_descriptor_set->update(m_update_cmdbuf, m_chroma_texture_id, active.chromaDesc);

    m_queue.submitCommands(m_update_cmdbuf.finishList());
    m_current_mapping_index = mappingIndex;
}

void Deko3dRenderer::cleanup()
{
    if (!m_initialized)
        return;

    brls::Logger::info("Deko3dRenderer::cleanup");

    unregisterCallback();

    m_queue.waitIdle();

    if (m_current_frame)
        av_frame_free(&m_current_frame);

    for (int i = 0; i < FRAME_RING_SIZE; ++i)
    {
        if (m_frame_ring[i])
            av_frame_free(&m_frame_ring[i]);
    }
    m_frame_ring_index = 0;

    cleanupFsr();
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

    m_frame_mappings.clear();
    m_current_mapping_index = -1;

    m_video_cmdlist = 0;
    m_update_cmdmem.destroy();
    m_overlay_cmdmem.destroy();

    m_luma_layout = dk::ImageLayout{};
    m_chroma_layout = dk::ImageLayout{};

    m_frame_bound = false;

    m_initialized = false;
    m_textures_initialized = false;
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
    static uint32_t call_count = 0;
    ++call_count;
    if (SettingsManager::getInstance()->getDebugRenderLog() && call_count % 60 == 1)
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

    auto& wg = WireGuardManager::instance();
    std::string vpnStatus = wg.isConnected() ? wg.getTunnelIP() : "Off";

    std::string renderedSection = std::format(
        "=== Rendered ===\n"
        "{}x{} @ {:.0f}fps\n"
        "Decoder: {} ({})\n",
        m_stats.video_width,
        m_stats.video_height,
        m_stats.fps,
        m_stats.is_hevc ? "HEVC" : "H.264",
        m_stats.is_hardware_decoder ? "NVTEGRA" : "SW");

    if (m_stats.low_latency_mode)
    {
        renderedSection += "Mode: Low Latency\n";
    }
    else
    {
        renderedSection += std::format(
            "Dropped: {}  Faked: {}\n"
            "Queue: {}\n",
            m_stats.dropped_frames,
            m_stats.faked_frames,
            m_stats.queue_size);
    }

    std::string statsText = std::format(
        "=== Requested ===\n"
        "{}x{} @ {}fps\n"
        "Target: {} kbps\n"
        "Codec: {}\n"
        "\n"
        "{}"
        "\n"
        "=== Network ===\n"
        "Packet Loss (Live): {:.1f}%\n"
        "Reported: {:.1f} Mbps\n"
        "Frame Loss: {} (Rec: {})\n"
        "Duration: {}m{:02}s\n"
        "GHASH: {}\n"
        "VPN: {}",
        m_stats.requested_width,
        m_stats.requested_height,
        m_stats.requested_fps,
        m_stats.requested_bitrate,
        m_stats.requested_hevc ? "HEVC" : "H.264",
        renderedSection,
        m_stats.packet_loss_percent,
        m_stats.measured_bitrate_mbps,
        m_stats.network_frames_lost,
        m_stats.frames_recovered,
        mins,
        secs,
        ghashMode,
        vpnStatus
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
    for (const char* p = statsText.c_str(); *p; p++)
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
    vertices.reserve(6 + 6 * statsText.size());  // Background quad + text quads

    // Background color (semi-transparent black)
    float bgR = 0.0f, bgG = 0.0f, bgB = 0.0f, bgA = 0.7f;

    // Text color (green)
    float txR = 0.0f, txG = 1.0f, txB = 0.0f, txA = 1.0f;

    // Background quad (NDC coordinates)
    float bgX1 = MARGIN;
    float bgY1 = MARGIN;
    float bgX2 = MARGIN + boxWidth;
    float bgY2 = MARGIN + boxHeight;

    unsigned screenW = brls::Application::windowWidth;
    unsigned screenH = brls::Application::windowHeight;

    float ndcX1, ndcY1, ndcX2, ndcY2;
    pixelToNDC(bgX1, bgY1, screenW, screenH, ndcX1, ndcY1);
    pixelToNDC(bgX2, bgY2, screenW, screenH, ndcX2, ndcY2);

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

    for (const char* p = statsText.c_str(); *p; p++)
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

        pixelToNDC(cx1, cy1, screenW, screenH, ndcX1, ndcY1);
        pixelToNDC(cx2, cy2, screenW, screenH, ndcX2, ndcY2);

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
    ++draw_count;
    if (SettingsManager::getInstance()->getDebugRenderLog() && draw_count % 60 == 1)
        brls::Logger::info("Stats overlay drawing {} vertices (bg=6, text={})",
            vertices.size(), vertices.size() - 6);

    // Copy to GPU buffer
    memcpy(m_text_vertex_buffer.getCpuAddr(), vertices.data(), vertices.size() * sizeof(TextVertex));

    // Get current framebuffer
    dk::Image* framebuffer = m_vctx->getFramebuffer();
    if (!framebuffer)
        return;

    m_overlay_cmdbuf.clear();
    m_overlay_cmdbuf.addMemory(m_overlay_cmdmem.getMemBlock(), m_overlay_cmdmem.getOffset(), m_overlay_cmdmem.getSize());

    dk::ImageView colorTarget{*framebuffer};
    m_overlay_cmdbuf.bindRenderTargets(&colorTarget);

    m_overlay_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)screenW, (float)screenH, 0.0f, 1.0f }});
    m_overlay_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)screenW, (uint32_t)screenH }});

    m_overlay_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_overlay_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_overlay_cmdbuf.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    m_overlay_cmdbuf.bindColorWriteState(dk::ColorWriteState{});

    m_overlay_cmdbuf.bindBlendStates(0, dk::BlendState{}
        .setColorBlendOp(DkBlendOp_Add)
        .setSrcColorBlendFactor(DkBlendFactor_SrcAlpha)
        .setDstColorBlendFactor(DkBlendFactor_InvSrcAlpha)
        .setAlphaBlendOp(DkBlendOp_Add)
        .setSrcAlphaBlendFactor(DkBlendFactor_One)
        .setDstAlphaBlendFactor(DkBlendFactor_InvSrcAlpha));

    m_overlay_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_text_vertex_shader, m_text_fragment_shader });

    m_image_descriptor_set->bindForImages(m_overlay_cmdbuf);

    m_overlay_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_font_texture_id, 2));

    m_overlay_cmdbuf.bindVtxBuffer(0, m_text_vertex_buffer.getGpuAddr(), vertices.size() * sizeof(TextVertex));
    m_overlay_cmdbuf.bindVtxAttribState(TextVertexAttribState);
    m_overlay_cmdbuf.bindVtxBufferState(TextVertexBufferState);

    m_overlay_cmdbuf.draw(DkPrimitive_Triangles, vertices.size(), 1, 0, 0);

    DkCmdList list = m_overlay_cmdbuf.finishList();
    if (list)
    {
        m_queue.submitCommands(list);
        m_queue.flush();
    }
}

void Deko3dRenderer::renderBorderFlash()
{
    if (m_border_flash_frames <= 0 || !m_text_initialized)
        return;

    m_border_flash_frames--;

    float fade = (float)m_border_flash_frames / (float)BORDER_FLASH_DURATION;
    float edgeAlpha = fade * 0.25f;
    float innerAlpha = 0.0f;

    unsigned screenW = brls::Application::windowWidth;
    unsigned screenH = brls::Application::windowHeight;

    constexpr float T = 64.0f;
    float r = 0.6f, g = 0.85f, b = 1.0f;

    std::vector<TextVertex> vertices;
    vertices.reserve(48);

    auto addGradientQuad = [&](float x1, float y1, float x2, float y2,
                               float x3, float y3, float x4, float y4,
                               float a1, float a2) {
        float nx1, ny1, nx2, ny2, nx3, ny3, nx4, ny4;
        pixelToNDC(x1, y1, screenW, screenH, nx1, ny1);
        pixelToNDC(x2, y2, screenW, screenH, nx2, ny2);
        pixelToNDC(x3, y3, screenW, screenH, nx3, ny3);
        pixelToNDC(x4, y4, screenW, screenH, nx4, ny4);

        vertices.push_back({{ nx1, ny1, 0.0f }, { -1.0f, -1.0f }, { r, g, b, a1 }});
        vertices.push_back({{ nx2, ny2, 0.0f }, { -1.0f, -1.0f }, { r, g, b, a1 }});
        vertices.push_back({{ nx3, ny3, 0.0f }, { -1.0f, -1.0f }, { r, g, b, a2 }});

        vertices.push_back({{ nx2, ny2, 0.0f }, { -1.0f, -1.0f }, { r, g, b, a1 }});
        vertices.push_back({{ nx4, ny4, 0.0f }, { -1.0f, -1.0f }, { r, g, b, a2 }});
        vertices.push_back({{ nx3, ny3, 0.0f }, { -1.0f, -1.0f }, { r, g, b, a2 }});
    };

    float sw = (float)screenW, sh = (float)screenH;

    addGradientQuad(0, 0,    sw, 0,     0, T,    sw, T,    edgeAlpha, innerAlpha);
    addGradientQuad(0, sh-T, sw, sh-T,  0, sh,   sw, sh,   innerAlpha, edgeAlpha);
    addGradientQuad(0, 0,    0, sh,     T, 0,    T, sh,    edgeAlpha, innerAlpha);
    addGradientQuad(sw-T, 0, sw-T, sh,  sw, 0,   sw, sh,   innerAlpha, edgeAlpha);

    memcpy(m_text_vertex_buffer.getCpuAddr(), vertices.data(), vertices.size() * sizeof(TextVertex));

    dk::Image* framebuffer = m_vctx->getFramebuffer();
    if (!framebuffer)
        return;

    m_overlay_cmdbuf.clear();
    m_overlay_cmdbuf.addMemory(m_overlay_cmdmem.getMemBlock(), m_overlay_cmdmem.getOffset(), m_overlay_cmdmem.getSize());

    dk::ImageView colorTarget{*framebuffer};
    m_overlay_cmdbuf.bindRenderTargets(&colorTarget);
    m_overlay_cmdbuf.setViewports(0, {{ 0.0f, 0.0f, (float)screenW, (float)screenH, 0.0f, 1.0f }});
    m_overlay_cmdbuf.setScissors(0, {{ 0, 0, (uint32_t)screenW, (uint32_t)screenH }});

    m_overlay_cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    m_overlay_cmdbuf.bindDepthStencilState(dk::DepthStencilState{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setStencilTestEnable(false));
    m_overlay_cmdbuf.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    m_overlay_cmdbuf.bindColorWriteState(dk::ColorWriteState{});
    m_overlay_cmdbuf.bindBlendStates(0, dk::BlendState{}
        .setColorBlendOp(DkBlendOp_Add)
        .setSrcColorBlendFactor(DkBlendFactor_SrcAlpha)
        .setDstColorBlendFactor(DkBlendFactor_InvSrcAlpha)
        .setAlphaBlendOp(DkBlendOp_Add)
        .setSrcAlphaBlendFactor(DkBlendFactor_One)
        .setDstAlphaBlendFactor(DkBlendFactor_InvSrcAlpha));

    m_overlay_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { m_text_vertex_shader, m_text_fragment_shader });
    m_image_descriptor_set->bindForImages(m_overlay_cmdbuf);
    m_overlay_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(m_font_texture_id, 2));
    m_overlay_cmdbuf.bindVtxBuffer(0, m_text_vertex_buffer.getGpuAddr(), vertices.size() * sizeof(TextVertex));
    m_overlay_cmdbuf.bindVtxAttribState(TextVertexAttribState);
    m_overlay_cmdbuf.bindVtxBufferState(TextVertexBufferState);

    m_overlay_cmdbuf.draw(DkPrimitive_Triangles, vertices.size(), 1, 0, 0);

    DkCmdList list = m_overlay_cmdbuf.finishList();
    if (list)
    {
        m_queue.submitCommands(list);
        m_queue.flush();
    }
}

#endif // BOREALIS_USE_DEKO3D
