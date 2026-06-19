// Demo: build a few complex scalar fields, ray-march them with iv::vk::Renderer,
// and save the frames as PNG. Usage: iv_render_demo [output_dir]   (default ".").
//
// This is example code exercising the public M2–M4 API end to end; it is not part
// of libiv and not on the test gate.

#include "iv/error.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"

#include "png.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using iv::GridDims;
using iv::vk::Context;
using iv::vk::ImageReadback;
using iv::vk::Renderer;
using iv::vk::RenderParams;
using iv::vk::Volume;

constexpr float kPi = 3.14159265358979323846f;

// Sample a complex field f(x,y,z) over a grid; coords are voxel centers in [0,1].
std::vector<std::complex<float>> buildField(
    GridDims d, const std::function<std::complex<float>(float, float, float)>& f) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(d.nx);
                const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(d.ny);
                const float fz = (static_cast<float>(z) + 0.5f) / static_cast<float>(d.nz);
                v[d.index(x, y, z)] = f(fx, fy, fz);
            }
        }
    }
    return v;
}

// Gaussian envelope along z so the fields are finite blobs, not infinite prisms.
float zEnvelope(float z) {
    const float t = (z - 0.5f) / 0.2f;
    return std::exp(-0.5f * t * t);
}

bool savePng(const std::string& dir, const std::string& name, const ImageReadback& img) {
    const std::string path = dir + "/" + name;
    if (!iv_demo::writePng(path, img.width(), img.height(), img.bytes())) {
        std::fprintf(stderr, "  failed to write %s\n", path.c_str());
        return false;
    }
    std::printf("  wrote %s (%ux%u)\n", path.c_str(), img.width(), img.height());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::string outDir = (argc > 1) ? argv[1] : ".";

    auto ctx = Context::create();
    if (!ctx) {
        std::fprintf(stderr, "Context::create failed: %s\n", iv::format(ctx.error()).c_str());
        return 1;
    }
    auto renderer = Renderer::create(*ctx);
    if (!renderer) {
        std::fprintf(stderr, "Renderer::create failed: %s\n",
                     iv::format(renderer.error()).c_str());
        return 1;
    }
    std::printf("Renderer ready (volume linear filtering: %s)\n",
                renderer->volumeLinearFilter() ? "yes" : "no");

    const GridDims dims{128, 128, 128};
    const std::uint32_t W = 512;
    const std::uint32_t H = 512;

    // f = w  (w = (x-0.5) + i(y-0.5)): phase = azimuth, magnitude = radius; a
    // colored vortex column faded along z.
    const auto vortex = buildField(dims, [](float x, float y, float z) {
        return std::complex<float>(x - 0.5f, y - 0.5f) * zEnvelope(z);
    });
    // f = w^3: phase wraps three times around the axis.
    const auto monomial = buildField(dims, [](float x, float y, float z) {
        const std::complex<float> w(x - 0.5f, y - 0.5f);
        return w * w * w * zEnvelope(z);
    });
    // A spherical shell (3D) colored by azimuthal phase.
    const auto shell = buildField(dims, [](float x, float y, float z) {
        const float dx = x - 0.5f;
        const float dy = y - 0.5f;
        const float dz = z - 0.5f;
        const float r = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float s = (r - 0.30f) / 0.04f;
        const float mag = std::exp(-0.5f * s * s);
        return std::polar(mag, std::atan2(dy, dx));
    });
    // Two poles: huge magnitude near each — log opacity reveals the falloff.
    const auto poles = buildField(dims, [](float x, float y, float z) {
        const std::complex<float> w(x - 0.5f, y - 0.5f);
        const std::complex<float> a(-0.18f, 0.0f);
        const std::complex<float> b(0.18f, 0.0f);
        return (1.0f / (w - a) + 1.0f / (w - b)) * zEnvelope(z);
    });

    struct Demo {
        const char* name;
        const std::vector<std::complex<float>>* field;
        RenderParams params;
    };

    RenderParams base;
    base.background = {0.05f, 0.05f, 0.07f, 1.0f};

    auto withColormap = [&](std::uint32_t mode, std::uint32_t opacity, float density) {
        RenderParams p = base;
        p.colormapMode = mode;
        p.opacityMode = opacity;
        p.densityScale = density;
        return p;
    };

    RenderParams faceon = base;
    faceon.eye = {0.5f, 0.5f, 2.2f};
    faceon.target = {0.5f, 0.5f, 0.5f};
    faceon.up = {0.0f, 1.0f, 0.0f};
    faceon.colormapMode = 1; // HSV
    faceon.densityScale = 5.0f;

    const std::vector<Demo> demos{
        {"demo_phasewheel_hsv.png", &vortex, faceon}, // face-on phase wheel (HSV)
        {"demo_vortex_hsv.png", &vortex, withColormap(1, 0, 2.5f)},      // HSV, linear
        {"demo_vortex_twilight.png", &vortex, withColormap(0, 0, 2.5f)}, // twilight, linear
        {"demo_shell_twilight.png", &shell, withColormap(0, 0, 3.0f)},   // 3D shell, linear
        {"demo_poles_log_twilight.png", &poles, withColormap(0, 1, 1.0f)}, // poles, log
    };

    bool ok = true;
    for (const auto& demo : demos) {
        auto vol = Volume::create(*ctx, *demo.field, dims);
        if (!vol) {
            std::fprintf(stderr, "Volume::create failed for %s: %s\n", demo.name,
                         iv::format(vol.error()).c_str());
            ok = false;
            continue;
        }
        const auto mr = vol->magnitudeRange();
        std::printf("%s: magnitude range [minPositive=%g, max=%g]\n", demo.name,
                    static_cast<double>(mr.minPositive), static_cast<double>(mr.max));
        auto img = renderer->render(*vol, W, H, demo.params);
        if (!img) {
            std::fprintf(stderr, "render failed for %s: %s\n", demo.name,
                         iv::format(img.error()).c_str());
            ok = false;
            continue;
        }
        ok = savePng(outDir, demo.name, *img) && ok;
    }

    return ok ? 0 : 1;
}
