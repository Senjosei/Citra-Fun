// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <set>
#include <tuple>
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif
#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include <glad/glad.h>
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/math_util.h"
#include "core/hw/gpu.h"
#include "video_core/regs_framebuffer.h"
#include "video_core/regs_texturing.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

struct CachedSurface;
using Surface = std::shared_ptr<CachedSurface>;
using SurfaceSet = std::set<Surface>;

using SurfaceRegions = boost::icl::interval_set<PAddr>;
using SurfaceMap = boost::icl::interval_map<PAddr, Surface>;
using SurfaceCache = boost::icl::interval_map<PAddr, SurfaceSet>;

using SurfaceInterval = SurfaceCache::interval_type;
static_assert(std::is_same<SurfaceRegions::interval_type, SurfaceCache::interval_type>() &&
                  std::is_same<SurfaceMap::interval_type, SurfaceCache::interval_type>(),
              "incorrect interval types");

using SurfaceRect_Tuple = std::tuple<Surface, MathUtil::Rectangle<u32>>;
using SurfaceSurfaceRect_Tuple = std::tuple<Surface, Surface, MathUtil::Rectangle<u32>>;

using PageMap = boost::icl::interval_map<u32, int>;

enum class ScaleMatch {
    Exact,   // only accept same res scale
    Upscale, // only allow higher scale than params
    Ignore   // accept every scaled res
};

struct SurfaceParams {
    enum class PixelFormat {
        // First 5 formats are shared between textures and color buffers
        RGBA8 = 0,
        RGB8 = 1,
        RGB5A1 = 2,
        RGB565 = 3,
        RGBA4 = 4,

        // Texture-only formats
        IA8 = 5,
        RG8 = 6,
        I8 = 7,
        A8 = 8,
        IA4 = 9,
        I4 = 10,
        A4 = 11,
        ETC1 = 12,
        ETC1A4 = 13,

        // Depth buffer-only formats
        D16 = 14,
        // gap
        D24 = 16,
        D24S8 = 17,

        Invalid = 255,
    };

    enum class SurfaceType {
        Color = 0,
        Texture = 1,
        Depth = 2,
        DepthStencil = 3,
        Fill = 4,
        Invalid = 5
    };

    static constexpr unsigned int GetFormatBpp(PixelFormat format) {
        constexpr std::array<unsigned int, 18> bpp_table = {
            32, // RGBA8
            24, // RGB8
            16, // RGB5A1
            16, // RGB565
            16, // RGBA4
            16, // IA8
            16, // RG8
            8,  // I8
            8,  // A8
            8,  // IA4
            4,  // I4
            4,  // A4
            4,  // ETC1
            8,  // ETC1A4
            16, // D16
            0,
            24, // D24
            32, // D24S8
        };

        ASSERT(static_cast<size_t>(format) < bpp_table.size());
        return bpp_table[static_cast<size_t>(format)];
    }
    unsigned int GetFormatBpp() const {
        return GetFormatBpp(pixel_format);
    }

    static PixelFormat PixelFormatFromTextureFormat(Pica::TexturingRegs::TextureFormat format) {
        return ((unsigned int)format < 14) ? (PixelFormat)format : PixelFormat::Invalid;
    }

    static PixelFormat PixelFormatFromColorFormat(Pica::FramebufferRegs::ColorFormat format) {
        return ((unsigned int)format < 5) ? (PixelFormat)format : PixelFormat::Invalid;
    }

    static PixelFormat PixelFormatFromDepthFormat(Pica::FramebufferRegs::DepthFormat format) {
        return ((unsigned int)format < 4) ? (PixelFormat)((unsigned int)format + 14)
                                          : PixelFormat::Invalid;
    }

    static PixelFormat PixelFormatFromGPUPixelFormat(GPU::Regs::PixelFormat format) {
        switch (format) {
        // RGB565 and RGB5A1 are switched in PixelFormat compared to ColorFormat
        case GPU::Regs::PixelFormat::RGB565:
            return PixelFormat::RGB565;
        case GPU::Regs::PixelFormat::RGB5A1:
            return PixelFormat::RGB5A1;
        default:
            return ((unsigned int)format < 5) ? (PixelFormat)format : PixelFormat::Invalid;
        }
    }

    static bool CheckFormatsBlittable(PixelFormat pixel_format_a, PixelFormat pixel_format_b) {
        SurfaceType a_type = GetFormatType(pixel_format_a);
        SurfaceType b_type = GetFormatType(pixel_format_b);

        if ((a_type == SurfaceType::Color || a_type == SurfaceType::Texture) &&
            (b_type == SurfaceType::Color || b_type == SurfaceType::Texture)) {
            return true;
        }

        if (a_type == SurfaceType::Depth && b_type == SurfaceType::Depth) {
            return true;
        }

        if (a_type == SurfaceType::DepthStencil && b_type == SurfaceType::DepthStencil) {
            return true;
        }

        return false;
    }

    static constexpr SurfaceType GetFormatType(PixelFormat pixel_format) {
        if ((unsigned int)pixel_format < 5) {
            return SurfaceType::Color;
        }

        if ((unsigned int)pixel_format < 14) {
            return SurfaceType::Texture;
        }

        if (pixel_format == PixelFormat::D16 || pixel_format == PixelFormat::D24) {
            return SurfaceType::Depth;
        }

        if (pixel_format == PixelFormat::D24S8) {
            return SurfaceType::DepthStencil;
        }

        return SurfaceType::Invalid;
    }

    /// Update the params "size", "end" and "type" from the already set "addr", "width", "height"
    /// and "pixel_format"
    void UpdateParams() {
        if (stride == 0) {
            stride = width;
        }
        type = GetFormatType(pixel_format);
        size = !is_tiled ? BytesInPixels(stride * (height - 1) + width)
                         : BytesInPixels(stride * 8 * (height / 8 - 1) + width * 8);
        end = addr + size;
    }

    SurfaceInterval GetInterval() const {
        return SurfaceInterval::right_open(addr, end);
    }

    // Returns the outer rectangle containing "interval"
    SurfaceParams FromInterval(SurfaceInterval interval) const;

    SurfaceInterval GetSubRectInterval(MathUtil::Rectangle<u32> unscaled_rect) const;

    // Returns the region of the biggest valid rectange within interval
    SurfaceInterval GetCopyableInterval(const Surface& src_surface) const;

    u32 GetScaledWidth() const {
        return width * res_scale;
    }

    u32 GetScaledHeight() const {
        return height * res_scale;
    }

    MathUtil::Rectangle<u32> GetRect() const {
        return {0, height, width, 0};
    }

    MathUtil::Rectangle<u32> GetScaledRect() const {
        return {0, GetScaledHeight(), GetScaledWidth(), 0};
    }

    u32 PixelsInBytes(u32 size) const {
        return size * 8 / GetFormatBpp(pixel_format);
    }

    u32 BytesInPixels(u32 pixels) const {
        return pixels * GetFormatBpp(pixel_format) / 8;
    }

    bool ExactMatch(const SurfaceParams& other_surface) const;
    bool CanSubRect(const SurfaceParams& sub_surface) const;
    bool CanExpand(const SurfaceParams& expanded_surface) const;
    bool CanTexCopy(const SurfaceParams& texcopy_params) const;

    MathUtil::Rectangle<u32> GetSubRect(const SurfaceParams& sub_surface) const;
    MathUtil::Rectangle<u32> GetScaledSubRect(const SurfaceParams& sub_surface) const;

    PAddr addr = 0;
    PAddr end = 0;
    u32 size = 0;

    u32 width = 0;
    u32 height = 0;
    u32 stride = 0;
    u16 res_scale = 1;

    bool is_tiled = false;
    PixelFormat pixel_format = PixelFormat::Invalid;
    SurfaceType type = SurfaceType::Invalid;
};

struct CachedSurface : SurfaceParams {
    bool CanFill(const SurfaceParams& dest_surface, SurfaceInterval fill_interval) const;
    bool CanCopy(const SurfaceParams& dest_surface, SurfaceInterval copy_interval) const;

    bool IsRegionValid(SurfaceInterval interval) const {
        return (invalid_regions.find(interval) == invalid_regions.end());
    }

    bool IsSurfaceFullyInvalid() const {
        return (invalid_regions & GetInterval()) == SurfaceRegions(GetInterval());
    }

    SurfaceRegions invalid_regions;

    u32 fill_size = 0; /// Number of bytes to read from fill_data
    std::array<u8, 4> fill_data;

    OGLTexture texture;

    static constexpr unsigned int GetGLBytesPerPixel(PixelFormat format) {
        // OpenGL needs 4 bpp alignment for D24 since using GL_UNSIGNED_INT as type
        return format == PixelFormat::Invalid
                   ? 0
                   : (format == PixelFormat::D24 || GetFormatType(format) == SurfaceType::Texture)
                         ? 4
                         : SurfaceParams::GetFormatBpp(format) / 8;
    }

    std::unique_ptr<u8[]> gl_buffer;
    size_t gl_buffer_size = 0;

    // Read/Write data in 3DS memory to/from gl_buffer
    void LoadGLBuffer(PAddr load_start, PAddr load_end);
    void FlushGLBuffer(PAddr flush_start, PAddr flush_end);

    // Upload/Download data in gl_buffer in/to this surface's texture
    void UploadGLTexture(const MathUtil::Rectangle<u32>& rect);
    void DownloadGLTexture(const MathUtil::Rectangle<u32>& rect);
};

class RasterizerCacheOpenGL : NonCopyable {
public:
    RasterizerCacheOpenGL();
    ~RasterizerCacheOpenGL();

    /// Blits one texture to another
    void BlitTextures(GLuint src_tex, GLuint dst_tex, CachedSurface::SurfaceType type,
                      const MathUtil::Rectangle<int>& src_rect,
                      const MathUtil::Rectangle<int>& dst_rect);

    /// Attempt to blit one surface's texture to another
    bool TryBlitSurfaces(CachedSurface* src_surface, const MathUtil::Rectangle<int>& src_rect,
                         CachedSurface* dst_surface, const MathUtil::Rectangle<int>& dst_rect);

    /// Loads a texture from 3DS memory to OpenGL and caches it (if not already cached)
    CachedSurface* GetSurface(const CachedSurface& params, bool match_res_scale,
                              bool load_if_create);

    /// Attempt to find a subrect (resolution scaled) of a surface, otherwise loads a texture from
    /// 3DS memory to OpenGL and caches it (if not already cached)
    CachedSurface* GetSurfaceRect(const CachedSurface& params, bool match_res_scale,
                                  bool load_if_create, MathUtil::Rectangle<int>& out_rect);

    /// Gets a surface based on the texture configuration
    CachedSurface* GetTextureSurface(const Pica::TexturingRegs::FullTextureConfig& config);

    /// Gets the color and depth surfaces and rect (resolution scaled) based on the framebuffer
    /// configuration
    std::tuple<CachedSurface*, CachedSurface*, MathUtil::Rectangle<int>> GetFramebufferSurfaces(
        const Pica::FramebufferRegs::FramebufferConfig& config);

    /// Attempt to get a surface that exactly matches the fill region and format
    CachedSurface* TryGetFillSurface(const GPU::Regs::MemoryFillConfig& config);

    /// Write the surface back to memory
    void FlushSurface(CachedSurface* surface);

    /// Write any cached resources overlapping the region back to memory (if dirty) and optionally
    /// invalidate them in the cache
    void FlushRegion(PAddr addr, u32 size, const CachedSurface* skip_surface, bool invalidate);

    /// Flush all cached resources tracked by this cache manager
    void FlushAll();

private:
    SurfaceCache surface_cache;
    OGLFramebuffer transfer_framebuffers[2];
};
