#ifndef AKIRA_DEKO3D_RENDERER_HPP
#define AKIRA_DEKO3D_RENDERER_HPP

#ifdef BOREALIS_USE_DEKO3D

#include "core/io/video_renderer.hpp"

#include <deko3d.hpp>
#include <memory>
#include <optional>

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
                   int screen_width, int screen_height,
                   ChiakiLog* log) override;
    bool isInitialized() const override { return m_initialized; }
    void draw(AVFrame* frame) override;
    void cleanup() override;
    void waitIdle() override;

    void setShowStatsOverlay(bool show) override { m_show_stats = show; }
    void setStreamStats(const StreamStats& stats) override { m_stats = stats; }
    void setPaused(bool paused) { m_paused = paused; }

private:
    bool m_paused = false;
    std::optional<CMemPool> m_pool_code;
    std::optional<CMemPool> m_pool_data;

    // Begin debug menu
    void initTextRendering();
    void renderStatsOverlay();
    void cleanupTextRendering();

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

    // End debug menu, this bit draws the actual
    // video from chiaki-ng
    bool m_initialized = false;
    ChiakiLog* m_log = nullptr;

    int m_frame_width = 0;
    int m_frame_height = 0;
    int m_screen_width = 0;
    int m_screen_height = 0;

    // borealis also uses deko3d, this is the context from
    // borealis
    brls::SwitchVideoContext* m_vctx = nullptr;
    dk::Device m_device;
    dk::Queue m_queue;

    dk::UniqueCmdBuf m_cmdbuf;

    CDescriptorSet<4096U>* m_image_descriptor_set = nullptr;

    CShader m_vertex_shader;
    CShader m_fragment_shader;

    CMemPool::Handle m_vertex_buffer;

    // map to NV12 layouts.
    dk::ImageLayout m_luma_layout;
    dk::ImageLayout m_chroma_layout;
    dk::MemBlock m_mapping_memblock;

    dk::Image m_luma;
    dk::Image m_chroma;

    dk::ImageDescriptor m_luma_desc;
    dk::ImageDescriptor m_chroma_desc;

    int m_luma_texture_id = 0;
    int m_chroma_texture_id = 0;

    bool m_textures_initialized = false;

    bool setupTextures(AVFrame* frame);

    void updateTextureBindings(AVFrame* frame, AVNVTegraMap* map);

    static constexpr int GPU_BUFFER_COUNT = 3;
    dk::MemBlock m_frame_buffers[GPU_BUFFER_COUNT];
    dk::Image m_luma_buffers[GPU_BUFFER_COUNT];
    dk::Image m_chroma_buffers[GPU_BUFFER_COUNT];
    dk::ImageDescriptor m_luma_descs[GPU_BUFFER_COUNT];
    dk::ImageDescriptor m_chroma_descs[GPU_BUFFER_COUNT];
    int m_current_buffer = 0;
    int m_next_buffer = 0;
    bool m_buffers_initialized = false;

    bool initPersistentBuffers(AVFrame* frame);
    void copyFrameToBuffer();

    void* m_current_map_addr = nullptr;

    DkFence m_copy_fences[GPU_BUFFER_COUNT] = {};
    AVFrame* m_pending_nvtegra_release = nullptr;

    // Fences for GPU synchronization
    // Borealis is ready
    DkFence m_ready_fence = {};  
    // finished rendering
    DkFence m_done_fence = {}; 
    // wait for first frame, after that let gpu handle it,
    // dont wait for idle on every frame
    bool m_first_frame = true;    

    // Dont ask me why but borealis drew a very nice background on top
    // of what we were rending in deko3d
    // so we end up ahving to add a callback to it to actually render the
    // video frame AFTER borealis has drawn whatever it needs to draw
    AVFrame* m_pending_frame = nullptr;

    // Use this in the below function to actually draw
    void renderVideo(AVFrame* frame);

    // Register/unregister the post-render callback with borealis
    void registerCallback();
    void unregisterCallback();
    bool m_callback_registered = false;
};

#endif // BOREALIS_USE_DEKO3D

#endif // AKIRA_DEKO3D_RENDERER_HPP
