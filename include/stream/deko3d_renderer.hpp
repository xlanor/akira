#ifndef AKIRA_DEKO3D_RENDERER_HPP
#define AKIRA_DEKO3D_RENDERER_HPP

#ifdef BOREALIS_USE_DEKO3D

#include "stream/video_renderer.hpp"

#include <deko3d.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <borealis.hpp>
#include <borealis/platforms/switch/switch_video.hpp>
#include <nanovg/framework/CShader.h>
#include <nanovg/framework/CMemPool.h>
#include <nanovg/framework/CDescriptorSet.h>

extern "C"
{
#include <libavutil/hwcontext_nvtegra.h>
}

class Deko3dRenderer : public IVideoRenderer
{
public:
    Deko3dRenderer();
    ~Deko3dRenderer() override;

    bool initialize(int frame_width, int frame_height,
                   ChiakiLog* log) override;
    bool isInitialized() const override { return m_initialized; }
    void draw(AVFrame* frame) override;
    void presentFrame(AVFrame* frame) override;
    void cleanup() override;
    void waitIdle() override;

    void setShowStatsOverlay(bool show) override { m_show_stats = show; }
    void setStreamStats(const StreamStats& stats) override { m_stats = stats; }
    void setPaused(bool paused) { m_paused = paused; }
    void updateResolution(int width, int height) { m_frame_width = width; m_frame_height = height; }

    void triggerBorderFlash() { m_border_flash_frames = BORDER_FLASH_DURATION; }

    void setTickCallback(TickCallback cb) override { m_tick_callback = std::move(cb); }

    void setDithering(bool enabled);

private:
    bool m_paused = false;
    std::optional<CMemPool> m_pool_code;
    std::optional<CMemPool> m_pool_data;

    void initTextRendering();
    void renderStatsOverlay();
    void renderBorderFlash();
    void cleanupTextRendering();

    int m_border_flash_frames = 0;
    static constexpr int BORDER_FLASH_DURATION = 20;

    CShader m_text_vertex_shader;
    CShader m_text_fragment_shader;
    bool m_text_initialized = false;

    dk::Image m_font_image;
    dk::ImageLayout m_font_layout;
    dk::ImageDescriptor m_font_desc;
    dk::MemBlock m_font_memblock;
    int m_font_texture_id = 0;

    CMemPool::Handle m_text_vertex_buffer;
    static constexpr size_t MAX_TEXT_VERTICES = 2048;

    bool m_initialized = false;
    ChiakiLog* m_log = nullptr;

    int m_frame_width = 0;
    int m_frame_height = 0;

    brls::SwitchVideoContext* m_vctx = nullptr;
    dk::Device m_device;
    dk::Queue m_queue;

    dk::UniqueCmdBuf m_cmdbuf;
    DkCmdList m_video_cmdlist = 0;

    dk::UniqueCmdBuf m_update_cmdbuf;
    CMemPool::Handle m_update_cmdmem;
    uint32_t m_update_cmdmem_slice = 0;
    static constexpr unsigned UpdateCmdSliceSize = 0x1000;

    dk::UniqueCmdBuf m_overlay_cmdbuf;
    CMemPool::Handle m_overlay_cmdmem;
    static constexpr unsigned OverlayCmdSize = 0x10000;

    CShader m_vertex_shader;

    struct ShaderVariantKey {
        bool dithering = false;
        bool fsrEasu = false;
        bool fsrRcas = false;
        bool fsrPass = false;

        bool operator==(const ShaderVariantKey& other) const = default;
    };

    struct CachedFragmentShader {
        ShaderVariantKey key;
        CShader shader;
    };

    CShader* m_fragment_shader = nullptr;

    CMemPool::Handle m_vertex_buffer;

    dk::ImageLayout m_luma_layout;
    dk::ImageLayout m_chroma_layout;

    struct FrameMapping {
        uint32_t handle = 0;
        void* cpuAddr = nullptr;
        uint32_t size = 0;
        uint32_t chromaOffset = 0;
        dk::UniqueMemBlock memblock;
        dk::Image luma;
        dk::Image chroma;
        dk::ImageDescriptor lumaDesc;
        dk::ImageDescriptor chromaDesc;
    };

    std::vector<FrameMapping> m_frame_mappings;
    int m_current_mapping_index = -1;

    int m_luma_texture_id = 0;
    int m_chroma_texture_id = 0;

    bool m_textures_initialized = false;

    bool setupTextures(AVFrame* frame);
    void updateFrameBindings(AVFrame* frame);
    void recordStaticVideoCommands();

    bool compileShaderFromSource(CShader& shader, const std::string& source, bool isVertex);
    bool ensureVertexShaderCompiled();
    CShader* getOrCreateFragmentShader(const ShaderVariantKey& key);
    bool compileVideoShaders(bool dithering);
    bool m_dithering_enabled = false;
    bool m_uam_initialized = false;
    bool m_fsr_enabled = false;
    bool m_fsr_pending = false;
    bool m_easu_enabled = false;
    bool m_rcas_enabled = false;
    bool m_fsr_supersampling = false;
    float m_fsr_sharpness = 0.2f;
    int m_display_width = 0;
    int m_display_height = 0;
    int m_fsr_target_width = 0;
    int m_fsr_target_height = 0;

    CShader* m_fsr_easu_shader = nullptr;
    CShader* m_fsr_rcas_shader = nullptr;
    CShader* m_fsr_pass_shader = nullptr;
    std::vector<std::unique_ptr<CachedFragmentShader>> m_fragment_shader_cache;

    CMemPool::Handle m_rt_easu_handle;
    dk::Image m_rt_easu_image;
    dk::ImageLayout m_rt_easu_layout;
    dk::ImageDescriptor m_rt_easu_desc;
    int m_rt_easu_texture_id = 0;

    CMemPool::Handle m_rt_rcas_handle;
    dk::Image m_rt_rcas_image;
    dk::ImageDescriptor m_rt_rcas_desc;
    int m_rt_rcas_texture_id = 0;

    dk::UniqueCmdBuf m_fsr_static_cmdbuf;
    CMemPool::Handle m_fsr_static_cmdmem;
    static constexpr unsigned FsrStaticCmdSize = 0x10000;
    DkCmdList m_fsr_easu_cmdlist = 0;

    dk::UniqueCmdBuf m_fsr_rcas_cmdbuf;
    CMemPool::Handle m_fsr_rcas_cmdmem;
    static constexpr unsigned FsrRcasCmdSize = 0x4000;

    CMemPool::Handle m_fsr_uniform_buffer;

    void initFsr();
    void cleanupFsr();
    void recordFsrCommands();
    void computeFsrConstants();

    bool m_frame_bound = false;

    static constexpr int FRAME_RING_SIZE = 3;
    AVFrame* m_frame_ring[FRAME_RING_SIZE] = {};
    int m_frame_ring_index = 0;
    AVFrame* m_current_frame = nullptr;

    void renderVideo();

    void registerCallback();
    void unregisterCallback();
    bool m_callback_registered = false;
    TickCallback m_tick_callback;
};

#endif // BOREALIS_USE_DEKO3D

#endif // AKIRA_DEKO3D_RENDERER_HPP
