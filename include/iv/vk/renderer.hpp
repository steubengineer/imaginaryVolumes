#ifndef IV_VK_RENDERER_HPP
#define IV_VK_RENDERER_HPP

// Offscreen direct-volume ray-marcher (M4): a compute pipeline (ADR-0011) that
// ray-marches an iv::vk::Volume (ADR-0012 camera/compositing) with an
// abs→opacity transfer function (ADR-0013) and an arg→color cyclic colormap
// (ADR-0014), writing an R8G8B8A8_UNORM image read back with the ADR-0006 layout.
// Move-only; borrows the Context (which must outlive it). Not thread-safe
// (ADR-0007): a Debug thread-affinity check guards render().

#include "iv/error.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/offscreen.hpp" // ImageReadback (ADR-0006)
#include "iv/vk/unique.hpp"
#include "iv/vk/volume.hpp"
#include "iv/vk/vulkan.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace iv::vk {

// Per-render parameters (ADR-0012/0013/0014). The magnitude range is taken from
// the Volume (`volume.magnitudeRange()`), so it is not duplicated here.
struct RenderParams {
    // Camera (ADR-0012): right-handed, +Y up; the volume is the unit cube [0,1]^3.
    std::array<float, 3> eye{2.0f, 1.6f, 2.4f};
    std::array<float, 3> target{0.5f, 0.5f, 0.5f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    float vfovRadians{0.6f};
    // Compositing (ADR-0012).
    std::uint32_t stepCount{256};
    float alphaTermination{0.995f};
    std::array<float, 4> background{0.0f, 0.0f, 0.0f, 1.0f};
    // Opacity transfer function (ADR-0013): 0 = linear, 1 = logarithmic.
    std::uint32_t opacityMode{0};
    float densityScale{1.0f};
    // Log-mode decade window (ADR-0027): when > 0, the log opacity ramp spans the top
    // `logDecades` decades below the max magnitude (`max·10^-logDecades` → transparent,
    // `max` → opaque). 0 (default) = the full ADR-0013 [minPositive, max] range. No
    // effect in linear mode.
    float logDecades{0.0f};
    // Phase colormap (ADR-0014): 0 = perceptually-uniform LUT, 1 = HSV hue wheel.
    std::uint32_t colormapMode{0};
};

// A 2D/3D overlay drawn over the volume render (ADR-0021): colored line-list and
// triangle-list geometry, alpha-blended. Positions are transformed by `transform`
// in the vertex shader: leave it identity for screen-space (clip-space) geometry,
// or set the camera view-projection (ADR-0012) for world-space geometry (the M7
// bounding box / axes). Glyph quads (ADR-0023) are emitted as triangles.
struct OverlayVertex {
    std::array<float, 3> pos;   // clip-space (identity transform) or world-space
    std::array<float, 4> color; // RGBA, straight (non-premultiplied) alpha
};

// One vertex of a Slug glyph quad (ADR-0023). Positions are pre-projected to
// clip space (NDC) on the CPU; `texcoord` is the em-space (font-unit) sample
// coordinate; `glyphLoc` is the glyph's texel offset into the atlas (see
// iv::text). Six vertices (two triangles) per glyph. Produced by the text layer
// (iv::text::appendText); the renderer treats it as opaque geometry + an int atlas
// and never links HarfBuzz.
struct GlyphVertex {
    std::array<float, 2> pos;      // clip-space (NDC) quad corner
    std::array<float, 2> texcoord; // em-space sample coordinate (font units)
    std::uint32_t glyphLoc;        // atlas texel offset for this glyph
    std::array<float, 4> color;    // RGBA, straight (non-premultiplied) alpha
};

struct Overlay {
    std::vector<OverlayVertex> lines;     // line list (consecutive vertex pairs)
    std::vector<OverlayVertex> triangles; // triangle list (consecutive vertex triples)
    // Screen-space line/triangle lists drawn with the IDENTITY transform, independent of
    // `transform` below (which carries the world view-projection for the 3D box/axes). For
    // 2D HUD geometry that stays fixed on screen while lines/triangles track the camera — the
    // legend swatch / border / ticks (ADR-0028). Vertices are Vulkan clip space (y-down: y=-1
    // top, +1 bottom), like all overlay geometry after `transform` and like the baked glyphs.
    std::vector<OverlayVertex> screenLines;
    std::vector<OverlayVertex> screenTriangles;
    // Glyph quads + their Slug atlas (ADR-0023). `glyphs` is a triangle list (6
    // verts/glyph); `glyphAtlas` is the packed RGBA16I texel stream uploaded as an
    // R16G16B16A16_SINT uniform texel buffer that the Slug fragment shader samples.
    // (Currently drawn on the headless render() path; the present path is M7.)
    std::vector<GlyphVertex> glyphs;
    std::vector<std::int16_t> glyphAtlas;
    // Column-major 4x4 (GLSL convention); identity => positions are clip-space.
    // Applies to lines/triangles only; screenLines/screenTriangles use the identity and
    // glyph positions are already clip-space.
    std::array<float, 16> transform{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    [[nodiscard]] bool empty() const noexcept {
        return lines.empty() && triangles.empty() && screenLines.empty() &&
               screenTriangles.empty() && glyphs.empty();
    }
};

class Renderer {
public:
    // Build the compute pipeline, samplers, and the colormap LUT (ADR-0011/0014).
    [[nodiscard]] static Result<Renderer> create(const Context& ctx);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;
    ~Renderer() = default;

    // Render `volume` to a width x height image and read it back (ADR-0006 layout:
    // top-left origin, row-major, pixel (x,y) at byte (y*w+x)*4, R,G,B,A). If
    // `overlay` is non-null and non-empty, it is composited over the volume in a
    // graphics pass before readback (ADR-0021).
    [[nodiscard]] Result<ImageReadback> render(const Volume& volume, std::uint32_t width,
                                               std::uint32_t height, const RenderParams& params,
                                               const Overlay* overlay = nullptr);

    // Present path (ADR-0017): record a render of `volume` with `params` into the
    // caller-provided command buffer, dispatching into an internal storage image
    // (sized to `dstExtent`, lazily (re)created) and blitting it into `dstImage`.
    // No host readback. `dstImage` must have eTransferDst usage; its prior contents
    // are discarded and it is left in eTransferDstOptimal (the caller transitions
    // it to ePresentSrcKHR). The command buffer must be in the recording state.
    [[nodiscard]] Status recordFrame(::vk::CommandBuffer cmd, const Volume& volume,
                                     const RenderParams& params, ::vk::Image dstImage,
                                     ::vk::Extent2D dstExtent, const Overlay* overlay = nullptr);

    // True if the volume sampler uses linear filtering (false => nearest fallback
    // when R32G32_SFLOAT lacks linear-filter support; ADR-0009 note).
    [[nodiscard]] bool volumeLinearFilter() const noexcept { return volumeLinearFilter_; }

private:
    Renderer() = default;
    void checkAffinity() const noexcept;

    // Write the 4 compute descriptors (volume sampler, storage image, UBO, LUT).
    void writeComputeDescriptors(::vk::DescriptorSet set, ::vk::ImageView volumeView,
                                 ::vk::ImageView storageView, ::vk::Buffer ubo);
    // Lazily (re)create the present-path storage image / UBO / descriptor set when
    // the extent or volume changes (ADR-0017).
    [[nodiscard]] Status ensureFrameResources(const Volume& volume, ::vk::Extent2D extent);

    // Transient per-render Slug glyph resources (ADR-0023): the glyph vertex
    // buffer, the atlas uniform texel buffer + its R16G16B16A16_SINT view, and a
    // descriptor set bound to the view. Built by buildGlyphResources(); must live
    // until the render's GPU work completes.
    struct GlyphResources {
        Unique<::vk::Buffer> vbuf;
        Unique<::vk::DeviceMemory> vmem;
        Unique<::vk::Buffer> atlasBuf;
        Unique<::vk::DeviceMemory> atlasMem;
        Unique<::vk::BufferView> atlasView;
        Unique<::vk::DescriptorPool> pool;
        ::vk::DescriptorSet set{};
        std::uint32_t vertexCount{0};
    };
    // Build the glyph resources for one render from `glyphs` + the packed `atlas`.
    // Returns a bundle with vertexCount == 0 when `glyphs` is empty.
    [[nodiscard]] Result<GlyphResources> buildGlyphResources(
        const std::vector<GlyphVertex>& glyphs, std::span<const std::int16_t> atlas);

    // Record the overlay graphics pass (ADR-0021/0028): begin the render pass on
    // `framebuffer`; draw the world-space `lineVertexCount` lines then
    // `triangleVertexCount` triangles under `transform`; then the screen-space
    // `screenLineVertexCount` lines + `screenTriangleVertexCount` triangles under the
    // identity transform (ADR-0028 legend); then — if `glyphVertexCount > 0` — the Slug
    // glyph quads (ADR-0023) from `glyphVbuf` with the atlas `glyphSet`, end. The vertex
    // buffer packs the four lists in that order (lines, triangles, screenLines,
    // screenTriangles). The caller transitions the target to eColorAttachmentOptimal and
    // uploads the vertices beforehand. Passing handles (not a GlyphResources) lets the
    // headless and present paths share this.
    void drawOverlay(::vk::CommandBuffer cmd, ::vk::Framebuffer framebuffer, ::vk::Extent2D extent,
                     ::vk::Buffer vbuf, std::uint32_t lineVertexCount,
                     std::uint32_t triangleVertexCount, std::uint32_t screenLineVertexCount,
                     std::uint32_t screenTriangleVertexCount,
                     const std::array<float, 16>& transform, ::vk::Buffer glyphVbuf,
                     ::vk::DescriptorSet glyphSet, std::uint32_t glyphVertexCount);

    // Present-path glyph resources (ADR-0025): grow the persistent glyph vertex and
    // atlas (uniform texel) buffers on demand and re-upload `glyphs`/`atlas`,
    // (re)creating the atlas view + descriptor only on growth. Sets
    // frameGlyph* below; safe to overwrite (one frame in flight, ADR-0017). No-op
    // (vertex count 0) for an empty glyph list.
    [[nodiscard]] Status ensureFrameGlyphResources(const std::vector<GlyphVertex>& glyphs,
                                                   std::span<const std::int16_t> atlas);

    // Borrowed from the Context, which must outlive this Renderer.
    ::vk::Device device_{};
    ::vk::PhysicalDevice physicalDevice_{};
    ::vk::Queue queue_{};
    ::vk::CommandPool commandPool_{};
    std::thread::id ownerThread_{};
    bool volumeLinearFilter_{true};

    // Owned; destruction is reverse-declaration order (LUT view/image before its
    // memory; pipeline before its layout).
    Unique<::vk::DescriptorSetLayout> setLayout_;
    Unique<::vk::PipelineLayout> pipelineLayout_;
    Unique<::vk::Pipeline> pipeline_;
    Unique<::vk::DescriptorPool> descriptorPool_;
    Unique<::vk::Sampler> volumeSampler_;
    Unique<::vk::Sampler> colormapSampler_;
    Unique<::vk::DeviceMemory> lutMemory_;
    Unique<::vk::Image> lutImage_;
    Unique<::vk::ImageView> lutView_;

    // Overlay graphics pipeline (ADR-0021), created once. The line and triangle
    // pipelines share the layout (a single mat4 push constant) and the render pass.
    // Declared before the frame framebuffer so the render pass outlives it.
    Unique<::vk::RenderPass> renderPass_;
    Unique<::vk::PipelineLayout> overlayPipelineLayout_;
    Unique<::vk::Pipeline> overlayLinePipeline_;
    Unique<::vk::Pipeline> overlayTrianglePipeline_;

    // Slug glyph pipeline (ADR-0023): shares renderPass_; its own descriptor set
    // layout (set 0, binding 0 = the atlas uniform texel buffer) + pipeline layout.
    // Created once. Declared after the layout it depends on (destroyed first).
    Unique<::vk::DescriptorSetLayout> glyphSetLayout_;
    Unique<::vk::PipelineLayout> glyphPipelineLayout_;
    Unique<::vk::Pipeline> glyphPipeline_;

    // Present-path (recordFrame) resources, lazily (re)created on extent/volume
    // change. Declared after the pipeline objects; the view/image precede their
    // backing memory only within each (image, memory) pair via reset order below —
    // so memory_ members are declared before their image/view here.
    ::vk::Extent2D frameExtent_{0, 0};
    ::vk::ImageView frameVolumeView_{};
    void* frameUboMapped_{nullptr};
    Unique<::vk::DeviceMemory> frameImageMem_;
    Unique<::vk::Image> frameImage_;
    Unique<::vk::ImageView> frameView_;
    Unique<::vk::DeviceMemory> frameUboMem_;
    Unique<::vk::Buffer> frameUbo_;
    Unique<::vk::DescriptorPool> frameDescriptorPool_;
    ::vk::DescriptorSet frameSet_{};
    // Overlay framebuffer for the present path, (re)created with frameView_/extent.
    // Declared last so it is destroyed before frameView_ (which it references).
    Unique<::vk::Framebuffer> frameFramebuffer_;
    // Persistent overlay vertex buffer for the present path: grown on demand and
    // persistently mapped. One frame is in flight (the viewer waits the fence before
    // re-recording), so overwriting it each frame is safe.
    Unique<::vk::DeviceMemory> frameOverlayMem_;
    Unique<::vk::Buffer> frameOverlayBuf_;
    void* frameOverlayMapped_{nullptr};
    ::vk::DeviceSize frameOverlayCapacity_{0};

    // Present-path Slug glyph resources (ADR-0025): persistent, grown on demand. The
    // atlas buffer/view + descriptor are recreated only when the atlas outgrows
    // capacity; the vertex and atlas data are re-uploaded each frame (cheap for
    // labels). Declared so each view is destroyed before its backing buffer/memory.
    Unique<::vk::DeviceMemory> frameGlyphVbufMem_;
    Unique<::vk::Buffer> frameGlyphVbuf_;
    void* frameGlyphVbufMapped_{nullptr};
    ::vk::DeviceSize frameGlyphVbufCapacity_{0};
    Unique<::vk::DeviceMemory> frameGlyphAtlasMem_;
    Unique<::vk::Buffer> frameGlyphAtlasBuf_;
    void* frameGlyphAtlasMapped_{nullptr};
    ::vk::DeviceSize frameGlyphAtlasCapacity_{0};
    Unique<::vk::BufferView> frameGlyphAtlasView_;
    Unique<::vk::DescriptorPool> frameGlyphPool_;
    ::vk::DescriptorSet frameGlyphSet_{};
    std::uint32_t frameGlyphVertexCount_{0};
};

} // namespace iv::vk

#endif // IV_VK_RENDERER_HPP
