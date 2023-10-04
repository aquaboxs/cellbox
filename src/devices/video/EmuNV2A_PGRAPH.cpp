// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  This file is heavily based on code from XQEMU
// *  https://github.com/xqemu/xqemu/blob/master/hw/xbox/nv2a/nv2a_pgraph.c
// *  Copyright (c) 2012 espes
// *  Copyright (c) 2015 Jannik Vogel
// *  Copyright (c) 2018 Matt Borgerson
// *
// *  Contributions for Cxbx-Reloaded
// *  Copyright (c) 2017-2018 Luke Usher <luke.usher@outlook.com>
// *  Copyright (c) 2018 Patrick van Logchem <pvanlogchem@gmail.com>
// *
// *  All rights reserved
// *
// ******************************************************************

#include "core\hle\D3D8\XbD3D8Types.h" // For X_D3DFORMAT
#include "core\hle\D3D8\XbVertexShader.h"
#include "core\hle\D3D8\Direct3D9\HleInNv2a.h"
#include "core\hle\D3D8\Direct3D9\RenderStates.h"
#include "core\hle\D3D8\Direct3D9\TextureStates.h"

// FIXME
#define qemu_mutex_lock_iothread()
#define qemu_mutex_unlock_iothread()

// Xbox uses 4 KiB pages
#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_SIZE (1 << TARGET_PAGE_BITS)
#define TARGET_PAGE_MASK ~(TARGET_PAGE_SIZE - 1)
#define TARGET_PAGE_ALIGN(addr) (((addr) + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK)
typedef struct RAMHTEntry {
	uint32_t handle;
	xbox::addr_xt instance;
	enum FIFOEngine engine;
	unsigned int channel_id : 5;
	bool valid;
} RAMHTEntry;
static const GLenum pgraph_texture_min_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR_MIPMAP_LINEAR,
    GL_LINEAR, /* TODO: Convolution filter... */
};

static const GLenum pgraph_texture_mag_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    0,
    GL_LINEAR /* TODO: Convolution filter... */
};

static const GLenum pgraph_texture_addr_map[] = {
    0,
    GL_REPEAT,
    GL_MIRRORED_REPEAT,
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_BORDER,
    // GL_CLAMP
};

static const GLenum pgraph_blend_factor_map[] = {
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA_SATURATE,
    0,
    GL_CONSTANT_COLOR,
    GL_ONE_MINUS_CONSTANT_COLOR,
    GL_CONSTANT_ALPHA,
    GL_ONE_MINUS_CONSTANT_ALPHA,
};

static const GLenum pgraph_blend_equation_map[] = {
    GL_FUNC_SUBTRACT,
    GL_FUNC_REVERSE_SUBTRACT,
    GL_FUNC_ADD,
    GL_MIN,
    GL_MAX,
    GL_FUNC_REVERSE_SUBTRACT,
    GL_FUNC_ADD,
};

static const GLenum pgraph_blend_logicop_map[] = {
    GL_CLEAR,
    GL_AND,
    GL_AND_REVERSE,
    GL_COPY,
    GL_AND_INVERTED,
    GL_NOOP,
    GL_XOR,
    GL_OR,
    GL_NOR,
    GL_EQUIV,
    GL_INVERT,
    GL_OR_REVERSE,
    GL_COPY_INVERTED,
    GL_OR_INVERTED,
    GL_NAND,
    GL_SET,
};

static const GLenum pgraph_cull_face_map[] = {
    0,
    GL_FRONT,
    GL_BACK,
    GL_FRONT_AND_BACK
};

static const GLenum pgraph_depth_func_map[] = {
    GL_NEVER,
    GL_LESS,
    GL_EQUAL,
    GL_LEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_GEQUAL,
    GL_ALWAYS,
};

static const GLenum pgraph_stencil_func_map[] = {
    GL_NEVER,
    GL_LESS,
    GL_EQUAL,
    GL_LEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_GEQUAL,
    GL_ALWAYS,
};

static const GLenum pgraph_stencil_op_map[] = {
    0,
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_DECR,
    GL_INVERT,
    GL_INCR_WRAP,
    GL_DECR_WRAP,
};

enum FormatEncoding {
    linear = 0,
    swizzled, // for all NV097_SET_TEXTURE_FORMAT_*_SZ_*
    compressed // for all NV097_SET_TEXTURE_FORMAT_*_DXT*
};

typedef struct ColorFormatInfo {
    unsigned int bytes_per_pixel; // Derived from the total number of channel bits
    FormatEncoding encoding;
    GLint gl_internal_format;
    GLenum gl_format; // == 0 for compressed formats
    GLenum gl_type;
    GLint *gl_swizzle_mask; // == nullptr when gl_internal_format, gl_format and gl_type are sufficient
} ColorFormatInfo;

// Resulting gl_internal_format, gl_format and gl_type values, for formats handled by convert_texture_data()
#define GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV

static GLint gl_swizzle_mask_0RG1[4] = { GL_ZERO, GL_RED, GL_GREEN, GL_ONE };
static GLint gl_swizzle_mask_111R[4] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };
static GLint gl_swizzle_mask_ARGB[4] = { GL_ALPHA, GL_RED, GL_GREEN, GL_BLUE };
static GLint gl_swizzle_mask_BGRA[4] = { GL_BLUE, GL_GREEN, GL_RED, GL_ALPHA };
static GLint gl_swizzle_mask_GGGR[4] = { GL_GREEN, GL_GREEN, GL_GREEN, GL_RED };
static GLint gl_swizzle_mask_R0G1[4] = { GL_RED, GL_ZERO, GL_GREEN, GL_ONE };
static GLint gl_swizzle_mask_RRR1[4] = { GL_RED, GL_RED, GL_RED, GL_ONE };
static GLint gl_swizzle_mask_RRRG[4] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
static GLint gl_swizzle_mask_RRRR[4] = { GL_RED, GL_RED, GL_RED, GL_RED };

// Note : Avoid designated initializers to facilitate C++ builds
static const ColorFormatInfo kelvin_color_format_map[256] = {
    //0x00 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8] =
        {1, swizzled, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_RRR1},
    //0x01 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8] =
        {1, swizzled, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_RRRR},
    //0x02 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5] =
        {2, swizzled, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    //0x03 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5] =
        {2, swizzled, GL_RGB5, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    //0x04 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4] =
        {2, swizzled, GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV},
    //0x05 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5] =
        {2, swizzled, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    //0x06 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] =
        {4, swizzled, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    //0x07 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8] =
        {4, swizzled, GL_RGB8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    //0x08 [?] =
        {},
    //0x09 [?] =
        {},
    //0x0A [?] =
        {},

    /* paletted texture */
    //0x0B [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8] = // See convert_texture_data
        {1, swizzled, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT},

    //0x0C [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] =
        {4, compressed, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, GL_RGBA},
    //0x0D [?] =
        {},
    //0x0E [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] =
        {4, compressed, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, GL_RGBA},
    //0x0F [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8] =
        {4, compressed, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, GL_RGBA},
    //0x10 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5] =
        {2, linear, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    //0x11 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5] =
        {2, linear, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    //0x12 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] =
        {4, linear, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    //0x13 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8] =
        {1, linear, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_RRR1},
    //0x14 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_SY8] =
        {1, linear, GL_R8_SNORM, GL_RED, GL_BYTE,
         gl_swizzle_mask_RRR1}, // TODO : Verify
    //0x15 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X7SY9] = // See convert_texture_data FIXME
        {2, linear, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify
    //0x16 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8B8] =
        {2, linear, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_R0G1}, // TODO : Verify
    //0x17 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8] =
        {2, linear, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_0RG1}, // TODO : Verify
    //0x18 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_SG8SB8] =
        {2, linear, GL_RG8_SNORM, GL_RG, GL_BYTE,
         gl_swizzle_mask_0RG1}, // TODO : Verify

    //0x19 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8] =
        {1, swizzled, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_111R},
    //0x1A [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8] =
        {2, swizzled, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_GGGR},
    //0x1B [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8] =
        {1, linear, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_RRRR},
    //0x1C [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5] =
        {2, linear, GL_RGB5, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    //0x1D [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4] =
        {2, linear, GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV}, // TODO : Verify this is truely linear
    //0x1E [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8] =
        {4, linear, GL_RGB8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    //0x1F [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8] =
        {1, linear, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_111R},
    //0x20 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8] =
        {2, linear, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_GGGR},
    //0x21 [?] =
        {},
    //0x22 [?] =
        {},
    //0x23 [?] =
        {},
    //0x24 [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8] = // See convert_texture_data calling ____UYVYToARGBRow_C
        {2, linear, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify
    //0x25 [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8] = // See convert_texture_data calling ____YUY2ToARGBRow_C
        {2, linear, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify
    //0x26 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8CR8CB8Y8] = // See convert_texture_data FIXME
        {2, linear, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify

    //0x27 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5] = // See convert_texture_data calling __R6G5B5ToARGBRow_C
        {2, swizzled, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify
    //0x28 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8] =
        {2, swizzled, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_0RG1},
    //0x29 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8] =
        {2, swizzled, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         gl_swizzle_mask_R0G1},
    //0x2A [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_X8_Y24_FIXED] =
        {4, swizzled, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8}, // TODO : Verify
    //0x2B [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_X8_Y24_FLOAT] =
        {4, swizzled, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV}, // TODO : Verify
    //0x2C [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED] =
        {2, swizzled, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT}, // TODO : Verify
    //0x2D [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FLOAT] =
        {2, swizzled, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_FLOAT}, // TODO : Verify


    /* TODO: format conversion */
    //0x2E [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED] =
        {4, linear, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
    //0x2F [?] =
        {},
    //0x30 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED] =
        {2, linear, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT},
    //0x31 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT] =
        {2, linear, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_FLOAT}, // TODO : Verify
    //0x32 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y16] =
        {2, swizzled, GL_R16, GL_RED, GL_UNSIGNED_SHORT, // TODO : Verify
         gl_swizzle_mask_RRR1},
    //0x33 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_YB16YA16] =
        {4, swizzled, GL_RG16, GL_RG, GL_UNSIGNED_SHORT, // TODO : Verify
         gl_swizzle_mask_RRRG},
    //0x34 [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_A4V6YB6A4U6YA6] = // TODO : handle in convert_texture_data
        {2, linear, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify
    //0x35 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y16] =
        {2, linear, GL_R16, GL_RED, GL_UNSIGNED_SHORT,
         gl_swizzle_mask_RRR1},
    //0x36 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_YB16YA16] =
        {4, linear, GL_RG16, GL_RG, GL_UNSIGNED_SHORT, // TODO : Verify
         gl_swizzle_mask_RRRG},
    //0x37 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R6G5B5] = // See convert_texture_data calling __R6G5B5ToARGBRow_C
        {2, linear, GL_CONVERT_TEXTURE_DATA_RESULTING_FORMAT}, // TODO : Verify
    //0x38 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G5B5A1] =
        {2, swizzled, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1}, // TODO : Verify
    //0x39 [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R4G4B4A4] =
        {2, swizzled, GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4}, // TODO : Verify
    //0x3A [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8] =
        {4, swizzled, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV}, // TODO : Verify
    //0x3B [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8] =
        {4, swizzled, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8}, // TODO : Verify

    //0x3C [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8] =
        {4, swizzled, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8},
    //0x3D [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G5B5A1] =
        {2, linear, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1}, // TODO : Verify
    //0x3E [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R4G4B4A4] =
        {2, linear, GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4}, // TODO : Verify

    //0x3F [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8] =
        {4, linear, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV}, // TODO : Verify
    //0x40 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8] =
        {4, linear, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8}, // TODO : Verify
    //0x41 [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8] =
        {4, linear, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8}, // TODO : Verify
};

typedef struct SurfaceColorFormatInfo {
    unsigned int bytes_per_pixel;
    GLint gl_internal_format;
    GLenum gl_format;
    GLenum gl_type;
} SurfaceColorFormatInfo;

// Note : Avoid designated initializers to facilitate C++ builds
static const SurfaceColorFormatInfo kelvin_surface_color_format_map[16] = {
    //0x00 [?] = 
        {},
    //0x01 [NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5] =
        {2, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    //0x02 [NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5] =
        {},
    //0x03 [NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5] =
        {2, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    //0x04 [NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8] =
        {4, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    //0x05 [NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8] =
        {},
    //0x06 [NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8] =
        {},
    //0x07 [NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8] =
        {},
    //0x08 [NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8] =
        {4, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    //0x09 [NV097_SET_SURFACE_FORMAT_COLOR_LE_B8] =
        {}, // TODO : {1, GL_R8, GL_RED, GL_UNSIGNED_BYTE}, // PatrickvL guesstimate
    //0x0A [NV097_SET_SURFACE_FORMAT_COLOR_LE_G8B8] =
        {}, // TODO : {2, GL_RG8, GL_RG, GL_UNSIGNED_BYTE}, // PatrickvL guesstimate
    //0x0B [?] = 
        {},
    //0x0C [?] = 
        {},
    //0x0D [?] = 
        {},
    //0x0E [?] = 
        {},
    //0x0F [?] = 
        {}
};

void (*pgraph_draw_state_update)(NV2AState *d);
void (*pgraph_draw_clear)(NV2AState *d);
void (*pgraph_draw_arrays)(NV2AState *d);
void (*pgraph_draw_inline_buffer)(NV2AState *d);
void (*pgraph_draw_inline_array)(NV2AState *d);
void (*pgraph_draw_inline_elements)(NV2AState *d);

//static void pgraph_set_context_user(NV2AState *d, uint32_t value);
//void pgraph_handle_method(NV2AState *d, unsigned int subchannel, unsigned int method, uint32_t parameter);
static void pgraph_log_method(unsigned int subchannel, unsigned int graphics_class, unsigned int method, uint32_t parameter);
static float *pgraph_allocate_inline_buffer_vertices(PGRAPHState *pg, unsigned int attr);
float *pgraph_get_vertex_attribute_inline_value(PGRAPHState *pg, int attribute_index);
static void pgraph_finish_inline_buffer_vertex(PGRAPHState *pg);
static void pgraph_update_shader_constants(PGRAPHState *pg, ShaderBinding *binding, bool binding_changed, bool vertex_program, bool fixed_function);
static void pgraph_bind_shaders(PGRAPHState *pg);
static bool pgraph_get_framebuffer_dirty(PGRAPHState *pg);
static bool pgraph_get_color_write_enabled(PGRAPHState *pg);
static bool pgraph_get_zeta_write_enabled(PGRAPHState *pg);
static void pgraph_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta);
static void pgraph_update_surface_part(NV2AState *d, bool upload, bool color);
static void pgraph_update_surface(NV2AState *d, bool upload, bool color_write, bool zeta_write);
static void pgraph_bind_textures(NV2AState *d);
static void pgraph_apply_anti_aliasing_factor(PGRAPHState *pg, unsigned int *width, unsigned int *height);
static void pgraph_get_surface_dimensions(PGRAPHState *pg, unsigned int *width, unsigned int *height);
static void pgraph_update_memory_buffer(NV2AState *d, hwaddr addr, hwaddr size, bool f);
static void pgraph_bind_vertex_attributes(NV2AState *d, unsigned int num_elements, bool inline_data, unsigned int inline_stride);
static unsigned int pgraph_bind_inline_array(NV2AState *d);

static float convert_f16_to_float(uint16_t f16);
static float convert_f24_to_float(uint32_t f24);
static uint8_t* convert_texture_data(const unsigned int color_format, const uint8_t *data, const uint8_t *palette_data, const unsigned int width, const unsigned int height, const unsigned int depth, const unsigned int row_pitch, const unsigned int slice_pitch);
static int upload_gl_texture(GLenum gl_target, const TextureShape s, const uint8_t *texture_data, const uint8_t *palette_data);
static TextureBinding* generate_texture(const TextureShape s, const uint8_t *texture_data, const uint8_t *palette_data);
static guint texture_key_hash(gconstpointer key);
static gboolean texture_key_equal(gconstpointer a, gconstpointer b);
static gpointer texture_key_retrieve(gpointer key, gpointer user_data, GError **error);
static void texture_key_destroy(gpointer data);
static void texture_binding_destroy(gpointer data);
static guint shader_hash(gconstpointer key);
static gboolean shader_equal(gconstpointer a, gconstpointer b);
static unsigned int kelvin_map_stencil_op(uint32_t parameter);
static unsigned int kelvin_map_polygon_mode(uint32_t parameter);
static unsigned int kelvin_map_texgen(uint32_t parameter, unsigned int channel);
static uint64_t fnv_hash(const uint8_t *data, size_t len);
static uint64_t fast_hash(const uint8_t *data, size_t len, unsigned int samples);

/* PGRAPH - accelerated 2d/3d drawing engine */

static uint32_t pgraph_rdi_read(PGRAPHState *pg,
                                unsigned int select, unsigned int address)
{
    uint32_t r = 0;
    switch(select) {
    case RDI_INDEX_VTX_CONSTANTS0:
    case RDI_INDEX_VTX_CONSTANTS1:
        assert((address / 4) < NV2A_VERTEXSHADER_CONSTANTS);
        r = pg->vsh_constants[address / 4][3 - address % 4];
        break;
    default:
        fprintf(stderr, "nv2a: unknown rdi read select 0x%x address 0x%x\n",
                select, address);
        assert(false);
        break;
    }
    return r;
}

static void pgraph_rdi_write(PGRAPHState *pg,
                             unsigned int select, unsigned int address,
                             uint32_t val)
{
    switch(select) {
    case RDI_INDEX_VTX_CONSTANTS0:
    case RDI_INDEX_VTX_CONSTANTS1:
        assert(false); /* Untested */
        assert((address / 4) < NV2A_VERTEXSHADER_CONSTANTS);
        pg->vsh_constants_dirty[address / 4] |=
            (val != pg->vsh_constants[address / 4][3 - address % 4]);
        pg->vsh_constants[address / 4][3 - address % 4] = val;
        break;
    default:
        NV2A_DPRINTF("unknown rdi write select 0x%x, address 0x%x, val 0x%08x\n",
                     select, address, val);
        break;
    }
}

DEVICE_READ32(PGRAPH)
{
    qemu_mutex_lock(&d->pgraph.pgraph_lock);

    PGRAPHState *pg = &d->pgraph;
    DEVICE_READ32_SWITCH() {
    case NV_PGRAPH_INTR:
        result = pg->pending_interrupts;
        break;
    case NV_PGRAPH_INTR_EN:
        result = pg->enabled_interrupts;
        break;
    case NV_PGRAPH_RDI_DATA: {
        unsigned int select = GET_MASK(pg->pgraph_regs[NV_PGRAPH_RDI_INDEX/4],
                                       NV_PGRAPH_RDI_INDEX_SELECT);
        int address = GET_MASK(pg->pgraph_regs[NV_PGRAPH_RDI_INDEX/4],
                                        NV_PGRAPH_RDI_INDEX_ADDRESS);

        result = pgraph_rdi_read(pg, select, address);

        /* FIXME: Overflow into select? */
        assert(address < GET_MASK(NV_PGRAPH_RDI_INDEX_ADDRESS,
                                  NV_PGRAPH_RDI_INDEX_ADDRESS));
        SET_MASK(pg->pgraph_regs[NV_PGRAPH_RDI_INDEX/4],
                 NV_PGRAPH_RDI_INDEX_ADDRESS, address + 1);
        break;
    }
    default:
        DEVICE_READ32_REG(pgraph); // Was : DEBUG_READ32_UNHANDLED(PGRAPH);
    }

    qemu_mutex_unlock(&pg->pgraph_lock);

//    reg_log_read(NV_PGRAPH, addr, r);

    DEVICE_READ32_END(PGRAPH);
}

DEVICE_WRITE32(PGRAPH)
{
    PGRAPHState *pg = &d->pgraph;
//    reg_log_write(NV_PGRAPH, addr, val);

    qemu_mutex_lock(&pg->pgraph_lock);

    switch (addr) {
    case NV_PGRAPH_INTR:
        pg->pending_interrupts &= ~value;
        qemu_cond_broadcast(&pg->interrupt_cond);
        break;
    case NV_PGRAPH_INTR_EN:
        pg->enabled_interrupts = value;
        break;
    case NV_PGRAPH_INCREMENT:
        if (value & NV_PGRAPH_INCREMENT_READ_3D) {
			pg->KelvinPrimitive.SetFlipRead=
                (pg->KelvinPrimitive.SetFlipRead + 1)
                % pg->KelvinPrimitive.SetFlipModulo;
            qemu_cond_broadcast(&pg->flip_3d);
        }
        break;
    case NV_PGRAPH_RDI_DATA: {
        unsigned int select = GET_MASK(pg->pgraph_regs[NV_PGRAPH_RDI_INDEX / 4],
                                       NV_PGRAPH_RDI_INDEX_SELECT);
        int address = GET_MASK(pg->pgraph_regs[NV_PGRAPH_RDI_INDEX / 4],
                                        NV_PGRAPH_RDI_INDEX_ADDRESS);

        pgraph_rdi_write(pg, select, address, value);

        /* FIXME: Overflow into select? */
        assert(address < GET_MASK(NV_PGRAPH_RDI_INDEX_ADDRESS/4,
                                  NV_PGRAPH_RDI_INDEX_ADDRESS));
        SET_MASK(pg->pgraph_regs[NV_PGRAPH_RDI_INDEX / 4],
                 NV_PGRAPH_RDI_INDEX_ADDRESS, address + 1);
        break;
    }
    case NV_PGRAPH_CHANNEL_CTX_TRIGGER: {
        xbox::addr_xt context_address =
			//warning pg->pgraph_regs[NV_PGRAPH_CHANNEL_CTX_POINTER / 4] was never set.
			GET_MASK(pg->pgraph_regs[NV_PGRAPH_CHANNEL_CTX_POINTER / 4],
                NV_PGRAPH_CHANNEL_CTX_POINTER_INST) << 4;

        if (value & NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN) {
            unsigned pgraph_channel_id =
                GET_MASK(pg->pgraph_regs[NV_PGRAPH_CTX_USER / 4], NV_PGRAPH_CTX_USER_CHID);

            NV2A_DPRINTF("PGRAPH: read channel %d context from %" HWADDR_PRIx "\n",
                pgraph_channel_id, context_address);

            uint8_t *context_ptr = d->pramin.ramin_ptr + context_address;
            uint32_t context_user = ldl_le_p((uint32_t*)context_ptr);

            NV2A_DPRINTF("    - CTX_USER = 0x%08X\n", context_user);

            pg->pgraph_regs[NV_PGRAPH_CTX_USER / 4] = context_user;
            // pgraph_set_context_user(d, context_user);
        }
        if (value & NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT) {
            /* do stuff ... */
        }

        break;
    }
    default: 
        DEVICE_WRITE32_REG(pgraph); // Was : DEBUG_WRITE32_UNHANDLED(PGRAPH);
        break;
    }

    // events
    switch (addr) {
    case NV_PGRAPH_FIFO:
        qemu_cond_broadcast(&pg->fifo_access_cond);
        break;
    }

    qemu_mutex_unlock(&pg->pgraph_lock);

    DEVICE_WRITE32_END(PGRAPH);
}

void OpenGL_draw_end(NV2AState *d); // forward declaration

void OpenGL_draw_arrays(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);
    assert(pg->shader_binding);

    pgraph_bind_vertex_attributes(d, pg->draw_arrays_max_count,
        false, 0);
    glMultiDrawArrays(pg->shader_binding->gl_primitive_mode,
        pg->gl_draw_arrays_start,
        pg->gl_draw_arrays_count,
        pg->draw_arrays_length);

    OpenGL_draw_end(d);
}

void OpenGL_draw_inline_buffer(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);
    assert(pg->shader_binding);

    for (unsigned int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *vertex_attribute = &pg->vertex_attributes[i];

        if (vertex_attribute->inline_buffer) {

            glBindBuffer(GL_ARRAY_BUFFER,
                vertex_attribute->gl_inline_buffer);
            glBufferData(GL_ARRAY_BUFFER,
                pg->inline_buffer_length
                * sizeof(float) * 4,
                vertex_attribute->inline_buffer,
                GL_DYNAMIC_DRAW);

            /* Clear buffer for next batch */
            g_free(vertex_attribute->inline_buffer);
            vertex_attribute->inline_buffer = NULL;

            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(i);
        }
        else {
            glDisableVertexAttribArray(i);
			float *inline_value = pgraph_get_vertex_attribute_inline_value(pg, i);
            glVertexAttrib4fv(i, inline_value);
        }
    }

    glDrawArrays(pg->shader_binding->gl_primitive_mode,
        0, pg->inline_buffer_length);

    OpenGL_draw_end(d);
}

void OpenGL_draw_inline_array(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);
    assert(pg->shader_binding);

    unsigned int index_count = pgraph_bind_inline_array(d);
    glDrawArrays(pg->shader_binding->gl_primitive_mode,
        0, index_count);

    OpenGL_draw_end(d);
}

void OpenGL_draw_inline_elements(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);
    assert(pg->shader_binding);

    uint32_t max_element = 0;
    uint32_t min_element = (uint32_t)-1;
    for (unsigned int i = 0; i < pg->inline_elements_length; i++) {
        max_element = MAX(pg->inline_elements[i], max_element);
        min_element = MIN(pg->inline_elements[i], min_element);
    }
    pgraph_bind_vertex_attributes(d, max_element + 1, false, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pg->gl_element_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        pg->inline_elements_length * sizeof(pg->inline_elements[0]),
        pg->inline_elements,
        GL_DYNAMIC_DRAW);

    glDrawRangeElements(pg->shader_binding->gl_primitive_mode,
        min_element, max_element,
        pg->inline_elements_length,
        GL_UNSIGNED_SHORT, // Cxbx-Reloaded TODO : Restore GL_UNSIGNED_INT once D3D_draw_inline_elements can draw using uint32_t
        (void*)0);

    OpenGL_draw_end(d);
}

static void CxbxImGui_RenderOpenGL(ImGuiUI* m_imgui, std::nullptr_t unused)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    m_imgui->DrawMenu();
    m_imgui->DrawWidgets();

    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData->TotalVtxCount > 0) {
        ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }
}

void OpenGL_draw_state_update(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);

    NV2A_GL_DGROUP_BEGIN("NV097_SET_BEGIN_END: 0x%x", pg->primitive_mode);

    //uint32_t control_0 = pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4];
    //uint32_t control_1 = pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4];

    bool depth_test = pg->KelvinPrimitive.SetCullFaceEnable!=0;
    bool stencil_test = pg->KelvinPrimitive.SetStencilTestEnable!=0;

    pgraph_update_surface(d, true, true, depth_test || stencil_test);

    bool alpha = pg->KelvinPrimitive.SetColorMask & NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE;
    bool red = pg->KelvinPrimitive.SetColorMask & NV097_SET_COLOR_MASK_RED_WRITE_ENABLE;
    bool green = pg->KelvinPrimitive.SetColorMask & NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE;
    bool blue = pg->KelvinPrimitive.SetColorMask & NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE;
    glColorMask(red, green, blue, alpha);
    glDepthMask(!!(pg->KelvinPrimitive.SetDepthMask&0x1!=0));
    glStencilMask(pg->KelvinPrimitive.SetStencilMask & 0xFF);

    //uint32_t blend = pg->pgraph_regs[NV_PGRAPH_BLEND / 4];
    if (pg->KelvinPrimitive.SetBlendEnable!=0) {
        glEnable(GL_BLEND);
        uint32_t sfactor = pg->KelvinPrimitive.SetBlendFuncSfactor;
        uint32_t dfactor = pg->KelvinPrimitive.SetBlendFuncDfactor;
        assert(sfactor < ARRAY_SIZE(pgraph_blend_factor_map));
        assert(dfactor < ARRAY_SIZE(pgraph_blend_factor_map));
        glBlendFunc(pgraph_blend_factor_map[sfactor],
            pgraph_blend_factor_map[dfactor]);

        uint32_t equation = pg->KelvinPrimitive.SetBlendEquation;
        assert(equation < ARRAY_SIZE(pgraph_blend_equation_map));
        glBlendEquation(pgraph_blend_equation_map[equation]);

        uint32_t blend_color = pg->KelvinPrimitive.SetBlendColor;
        glBlendColor(((blend_color >> 16) & 0xFF) / 255.0f, /* red */
            ((blend_color >> 8) & 0xFF) / 255.0f,  /* green */
            (blend_color & 0xFF) / 255.0f,         /* blue */
            ((blend_color >> 24) & 0xFF) / 255.0f);/* alpha */
    }
    else {
        glDisable(GL_BLEND);
    }

    /* Face culling */
    //uint32_t setupraster = pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4];
    if (pg->KelvinPrimitive.SetBlendEnable!=0) {
        uint32_t cull_face = pg->KelvinPrimitive.SetCullFace;
        assert(cull_face < ARRAY_SIZE(pgraph_cull_face_map));
        glCullFace(pgraph_cull_face_map[cull_face]);
        glEnable(GL_CULL_FACE);
    }
    else {
        glDisable(GL_CULL_FACE);
    }

    /* Front-face select */
    glFrontFace(pg->KelvinPrimitive.SetFrontFace & 1
        ? GL_CCW : GL_CW);

    /* Polygon offset */
    /* FIXME: GL implementation-specific, maybe do this in VS? */
    if (pg->KelvinPrimitive.SetPolyOffsetFillEnable!=0) {
        glEnable(GL_POLYGON_OFFSET_FILL);
    }
    else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (pg->KelvinPrimitive.SetPolyOffsetLineEnable!=0) {
        glEnable(GL_POLYGON_OFFSET_LINE);
    }
    else {
        glDisable(GL_POLYGON_OFFSET_LINE);
    }
    if (pg->KelvinPrimitive.SetPolyOffsetPointEnable!=0) {
        glEnable(GL_POLYGON_OFFSET_POINT);
    }
    else {
        glDisable(GL_POLYGON_OFFSET_POINT);
    }

    if ((  pg->KelvinPrimitive.SetPolyOffsetPointEnable!=0)
        || pg->KelvinPrimitive.SetPolyOffsetLineEnable!=0
        || pg->KelvinPrimitive.SetPolyOffsetPointEnable!=0) {
        GLfloat zfactor = *(float*)&pg->KelvinPrimitive.SetPolygonOffsetScaleFactor;
        GLfloat zbias = *(float*)&pg->KelvinPrimitive.SetPolygonOffsetBias;
        glPolygonOffset(zfactor, zbias);
    }

    /* Depth testing */
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);

        uint32_t depth_func = (pg->KelvinPrimitive.SetDepthFunc & 0x0F);
        assert(depth_func < ARRAY_SIZE(pgraph_depth_func_map));
        glDepthFunc(pgraph_depth_func_map[depth_func]);
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }

    if (stencil_test) {
        glEnable(GL_STENCIL_TEST);

        uint32_t stencil_func = pg->KelvinPrimitive.SetStencilFunc & 0x0F;
        uint32_t stencil_ref = pg->KelvinPrimitive.SetStencilFuncRef & 0xFF;
        uint32_t func_mask = pg->KelvinPrimitive.SetStencilFuncMask & 0xFF;
        //uint32_t control2 = pg->pgraph_regs[NV_PGRAPH_CONTROL_2/4];
        uint32_t op_fail = kelvin_map_stencil_op(pg->KelvinPrimitive.SetStencilOpFail & 0x0F);
        uint32_t op_zfail = pg->KelvinPrimitive.SetStencilOpZfail & 0xF;
        uint32_t op_zpass = kelvin_map_stencil_op(pg->KelvinPrimitive.SetStencilOpZpass & 0xF);

        assert(stencil_func < ARRAY_SIZE(pgraph_stencil_func_map));
        assert(op_fail < ARRAY_SIZE(pgraph_stencil_op_map));
        assert(op_zfail < ARRAY_SIZE(pgraph_stencil_op_map));
        assert(op_zpass < ARRAY_SIZE(pgraph_stencil_op_map));

        glStencilFunc(
            pgraph_stencil_func_map[stencil_func],
            stencil_ref,
            func_mask);

        glStencilOp(
            pgraph_stencil_op_map[op_fail],
            pgraph_stencil_op_map[op_zfail],
            pgraph_stencil_op_map[op_zpass]);

    }
    else {
        glDisable(GL_STENCIL_TEST);
    }

    /* Dither */
    /* FIXME: GL implementation dependent */
    if (pg->KelvinPrimitive.SetDitherEnable!=0) {
        glEnable(GL_DITHER);
    }
    else {
        glDisable(GL_DITHER);
    }

    pgraph_bind_shaders(pg);
    pgraph_bind_textures(d);

    //glDisableVertexAttribArray(NV2A_VERTEX_ATTR_DIFFUSE);
    //glVertexAttrib4f(NV2A_VERTEX_ATTR_DIFFUSE, 1.0f, 1.0f, 1.0f, 1.0f);

    unsigned int width, height;
    pgraph_get_surface_dimensions(pg, &width, &height);
    pgraph_apply_anti_aliasing_factor(pg, &width, &height);
    glViewport(0, 0, width, height);

    /* Visibility testing */
    if (pg->KelvinPrimitive.SetZpassPixelCountEnable!=0) {
        GLuint gl_query;
        glGenQueries(1, &gl_query);
        pg->gl_zpass_pixel_count_query_count++;
        pg->gl_zpass_pixel_count_queries = (GLuint*)g_realloc(
            pg->gl_zpass_pixel_count_queries,
            sizeof(GLuint) * pg->gl_zpass_pixel_count_query_count);
        pg->gl_zpass_pixel_count_queries[
            pg->gl_zpass_pixel_count_query_count - 1] = gl_query;
        glBeginQuery(GL_SAMPLES_PASSED, gl_query);
    }

    // Render ImGui
    static std::function<void(ImGuiUI*, std::nullptr_t)> internal_render = &CxbxImGui_RenderOpenGL;
    g_renderbase->Render(internal_render, nullptr);
}

void OpenGL_draw_end(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);

    /* End of visibility testing */
    if (pg->KelvinPrimitive.SetZpassPixelCountEnable !=0) {
        glEndQuery(GL_SAMPLES_PASSED);
    }

    NV2A_GL_DGROUP_END();
}

void OpenGL_draw_clear(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);

    NV2A_DPRINTF("---------PRE CLEAR ------\n");
    GLbitfield gl_mask = 0;

    bool write_color = (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_COLOR);
    bool write_zeta =
        (pg->KelvinPrimitive.ClearSurface & (NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL));

    if (write_zeta) {
        uint32_t clear_zstencil =
            d->pgraph.KelvinPrimitive.SetZStencilClearValue;
        GLint gl_clear_stencil;
        GLfloat gl_clear_depth;

        /* FIXME: Put these in some lookup table */
        const float f16_max = 511.9375f;
        /* FIXME: 7 bits of mantissa unused. maybe use full buffer? */
        const float f24_max = 3.4027977E38f;

        switch (pg->surface_shape.zeta_format) {
        case NV097_SET_SURFACE_FORMAT_ZETA_Z16: {
            if (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_Z) {
                gl_mask |= GL_DEPTH_BUFFER_BIT;
                uint16_t z = clear_zstencil & 0xFFFF;
                if (pg->surface_shape.z_format) {
                    gl_clear_depth = convert_f16_to_float(z) / f16_max;
                    assert(false); /* FIXME: Untested */
                }
                else {
                    gl_clear_depth = z / (float)0xFFFF;
                }
            }
            break;
        }
        case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8: {
            if (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_STENCIL) {
                gl_mask |= GL_STENCIL_BUFFER_BIT;
                gl_clear_stencil = clear_zstencil & 0xFF;
            }
            if (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_Z) {
                gl_mask |= GL_DEPTH_BUFFER_BIT;
                uint32_t z = clear_zstencil >> 8;
                if (pg->surface_shape.z_format) {
                    gl_clear_depth = convert_f24_to_float(z) / f24_max;
                    assert(false); /* FIXME: Untested */
                }
                else {
                    gl_clear_depth = z / (float)0xFFFFFF;
                }
            }
            break;
        }
        default:
            fprintf(stderr, "Unknown zeta surface format: 0x%x\n", pg->surface_shape.zeta_format);
            assert(false);
            break;
        }

        if (gl_mask & GL_DEPTH_BUFFER_BIT) {
            glDepthMask(GL_TRUE);
            glClearDepth(gl_clear_depth);
        }

        if (gl_mask & GL_STENCIL_BUFFER_BIT) {
            glStencilMask(0xff);
            glClearStencil(gl_clear_stencil);
        }
    }

    if (write_color) {
        gl_mask |= GL_COLOR_BUFFER_BIT;
        glColorMask((pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_R)
            ? GL_TRUE : GL_FALSE,
            (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_G)
            ? GL_TRUE : GL_FALSE,
            (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_B)
            ? GL_TRUE : GL_FALSE,
            (pg->KelvinPrimitive.ClearSurface & NV097_CLEAR_SURFACE_A)
            ? GL_TRUE : GL_FALSE);
        uint32_t clear_color = d->pgraph.KelvinPrimitive.SetColorClearValue;

        /* Handle RGB */
        GLfloat red, green, blue;
        switch (pg->surface_shape.color_format) {
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5:
            red = ((clear_color >> 10) & 0x1F) / 31.0f;
            green = ((clear_color >> 5) & 0x1F) / 31.0f;
            blue = (clear_color & 0x1F) / 31.0f;
            assert(false); /* Untested */
            break;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
            red = ((clear_color >> 11) & 0x1F) / 31.0f;
            green = ((clear_color >> 5) & 0x3F) / 63.0f;
            blue = (clear_color & 0x1F) / 31.0f;
            break;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
            red = ((clear_color >> 16) & 0xFF) / 255.0f;
            green = ((clear_color >> 8) & 0xFF) / 255.0f;
            blue = (clear_color & 0xFF) / 255.0f;
            break;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_G8B8:
            /* Xbox D3D doesn't support clearing those */
        default:
            red = 1.0f;
            green = 0.0f;
            blue = 1.0f;
            fprintf(stderr, "CLEAR_SURFACE for color_format 0x%x unsupported",
                pg->surface_shape.color_format);
            assert(false);
            break;
        }

        /* Handle alpha */
        GLfloat alpha;
        switch (pg->surface_shape.color_format) {
            /* FIXME: CLEAR_SURFACE seems to work like memset, so maybe we
            *        also have to clear non-alpha bits with alpha value?
            *        As GL doesn't own those pixels we'd have to do this on
            *        our own in xbox memory.
            */
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8:
            alpha = ((clear_color >> 24) & 0x7F) / 127.0f;
            assert(false); /* Untested */
            break;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
            alpha = ((clear_color >> 24) & 0xFF) / 255.0f;
            break;
        default:
            alpha = 1.0f;
            break;
        }

        glClearColor(red, green, blue, alpha);
    }

    if (gl_mask) {
        pgraph_update_surface(d, true, write_color, write_zeta);

        glEnable(GL_SCISSOR_TEST);

        unsigned int xmin = GET_MASK(pg->KelvinPrimitive.SetClearRectHorizontal,
            NV_PGRAPH_CLEARRECTX_XMIN);
        unsigned int xmax = GET_MASK(pg->KelvinPrimitive.SetClearRectHorizontal,
            NV_PGRAPH_CLEARRECTX_XMAX);
        unsigned int ymin = GET_MASK(pg->KelvinPrimitive.SetClearRectVertical,
            NV_PGRAPH_CLEARRECTY_YMIN);
        unsigned int ymax = GET_MASK(pg->KelvinPrimitive.SetClearRectVertical,
            NV_PGRAPH_CLEARRECTY_YMAX);

        unsigned int scissor_x = xmin;
        unsigned int scissor_y = pg->surface_shape.clip_height - ymax - 1;

        unsigned int scissor_width = xmax - xmin + 1;
        unsigned int scissor_height = ymax - ymin + 1;

        pgraph_apply_anti_aliasing_factor(pg, &scissor_x, &scissor_y);
        pgraph_apply_anti_aliasing_factor(pg, &scissor_width, &scissor_height);

        /* FIXME: Should this really be inverted instead of ymin? */
        glScissor(scissor_x, scissor_y, scissor_width, scissor_height);

        /* FIXME: Respect window clip?!?! */

        NV2A_DPRINTF("------------------CLEAR 0x%x %d,%d - %d,%d  %x---------------\n",
            parameter, xmin, ymin, xmax, ymax, d->pgraph.KelvinPrimitive.SetColorClearValue);

        /* Dither */
        /* FIXME: Maybe also disable it here? + GL implementation dependent */
        if (pg->KelvinPrimitive.SetDitherEnable!=0) {
            glEnable(GL_DITHER);
        }
        else {
            glDisable(GL_DITHER);
        }

        glClear(gl_mask);

        glDisable(GL_SCISSOR_TEST);
    }

    pgraph_set_surface_dirty(pg, write_color, write_zeta);
}

void OpenGL_init_pgraph_plugins()
{
    pgraph_draw_state_update = OpenGL_draw_state_update;
    pgraph_draw_clear = OpenGL_draw_clear;
    pgraph_draw_arrays = OpenGL_draw_arrays;
    pgraph_draw_inline_buffer = OpenGL_draw_inline_buffer;
    pgraph_draw_inline_array = OpenGL_draw_inline_array;
    pgraph_draw_inline_elements = OpenGL_draw_inline_elements;
}

//Contex Handle Define
#define NV_DD_DMA_PUSHER_SYNC_NOTIFIER_CONTEXT_DMA_TO_MEMORY    2 
#define NV_DD_DMA_CONTEXT_DMA_IN_VIDEO_MEMORY                   3 
#define NV_DD_DMA_CONTEXT_DMA_TO_VIDEO_MEMORY                   4 
#define NV_DD_DMA_CONTEXT_DMA_FROM_VIDEO_MEMORY                 5 
#define NV_DD_DMA_PUSHER_CONTEXT_DMA_FROM_MEMORY                6 
#define D3D_MEMCOPY_NOTIFIER_CONTEXT_DMA_TO_MEMORY              7 
#define D3D_SEMAPHORE_CONTEXT_DMA_IN_MEMORY                     8
#define D3D_COLOR_CONTEXT_DMA_IN_VIDEO_MEMORY                   9
#define D3D_ZETA_CONTEXT_DMA_IN_VIDEO_MEMORY                   10
#define D3D_COPY_CONTEXT_DMA_IN_VIDEO_MEMORY                   11
#define D3D_CONTEXT_IN_CACHED_MEMORY                           12

#define D3D_KELVIN_PRIMITIVE                                   13 
#define D3D_MEMORY_TO_MEMORY_COPY                              14 
#define D3D_SCALED_IMAGE_FROM_MEMORY                           15   
#define D3D_RECTANGLE_COPY                                     16
#define D3D_RECTANGLE_COPY_SURFACES                            17

#define D3D_RECTANGLE_COPY_PATTERN                             18
#define D3D_RECTANGLE_COPY_COLOR_KEY                           19
#define D3D_RECTANGLE_COPY_ROP                                 20
#define D3D_RECTANGLE_COPY_BETA1                               21
#define D3D_RECTANGLE_COPY_BETA4                               22
#define D3D_RECTANGLE_COPY_CLIP_RECTANGLE                      24

static uint32_t subchannel_to_graphic_class[8] = { NV_KELVIN_PRIMITIVE,NV_MEMORY_TO_MEMORY_FORMAT,NV_IMAGE_BLIT,NV_CONTEXT_SURFACES_2D,0,NV_CONTEXT_PATTERN,0,0, };
extern VertexShaderMode g_Xbox_VertexShaderMode;
extern bool g_VertexShader_dirty;
extern void CxbxUpdateHostVertexShader();
//starting address of vertex shader user program
extern xbox::dword_xt g_Xbox_VertexShader_FunctionSlots_StartAddress;
//xbox vertex shader attributes slots. set by SetVertexShaderInput(). try to set it directly before set vertex shader or draw prmitives.
extern xbox::X_VERTEXATTRIBUTEFORMAT g_Xbox_SetVertexShaderInput_Attributes;
extern DWORD ABGR_to_ARGB(const uint32_t color);
extern void set_IVB_DECL_override(void);
extern void reset_IVB_DECL_override(void);
extern RAMHTEntry ramht_lookup(NV2AState *d, uint32_t handle);
extern DWORD NV2A_DirtyFlags;
extern void pgraph_SetModelViewMatrixDirty(unsigned int index);
extern void pgraph_SetInverseModelViewMatrixDirty(unsigned int index);
extern void pgraph_SetCompositeMatrixDirty(void);
extern void pgraph_use_UserPixelShader(void);
extern void pgraph_use_FixedPixelShader(void);
extern bool NV2A_ShaderOtherStageInputDirty;
extern bool NV2A_TextureFactorAllTheSame;
extern void pgraph_SetNV2AStateFlag(DWORD flag);
extern bool pgraph_GetNV2AStateFlag(DWORD flag);
extern void pgraph_ClearNV2AStateFlag(DWORD flag);
extern void pgraph_ComposeViewport(NV2AState *d);
extern NV2ADevice* g_NV2A; //TMP GLUE
extern bool NV2A_viewport_dirty;
extern void pgraph_use_NV2A_Kelvin(void);
extern void pgraph_notuse_NV2A_Kelvin(void);

D3DMATRIX * pgraph_get_ModelViewMatrix(unsigned index)
{
	// Retrieve NV2AState via the (LLE) NV2A device :
	NV2AState *d = g_NV2A->GetDeviceState();
	PGRAPHState *pg = &d->pgraph;

	return (D3DMATRIX *)&pg->KelvinPrimitive.SetModelViewMatrix[index][0];
}
D3DMATRIX * pgraph_get_InverseModelViewMatrix(unsigned index)
{
	// Retrieve NV2AState via the (LLE) NV2A device :
	NV2AState *d = g_NV2A->GetDeviceState();
	PGRAPHState *pg = &d->pgraph;

	return (D3DMATRIX *)&pg->KelvinPrimitive.SetInverseModelViewMatrix[index][0];
}
D3DMATRIX* pgraph_get_TextureTransformMatrix(unsigned index)
{
    // Retrieve NV2AState via the (LLE) NV2A device :
    NV2AState* d = g_NV2A->GetDeviceState();
    PGRAPHState* pg = &d->pgraph;

    return (D3DMATRIX*)&pg->KelvinPrimitive.SetTextureMatrix[index][0];
}

void kelvin_validate_struct_field_offsets_against_NV097_defines()
{
	static_assert(offsetof(NV097KelvinPrimitive, SetObject) == NV097_SET_OBJECT);
	// uint32_t Rev_0004[0xfc / 4];
	static_assert(offsetof(NV097KelvinPrimitive, NoOperation) == NV097_NO_OPERATION); // 0x00000100
	static_assert(offsetof(NV097KelvinPrimitive, Notify) == NV097_NOTIFY);
	static_assert(offsetof(NV097KelvinPrimitive, SetWarningEnable) == NV097_SET_WARNING_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, GetState) == NV097_GET_STATE);
	static_assert(offsetof(NV097KelvinPrimitive, WaitForIdle) == NV097_WAIT_FOR_IDLE);
	// uint32_t Rev_0114[0xc / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetFlipRead) == NV097_SET_FLIP_READ);
	static_assert(offsetof(NV097KelvinPrimitive, SetFlipWrite) == NV097_SET_FLIP_WRITE);
	static_assert(offsetof(NV097KelvinPrimitive, SetFlipModulo) == NV097_SET_FLIP_MODULO);
	static_assert(offsetof(NV097KelvinPrimitive, FlipIncrementWrite) == NV097_FLIP_INCREMENT_WRITE);
	static_assert(offsetof(NV097KelvinPrimitive, FlipStall) == NV097_FLIP_STALL);
	// uint32_t Rev_0134[0xc / 4];
	static_assert(offsetof(NV097KelvinPrimitive, PmTrigger) == NV097_PM_TRIGGER);
	// uint32_t Rev_0144[0x3c / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaNotifies) == NV097_SET_CONTEXT_DMA_NOTIFIES);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaA) == NV097_SET_CONTEXT_DMA_A);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaB) == NV097_SET_CONTEXT_DMA_B);
	// uint32_t Rev_018c[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaState) == NV097_SET_CONTEXT_DMA_STATE);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaColor) == NV097_SET_CONTEXT_DMA_COLOR);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaZeta) == NV097_SET_CONTEXT_DMA_ZETA);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaVertexA) == NV097_SET_CONTEXT_DMA_VERTEX_A);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaVertexB) == NV097_SET_CONTEXT_DMA_VERTEX_B);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaSemaphore) == NV097_SET_CONTEXT_DMA_SEMAPHORE);
	static_assert(offsetof(NV097KelvinPrimitive, SetContextDmaReport) == NV097_SET_CONTEXT_DMA_REPORT);
	// uint32_t Rev_01ac[0x54 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetSurfaceClipHorizontal) == NV097_SET_SURFACE_CLIP_HORIZONTAL); // 0x00000200
	static_assert(offsetof(NV097KelvinPrimitive, SetSurfaceClipVertical) == NV097_SET_SURFACE_CLIP_VERTICAL); // 0x00000204
	static_assert(offsetof(NV097KelvinPrimitive, SetSurfaceFormat) == NV097_SET_SURFACE_FORMAT); // 0x00000204
	static_assert(offsetof(NV097KelvinPrimitive, SetSurfacePitch) == NV097_SET_SURFACE_PITCH); // 0x00000204
	static_assert(offsetof(NV097KelvinPrimitive, SetSurfaceColorOffset) == NV097_SET_SURFACE_COLOR_OFFSET); // 0x00000204
	static_assert(offsetof(NV097KelvinPrimitive, SetSurfaceZetaOffset) == NV097_SET_SURFACE_ZETA_OFFSET); // 0x00000204
	// uint32_t Rev_0218[0x48 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerAlphaICW[0]) == NV097_SET_COMBINER_ALPHA_ICW); // 0x00000260 [2]
	// uint32_t Rev_0280[0x8 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerSpecularFogCW0) == NV097_SET_COMBINER_SPECULAR_FOG_CW0); // 0x00000288
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerSpecularFogCW1) == NV097_SET_COMBINER_SPECULAR_FOG_CW1); // 0x0000028C
	static_assert(offsetof(NV097KelvinPrimitive, SetControl0) == NV097_SET_CONTROL0);
	static_assert(offsetof(NV097KelvinPrimitive, SetLightControl) == NV097_SET_LIGHT_CONTROL);
	static_assert(offsetof(NV097KelvinPrimitive, SetColorMaterial) == NV097_SET_COLOR_MATERIAL);
	static_assert(offsetof(NV097KelvinPrimitive, SetFogMode) == NV097_SET_FOG_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetFogGenMode) == NV097_SET_FOG_GEN_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetFogEnable) == NV097_SET_FOG_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetFogColor) == NV097_SET_FOG_COLOR);
	// uint32_t Rev_02ac[0x8 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetWindowClipType) == NV097_SET_WINDOW_CLIP_TYPE); // 0x000002B4
	// uint32_t Rev_02b8[0x8 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetWindowClipHorizontal[0]) == NV097_SET_WINDOW_CLIP_HORIZONTAL); // 0x000002C0 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetWindowClipVertical[0]) == NV097_SET_WINDOW_CLIP_VERTICAL); // 0x000002E0 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetAlphaTestEnable) == NV097_SET_ALPHA_TEST_ENABLE); // 0x00000300
	static_assert(offsetof(NV097KelvinPrimitive, SetBlendEnable) == NV097_SET_BLEND_ENABLE); // 0x00000304
	static_assert(offsetof(NV097KelvinPrimitive, SetCullFaceEnable) == NV097_SET_CULL_FACE_ENABLE); // 0x00000308
	static_assert(offsetof(NV097KelvinPrimitive, SetDepthTestEnable) == NV097_SET_DEPTH_TEST_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetDitherEnable) == NV097_SET_DITHER_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetLightingEnable) == NV097_SET_LIGHTING_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetPointParamsEnable) == NV097_SET_POINT_PARAMS_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetPointSmoothEnable) == NV097_SET_POINT_SMOOTH_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetLineSmoothEnable) == NV097_SET_LINE_SMOOTH_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetPolySmoothEnable) == NV097_SET_POLY_SMOOTH_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetSkinMode) == NV097_SET_SKIN_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilTestEnable) == NV097_SET_STENCIL_TEST_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetPolyOffsetPointEnable) == NV097_SET_POLY_OFFSET_POINT_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetPolyOffsetLineEnable) == NV097_SET_POLY_OFFSET_LINE_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetPolyOffsetFillEnable) == NV097_SET_POLY_OFFSET_FILL_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetAlphaFunc) == NV097_SET_ALPHA_FUNC);
	static_assert(offsetof(NV097KelvinPrimitive, SetAlphaRef) == NV097_SET_ALPHA_REF);
	static_assert(offsetof(NV097KelvinPrimitive, SetBlendFuncSfactor) == NV097_SET_BLEND_FUNC_SFACTOR);
	static_assert(offsetof(NV097KelvinPrimitive, SetBlendFuncDfactor) == NV097_SET_BLEND_FUNC_DFACTOR);
	static_assert(offsetof(NV097KelvinPrimitive, SetBlendColor) == NV097_SET_BLEND_COLOR);
	static_assert(offsetof(NV097KelvinPrimitive, SetBlendEquation) == NV097_SET_BLEND_EQUATION);
	static_assert(offsetof(NV097KelvinPrimitive, SetDepthFunc) == NV097_SET_DEPTH_FUNC);
	static_assert(offsetof(NV097KelvinPrimitive, SetColorMask) == NV097_SET_COLOR_MASK);
	static_assert(offsetof(NV097KelvinPrimitive, SetDepthMask) == NV097_SET_DEPTH_MASK);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilMask) == NV097_SET_STENCIL_MASK);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilFunc) == NV097_SET_STENCIL_FUNC);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilFuncRef) == NV097_SET_STENCIL_FUNC_REF);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilFuncMask) == NV097_SET_STENCIL_FUNC_MASK);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilOpFail) == NV097_SET_STENCIL_OP_FAIL);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilOpZfail) == NV097_SET_STENCIL_OP_ZFAIL);
	static_assert(offsetof(NV097KelvinPrimitive, SetStencilOpZpass) == NV097_SET_STENCIL_OP_ZPASS);
	static_assert(offsetof(NV097KelvinPrimitive, SetShadeMode) == NV097_SET_SHADE_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetLineWidth) == NV097_SET_LINE_WIDTH);
	static_assert(offsetof(NV097KelvinPrimitive, SetPolygonOffsetScaleFactor) == NV097_SET_POLYGON_OFFSET_SCALE_FACTOR);
	static_assert(offsetof(NV097KelvinPrimitive, SetPolygonOffsetBias) == NV097_SET_POLYGON_OFFSET_BIAS);
	static_assert(offsetof(NV097KelvinPrimitive, SetFrontPolygonMode) == NV097_SET_FRONT_POLYGON_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetBackPolygonMode) == NV097_SET_BACK_POLYGON_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetClipMin) == NV097_SET_CLIP_MIN);
	static_assert(offsetof(NV097KelvinPrimitive, SetClipMax) == NV097_SET_CLIP_MAX);
	static_assert(offsetof(NV097KelvinPrimitive, SetCullFace) == NV097_SET_CULL_FACE);
	static_assert(offsetof(NV097KelvinPrimitive, SetFrontFace) == NV097_SET_FRONT_FACE);
	static_assert(offsetof(NV097KelvinPrimitive, SetNormalizationEnable) == NV097_SET_NORMALIZATION_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetMaterialEmission[0]) == NV097_SET_MATERIAL_EMISSION); // 0x000003A8 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetMaterialAlpha) == NV097_SET_MATERIAL_ALPHA);
	static_assert(offsetof(NV097KelvinPrimitive, SetSpecularEnable) == NV097_SET_SPECULAR_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetLightEnableMask) == NV097_SET_LIGHT_ENABLE_MASK);
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgen[0].S) == NV097_SET_TEXGEN_S); // 0x000003C0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgen[0].T) == NV097_SET_TEXGEN_T); // [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgen[0].R) == NV097_SET_TEXGEN_R); // [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgen[0].Q) == NV097_SET_TEXGEN_Q); // [4]
	// uint32_t Rev_0400[0x20 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTextureMatrixEnable) == NV097_SET_TEXTURE_MATRIX_ENABLE); // 0x00000420 [4]
	// uint32_t Rev_0430[0xc / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetPointSize) == NV097_SET_POINT_SIZE); // 0x0000043C
	static_assert(offsetof(NV097KelvinPrimitive, SetProjectionMatrix) == NV097_SET_PROJECTION_MATRIX); // 0x00000440 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetModelViewMatrix0) == NV097_SET_MODEL_VIEW_MATRIX); // 0x00000480 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetModelViewMatrix1) == NV097_SET_MODEL_VIEW_MATRIX1); // 0x000004C0 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetModelViewMatrix2) == NV097_SET_MODEL_VIEW_MATRIX2); // 0x00000500 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetModelViewMatrix3) == NV097_SET_MODEL_VIEW_MATRIX3); // 0x00000540 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetInverseModelViewMatrix0) == NV097_SET_INVERSE_MODEL_VIEW_MATRIX); // 0x00000580 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetInverseModelViewMatrix1) == NV097_SET_INVERSE_MODEL_VIEW_MATRIX1); // 0x000005C0 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetInverseModelViewMatrix2) == NV097_SET_INVERSE_MODEL_VIEW_MATRIX2); // 0x00000600 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetInverseModelViewMatrix3) == NV097_SET_INVERSE_MODEL_VIEW_MATRIX3); // 0x00000640 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetTextureMatrix0) == NV097_SET_TEXTURE_MATRIX); // 0x000006C0 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetTextureMatrix1) == NV097_SET_TEXTURE_MATRIX1); // 0x00000700 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetTextureMatrix2) == NV097_SET_TEXTURE_MATRIX2); // 0x00000740 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetTextureMatrix3) == NV097_SET_TEXTURE_MATRIX3); // 0x00000780 [16]
	// uint32_t Rev_07c0[0x80 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgenPlane[0].S) == NV097_SET_TEXGEN_PLANE_S); // 0x00000840 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgenPlane[0].T) == NV097_SET_TEXGEN_PLANE_T); // [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgenPlane[0].R) == NV097_SET_TEXGEN_PLANE_R); // [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgenPlane[0].Q) == NV097_SET_TEXGEN_PLANE_Q); // [4]
	// uint32_t Rev_0940[0x80 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetFogParams[0]) == NV097_SET_FOG_PARAMS); // 0x000009C0 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexgenViewModel) == NV097_SET_TEXGEN_VIEW_MODEL);
	static_assert(offsetof(NV097KelvinPrimitive, SetFogPlane[0]) == NV097_SET_FOG_PLANE); // 0x000009D0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetSpecularParams[0]) == NV097_SET_SPECULAR_PARAMS); // 0x000009E0 [6]
	static_assert(offsetof(NV097KelvinPrimitive, SetSwathWidth) == NV097_SET_SWATH_WIDTH);
	static_assert(offsetof(NV097KelvinPrimitive, SetFlatShadeOp) == NV097_SET_FLAT_SHADE_OP);
	// uint32_t Rev_0a00[0x10 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetSceneAmbientColor[0]) == NV097_SET_SCENE_AMBIENT_COLOR); // 0x00000A10 [3]
	// uint32_t Rev_0a1c[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetViewportOffset[0]) == NV097_SET_VIEWPORT_OFFSET); // 0x00000A20 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetPointParams[0]) == NV097_SET_POINT_PARAMS); // 0x00000A30 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetEyePosition[0]) == NV097_SET_EYE_POSITION); // 0x00000A50 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerFactor0[0]) == NV097_SET_COMBINER_FACTOR0); // 0x00000A60 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerFactor1[0]) == NV097_SET_COMBINER_FACTOR1); // 0x00000A80 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerAlphaOCW[0]) == NV097_SET_COMBINER_ALPHA_OCW); // 0x00000AA0 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerColorICW[0]) == NV097_SET_COMBINER_COLOR_ICW); // 0x00000AC0 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetColorKeyColor[0]) == NV097_SET_COLOR_KEY_COLOR); // 0x00000AE0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetViewportScale[0]) == NV097_SET_VIEWPORT_SCALE); // 0x00000AF0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformProgram[0]) == NV097_SET_TRANSFORM_PROGRAM); // 0x00000B00 [32]
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformConstant[0]) == NV097_SET_TRANSFORM_CONSTANT); // 0x00000B80 [32]
	static_assert(offsetof(NV097KelvinPrimitive, SetBackLight[0].AmbientColor[0]) == NV097_SET_BACK_LIGHT_AMBIENT_COLOR); // 0x00000C00 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetBackLight[0].DiffuseColor[0]) == NV097_SET_BACK_LIGHT_DIFFUSE_COLOR); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetBackLight[0].SpecularColor[0]) == NV097_SET_BACK_LIGHT_SPECULAR_COLOR); // [3]
	// uint32_t Rev_0c24[0x1c / 4];//dd (7)
	// uint32_t Rev_0e00[0x200 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].AmbientColor[0]) == NV097_SET_LIGHT_AMBIENT_COLOR); // 0x00001000 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].DiffuseColor[0]) == NV097_SET_LIGHT_DIFFUSE_COLOR); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].SpecularColor[0]) == NV097_SET_LIGHT_SPECULAR_COLOR); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].LocalRange) == NV097_SET_LIGHT_LOCAL_RANGE); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].InfiniteHalfVector[0]) == NV097_SET_LIGHT_INFINITE_HALF_VECTOR); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].InfiniteDirection[0]) == NV097_SET_LIGHT_INFINITE_DIRECTION); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].SpotFalloff[0]) == NV097_SET_LIGHT_SPOT_FALLOFF); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].SpotDirection[0]) == NV097_SET_LIGHT_SPOT_DIRECTION); // [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].LocalPosition[0]) == NV097_SET_LIGHT_LOCAL_POSITION); // [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLight[0].LocalAttenuation[0]) == NV097_SET_LIGHT_LOCAL_ATTENUATION); // [3]
	// uint32_t Rev_1074[0xc / 4];
	// uint32_t Rev_1400[0x7c / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetStippleControl) == NV097_SET_STIPPLE_CONTROL);
	static_assert(offsetof(NV097KelvinPrimitive, SetStipplePattern) == NV097_SET_STIPPLE_PATTERN); // 0x00001480 [32]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertex3f) == NV097_SET_VERTEX3F); // 0x00001500 [3]
	// uint32_t Rev_150c[0xc / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetVertex4f) == NV097_SET_VERTEX4F); // 0x00001518 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertex4s) == NV097_SET_VERTEX4S); // 0x00001528 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetNormal3f) == NV097_SET_NORMAL3F); // 0x00001530 [3]
	// uint32_t Rev_153c[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetNormal3s) == NV097_SET_NORMAL3S); // 0x00001540 [2]
	// uint32_t Rev_1548[0x8 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetDiffuseColor4f) == NV097_SET_DIFFUSE_COLOR4F); // 0x00001550 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetDiffuseColor3f) == NV097_SET_DIFFUSE_COLOR3F); // 0x00001560 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetDiffuseColor4ub) == NV097_SET_DIFFUSE_COLOR4UB);
	static_assert(offsetof(NV097KelvinPrimitive, SetSpecularColor4f) == NV097_SET_SPECULAR_COLOR4F); // 0x00001570 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetSpecularColor3f) == NV097_SET_SPECULAR_COLOR3F); // 0x00001580 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetSpecularColor4ub) == NV097_SET_SPECULAR_COLOR4UB);
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord0_2f) == NV097_SET_TEXCOORD0_2F); // 0x00001590 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord0_2s) == NV097_SET_TEXCOORD0_2S);
	// uint32_t Rev_159c[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord0_4f) == NV097_SET_TEXCOORD0_4F); // 0x000015A0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord0_4s[0]) == NV097_SET_TEXCOORD0_4S); //0x000015B0 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord1_2f[0]) == NV097_SET_TEXCOORD1_2F); //0x000015B8 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord1_2s) == NV097_SET_TEXCOORD1_2S);
	// uint32_t Rev_15c4[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord1_4f[0]) == NV097_SET_TEXCOORD1_4F); // 0x000015C8 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord1_4s[0]) == NV097_SET_TEXCOORD1_4S); // 0x000015D8 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord2_2f[0]) == NV097_SET_TEXCOORD2_2F); // 0x000015E0 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord2_2s) == NV097_SET_TEXCOORD2_2S);
	// uint32_t Rev_15ec[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord2_4f[0]) == NV097_SET_TEXCOORD2_4F); // 0x000015F0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord2_4s[0]) == NV097_SET_TEXCOORD2_4S); // 0x00001600 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord3_2f[0]) == NV097_SET_TEXCOORD3_2F); // 0x00001608 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord3_2s) == NV097_SET_TEXCOORD3_2S);
	// uint32_t Rev_1614[0xc / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord3_4f[0]) == NV097_SET_TEXCOORD3_4F); // 0x00001620 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexcoord3_4s[0]) == NV097_SET_TEXCOORD3_4S); // 0x00001630 [2]
	// uint32_t Rev_1638[0x60 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetFog1f) == NV097_SET_FOG1F);
	static_assert(offsetof(NV097KelvinPrimitive, SetWeight1f) == NV097_SET_WEIGHT1F);
	static_assert(offsetof(NV097KelvinPrimitive, SetWeight2f[0]) == NV097_SET_WEIGHT2F); // 0x000016A0 [2]
	// uint32_t Rev_16a8[0x8 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetWeight3f[0]) == NV097_SET_WEIGHT3F); // 0x000016B0 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetEdgeFlag) == NV097_SET_EDGE_FLAG);
	static_assert(offsetof(NV097KelvinPrimitive, SetWeight4f[0]) == NV097_SET_WEIGHT4F); // 0x000016C0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformFixedConst3[0]) == NV097_SET_TRANSFORM_FIXED_CONST3); // 0x000016D0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformFixedConst0[0]) == NV097_SET_TRANSFORM_FIXED_CONST0); // 0x000016E0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformFixedConst1[0]) == NV097_SET_TRANSFORM_FIXED_CONST1); // 0x000016F0 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformFixedConst2[0]) == NV097_SET_TRANSFORM_FIXED_CONST2); // 0x00001700 [4]
	static_assert(offsetof(NV097KelvinPrimitive, InvalidateVertexCacheFile) == NV097_INVALIDATE_VERTEX_CACHE_FILE);
	static_assert(offsetof(NV097KelvinPrimitive, InvalidateVertexFile) == NV097_INVALIDATE_VERTEX_FILE);
	static_assert(offsetof(NV097KelvinPrimitive, TlNop) == NV097_TL_NOP);
	static_assert(offsetof(NV097KelvinPrimitive, TlSync) == NV097_TL_SYNC);
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexDataArrayOffset[0]) == NV097_SET_VERTEX_DATA_ARRAY_OFFSET); // 0x00001720 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexDataArrayFormat[0]) == NV097_SET_VERTEX_DATA_ARRAY_FORMAT); // 0x00001760 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetBackSceneAmbientColor[0]) == NV097_SET_BACK_SCENE_AMBIENT_COLOR); // 0x000017A0 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetBackMaterialAlpha) == NV097_SET_BACK_MATERIAL_ALPHA);
	static_assert(offsetof(NV097KelvinPrimitive, SetBackMaterialEmission[0]) == NV097_SET_BACK_MATERIAL_EMISSIONR); // 0x000017B0 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLogicOpEnable) == NV097_SET_LOGIC_OP_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, SetLogicOp) == NV097_SET_LOGIC_OP);
	static_assert(offsetof(NV097KelvinPrimitive, SetTwoSidedLightEn) == NV097_SET_TWO_SIDED_LIGHT_EN);
	static_assert(offsetof(NV097KelvinPrimitive, ClearReportValue) == NV097_CLEAR_REPORT_VALUE);
	static_assert(offsetof(NV097KelvinPrimitive, SetZpassPixelCountEnable) == NV097_SET_ZPASS_PIXEL_COUNT_ENABLE);
	static_assert(offsetof(NV097KelvinPrimitive, GetReport) == NV097_GET_REPORT);
	static_assert(offsetof(NV097KelvinPrimitive, SetTLConstZero[0]) == NV097_SET_TL_CONST_ZERO); // 0x000017D4 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetEyeDirection[0]) == NV097_SET_EYE_DIRECTION); // 0x000017E0 [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetLinearFogConst[0]) == NV097_SET_LINEAR_FOG_CONST); // 0x000017EC [3]
	static_assert(offsetof(NV097KelvinPrimitive, SetShaderClipPlaneMode) == NV097_SET_SHADER_CLIP_PLANE_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginEnd) == NV097_SET_BEGIN_END);
	static_assert(offsetof(NV097KelvinPrimitive, ArrayElement16) == NV097_ARRAY_ELEMENT16); // 0x00001800
	// uint32_t Rev_1804[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, ArrayElement32) == NV097_ARRAY_ELEMENT32); // 0x00001808
	// uint32_t Rev_180c[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, DrawArrays) == NV097_DRAW_ARRAYS); // 0x00001810
	// uint32_t Rev_1814[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, InlineArray) == NV097_INLINE_ARRAY); // 0x00001818
	static_assert(offsetof(NV097KelvinPrimitive, SetEyeVector[0]) == NV097_SET_EYE_VECTOR); // 0x0000181C [3]
	static_assert(offsetof(NV097KelvinPrimitive, InlineVertexReuse) == NV097_INLINE_VERTEX_REUSE);
	// uint32_t Rev_182c[0x54 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexData2f[0].M[0]) == NV097_SET_VERTEX_DATA2F_M); // 0x00001880 [16][2]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexData2s[0]) == NV097_SET_VERTEX_DATA2S); // 0x00001900 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexData4ub[0]) == NV097_SET_VERTEX_DATA4UB); // 0x00001940 [16]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexData4s[0].M[0]) == NV097_SET_VERTEX_DATA4S_M); // 0x00001980 [16][2]
	static_assert(offsetof(NV097KelvinPrimitive, SetVertexData4f[0].M[0]) == NV097_SET_VERTEX_DATA4F_M); // 0x00001A00 [16][4]
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Offset) == NV097_SET_TEXTURE_OFFSET); // 0x00001B00
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Format) == NV097_SET_TEXTURE_FORMAT); // 0x00001B04
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Address) == NV097_SET_TEXTURE_ADDRESS); // 0x00001B08
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Control0) == NV097_SET_TEXTURE_CONTROL0); // 0x00001B0C
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Control1) == NV097_SET_TEXTURE_CONTROL1); // 0x00001B10
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Filter) == NV097_SET_TEXTURE_FILTER); // 0x00001B14
	// uint32_t Rev_1b18[0x4 / 4];			//	0x00001B18 +i*0x40 			
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].ImageRect) == NV097_SET_TEXTURE_IMAGE_RECT); // 0x00001B1C
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].Palette) == NV097_SET_TEXTURE_PALETTE); // 0x00001B20
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].BorderColor) == NV097_SET_TEXTURE_BORDER_COLOR); // 0x00001B24
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].SetBumpEnvMat00) == NV097_SET_TEXTURE_SET_BUMP_ENV_MAT); // 0x00001B28
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].SetBumpEnvMat01) == NV097_SET_TEXTURE_SET_BUMP_ENV_MAT01); // 0x00001B2C
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].SetBumpEnvMat11) == NV097_SET_TEXTURE_SET_BUMP_ENV_MAT11); // 0x00001B30
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].SetBumpEnvMat10) == NV097_SET_TEXTURE_SET_BUMP_ENV_MAT10); // 0x00001B34
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].SetBumpEnvScale) == NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE ); // 0x00001B38
	static_assert(offsetof(NV097KelvinPrimitive, SetTexture[0].SetBumpEnvOffset) == NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET); // 0x00001B3C
	// uint32_t Rev_1c00[0x164 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, ParkAttribute) == NV097_PARK_ATTRIBUTE); // 0x00001D64
	static_assert(offsetof(NV097KelvinPrimitive, UnparkAttribute) == NV097_UNPARK_ATTRIBUTE);
	static_assert(offsetof(NV097KelvinPrimitive, SetSemaphoreOffset) == NV097_SET_SEMAPHORE_OFFSET);
	static_assert(offsetof(NV097KelvinPrimitive, BackEndWriteSemaphoreRelease) == NV097_BACK_END_WRITE_SEMAPHORE_RELEASE);
	static_assert(offsetof(NV097KelvinPrimitive, TextureReadSemaphoreRelease) == NV097_TEXTURE_READ_SEMAPHORE_RELEASE);
	static_assert(offsetof(NV097KelvinPrimitive, SetZMinMaxControl) == NV097_SET_ZMIN_MAX_CONTROL);
	static_assert(offsetof(NV097KelvinPrimitive, SetAntiAliasingControl) == NV097_SET_ANTI_ALIASING_CONTROL);
	static_assert(offsetof(NV097KelvinPrimitive, SetCompressZBufferEn) == NV097_SET_COMPRESS_ZBUFFER_EN);
	static_assert(offsetof(NV097KelvinPrimitive, SetOccludeZStencilEn) == NV097_SET_OCCLUDE_ZSTENCIL_EN);
	// uint32_t Rev_1d88[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetZStencilClearValue) == NV097_SET_ZSTENCIL_CLEAR_VALUE); // 0x00001D8C
	static_assert(offsetof(NV097KelvinPrimitive, SetColorClearValue) == NV097_SET_COLOR_CLEAR_VALUE);
	static_assert(offsetof(NV097KelvinPrimitive, ClearSurface) == NV097_CLEAR_SURFACE);
	static_assert(offsetof(NV097KelvinPrimitive, SetClearRectHorizontal) == NV097_SET_CLEAR_RECT_HORIZONTAL);
	static_assert(offsetof(NV097KelvinPrimitive, SetClearRectVertical) == NV097_SET_CLEAR_RECT_VERTICAL); // 0x00001D9C
	// uint32_t Rev_1da0[0x40 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginPatch0) == NV097_SET_BEGIN_PATCH0); // 0x00001DE0
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginPatch1) == NV097_SET_BEGIN_PATCH1);
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginPatch2) == NV097_SET_BEGIN_PATCH2);
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginPatch3) == NV097_SET_BEGIN_PATCH3);
	static_assert(offsetof(NV097KelvinPrimitive, SetEndPatch) == NV097_SET_END_PATCH); // 0x00001DF0
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginEndSwatch) == NV097_SET_BEGIN_END_SWATCH);
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginEndCurve) == NV097_SET_BEGIN_END_CURVE);
	// uint32_t Rev_1dfc[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetCurveCoefficients[0]) == NV097_SET_CURVE_COEFFICIENTS); // 0x00001E00 [4]
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginTransition0) == NV097_SET_BEGIN_TRANSITION0);
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginTransition1) == NV097_SET_BEGIN_TRANSITION1);
	static_assert(offsetof(NV097KelvinPrimitive, SetBeginTransition2) == NV097_SET_BEGIN_TRANSITION2);
	static_assert(offsetof(NV097KelvinPrimitive, SetEndTransition) == NV097_SET_END_TRANSITION);
	static_assert(offsetof(NV097KelvinPrimitive, SetSpecularFogFactor[0]) == NV097_SET_SPECULAR_FOG_FACTOR); // 0x00001E20 [2]
	static_assert(offsetof(NV097KelvinPrimitive, SetBackSpecularParams[0]) == NV097_SET_BACK_SPECULAR_PARAMS); // 0x00001E28 [6]
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerColorOCW[0]) == NV097_SET_COMBINER_COLOR_OCW); // 0x00001E28 [8]
	static_assert(offsetof(NV097KelvinPrimitive, SetCombinerControl) == NV097_SET_COMBINER_CONTROL);
	// uint32_t Rev_1e64[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetShadowZSlopeThreshold) == NV097_SET_SHADOW_ZSLOPE_THRESHOLD); // 0x00001E68
	static_assert(offsetof(NV097KelvinPrimitive, SetShadowDepthFunc) == NV097_SET_SHADOW_DEPTH_FUNC);
	static_assert(offsetof(NV097KelvinPrimitive, SetShaderStageProgram) == NV097_SET_SHADER_STAGE_PROGRAM);
	static_assert(offsetof(NV097KelvinPrimitive, SetDotRGBMapping) == NV097_SET_DOT_RGBMAPPING);
	static_assert(offsetof(NV097KelvinPrimitive, SetShaderOtherStageInput) == NV097_SET_SHADER_OTHER_STAGE_INPUT); // 0x00001E78
	// uint32_t Rev_1e7c[0x4 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformData[0]) == NV097_SET_TRANSFORM_DATA); // 0x00001E80 [4]
	static_assert(offsetof(NV097KelvinPrimitive, LaunchTransformProgram) == NV097_LAUNCH_TRANSFORM_PROGRAM);
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformExecutionMode) == NV097_SET_TRANSFORM_EXECUTION_MODE);
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformProgramCxtWriteEn) == NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN);
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformProgramLoad) == NV097_SET_TRANSFORM_PROGRAM_LOAD);
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformProgramStart) == NV097_SET_TRANSFORM_PROGRAM_START);
	static_assert(offsetof(NV097KelvinPrimitive, SetTransformConstantLoad) == NV097_SET_TRANSFORM_CONSTANT_LOAD); // 0x00001EA4
	// uint32_t Rev_1ea8[0x118 / 4];
	static_assert(offsetof(NV097KelvinPrimitive, DebugInit[0]) == NV097_DEBUG_INIT); // 0x00001FC0 [10]
	// uint32_t Rev_1fe8[0x18 / 4];
}

extern XboxRenderStateConverter XboxRenderStates; // temp glue
extern XboxRenderStateConverter NV2ARenderStates; // temp glue
extern XboxTextureStateConverter NV2ATextureStates;

//method count always represnt total dword needed as the arguments following the method.
//caller must ensure there are enough argements available in argv.
int pgraph_handle_method(
    NV2AState *d,
    unsigned int subchannel,
    uint32_t command_word,
    uint32_t arg0,
    uint32_t *argv,
    uint32_t method_count,
    uint32_t max_lookahead_words)

{
    int num_processed = 1;			//num_processed default to 1, which represent the first parameter passed in this call.
    int num_words_consumed = 1;		//actual word consumed here in method processing.
    int num_words_available = MIN(method_count, max_lookahead_words);
    size_t arg_count = 0;

    unsigned int i;
    unsigned int slot;
	uint32_t method = command_word & COMMAND_WORD_MASK_METHOD;

    PGRAPHState *pg = &d->pgraph;

    bool channel_valid =
        d->pgraph.pgraph_regs[NV_PGRAPH_CTX_CONTROL / 4] & NV_PGRAPH_CTX_CONTROL_CHID;
    assert(channel_valid);

    unsigned channel_id = GET_MASK(pg->pgraph_regs[NV_PGRAPH_CTX_USER / 4], NV_PGRAPH_CTX_USER_CHID);

    ContextSurfaces2DState *context_surfaces_2d = &pg->context_surfaces_2d;
    ImageBlitState *image_blit = &pg->image_blit;

    assert(subchannel < 8);
    //xbox d3d binds subchannel with graphic class in CDeivice_CreateDevice(). in the beginning,
    //set_object can be used to recognize the binding between graphic class and subchannel.
    //the arg0 is object handle,rather than an address here. need to use a function called ramht_look() to lookup the object entry.
    //quesiton is that the object was created and binded using xbx d3d routine which we didn't patch. need further study.
	if (method == NV_SET_OBJECT) {
		/*during init, arg0 = Handle of Miniport GraphicObject Handle,
		we must setup links between subchannel used here with the handle and the graphic class associate with that handle.
		the link could be changed dynamicaly using the NV_SET_OBJECT method.
		*/

		// get object entry from object handle.
		RAMHTEntry entry = ramht_lookup(d, arg0);
		assert(entry.valid);

		//xbox::addr_xt instance = entry.instance;
		// = &entry.instance;

		assert(entry.instance < d->pramin.ramin_size);
		uint8_t *obj_ptr = d->pramin.ramin_ptr + entry.instance;

		/* the engine is bound to the subchannel */
		assert(subchannel < 8);
		uint32_t *pull1 = &d->pfifo.regs[NV_PFIFO_CACHE1_PULL1 / 4];
		uint32_t *engine_reg = &d->pfifo.regs[NV_PFIFO_CACHE1_ENGINE / 4];

		SET_MASK(*engine_reg, 3 << (4 * subchannel), entry.engine);
		SET_MASK(*pull1, NV_PFIFO_CACHE1_PULL1_ENGINE, entry.engine);


		uint32_t ctx_1 = ldl_le_p((uint32_t*)obj_ptr);
		uint32_t ctx_2 = ldl_le_p((uint32_t*)(obj_ptr + 4));
		uint32_t ctx_3 = ldl_le_p((uint32_t*)(obj_ptr + 8));
		uint32_t ctx_4 = ldl_le_p((uint32_t*)(obj_ptr + 12));
		uint32_t ctx_5 = entry.instance;
		//these code below doesn't make sense, each NV_PGRAPH_CTX_CACHE only has 0x20 bytes, we have 8 subchannel, 0x20/8=4 bytes, that means 1 NV_PGRAPH_CTX_CACHE can only provide 1 DWORD to 1 subchannel.
		//somehow the original code pg->pgraph_regs[NV_PGRAPH_CTX_CACHE1 + subchannel *4  ] = ctx_1; really works, but in this new code it no longer works. need further study.
		pg->pgraph_regs[NV_PGRAPH_CTX_CACHE1 / 4 + subchannel] = ctx_1;
		pg->pgraph_regs[NV_PGRAPH_CTX_CACHE2 / 4 + subchannel] = ctx_2;
		pg->pgraph_regs[NV_PGRAPH_CTX_CACHE3 / 4 + subchannel] = ctx_3;
		pg->pgraph_regs[NV_PGRAPH_CTX_CACHE4 / 4 + subchannel] = ctx_4;
		pg->pgraph_regs[NV_PGRAPH_CTX_CACHE5 / 4 + subchannel] = ctx_5;
		/* //disable the lookup table setup code for now. the graphics_class setup is working.
		switch (arg0) {
		case D3D_KELVIN_PRIMITIVE:
			subchannel_to_graphic_class[subchannel] = NV_KELVIN_PRIMITIVE;
			break;
		case D3D_MEMORY_TO_MEMORY_COPY:
			subchannel_to_graphic_class[subchannel] = NV_MEMORY_TO_MEMORY_FORMAT;//should be copy, supposed to dma copy an image rect and change the pixel format in the same time. need further study.
			break;
		case D3D_RECTANGLE_COPY:
			subchannel_to_graphic_class[subchannel] = NV_IMAGE_BLIT;
			break;
		case D3D_RECTANGLE_COPY_SURFACES:
			subchannel_to_graphic_class[subchannel] = NV_CONTEXT_SURFACES_2D;
			break;
		case D3D_RECTANGLE_COPY_PATTERN:
			subchannel_to_graphic_class[subchannel] = NV_CONTEXT_PATTERN;
			break;
		default:
			assert(0);
			break;
		}
		*/
		argv[0] = entry.instance;
		arg0 = entry.instance;
	}
    // is this right? needs double check.
    pg->pgraph_regs[NV_PGRAPH_CTX_SWITCH1 / 4 ] = pg->pgraph_regs[NV_PGRAPH_CTX_CACHE1 / 4 + subchannel];
    pg->pgraph_regs[NV_PGRAPH_CTX_SWITCH2 / 4 ] = pg->pgraph_regs[NV_PGRAPH_CTX_CACHE2 / 4 + subchannel];
    pg->pgraph_regs[NV_PGRAPH_CTX_SWITCH3 / 4 ] = pg->pgraph_regs[NV_PGRAPH_CTX_CACHE3 / 4 + subchannel];
    pg->pgraph_regs[NV_PGRAPH_CTX_SWITCH4 / 4 ] = pg->pgraph_regs[NV_PGRAPH_CTX_CACHE4 / 4 + subchannel];
    pg->pgraph_regs[NV_PGRAPH_CTX_SWITCH5 / 4 ] = pg->pgraph_regs[NV_PGRAPH_CTX_CACHE5 / 4 + subchannel];

    //if the graphics_class doesn't work, we have to switch to use subchannel instead.
	uint32_t graphics_class = GET_MASK(pg->pgraph_regs[NV_PGRAPH_CTX_SWITCH1/4],
                                       NV_PGRAPH_CTX_SWITCH1_GRCLASS);
	// graphic_class now works, disable the lookup table for now.
	// graphics_class = subchannel_to_graphic_class[subchannel];
    // Logging is slow.. disable for now..
    //pgraph_log_method(subchannel, graphics_class, method, parameter);

    //if (subchannel != 0) {
        // catches context switching issues on xbox d3d
        // assert(graphics_class != 0x97); // no need to assert, this is normap for graphic classes other than KELVIN_PRIMITIVE
    //}

    /* ugly switch for now */
    //we shall switch the sub_channel instead of graphics_class.
    switch (graphics_class) {

        case NV_MEMORY_TO_MEMORY_FORMAT:
            switch (method) {
            case NV039_SET_OBJECT:
                ;//check whether we should add object instance for this class or not.
                break;
            }
            break;

        case NV_CONTEXT_PATTERN: {
            switch (method) {
            case NV044_SET_OBJECT:
                ;//check whether we should add object instance for this class or not.
                break;

            case NV044_SET_MONOCHROME_COLOR0:
                pg->pgraph_regs[NV_PGRAPH_PATT_COLOR0/4] = arg0;//why not using NV044_SET_MONOCHROME_COLOR0?
                break;

            }
        }   break;

        case NV_CONTEXT_SURFACES_2D: {
            switch (method) {
            case NV062_SET_OBJECT:
                context_surfaces_2d->object_instance = arg0;
                break;
            case NV062_SET_CONTEXT_DMA_IMAGE_SOURCE:
                context_surfaces_2d->dma_image_source = arg0;
                break;
            case NV062_SET_CONTEXT_DMA_IMAGE_DESTIN:
                context_surfaces_2d->dma_image_dest = arg0;
                break;
            case NV062_SET_COLOR_FORMAT:
                context_surfaces_2d->color_format = arg0;
                break;
            case NV062_SET_PITCH:
                context_surfaces_2d->source_pitch = arg0 & 0xFFFF;
                context_surfaces_2d->dest_pitch = arg0 >> 16;
                break;
            case NV062_SET_OFFSET_SOURCE:
                context_surfaces_2d->source_offset = arg0 & 0x07FFFFFF;
                break;
            case NV062_SET_OFFSET_DESTIN:
                context_surfaces_2d->dest_offset = arg0 & 0x07FFFFFF;
                break;
            default:
                EmuLog(LOG_LEVEL::WARNING, "Unknown NV_CONTEXT_SURFACES_2D Method: 0x%08X", method);
            }
    
            break; 
        }
    
        case NV_IMAGE_BLIT: {
            switch (method) {
                case NV09F_SET_OBJECT:
                    image_blit->object_instance = arg0;
                    break;
                case NV09F_SET_CONTEXT_SURFACES:
                    image_blit->context_surfaces = arg0;
                    break;
                case NV09F_SET_OPERATION:
                    image_blit->operation = arg0;
                    break;
                case NV09F_CONTROL_POINT_IN:
                    image_blit->in_x = arg0 & 0xFFFF;
                    image_blit->in_y = arg0 >> 16;
                    break;
                case NV09F_CONTROL_POINT_OUT:
                    image_blit->out_x = arg0 & 0xFFFF;
                    image_blit->out_y = arg0 >> 16;
                    break;
                case NV09F_SIZE:
                    image_blit->width = arg0 & 0xFFFF;
                    image_blit->height = arg0 >> 16;

                    /* I guess this kicks it off? */
                    if (image_blit->operation == NV09F_SET_OPERATION_SRCCOPY) {

                        NV2A_GL_DPRINTF(true, "NV09F_SET_OPERATION_SRCCOPY");

                        ContextSurfaces2DState *context_surfaces = context_surfaces_2d;
                        assert(context_surfaces->object_instance
                            == image_blit->context_surfaces);

                        unsigned int bytes_per_pixel;
                        switch (context_surfaces->color_format) {
                        case NV062_SET_COLOR_FORMAT_LE_Y8:
                            bytes_per_pixel = 1;
                            break;
                        case NV062_SET_COLOR_FORMAT_LE_R5G6B5:
                            bytes_per_pixel = 2;
                            break;
                        case NV062_SET_COLOR_FORMAT_LE_A8R8G8B8:
                            bytes_per_pixel = 4;
                            break;
                        default:
                            printf("Unknown blit surface format: 0x%x\n", context_surfaces->color_format);
                            assert(false);
                            break;
                        }

                        xbox::addr_xt source_dma_len, dest_dma_len;
                        uint8_t *source, *dest;

                        source = (uint8_t*)nv_dma_map(d, context_surfaces->dma_image_source,
                                                        &source_dma_len);
                        assert(context_surfaces->source_offset < source_dma_len);
                        source += context_surfaces->source_offset;

                        dest = (uint8_t*)nv_dma_map(d, context_surfaces->dma_image_dest,
                                                        &dest_dma_len);
                        assert(context_surfaces->dest_offset < dest_dma_len);
                        dest += context_surfaces->dest_offset;

                        NV2A_DPRINTF("  - 0x%tx -> 0x%tx\n", source - d->vram_ptr,
                                                                dest - d->vram_ptr);

                        unsigned int y;
                        for (y = 0; y<image_blit->height; y++) {
                            uint8_t *source_row = source
                                + (image_blit->in_y + y) * context_surfaces->source_pitch
                                + image_blit->in_x * bytes_per_pixel;

                            uint8_t *dest_row = dest
                                + (image_blit->out_y + y) * context_surfaces->dest_pitch
                                + image_blit->out_x * bytes_per_pixel;

                            memmove(dest_row, source_row,
                                image_blit->width * bytes_per_pixel);
                        }

                    } else {
                        assert(false);
                    }

                    break;
                default:
                    EmuLog(LOG_LEVEL::WARNING, "Unknown NV_IMAGE_BLIT Method: 0x%08X", method);
            }
            break;
        }

        //test case:xdk pushbuffer sample.
 
        case NV_KELVIN_PRIMITIVE: {

			//	 code to retrive object entry/instance from object handle.
			/* methods that take objects.
			* TODO: Check this range is correct for the nv2a */

			if (method >= 0x180 && method < 0x200) {
				for (int argc = 0; argc < method_count; argc++) {
					arg0 = argv[argc];
					//qemu_mutex_lock_iothread();
					RAMHTEntry entry = ramht_lookup(d, arg0);
					assert(entry.valid);
					// assert(entry.channel_id == state->channel_id);
					// copied the looked up entry.instance to the argv[], so the first round update to KelvinPrimitive could use the correct value.
					argv[argc] = entry.instance;

					//qemu_mutex_unlock_iothread();
				}
				// update arg0 to avoid assert();
				arg0 = argv[0];
			}

			// uint32_t previous_word = pg->regs[method / 4]; // TODO : Enable if the previous method register value is required
            //update struct KelvinPrimitive/array regs[] in first round, skip special cases. then we process those state variables if necessary in 2nd round.
            switch (method) { // TODO : Replace 'special cases' with check on (arg0 >> 29 == COMMAND_INSTRUCTION_NON_INCREASING_METHODS)
                //list all special cases here.
                //case NV097_SET_OBJECT:
                case NV097_HLE_API:
				case NV097_NO_OPERATION:	//this is used as short jump or interrupt, padding in front of fixups in order to make sure fixup will be applied before the instruction enter cache.
                //case NV097_SET_BEGIN_END://now we use pg->primitive_mode for PrititiveType state   //enclave subset of drawing instructions. need special handling.
				// NV097_ARRAY_ELEMENT32 is PUSH_INSTR_IMM_INC, test case: Otogi. it's logical since NV097_ARRAY_ELEMENT32 is used to transfer the last odd index, if there were one.
				case NV097_ARRAY_ELEMENT32: //PUSH_INSTR_IMM_INC
					break;
                case NV097_ARRAY_ELEMENT16: //PUSH_INSTR_IMM_NOINC
                case NV097_DRAW_ARRAYS:		//PUSH_INSTR_IMM_NOINC
                case NV097_INLINE_ARRAY:	//PUSH_INSTR_IMM_NOINC
                    assert(command_word >> 29 == COMMAND_INSTRUCTION_NON_INCREASING_METHODS); // All above commands should be non-increasing
					break;

                default:
                    assert(command_word >> 29 != COMMAND_INSTRUCTION_NON_INCREASING_METHODS); // All other commands should not be non-increasing
                    assert(command_word >> 29 == COMMAND_INSTRUCTION_INCREASING_METHODS); // Actually, all other commands should be increasing (as jumps and unknown bits shouldn't arrive here!)

#if 0
                    for (int argc = 0; argc < method_count; argc++) {
                        pg->regs[ method/4 + argc] = argv[argc];
                    }
#else
                    assert(method + (method_count * sizeof(uint32_t)) <= sizeof(pg->regs));

                    memcpy(&(pg->regs[method / 4]), argv, method_count * sizeof(uint32_t));
#endif
                    // Note : Writing to pg->regs[] will also reflect in unioned pg->KelvinPrimitive fields!
                    break;
            }
		 
            //2nd round, handle special cases, setup bit mask flags, setup pgraph internal state vars, 
            switch (method) {
                case NV097_SET_OBJECT://done
                    break;
                case NV097_HLE_API:
                {
                    X_D3DAPI_ENUM HLEApi;
                    HLEApi = (X_D3DAPI_ENUM)argv[0];
                    switch (HLEApi)
                    {
                    //case X_CDevice_SetStateUP:  break;	case X_CDevice_SetStateUP_4:  break;	case X_CDevice_SetStateUP_0__LTCG_esi1:  break;
                    //case X_CDevice_SetStateVB:  break;	case X_CDevice_SetStateVB_8:  break;
                    case X_D3DDevice_ApplyStateBlock:  break;
                    case X_D3DDevice_Begin: CxbxrImpl_Begin((xbox::X_D3DPRIMITIVETYPE)argv[1]); break;
                    case X_D3DDevice_BeginPush:  break;	case X_D3DDevice_BeginPush_4:  break;	case X_D3DDevice_BeginPush_8:  break;
                    case X_D3DDevice_BeginPushBuffer:  break;
                    case X_D3DDevice_BeginScene:  break;
                    case X_D3DDevice_BeginState:  break;	case X_D3DDevice_BeginStateBig:  break;
                    case X_D3DDevice_BeginStateBlock:  break;
                    case X_D3DDevice_BeginVisibilityTest:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_BlockOnFence:  break;
                    case X_D3DDevice_BlockUntilIdle:  break;
                    case X_D3DDevice_BlockUntilVerticalBlank:
                        CxbxrImpl_BlockUntilVerticalBlank();
                        break;
                    case X_D3DDevice_CaptureStateBlock:  break;
                    case X_D3DDevice_Clear:
                        CxbxrImpl_Clear((xbox::dword_xt) argv[1], (D3DRECT *)argv[2], (xbox::dword_xt) argv[3], (D3DCOLOR) argv[4], DWtoF(argv[5]), (xbox::dword_xt) argv[6]);
                        break;
                    case X_D3DDevice_CopyRects:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_CreateCubeTexture:  break;
                    case X_D3DDevice_CreateDepthStencilSurface:  break;
                    case X_D3DDevice_CreateFixup:  break;
                    case X_D3DDevice_CreateImageSurface:  break;
                    case X_D3DDevice_CreateIndexBuffer:  break;
                    case X_D3DDevice_CreatePalette:  break;
                    case X_D3DDevice_CreatePixelShader:  break;
                    case X_D3DDevice_CreatePushBuffer:  break;
                    case X_D3DDevice_CreateRenderTarget:  break;
                    case X_D3DDevice_CreateStateBlock:  break;
                    case X_D3DDevice_CreateTexture:  break;
                    case X_D3DDevice_CreateTexture2:  break;
                    case X_D3DDevice_CreateVertexBuffer:  break;
                    case X_D3DDevice_CreateVertexBuffer2:  break;
                    case X_D3DDevice_CreateVertexShader:  break;
                    case X_D3DDevice_CreateVolumeTexture:  break;
                    case X_D3DDevice_DeletePatch:  break;
                    case X_D3DDevice_DeletePixelShader:  break;
                    case X_D3DDevice_DeleteStateBlock:  break;
                    case X_D3DDevice_DeleteVertexShader:  break;
                    case X_D3DDevice_DeleteVertexShader_0:  break;
                    case X_D3DDevice_DrawIndexedPrimitive:  //break;  //fall through
                    case X_D3DDevice_DrawIndexedPrimitiveUP:  //break;  //fall through
                    case X_D3DDevice_DrawIndexedVertices:  //break;  //fall through
                    case X_D3DDevice_DrawIndexedVerticesUP:  //break;  //fall through
                    case X_D3DDevice_DrawPrimitive:  //break;  //fall through
                    case X_D3DDevice_DrawPrimitiveUP:  //break;  //fall through
                    case X_D3DDevice_DrawRectPatch:  //break;  //fall through
                    case X_D3DDevice_DrawTriPatch:  //break;  //fall through
                    case X_D3DDevice_DrawVertices:  //break;  //fall through
                    case X_D3DDevice_DrawVertices_4__LTCG_ecx2_eax3:  //break;  //fall through
                    case X_D3DDevice_DrawVertices_8__LTCG_eax3:  //break;  //fall through
                    case X_D3DDevice_DrawVerticesUP:  //break;  //fall through
                    case X_D3DDevice_DrawVerticesUP_12__LTCG_ebx3: {
                        extern void CxbxUpdateNativeD3DResources();
                        CxbxUpdateNativeD3DResources();
                        *(bool*)argv[1] = false;
                    }break;
                    case X_D3DDevice_EnableOverlay:  break;
                    case X_D3DDevice_End:
                        CxbxrImpl_End();
                        break;
                    case X_D3DDevice_EndPush:  break;
                    case X_D3DDevice_EndPushBuffer:  break;
                    case X_D3DDevice_EndScene:  break;
                    case X_D3DDevice_EndState:  break;
                    case X_D3DDevice_EndStateBlock:  break;
                    case X_D3DDevice_EndVisibilityTest:  //break;  //fall through
                    case X_D3DDevice_EndVisibilityTest_0:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_FlushVertexCache:  break;
                    case X_D3DDevice_GetBackBuffer:  break;
                    case X_D3DDevice_GetBackBuffer2:  break;
                    case X_D3DDevice_GetBackBuffer2_0__LTCG_eax1:  break;
                    case X_D3DDevice_GetBackBufferScale:  break;
                    case X_D3DDevice_GetBackMaterial:  break;
                    case X_D3DDevice_GetCopyRectsState:  break;
                    case X_D3DDevice_GetCreationParameters:  break;
                    case X_D3DDevice_GetDebugMarker:  break;
                    case X_D3DDevice_GetDepthClipPlanes:  break;
                    case X_D3DDevice_GetDepthStencilSurface:  break;
                    case X_D3DDevice_GetDepthStencilSurface2:  break;
                    case X_D3DDevice_GetDeviceCaps:  break;
                    case X_D3DDevice_GetDirect3D:  break;
                    case X_D3DDevice_GetDisplayFieldStatus:  break;
                    case X_D3DDevice_GetDisplayMode:  break;
                    case X_D3DDevice_GetGammaRamp:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_GetIndices:  break;
                    case X_D3DDevice_GetLight:  break;
                    case X_D3DDevice_GetLightEnable:  break;
                    case X_D3DDevice_GetMaterial:  break;
                    case X_D3DDevice_GetModelView:  break;
                    case X_D3DDevice_GetOverlayUpdateStatus:  break;
                    case X_D3DDevice_GetOverscanColor:  break;
                    case X_D3DDevice_GetPalette:  break;
                    case X_D3DDevice_GetPersistedSurface:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_GetPixelShader:  break;
                    case X_D3DDevice_GetPixelShaderConstant:  break;
                    case X_D3DDevice_GetPixelShaderFunction:  break;
                    case X_D3DDevice_GetProjectionViewportMatrix:  break;
                    case X_D3DDevice_GetPushBufferOffset:  break;
                    case X_D3DDevice_GetPushDistance:  break;
                    case X_D3DDevice_GetRasterStatus:  break;
                    case X_D3DDevice_GetRenderState:  break;
                    case X_D3DDevice_GetRenderTarget:  break;
                    case X_D3DDevice_GetRenderTarget2:  break;
                    case X_D3DDevice_GetScissors:  break;
                    case X_D3DDevice_GetScreenSpaceOffset:  break;
                    case X_D3DDevice_GetShaderConstantMode:  break;
                    case X_D3DDevice_GetStipple:  break;
                    case X_D3DDevice_GetStreamSource:  break;
                    case X_D3DDevice_GetTexture:  break;
                    case X_D3DDevice_GetTextureStageState:  break;
                    case X_D3DDevice_GetTile:  break;
                    case X_D3DDevice_GetTileCompressionTags:  break;
                    case X_D3DDevice_GetTransform:  break;
                    case X_D3DDevice_GetVertexBlendModelView:  break;
                    case X_D3DDevice_GetVertexShader:  break;
                    case X_D3DDevice_GetVertexShaderConstant:  break;
                    case X_D3DDevice_GetVertexShaderDeclaration:  break;
                    case X_D3DDevice_GetVertexShaderFunction:  break;
                    case X_D3DDevice_GetVertexShaderInput:  break;
                    case X_D3DDevice_GetVertexShaderSize:  break;
                    case X_D3DDevice_GetVertexShaderType:  break;
                    case X_D3DDevice_GetViewport:  break;
                    case X_D3DDevice_GetViewportOffsetAndScale:  break;
                    case X_D3DDevice_GetVisibilityTestResult:  break;
                    case X_D3DDevice_InsertCallback:  break;
                    case X_D3DDevice_InsertFence:  break;
                    case X_D3DDevice_IsBusy:  break;
                    case X_D3DDevice_IsFencePending:  break;
                    case X_D3DDevice_KickPushBuffer:  break;
                    case X_D3DDevice_LightEnable:
                        CxbxrImpl_LightEnable((xbox::dword_xt) /* Index */ argv[1], (xbox::bool_xt) /* bEnable */ argv[2]);
                        break;
                    case X_D3DDevice_LoadVertexShader:       // break;
                    case X_D3DDevice_LoadVertexShader_0__LTCG_eax_Address_ecx_Handle:       // break;  //fall through
                    case X_D3DDevice_LoadVertexShader_0__LTCG_eax_Address_edx_Handle:       // break;  //fall through
                    case X_D3DDevice_LoadVertexShader_4:                                    // break;  //fall through
                        CxbxrImpl_LoadVertexShader(argv[1], argv[2]);
                        break;
                    case X_D3DDevice_LoadVertexShaderProgram:
                        CxbxrImpl_LoadVertexShaderProgram((DWORD * )argv[1], argv[2]);
                        break;
                    case X_D3DDevice_MultiplyTransform:  break;
                    case X_D3DDevice_Nop:  break;
                    case X_D3DDevice_PersistDisplay:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_Present:
#define CXBX_SWAP_PRESENT_FORWARD (256 + xbox::X_D3DSWAP_FINISH + xbox::X_D3DSWAP_COPY) // = CxbxPresentForwardMarker + D3DSWAP_FINISH + D3DSWAP_COPY

                        //CxbxrImpl_Swap(CXBX_SWAP_PRESENT_FORWARD);
                        * (bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_PrimeVertexCache:  break;
                    case X_D3DDevice_Reset:               //break;  //fall through
                    case X_D3DDevice_Reset_0__LTCG_edi1:  //break;  //fall through
                    case X_D3DDevice_Reset_0__LTCG_ebx1:
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_RunPushBuffer:
                        //todo: RunPushbuffer() could be nested, here we assume it only runs in one level. 
                        if (argv[1] != 0) {
                            //NV2A_stateFlags |= X_STATE_RUNPUSHBUFFERWASCALLED;
                            pgraph_SetNV2AStateFlag(X_STATE_RUNPUSHBUFFERWASCALLED);
                        }
                        else {
                            //NV2A_stateFlags &= !X_STATE_RUNPUSHBUFFERWASCALLED;
                            pgraph_ClearNV2AStateFlag(X_STATE_RUNPUSHBUFFERWASCALLED);
                        }
                        break;
                    case X_D3DDevice_RunVertexStateShader:
                        CxbxrImpl_RunVertexStateShader(argv[1], (xbox::float_xt*)argv[2]);
                        break;
                    case X_D3DDevice_SelectVertexShader:                  // break;
                    case X_D3DDevice_SelectVertexShader_0__LTCG_eax1_ebx2: // break;
                    case X_D3DDevice_SelectVertexShader_4__LTCG_eax1:
                        CxbxrImpl_SelectVertexShader(argv[1], argv[2]);
                        break;
                    case X_D3DDevice_SelectVertexShaderDirect:  break;
                    case X_D3DDevice_SetBackBufferScale:
                        CxbxrImpl_SetBackBufferScale((xbox::float_xt) DWtoF(argv[1]), (xbox::float_xt) DWtoF(argv[2]));
                        break;
                    case X_D3DDevice_SetBackMaterial:  break;
                    case X_D3DDevice_SetCopyRectsState:  break;
                    case X_D3DDevice_SetDebugMarker:  break;
                    case X_D3DDevice_SetDepthClipPlanes:  break;
                    case X_D3DDevice_SetFlickerFilter:  break;
                    case X_D3DDevice_SetFlickerFilter_0:  break;
                    case X_D3DDevice_SetGammaRamp:
                        CxbxrImpl__SetGammaRamp((xbox::dword_xt)/* dwFlags*/argv[1], (D3DGAMMARAMP*)/* pRamp*/argv[2]);
                        *(bool*)argv[3] = false;
                        break;
                    case X_D3DDevice_SetIndices:  //break;  //fall through
                    case X_D3DDevice_SetIndices_4:
                        CxbxrImpl_SetIndices((xbox::X_D3DIndexBuffer *)/* pIndexData */argv[1], (xbox::uint_xt) /* BaseVertexIndex */ argv[2]);
                        break;
                    case X_D3DDevice_SetLight:
                        CxbxrImpl_SetLight((xbox::dword_xt) /* Index */ argv[1], (CONST xbox::X_D3DLIGHT8*) /* pLight */ argv[2]);
                        break;
                    case X_D3DDevice_SetMaterial:
                        CxbxrImpl_SetMaterial((CONST xbox::X_D3DMATERIAL8 *)/* pMaterial */argv[1]);
                        break;
                    case X_D3DDevice_SetModelView:
                        CxbxrImpl_SetModelView((CONST D3DMATRIX *)/*pModelView*/argv[1], (CONST D3DMATRIX *)/*pInverseModelView*/argv[2], (CONST D3DMATRIX *) /*pComposite*/argv[3]);
                        break;
                    case X_D3DDevice_SetOverscanColor:  break;
                    case X_D3DDevice_SetPalette:  //break;  //fall through
                    case X_D3DDevice_SetPalette_4:
                        CxbxrImpl_SetPalette((xbox::dword_xt)/* Stage */argv[1], (xbox::X_D3DPalette *)/* pPalette */argv[2]);
                        break;
                    case X_D3DDevice_SetPixelShader:  //break;  //fall through
                    case X_D3DDevice_SetPixelShader_0__LTCG_eax_handle:
                        CxbxrImpl_SetPixelShader((xbox::dword_xt)/*Handle*/argv[1]);
                        break;
                    case X_D3DDevice_SetPixelShaderConstant:  break;
                    case X_D3DDevice_SetPixelShaderConstant_4:  break;
                    case X_D3DDevice_SetPixelShaderProgram:  break;
                    case X_D3DDevice_SetRenderState:  break;
                    case X_D3DDevice_SetRenderState_Simple:  break;
                    case X_D3DDevice_SetRenderStateNotInline:  break;
                    case X_D3DDevice_SetRenderTarget:    //break;  //fall through
                    case X_D3DDevice_SetRenderTarget_0:  //break;  //fall through
                    case X_D3DDevice_SetRenderTargetFast:
                        CxbxrImpl_SetRenderTarget((xbox::X_D3DSurface*)argv[1], (xbox::X_D3DSurface*)argv[2]);
                        // release reference to the surfaces since we add extra references to them in the patched SetRenderTarget()
                        CxbxrImpl_ReleaseRenderTarget((xbox::X_D3DSurface*)argv[1], (xbox::X_D3DSurface*)argv[2]);
                        break;
                    case X_D3DDevice_SetScissors:  break;
                    case X_D3DDevice_SetScreenSpaceOffset:
                        CxbxrImpl_SetScreenSpaceOffset(DWtoF(argv[1]), DWtoF(argv[2]));
                        break;
                    case X_D3DDevice_SetShaderConstantMode:  //break;
                    case X_D3DDevice_SetShaderConstantMode_0__LTCG_eax1:
                        CxbxrImpl_SetShaderConstantMode((xbox::X_VERTEXSHADERCONSTANTMODE )argv[1]);
                        break;
                    case X_D3DDevice_SetSoftDisplayFilter:  break;
                    case X_D3DDevice_SetStipple:  break;
                    case X_D3DDevice_SetStreamSource:  //break;
                    case X_D3DDevice_SetStreamSource_0__LTCG_eax_StreamNumber_edi_pStreamData_ebx_Stride:  //break;
                    case X_D3DDevice_SetStreamSource_4:  //break;
                    case X_D3DDevice_SetStreamSource_8:  //break;
                    case X_D3DDevice_SetStreamSource_8__LTCG_edx_StreamNumber:
                        CxbxrImpl_SetStreamSource((UINT) argv[1], (xbox::X_D3DVertexBuffer *)argv[2], (UINT)argv[3]);
                        break;
                    case X_D3DDevice_SetSwapCallback:  break;
                    case X_D3DDevice_SetTexture:
                        CxbxrImpl_SetTexture((xbox::dword_xt) argv[1], (xbox::X_D3DBaseTexture *) argv[2]);
                        break;
                    case X_D3DDevice_SetTexture_4__LTCG_eax_pTexture:  break;
                    case X_D3DDevice_SetTexture_4__LTCG_eax_Stage:  break;
                    case X_D3DDevice_SetTextureStageState:  break;
                    case X_D3DDevice_SetTextureStageStateNotInline:  break;
                    case X_D3DDevice_SetTile:  break;
                    case X_D3DDevice_SetTimerCallback:  break;
                    case X_D3DDevice_SetTransform:  //break;//fall throught
                    case X_D3DDevice_SetTransform_0__LTCG_eax1_edx2:
                        CxbxrImpl_SetTransform((xbox::X_D3DTRANSFORMSTATETYPE) argv[1], (CONST D3DMATRIX * )argv[2]);
                        break;
                    case X_D3DDevice_SetVertexBlendModelView:  break;
                    case X_D3DDevice_SetVertexData2f:  break;
                    case X_D3DDevice_SetVertexData2s:  break;
                    case X_D3DDevice_SetVertexData4f:
                        CxbxrImpl_SetVertexData4f(argv[1],DWtoF(argv[2]), DWtoF(argv[3]), DWtoF(argv[4]), DWtoF(argv[5]));
                        break;
                    case X_D3DDevice_SetVertexData4f_16:  break;
                    case X_D3DDevice_SetVertexData4s:  break;
                    case X_D3DDevice_SetVertexData4ub:  break;
                    case X_D3DDevice_SetVertexDataColor:  break;
                    case X_D3DDevice_SetVertexShader:   // break;
                    case X_D3DDevice_SetVertexShader_0:
                        CxbxrImpl_SetVertexShader((DWORD)argv[1]);
                        break;
                    case X_D3DDevice_SetVertexShaderConstant:  break;
                    case X_D3DDevice_SetVertexShaderConstant_8:  break;
                    case X_D3DDevice_SetVertexShaderConstant1:  break;
                    case X_D3DDevice_SetVertexShaderConstant4:  break;
                    case X_D3DDevice_SetVertexShaderConstantFast:  break;
                    case X_D3DDevice_SetVertexShaderConstant1Fast:  break;
                    case X_D3DDevice_SetVertexShaderConstantNotInline:  break;
                    case X_D3DDevice_SetVertexShaderConstantNotInlineFast:  break;
                    case X_D3DDevice_SetVertexShaderInput:  break;
                    case X_D3DDevice_SetVertexShaderInputDirect:  break;
                    case X_D3DDevice_SetVerticalBlankCallback:  break;
                    case X_D3DDevice_SetViewport:
                        // todo: special case:argv[1] is actually pointing to PBTokenArray[3] where PBTokenArray[] would be over written by other patched HLE apis so we have to preserve the viewport and call CxbxImpl_SetViewport() with the preserved viewport.
                        extern xbox::X_D3DVIEWPORT8 HLEViewport;
                        if (argv[1] != 0) {
                            HLEViewport = *(xbox::X_D3DVIEWPORT8*)&argv[2];
                            CxbxrImpl_SetViewport((xbox::X_D3DVIEWPORT8*)&HLEViewport);
                        }
                        CxbxrImpl_SetViewport((xbox::X_D3DVIEWPORT8 * )argv[1]);
                        break;
                    case X_D3DDevice_SetWaitCallback:  break;
                    case X_D3DDevice_Swap:
                        //CxbxrImpl_Swap(argv[1]);
                        //break; //fall through
                    case X_D3DDevice_Swap_0:
                        //CxbxrImpl_Swap(argv[1]);
                        *(bool*)argv[1] = false;
                        break;
                    case X_D3DDevice_SwitchTexture:
                        CxbxrImpl_SwitchTexture((xbox::dword_xt)argv[1], (xbox::dword_xt)argv[2], (xbox::dword_xt)argv[3]);
                        break;
                    case X_D3DDevice_UpdateOverlay:  break;
                    case X_D3DResource_BlockUntilNotBusy:  break;
                    case X_D3D_BlockOnTime:  break;	case X_D3D_BlockOnTime_4:  break;
                    case X_D3D_CommonSetRenderTarget:
                        //todo:this might be redundant because the HLE implementation of this api never set the call level, so this patch will always calls CxbxrImpl_SetRenderTarget(). we might use the fall through directly.
                        CxbxrImpl_D3D_CommonSetRenderTarget((xbox::X_D3DSurface*)/* pRenderTarget*/argv[1], (xbox::X_D3DSurface*)/* pNewZStencil*/argv[2], (void*)/* unknown*/argv[3]);
                        // release reference to the surfaces since we add extra references to them in the patched SetRenderTarget()
                        CxbxrImpl_ReleaseRenderTarget((xbox::X_D3DSurface*)argv[1], (xbox::X_D3DSurface*)argv[2]);
                        break;
                    case X_D3D_DestroyResource:  //break;
                    case X_D3D_DestroyResource__LTCG:
                        CxbxrImpl_DestroyResource((xbox::X_D3DResource *) argv[1]);
                        break;
                    case X_D3D_LazySetPointParams:  break;
                    case X_D3D_SetCommonDebugRegisters:  break;
                    case X_Direct3D_CreateDevice:  break;
                    case X_Direct3D_CreateDevice_16__LTCG_eax_BehaviorFlags_ebx_ppReturnedDeviceInterface:  break;
                    case X_Direct3D_CreateDevice_16__LTCG_eax_BehaviorFlags_ecx_ppReturnedDeviceInterface:  break;
                    case X_Direct3D_CreateDevice_4:  break;
                    case X_Lock2DSurface:
                        //CxbxrImpl_Lock2DSurface((xbox::X_D3DPixelContainer *) /*pPixelContainer*/argv[1], (D3DCUBEMAP_FACES)/* FaceType*/argv[2], (xbox::uint_xt)/* Level*/argv[3], (D3DLOCKED_RECT *)/* pLockedRect*/argv[3], (RECT *)/* pRect*/argv[5], (xbox::dword_xt)/* Flags*/argv[6]);
                        *(bool*)argv[1] = false;
                        break;
                    case X_Lock3DSurface:
                        //CxbxrImpl_Lock3DSurface((xbox::X_D3DPixelContainer*)/* pPixelContainer*/argv[1], (xbox::uint_xt)/*Level*/argv[2], (D3DLOCKED_BOX*)/* pLockedVolume*/argv[3], (D3DBOX*)/* pBox*/argv[4], (xbox::dword_xt)/*Flags*/argv[5]);
                        *(bool*)argv[1] = false;
                        break;
                    default:break;
                    }
                }
                    break;
                case NV097_NO_OPERATION://done
                    /* The bios uses nop as a software method call -
                     * it seems to expect a notify interrupt if the parameter isn't 0.
                     * According to a nouveau guy it should still be a nop regardless
                     * of the parameter. It's possible a debug register enables this,
                     * but nothing obvious sticks out. Weird.

					* when arg0 !=0, this is an interrupt/call back operation
					* arg0=operation
					* WRITE_REGISTER_VALUE(9), from Otogi SetDebugRegisters(register, value)
					*     (NV097_SET_ZSTENCIL_CLEAR_VALUE,register, method_cout=1)
					*     (NV097_SET_COLOR_CLEAR_VALUE,value to write,  method_cout=1)
					  
					* READ_CALLBACK(7), WRITE_CALLBACK(6), from Otogi D3DDevice_InsertCallback()
					*     (NV097_SET_ZSTENCIL_CLEAR_VALUE 0x00001D8C,  READ_CALLBACK Data,  method_cout=1)
					*     (NV097_SET_COLOR_CLEAR_VALUE 0x00001D90,  READ_CALLBACK Context,  method_cout=1)
					* 
					* xboe d3d instruction pattern
					
					* (NV097_SET_ZSTENCIL_CLEAR_VALUE, method_cout=2)
					* argv[]={arg0, arg1,} //arg1 will over write NV097_SET_COLOR_CLEAR_VALUE

					* (NV097_NO_OPERATION,method_cout=1 )
					* argv[]={Operation,}
					*
					
					 */
                    switch (arg0) {
                    case NVX_FLIP_IMMEDIATE: break;
                    case NVX_FLIP_SYNCHRONIZED: break;
                    case NVX_PUSH_BUFFER_RUN: break;
                    case NVX_PUSH_BUFFER_FIXUP: break;
                    case NVX_FENCE: break;
                    case NVX_READ_CALLBACK:
                        CxbxrImpl_InsertCallback(xbox::X_D3DCALLBACK_READ,(xbox::X_D3DCALLBACK) pg->KelvinPrimitive.SetZStencilClearValue, pg->KelvinPrimitive.SetColorClearValue);
                        break;
                    case NVX_WRITE_CALLBACK:
                        CxbxrImpl_InsertCallback(xbox::X_D3DCALLBACK_WRITE, (xbox::X_D3DCALLBACK)pg->KelvinPrimitive.SetZStencilClearValue, pg->KelvinPrimitive.SetColorClearValue);
                        break;
                    case NVX_DXT1_NOISE_ENABLE://value stores in NV097_SET_ZSTENCIL_CLEAR_VALUE  D3DRS_DXT1NOISEENABLE //KelvinPrimitive.
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_DXT1NOISEENABLE, pg->KelvinPrimitive.SetZStencilClearValue);
                        break;
                    case NVX_WRITE_REGISTER_VALUE:
                        switch ((pg->KelvinPrimitive.SetZStencilClearValue&0xffff)) {
                        case NV_PGRAPH_DEBUG_5://D3DRS_DONOTCULLUNCOMPRESSED
                            if((pg->KelvinPrimitive.SetColorClearValue& NV_PGRAPH_DEBUG_5_ZCULL_RETURN_COMP_ENABLED)!=0)
                                NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_DONOTCULLUNCOMPRESSED, 1);
                            else
                                NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_DONOTCULLUNCOMPRESSED, 0);
                            break;
                        case NV_PGRAPH_DEBUG_6://D3DRS_ROPZCMPALWAYSREAD D3DRS_ROPZREAD
                            if ((pg->KelvinPrimitive.SetColorClearValue & NV_PGRAPH_DEBUG_6_ROP_ZCMP_ALWAYS_READ_ENABLED) != 0)
                                NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ROPZCMPALWAYSREAD, 1);
                            else
                                NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ROPZCMPALWAYSREAD, 0);

                            if ((pg->KelvinPrimitive.SetColorClearValue & NV_PGRAPH_DEBUG_6_ROP_ZREAD_FORCE_ZREAD) != 0)
                                NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ROPZREAD, 1);
                            else
                                NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ROPZREAD, 0);
                            
                            break;

                        }


                        break;
                    }
                    if (arg0 != 0) {
						
						//disable the original code. now we know how this code shall work. but it's not implement yet.
						/*
						assert(!(pg->pending_interrupts & NV_PGRAPH_INTR_ERROR));

                        SET_MASK(pg->pgraph_regs[NV_PGRAPH_TRAPPED_ADDR / 4],
                            NV_PGRAPH_TRAPPED_ADDR_CHID, channel_id);
                        SET_MASK(pg->pgraph_regs[NV_PGRAPH_TRAPPED_ADDR / 4],
                            NV_PGRAPH_TRAPPED_ADDR_SUBCH, subchannel);
                        SET_MASK(pg->pgraph_regs[NV_PGRAPH_TRAPPED_ADDR / 4],
                            NV_PGRAPH_TRAPPED_ADDR_MTHD, method);
                        pg->pgraph_regs[NV_PGRAPH_TRAPPED_DATA_LOW / 4] = arg0;
                        pg->pgraph_regs[NV_PGRAPH_NSOURCE / 4] = NV_PGRAPH_NSOURCE_NOTIFICATION; // TODO: check this 
                        pg->pending_interrupts |= NV_PGRAPH_INTR_ERROR;

                        qemu_mutex_unlock(&pg->pgraph_lock);
                        qemu_mutex_lock_iothread();
                        update_irq(d);
                        qemu_mutex_lock(&pg->pgraph_lock);
                        qemu_mutex_unlock_iothread();

                        while (pg->pending_interrupts & NV_PGRAPH_INTR_ERROR) {
                            qemu_cond_wait(&pg->interrupt_cond, &pg->pgraph_lock);
                        }
						*/
                    }
                    num_words_consumed = method_count; //test case: xdk pushbuffer sample. 3rd method from file is NOP with method count 0x81.
                    break;

                case NV097_WAIT_FOR_IDLE://done  //this method is used to wait for NV2A state machine to sync to pushbuffer.
                    //pgraph_update_surface(d, false, true, true);
                    break;

                case NV097_SET_FLIP_READ://done  //pg->KelvinPrimitive.SetFlipRead
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SURFACE / 4], NV_PGRAPH_SURFACE_READ_3D,
                    //    arg0);
                    break;
                case NV097_SET_FLIP_WRITE://done  //pg->KelvinPrimitive.SetFlipWrite
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SURFACE / 4], NV_PGRAPH_SURFACE_WRITE_3D,
                    //    arg0);
                    break; 
                case NV097_SET_FLIP_MODULO://done  //pg->KelvinPrimitive.SetFlipModulo
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SURFACE / 4], NV_PGRAPH_SURFACE_MODULO_3D,
                    //    arg0);
                    break;
                case NV097_FLIP_INCREMENT_WRITE: {//done
                    NV2A_DPRINTF("flip increment write %d -> ",
						pg->KelvinPrimitive.SetFlipWrite);
					pg->KelvinPrimitive.SetFlipWrite=
                        (pg->KelvinPrimitive.SetFlipWrite + 1)
                        % pg->KelvinPrimitive.SetFlipModulo;
                    NV2A_DPRINTF("%d\n",
						pg->KelvinPrimitive.SetFlipWrite);
        #ifdef __APPLE__
                    if (glFrameTerminatorGREMEDY) {
                        glFrameTerminatorGREMEDY();
						}
        #endif // __APPLE__
						break;
					}
				case NV097_FLIP_STALL://done
                    pgraph_update_surface(d, false, true, true);
                    // TODO: Fix this (why does it hang?)
                    /* while (true) */ {
                        //uint32_t surface = pg->pgraph_regs[NV_PGRAPH_SURFACE / 4];
                        NV2A_DPRINTF("flip stall read: %d, write: %d, modulo: %d\n",
							pg->KelvinPrimitive.SetFlipRead,
							pg->KelvinPrimitive.SetFlipWrite,
							pg->KelvinPrimitive.SetFlipModulo);

                        if (pg->KelvinPrimitive.SetFlipRead
                            != pg->KelvinPrimitive.SetFlipWrite) {
                            break;
                        }
                    }
                    // TODO: Remove this when the AMD crash is solved in vblank_thread
                    NV2ADevice::UpdateHostDisplay(d);
                    NV2A_DPRINTF("flip stall done\n");
                    break;

                case NV097_SET_CONTEXT_DMA_NOTIFIES://done
                    break;
                case NV097_SET_CONTEXT_DMA_A://done
                    break;
                case NV097_SET_CONTEXT_DMA_B://done
                    break;
                case NV097_SET_CONTEXT_DMA_STATE://done
                    break;
                case NV097_SET_CONTEXT_DMA_COLOR://done
                    /* try to get any straggling draws in before the surface's changed :/ */
                    pgraph_update_surface(d, false, true, true); // TODO : Move actions-before-writes to, well : before the (generic) write!
                    break;
                case NV097_SET_CONTEXT_DMA_ZETA://done
                    break;
                case NV097_SET_CONTEXT_DMA_VERTEX_A://done
                    break;
                case NV097_SET_CONTEXT_DMA_VERTEX_B://done
                    break;
                case NV097_SET_CONTEXT_DMA_SEMAPHORE://done
                    break;
                case NV097_SET_CONTEXT_DMA_REPORT://done
                    break;

                case NV097_SET_SURFACE_CLIP_HORIZONTAL://done KelvinPrimitive.SetSurfaceClipHorizontal could use union with word ClipX, ClipWidth
                    pgraph_update_surface(d, false, true, true);
                    pg->surface_shape.clip_x =
                        GET_MASK(arg0, NV097_SET_SURFACE_CLIP_HORIZONTAL_X);
                    pg->surface_shape.clip_width =
                        GET_MASK(arg0, NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH);
                    break;
                case NV097_SET_SURFACE_CLIP_VERTICAL://done KelvinPrimitive.SetSurfaceClipVertical could use union with word ClipY, ClipHeight
                    pgraph_update_surface(d, false, true, true);
                    pg->surface_shape.clip_y =
                        GET_MASK(arg0, NV097_SET_SURFACE_CLIP_VERTICAL_Y);
                    pg->surface_shape.clip_height =
                        GET_MASK(arg0, NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT);
                    break;
                case NV097_SET_SURFACE_FORMAT://done
                    pgraph_update_surface(d, false, true, true);
                    pg->surface_shape.color_format =
                        GET_MASK(arg0, NV097_SET_SURFACE_FORMAT_COLOR);
                    pg->surface_shape.zeta_format =
                        GET_MASK(arg0, NV097_SET_SURFACE_FORMAT_ZETA);
                    pg->surface_type =
                        GET_MASK(arg0, NV097_SET_SURFACE_FORMAT_TYPE);
                    pg->surface_shape.anti_aliasing =
                        GET_MASK(arg0, NV097_SET_SURFACE_FORMAT_ANTI_ALIASING);
                    pg->surface_shape.log_width =
                        GET_MASK(arg0, NV097_SET_SURFACE_FORMAT_WIDTH);
                    pg->surface_shape.log_height =
                        GET_MASK(arg0, NV097_SET_SURFACE_FORMAT_HEIGHT);
                    break;
                case NV097_SET_SURFACE_PITCH://done
                    pgraph_update_surface(d, false, true, true);
                    pg->surface_color.pitch =
                        GET_MASK(arg0, NV097_SET_SURFACE_PITCH_COLOR);
                    pg->surface_zeta.pitch =
                        GET_MASK(arg0, NV097_SET_SURFACE_PITCH_ZETA);
                    pg->surface_color.buffer_dirty = true;
                    pg->surface_zeta.buffer_dirty = true;
                    break;
                case NV097_SET_SURFACE_COLOR_OFFSET://done
                    pgraph_update_surface(d, false, true, true);
                    pg->surface_color.offset = arg0;
                    pg->surface_color.buffer_dirty = true;
                    break;
                case NV097_SET_SURFACE_ZETA_OFFSET://done
                    pgraph_update_surface(d, false, true, true);
                    pg->surface_zeta.offset = arg0;
                    pg->surface_zeta.buffer_dirty = true;
                    break;

                CASE_8(NV097_SET_COMBINER_ALPHA_ICW, 4) ://done
                    //slot = (method - NV097_SET_COMBINER_ALPHA_ICW) / 4;
                    //pg->pgraph_regs[NV_PGRAPH_COMBINEALPHAI0/4 + slot * 4] = arg0;
					// clear combiner need specular flag once we got hit here. this is the very first method to update pixel shader
					pgraph_ClearNV2AStateFlag(X_STATE_COMBINERNEEDSSPECULAR);
				    // set combiner dirty flag
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
				    break;

                case NV097_SET_COMBINER_SPECULAR_FOG_CW0://done
                case NV097_SET_COMBINER_SPECULAR_FOG_CW1://done
					NV2A_DirtyFlags|=X_D3DDIRTYFLAG_SPECFOG_COMBINER;
					// set combiner specular fog dirty flag, so in pixel shader generation stage we could know whether NV097_SET_COMBINER_SPECULAR_FOG_CW0 and NV097_SET_COMBINER_SPECULAR_FOG_CW1 should be put in PSDef or not
					// double check both either control dword is non-zero before setting the state flag
					if(pg->KelvinPrimitive.SetCombinerSpecularFogCW0 != 0 || pg->KelvinPrimitive.SetCombinerSpecularFogCW1 != 0)
					    pgraph_SetNV2AStateFlag(X_STATE_COMBINERNEEDSSPECULAR);
					break;

                case NV097_SET_CONTROL0: {//done  //pg->KelvinPrimitive.SetControl0& NV097_SET_CONTROL0_COLOR_SPACE_CONVERT GET_MASK(pg->KelvinPrimitive.SetControl0, NV097_SET_CONTROL0_COLOR_SPACE_CONVERT)
                    //stencil_write_enable used in pgraph_get_zeta_write_enabled()
                    bool stencil_write_enable =
                    	arg0 & NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE;
                    SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    	NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE,
                    	stencil_write_enable);
                    // used in pgraph_update_surface()
                    uint32_t z_format = GET_MASK(arg0, NV097_SET_CONTROL0_Z_FORMAT);//GET_MASK(pg->KelvinPrimitive.SetControl0, NV097_SET_CONTROL0_Z_FORMAT)
                    SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    	NV_PGRAPH_SETUPRASTER_Z_FORMAT, z_format);

                    // z_perspective is used in 
                    bool z_perspective =
                    	arg0 & NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE;
                    SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    	NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE,
                    	z_perspective);

                    // color_space_convert is used for overlay control in d3d_swap()
                    int color_space_convert =
                    	GET_MASK(arg0, NV097_SET_CONTROL0_COLOR_SPACE_CONVERT);
                    SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    	NV_PGRAPH_CONTROL_0_CSCONVERT,
                    	color_space_convert);
                    //pgraph_update_surface(d, false, true, true);
                    //CommonSetControl0(), D3DRS_ZENABLE and D3DRS_YUVENABLE related
                    if (method_count == 1) {
                        if ((pg->KelvinPrimitive.SetControl0 & NV097_SET_CONTROL0_COLOR_SPACE_CONVERT_CRYCB_TO_RGB) != 0) {
                            NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_YUVENABLE, 1);
                        }
                        if ((pg->KelvinPrimitive.SetControl0 & NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE) != 0) {
                            NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ZENABLE, D3DZB_USEW);
                        }
                    }
                    break;
                }
				case NV097_SET_LIGHT_CONTROL:
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                case NV097_SET_COLOR_MATERIAL: {//done
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4], NV_PGRAPH_CSV0_C_EMISSION,  //(pg->KelvinPrimitive.SetColorMaterial >> 0) & 3)
                    //	(arg0 >> 0) & 3);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4], NV_PGRAPH_CSV0_C_AMBIENT,  //(pg->KelvinPrimitive.SetColorMaterial >> 2) & 3)
                    //	(arg0 >> 2) & 3);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4], NV_PGRAPH_CSV0_C_DIFFUSE,  //(pg->KelvinPrimitive.SetColorMaterial >> 4) & 3)
                    //	(arg0 >> 4) & 3);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4], NV_PGRAPH_CSV0_C_SPECULAR,  //(pg->KelvinPrimitive.SetColorMaterial >> 6) & 3
                    //	(arg0 >> 6) & 3);
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                }

                case NV097_SET_FOG_MODE: {//done //pg->KelvinPrimitive.SetFogMode
                    /* FIXME: There is also NV_PGRAPH_CSV0_D_FOG_MODE */
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
                    unsigned int mode;
                    switch (arg0) {
                    case NV097_SET_FOG_MODE_V_LINEAR:
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR; break;
                    case NV097_SET_FOG_MODE_V_EXP:
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP; break;
                    case NV097_SET_FOG_MODE_V_EXP2:
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2; break;
                    case NV097_SET_FOG_MODE_V_EXP_ABS:
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP_ABS; break;
                    case NV097_SET_FOG_MODE_V_EXP2_ABS:
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2_ABS; break;
                    case NV097_SET_FOG_MODE_V_LINEAR_ABS:
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR_ABS; break;
                    default:
                        assert(false);
                        mode = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR;
                        break;
                    }
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_3 / 4], NV_PGRAPH_CONTROL_3_FOG_MODE,
                    //	mode);
                    //  pg->KelvinPrimitive.SetFogMode = mode; // TODO : Postpone conversion (of NV097_SET_FOG_MODE_V_* into NV_PGRAPH_CONTROL_3_FOG_MODE_*) towards readout
                    break;
                }
                case NV097_SET_FOG_GEN_MODE: {//done //pg->KelvinPrimitive.SetFogGenMode
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
                    unsigned int mode; 
                    switch (arg0) {
                    case NV097_SET_FOG_GEN_MODE_V_SPEC_ALPHA:
                        mode = FOG_MODE_LINEAR; break;
                    case NV097_SET_FOG_GEN_MODE_V_RADIAL:
                        mode = FOG_MODE_EXP; break;
                    case NV097_SET_FOG_GEN_MODE_V_PLANAR:
                        mode = FOG_MODE_ERROR2; break;
                    case NV097_SET_FOG_GEN_MODE_V_ABS_PLANAR:
                        mode = FOG_MODE_EXP2; break;
                    case NV097_SET_FOG_GEN_MODE_V_FOG_X:
                        mode = FOG_MODE_LINEAR_ABS; break;
                    default:
                        assert(false);
                        mode = FOG_MODE_LINEAR;
                        break;
                    }
                    // SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4], NV_PGRAPH_CSV0_D_FOGGENMODE, mode);
                    //pg->KelvinPrimitive.SetFogGenMode = mode; // TODO : Postpone conversion (of NV097_SET_FOG_GEN_MODE_V_* into FOG_MODE_*) towards readout
                    break;
                }
                case NV097_SET_FOG_ENABLE://done //pg->KelvinPrimitive.SetFogEnable
                    /*
                    FIXME: There is also:
                    SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D/4], NV_PGRAPH_CSV0_D_FOGENABLE,
                    parameter);
                    */
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_3 / 4], NV_PGRAPH_CONTROL_3_FOGENABLE,
                    //	arg0);
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
					break;
                case NV097_SET_FOG_COLOR: {//done //pg->KelvinPrimitive.SetFogColor
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
                    // the fog color in Kelvin is swaped in R/B channel, we have to swap it back. this only happens in fog color.
                     /* NV097 Kelvin fog color channels are ABGR, xbox/PGRAPH channels are ARGB */
                    /*
                    uint32_t kelvin_fog_color_ABGR = pg->KelvinPrimitive.SetFogColor;
                    uint8_t alpha = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_ALPHA);
                    uint8_t blue = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_BLUE);
                    uint8_t green = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_GREEN);
                    uint8_t red = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_RED);
                    uint32_t xbox_fog_color = alpha << 24 | red << 16 | green << 8 | blue;
                    */
                    
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_FOGCOLOR, ABGR_to_ARGB(pg->KelvinPrimitive.SetFogColor));
                    break;
                }

                case NV097_SET_WINDOW_CLIP_TYPE://done //pg->KelvinPrimitive.SetWindowClipType
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                        //NV_PGRAPH_SETUPRASTER_WINDOWCLIPTYPE, arg0);
                    break;

                case NV097_SET_ALPHA_TEST_ENABLE://D3DRS_ALPHATESTENABLE //pg->KelvinPrimitive.SetAlphaTestEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_ALPHATESTENABLE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ALPHATESTENABLE, pg->KelvinPrimitive.SetAlphaTestEnable);
					break;

                case NV097_SET_BLEND_ENABLE://D3DRS_ALPHABLENDENABLE //pg->KelvinPrimitive.SetBlendEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_BLEND / 4], NV_PGRAPH_BLEND_EN, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ALPHABLENDENABLE, pg->KelvinPrimitive.SetBlendEnable);
                    break;

                case NV097_SET_CULL_FACE_ENABLE://D3DRS_CULLMODE //pg->KelvinPrimitive.SetCullFaceEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_CULLENABLE,
                    //	arg0);
                    if (pg->KelvinPrimitive.SetCullFaceEnable == false) {
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_CULLMODE, xbox::X_D3DCULL_NONE);
                    }
                    else if(pg->KelvinPrimitive.SetCullFace >=NV097_SET_CULL_FACE_V_FRONT){
                        //pg->KelvinPrimitive.SetCullFace = either 404 or 405.;
                        DWORD backface = (pg->KelvinPrimitive.SetFrontFace == NV097_SET_FRONT_FACE_V_CW) ? NV097_SET_FRONT_FACE_V_CCW : NV097_SET_FRONT_FACE_V_CW;
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_CULLMODE, (pg->KelvinPrimitive.SetCullFace == NV097_SET_CULL_FACE_V_FRONT) ? (pg->KelvinPrimitive.SetFrontFace) : backface);
                    }
                    break;
                case NV097_SET_DEPTH_TEST_ENABLE://done //pg->KelvinPrimitive.SetDepthTestEnable
                    // Test-case : Whiplash
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4], NV_PGRAPH_CONTROL_0_ZENABLE,
                    //	arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ZENABLE, pg->KelvinPrimitive.SetDepthTestEnable);
                    break;
                case NV097_SET_DITHER_ENABLE://done //pg->KelvinPrimitive.SetDitherEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_DITHERENABLE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_DITHERENABLE, pg->KelvinPrimitive.SetDitherEnable);
                    break;
                case NV097_SET_LIGHTING_ENABLE://done X_D3DRS_LIGHTING //pg->KelvinPrimitive.SetLightingEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4], NV_PGRAPH_CSV0_C_LIGHTING,
                    //	arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_LIGHTING, pg->KelvinPrimitive.SetLightingEnable);
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                case NV097_SET_POINT_PARAMS_ENABLE://done //pg->KelvinPrimitive.SetPointParamsEnable
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_POINTPARAMS;
					//this state is not used yet.
                    break;
                case NV097_SET_POINT_SMOOTH_ENABLE://done //pg->KelvinPrimitive.SetPointSmoothEnable
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_POINTPARAMS;
					//this state is not used yet.
                    break;
                case NV097_SET_LINE_SMOOTH_ENABLE://done //pg->KelvinPrimitive.SetLineSmoothEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_LINESMOOTHENABLE, arg0);
                    //break;  both NV097_SET_LINE_SMOOTH_ENABLE and NV097_SET_POLY_SMOOTH_ENABLE have to be the same setting
                case NV097_SET_POLY_SMOOTH_ENABLE://done //pg->KelvinPrimitive.SetPolySmoothEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_POLYSMOOTHENABLE, arg0);
                    if(pg->KelvinPrimitive.SetLineSmoothEnable== pg->KelvinPrimitive.SetPolySmoothEnable)
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_EDGEANTIALIAS, pg->KelvinPrimitive.SetPolySmoothEnable);
                    else
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_EDGEANTIALIAS, true); // todo: to clearify the default setting true or false.
                    break;
                case NV097_SET_SKIN_MODE://done D3DRS_VERTEXBLEND //pg->KelvinPrimitive.SetSkinMode
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4], NV_PGRAPH_CSV0_D_SKIN,
                    //	arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_VERTEXBLEND, pg->KelvinPrimitive.SetSkinMode);
                    break;
                case NV097_SET_STENCIL_TEST_ENABLE://done //pg->KelvinPrimitive.SetStencilTestEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4],
                    //	NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILENABLE, pg->KelvinPrimitive.SetStencilTestEnable);
                    break;
                case NV097_SET_POLY_OFFSET_POINT_ENABLE://done //pg->KelvinPrimitive.SetPolyOffsetPointEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_POINTOFFSETENABLE, pg->KelvinPrimitive.SetPolyOffsetPointEnable);
                    break;
                case NV097_SET_POLY_OFFSET_LINE_ENABLE://done //pg->KelvinPrimitive.SetPolyOffsetLineEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_WIREFRAMEOFFSETENABLE, pg->KelvinPrimitive.SetPolyOffsetPointEnable);
                    break;
                case NV097_SET_POLY_OFFSET_FILL_ENABLE://done //pg->KelvinPrimitive.SetPolyOffsetFillEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_SOLIDOFFSETENABLE, pg->KelvinPrimitive.SetPolyOffsetPointEnable);
                    break;
                case NV097_SET_ALPHA_FUNC://done //pg->KelvinPrimitive.SetAlphaFunc
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_ALPHAFUNC, arg0 & 0xF);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ALPHAFUNC, pg->KelvinPrimitive.SetAlphaFunc);
                    break;
                case NV097_SET_ALPHA_REF://done //pg->KelvinPrimitive.SetAlphaRef
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_ALPHAREF, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ALPHAREF, pg->KelvinPrimitive.SetAlphaRef);
                    break;
                case NV097_SET_BLEND_FUNC_SFACTOR: {//done //pg->KelvinPrimitive.SetBlendFuncSfactor
                    unsigned int factor=arg0;
                    if (factor > 15) {
                        fprintf(stderr, "Unknown blend source factor: 0x%x, reset to NV_PGRAPH_BLEND_SFACTOR_ZERO\n", arg0);
                        //assert(false);
                        //set factor a default value, even this is not supposed to happen.
						// pushbuffer sample using method 304 with arg0 0x302.
						// pg->KelvinPrimitive.SetBlendFuncSfactor = factor & 0x0F;
                    }
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_BLEND / 4], NV_PGRAPH_BLEND_SFACTOR, factor);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_SRCBLEND, pg->KelvinPrimitive.SetBlendFuncSfactor);
                    break;
                }

                case NV097_SET_BLEND_FUNC_DFACTOR: {//done //pg->KelvinPrimitive.SetBlendFuncDfactor
                    unsigned int factor=arg0;
                    switch (arg0) {
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ZERO:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ZERO; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_COLOR:
                        factor = NV_PGRAPH_BLEND_DFACTOR_SRC_COLOR; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_COLOR:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_SRC_COLOR; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_ALPHA:
                        factor = NV_PGRAPH_BLEND_DFACTOR_SRC_ALPHA; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_SRC_ALPHA; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_DST_ALPHA:
                        factor = NV_PGRAPH_BLEND_DFACTOR_DST_ALPHA; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_DST_ALPHA:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_ALPHA; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_DST_COLOR:
                        factor = NV_PGRAPH_BLEND_DFACTOR_DST_COLOR; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_DST_COLOR:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_COLOR; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_ALPHA_SATURATE:
                        factor = NV_PGRAPH_BLEND_DFACTOR_SRC_ALPHA_SATURATE; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_CONSTANT_COLOR:
                        factor = NV_PGRAPH_BLEND_DFACTOR_CONSTANT_COLOR; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_CONSTANT_COLOR:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_CONSTANT_COLOR; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_CONSTANT_ALPHA:
                        factor = NV_PGRAPH_BLEND_DFACTOR_CONSTANT_ALPHA; break;
                    case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_CONSTANT_ALPHA:
                        factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_CONSTANT_ALPHA; break;
                    default:
                        factor=NV_PGRAPH_BLEND_DFACTOR_ZERO;
                        fprintf(stderr, "Unknown blend destination factor: 0x%x\n", arg0);
                        assert(false);
                        break;
                    }
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_BLEND / 4], NV_PGRAPH_BLEND_DFACTOR, factor);
                    //pg->KelvinPrimitive.SetBlendFuncDfactor = factor; // TODO : Postpone conversion (of NV097_SET_BLEND_FUNC_DFACTOR_V_* into NV_PGRAPH_BLEND_DFACTOR_*) towards readout
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_DESTBLEND, pg->KelvinPrimitive.SetBlendFuncDfactor);
                    break;
                }

                case NV097_SET_BLEND_COLOR://done //pg->KelvinPrimitive.SetBlendColor
                    //pg->pgraph_regs[NV_PGRAPH_BLENDCOLOR/4] = parameter;
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_BLENDCOLOR, pg->KelvinPrimitive.SetBlendColor);
                    break;

                case NV097_SET_BLEND_EQUATION: {//done //pg->KelvinPrimitive.SetBlendEquation
                    unsigned int equation;
                    switch (arg0) {
                    case NV097_SET_BLEND_EQUATION_V_FUNC_SUBTRACT:
                        equation = 0; break;
                    case NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT:
                        equation = 1; break;
                    case NV097_SET_BLEND_EQUATION_V_FUNC_ADD:
                        equation = 2; break;
                    case NV097_SET_BLEND_EQUATION_V_MIN:
                        equation = 3; break;
                    case NV097_SET_BLEND_EQUATION_V_MAX:
                        equation = 4; break;
                    case NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT_SIGNED:
                        equation = 5; break;
                    case NV097_SET_BLEND_EQUATION_V_FUNC_ADD_SIGNED:
                        equation = 6; break;
                    default:
                        assert(false);
                        equation = 0;
                        break;
                    }
                    //pg->KelvinPrimitive.SetBlendEquation = equation; // TODO : Postpone conversion (of NV097_SET_BLEND_EQUATION_V_* into pgraph_blend_equation_map indices) towards readout
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_BLEND / 4], NV_PGRAPH_BLEND_EQN, equation);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_BLENDOP, pg->KelvinPrimitive.SetBlendEquation);
                    break;
                }

                case NV097_SET_DEPTH_FUNC://done //pg->KelvinPrimitive.SetDepthFunc
                    // Test-case : Whiplash
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4], NV_PGRAPH_CONTROL_0_ZFUNC,
                    //	arg0 & 0xF);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ZFUNC, pg->KelvinPrimitive.SetDepthFunc);
                    break;

                case NV097_SET_COLOR_MASK: {//done //pg->KelvinPrimitive.SetColorMask
                    pg->surface_color.write_enabled_cache |= pgraph_get_color_write_enabled(pg);

                    bool alpha = arg0 & NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE;
                    bool red = arg0 & NV097_SET_COLOR_MASK_RED_WRITE_ENABLE;
                    bool green = arg0 & NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE;
                    bool blue = arg0 & NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE;
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE, alpha);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE, red);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE, green);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    //	NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE, blue);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_COLORWRITEENABLE, pg->KelvinPrimitive.SetColorMask);
                    break;
                }
                case NV097_SET_DEPTH_MASK://done //pg->KelvinPrimitive.SetDepthMask
                    //pg->surface_zeta.write_enabled_cache |= pgraph_get_zeta_write_enabled(pg);

                    // used in pgraph_get_zeta_write_enabled()
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4],
                    	//NV_PGRAPH_CONTROL_0_ZWRITEENABLE, arg0);

                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ZWRITEENABLE, pg->KelvinPrimitive.SetDepthMask);
                    break;
                case NV097_SET_STENCIL_MASK://done //pg->KelvinPrimitive.SetStencilMask
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4],
                    //	NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILWRITEMASK, pg->KelvinPrimitive.SetStencilMask);
                    break;
                case NV097_SET_STENCIL_FUNC://done //pg->KelvinPrimitive.SetStencilFunc
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4],
                        //NV_PGRAPH_CONTROL_1_STENCIL_FUNC, arg0 & 0xF);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILFUNC, pg->KelvinPrimitive.SetStencilFunc);
                    break;
                case NV097_SET_STENCIL_FUNC_REF://done //pg->KelvinPrimitive.SetStencilFuncRef
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4],
                    //	NV_PGRAPH_CONTROL_1_STENCIL_REF, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILREF, pg->KelvinPrimitive.SetStencilFuncRef);
                    break;
                case NV097_SET_STENCIL_FUNC_MASK://done //pg->KelvinPrimitive.SetStencilFuncMask
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4],
                        //NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ, arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILMASK, pg->KelvinPrimitive.SetStencilFuncMask);
                    break;
                case NV097_SET_STENCIL_OP_FAIL://done //pg->KelvinPrimitive.SetStencilOpFail
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_2 / 4],
                        //NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL,
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILFAIL, pg->KelvinPrimitive.SetStencilOpFail);
                    break;
                case NV097_SET_STENCIL_OP_ZFAIL://done //pg->KelvinPrimitive.SetStencilOpZfail
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_2 / 4],
                        //NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL,
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILZFAIL, pg->KelvinPrimitive.SetStencilOpZfail);
                    break;
                case NV097_SET_STENCIL_OP_ZPASS://done //pg->KelvinPrimitive.SetStencilOpZpass
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_2 / 4],
                        //NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS,
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILPASS, pg->KelvinPrimitive.SetStencilOpZpass);
                    break;

                case NV097_SET_SHADE_MODE://done //pg->KelvinPrimitive.SetShadeMode
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_SHADEMODE, pg->KelvinPrimitive.SetShadeMode);
                    break;

                case NV097_SET_LINE_WIDTH://done D3DRS_LINEWIDTH //pg->KelvinPrimitive.SetLineWidth=width = Round(Floatify(Value) * 8.0f * pDevice->m_SuperSampleScale)
                    //assert(arg0 == pg->KelvinPrimitive.SetLineWidth);
                    extern float CxbxrGetSuperSampleScale();
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_LINEWIDTH, FtoDW(DWtoF(pg->KelvinPrimitive.SetLineWidth) / (8.0f*CxbxrGetSuperSampleScale())));//  / SuperSampleScale); //use 1.0 for SuperSampleScale as a hack.
                    break;

                case NV097_SET_POLYGON_OFFSET_SCALE_FACTOR://done //pg->KelvinPrimitive.SetPolygonOffsetScaleFactor
                    // TODO : float assert(arg0 == pg->KelvinPrimitive.SetPolygonOffsetScaleFactor);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_POLYGONOFFSETZSLOPESCALE, pg->KelvinPrimitive.SetPolygonOffsetScaleFactor);
                    break;

                case NV097_SET_POLYGON_OFFSET_BIAS://done //pg->KelvinPrimitive.SetPolygonOffsetBias
                    // TODO : float assert(arg0 == pg->KelvinPrimitive.SetPolygonOffsetBias);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_POLYGONOFFSETZOFFSET, pg->KelvinPrimitive.SetPolygonOffsetBias);
                    break;

                case NV097_SET_FRONT_POLYGON_MODE://D3DRS_FILLMODE D3DRS_TWOSIDEDLIGHTING//pg->KelvinPrimitive.SetFrontPolygonMode , xbox sets KelvinPrimitive.SetBackPolygonMode together with KelvinPrimitive.SetFrontPolygonMode in the same time
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_FRONTFACEMODE,
                    //	kelvin_map_polygon_mode(arg0));

                    //  break;
                case NV097_SET_BACK_POLYGON_MODE://D3DRS_BACKFILLMODE D3DRS_TWOSIDEDLIGHTING//pg->KelvinPrimitive.SetBackPolygonMode
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_BACKFACEMODE,
                    //	kelvin_map_polygon_mode(arg0));
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_FILLMODE, pg->KelvinPrimitive.SetFrontPolygonMode);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_BACKFILLMODE, pg->KelvinPrimitive.SetBackPolygonMode);
                    // update D3DRS_TWOSIDEDLIGHTING
                    //if(pg->KelvinPrimitive.SetFrontPolygonMode!= pg->KelvinPrimitive.SetBackPolygonMode)
                        //NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_TWOSIDEDLIGHTING,( pg->KelvinPrimitive.SetFrontPolygonMode != pg->KelvinPrimitive.SetBackPolygonMode)?true:false);
                    //else
                      //  NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_TWOSIDEDLIGHTING, false);
                    break;
				// xbox d3d SetViewport() calls NV097_SET_VIEWPORT_OFFSET/    NV097_SET_VIEWPORT_SCALE(optional), then calls NV097_SET_CLIP_MIN with method count =2
				case NV097_SET_CLIP_MIN://done //pg->KelvinPrimitive.SetClipMin
                    // TODO : float assert(arg0 == pg->KelvinPrimitive.SetClipMin);
					// populate to next method handler if method count >1. this happened in xbox d3d SetViewport()
					if (method_count > 1) {
						method_count -= 1;
						argv += 1;
						arg0 = argv[0];
						method += (NV097_SET_CLIP_MAX - NV097_SET_CLIP_MIN);
						goto SETCLIPMAX;
					}
					NV2A_viewport_dirty = true;
					break;

                case NV097_SET_CLIP_MAX://done //pg->KelvinPrimitive.SetClipMax
					SETCLIPMAX:
					// TODO : float assert(arg0 == pg->KelvinPrimitive.SetClipMax);
					// call pgraph_SetViewport(d) in D3D_draw_state_update();
					// pgraph_SetViewport(d);
					NV2A_viewport_dirty = true;
					break;

                case NV097_SET_CULL_FACE: {//done //pg->KelvinPrimitive.SetCullFace
                    unsigned int face=arg0;
                    switch (arg0) {
                    case NV097_SET_CULL_FACE_V_FRONT:
                        face = NV_PGRAPH_SETUPRASTER_CULLCTRL_FRONT; break;
                    case NV097_SET_CULL_FACE_V_BACK:
                        face = NV_PGRAPH_SETUPRASTER_CULLCTRL_BACK; break;
                    case NV097_SET_CULL_FACE_V_FRONT_AND_BACK:
                        face = NV_PGRAPH_SETUPRASTER_CULLCTRL_FRONT_AND_BACK; break;
                    default:
                        assert(false);
                        face = NV097_SET_CULL_FACE_V_FRONT;
                        break;
                    }
                    SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    	NV_PGRAPH_SETUPRASTER_CULLCTRL,
                    	face);
                    //pg->KelvinPrimitive.SetCullFace = either 404 or 405.;
                    DWORD backface = (pg->KelvinPrimitive.SetFrontFace == NV097_SET_FRONT_FACE_V_CW) ? NV097_SET_FRONT_FACE_V_CCW : NV097_SET_FRONT_FACE_V_CW;
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_CULLMODE,  (pg->KelvinPrimitive.SetCullFace== NV097_SET_CULL_FACE_V_FRONT) ? (pg->KelvinPrimitive.SetFrontFace):backface);
                    break;
                }
                case NV097_SET_FRONT_FACE: {//done //pg->KelvinPrimitive.SetFrontFace
                    bool ccw;
                    switch (arg0) {
                    case NV097_SET_FRONT_FACE_V_CW:
                        ccw = false; break;
                    case NV097_SET_FRONT_FACE_V_CCW:
                        ccw = true; break;
                    default:
                        ccw = false; 
                        fprintf(stderr, "Unknown front face: 0x%x\n", arg0);
                        assert(false);
                        break;
                    }
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4],
                    //	NV_PGRAPH_SETUPRASTER_FRONTFACE,
                    //	ccw ? 1 : 0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_FRONTFACE, pg->KelvinPrimitive.SetFrontFace);
                    //pg->KelvinPrimitive.SetFrontFace = ccw ? 1 : 0; // TODO : Postpone conversion (of NV097_SET_FRONT_FACE_V_* into NV_PGRAPH_SETUPRASTER_FRONTFACE values) towards readout
                    //NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_FRONTFACE, pg->KelvinPrimitive.SetFrontFace);
                    break;
                }
                case NV097_SET_NORMALIZATION_ENABLE://done //pg->KelvinPrimitive.SetNormalizationEnable
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4],
                    //	NV_PGRAPH_CSV0_C_NORMALIZATION_ENABLE,
                    //	arg0);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_NORMALIZENORMALS, pg->KelvinPrimitive.SetNormalizationEnable);
                    break;

                CASE_3(NV097_SET_MATERIAL_EMISSION, 4)://done //pg->KelvinPrimitive.SetMaterialEmission[3]
                    // TODO : float assert(arg0 == pg->KelvinPrimitive.SetMaterialEmission[slot]);
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                case NV097_SET_MATERIAL_ALPHA://done //pg->KelvinPrimitive.SetMaterialAlpha
                    // TODO : float assert(arg0 == pg->KelvinPrimitive.SetMaterialAlpha);
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                case NV097_SET_SPECULAR_ENABLE : //done D3DRS_SPECULARENABLE//pg->KelvinPrimitive.SetSpecularEnable, LazyUpdateCombiners()/LazySetLights() will set this var.
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
                    pgraph_SetNV2AStateFlag(X_STATE_COMBINERNEEDSSPECULAR);
					break;
                case NV097_SET_LIGHT_ENABLE_MASK://done //pg->KelvinPrimitive.SetLightEnableMask
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4], NV_PGRAPH_CSV0_D_LIGHTS, arg0);
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;

                CASE_4(NV097_SET_TEXGEN_S, 16) : {//done //pg->KelvinPrimitive.SetTexgen[2].S  {S,T,R,Q}
                    slot = (method - NV097_SET_TEXGEN_S) / 16; //slot is 0 ..4  1
                    //unsigned int reg = (slot <2) ? NV_PGRAPH_CSV1_A / 4
                    //	: NV_PGRAPH_CSV1_B / 4;
                    //unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_S
                    //	: NV_PGRAPH_CSV1_A_T0_S;
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV1_A / 4], mask, kelvin_map_texgen(arg0, 0));
                    //pg->KelvinPrimitive.SetTexgen[slot].S = kelvin_map_texgen(arg0, 0);
                    
                    break;
                }
                CASE_4(NV097_SET_TEXGEN_T, 16) : {//done //pg->KelvinPrimitive.SetTexgen[2].T  {S,T,R,Q}
                    slot = (method - NV097_SET_TEXGEN_T) / 16;
                    //unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A / 4
                    //	: NV_PGRAPH_CSV1_B / 4;
                    //unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_T
                    //	: NV_PGRAPH_CSV1_A_T0_T;
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV1_A / 4], mask, kelvin_map_texgen(arg0, 1));
                    pg->KelvinPrimitive.SetTexgen[slot].T = kelvin_map_texgen(arg0, 0);
                    break;
                }
                CASE_4(NV097_SET_TEXGEN_R, 16) : {//done //pg->KelvinPrimitive.SetTexgen[2].R  {S,T,R,Q}
                    slot = (method - NV097_SET_TEXGEN_R) / 16;
                    //unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A / 4
                    //	: NV_PGRAPH_CSV1_B / 4;
                    //unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_R
                    //	: NV_PGRAPH_CSV1_A_T0_R;
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV1_A / 4], mask, kelvin_map_texgen(arg0, 2));
                    pg->KelvinPrimitive.SetTexgen[slot].R = kelvin_map_texgen(arg0, 0);
                    break;
                }
                CASE_4(NV097_SET_TEXGEN_Q, 16) : {//done //pg->KelvinPrimitive.SetTexgen[2].Q  {S,T,R,Q}
                    slot = (method - NV097_SET_TEXGEN_Q) / 16;
                    //original code uses condition slot < 2 , then NV_PGRAPH_CSV1_A will always be used.
                    //unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A / 4
                    //	: NV_PGRAPH_CSV1_B / 4;
                    //unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_Q
                    //	: NV_PGRAPH_CSV1_A_T0_Q;
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV1_A / 4], mask, kelvin_map_texgen(arg0, 3));
                    pg->KelvinPrimitive.SetTexgen[slot].Q = kelvin_map_texgen(arg0, 0);
                    break;
                }
                CASE_4(NV097_SET_TEXTURE_MATRIX_ENABLE, 4) ://done //pg->KelvinPrimitive.SetTextureMatrixEnable[4]
                    slot = (method - NV097_SET_TEXTURE_MATRIX_ENABLE) / 4;
                    //pg->texture_matrix_enable[slot] = arg0;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_TRANSFORM;
				    break;

                case NV097_SET_POINT_SIZE://done //pg->KelvinPrimitive.SetPointSize
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_POINTPARAMS;
					break;

                CASE_16(NV097_SET_PROJECTION_MATRIX, 4) : {//done
                    //KelvinPrimitive.SetProjectionMatrix[16] is update already. we update the vertex shader contant as well.
					// xbox d3d doesn't use NV097_SET_PROJECTION_MATRIX at all. set assert here to see if there is any code use it.
					assert(0);
					slot = (method - NV097_SET_PROJECTION_MATRIX) / 4;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        // pg->projection_matrix[slot] = *(float*)&parameter;
                        unsigned int row = NV_IGRAPH_XF_XFCTX_PMAT0 + slot / 4;
                        assert(arg0 == *((uint32_t*)&(pg->KelvinPrimitive.SetProjectionMatrix[slot])));
                        pg->vsh_constants[row][slot % 4] = arg0;
                        pg->vsh_constants_dirty[row] = true;
                    }
                    */
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TRANSFORM;
                    break;
                }
				//Matrix transposed before pushed, not always matrix 0, method count 16. in fixed vertex shader, there are setting with NV097_SET_MODEL_VIEW_MATRIX1/NV097_SET_MODEL_VIEW_MATRIX2/NV097_SET_MODEL_VIEW_MATRIX3 When skinning.
                CASE_64(NV097_SET_MODEL_VIEW_MATRIX, 4) : {//done //pg->KelvinPrimitive.SetModelViewMatrix0[16] SetModelViewMatrix1[16] SetModelViewMatrix2[16] SetModelViewMatrix3[16]
                     //KelvinPrimitive.SetModelViewMatrix?[] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_MODEL_VIEW_MATRIX) / 4;
                    unsigned int matnum = slot / 16;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
						matnum = slot / 16;
						unsigned int entry = slot % 16;
                        unsigned int row = NV_IGRAPH_XF_XFCTX_MMAT0 + matnum * 4 + entry / 4;
                        pg->vsh_constants[row][entry % 4] = arg0;
                        pg->vsh_constants_dirty[row] = true;
                    }
					// reinit matnum to first matrix index
                    */
                    matnum = (method - NV097_SET_MODEL_VIEW_MATRIX) / 64;
                    // if NV097_SET_MODEL_VIEW_MATRIX[0] was set, and the next method is to set NV097_SET_COMPOSITE_MATRIX[0], then we're in direct modelview mode.
                    if ((pg->KelvinPrimitive.SetSkinMode == 0)){
                        if ((method == NV097_SET_MODEL_VIEW_MATRIX) ) 
                            NV2A_DirtyFlags |= X_D3DDIRTYFLAG_DIRECT_MODELVIEW;
                        // if NV097_SET_MODEL_VIEW_MATRIX[1] was set, then we're in SetTransform(). Todo: this is a dirty hack, the LazySetTransform behaves the same as SetModelView when not in skinning mode.
                        else
                            NV2A_DirtyFlags &= !X_D3DDIRTYFLAG_DIRECT_MODELVIEW;
                    // if we're in skinning mode, then we're in SetTransform()
                    }else {
                            NV2A_DirtyFlags &= !X_D3DDIRTYFLAG_DIRECT_MODELVIEW;
                    }
					// set dirty flags for each matrix
					for (unsigned int matcnt = 0; matcnt < (method_count + 15) / 16; matcnt++, matnum++) {
						pgraph_SetModelViewMatrixDirty(matnum);
					}
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TRANSFORM;
					break;
                }
				//Matrix not transposed before pushed, always matrix 0, method count 12, only first 12 floats are set in each matrix. NV097_SET_INVERSE_MODEL_VIEW_MATRIX1/NV097_SET_INVERSE_MODEL_VIEW_MATRIX2/NV097_SET_INVERSE_MODEL_VIEW_MATRIX wil be set when skinning and if lighting or texgen need it.
                CASE_64(NV097_SET_INVERSE_MODEL_VIEW_MATRIX, 4) : {//done //pg->KelvinPrimitive.SetInverseModelViewMatrix0[16] SetInverseModelViewMatrix1[16] SetInverseModelViewMatrix2[16] SetInverseModelViewMatrix3[16]
                    //KelvinPrimitive.SetModelViewMatrix?[] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_INVERSE_MODEL_VIEW_MATRIX) / 4;
                    unsigned int matnum;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
						matnum = slot / 16;
                        unsigned int entry = slot % 16;
                        unsigned int row = NV_IGRAPH_XF_XFCTX_IMMAT0 + matnum * 4 + entry / 4;
                        pg->vsh_constants[row][entry % 4] = arg0;
                        pg->vsh_constants_dirty[row] = true;
                    }
					// reinit matnum to first matrix index
                    */
                    matnum = (method - NV097_SET_INVERSE_MODEL_VIEW_MATRIX) / 64;
                    
					// set dirty flags for each matrix
					for (unsigned int matcnt=0; matcnt < (method_count+15) / 16; matcnt++,matnum++) {
						pgraph_SetInverseModelViewMatrixDirty(matnum);
					}
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TRANSFORM;
                    break;
                }
				//Matrix transposed before pushed, always matrix 0, method count 16
                CASE_16(NV097_SET_COMPOSITE_MATRIX, 4) : {//done //pg->KelvinPrimitive.SetCompositeMatrix[16]
                        //KelvinPrimitive.SetCompositeMatrix[16] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_COMPOSITE_MATRIX) / 4;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        unsigned int row = NV_IGRAPH_XF_XFCTX_CMAT0 + slot / 4;
                        pg->vsh_constants[row][slot % 4] = arg0;
                        pg->vsh_constants_dirty[row] = true;
                    }
                    */
					pgraph_SetCompositeMatrixDirty();
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TRANSFORM;
                    break;
                }
				// KelvinPrimitive.SetTextureMatrix[4][16] includes texgen plane transform as used by xbox d3d.
                CASE_64(NV097_SET_TEXTURE_MATRIX, 4) : {//done //pg->KelvinPrimitive.SetTextureMatrix[4][16]
                    //KelvinPrimitive.SetTextureMatrix[16] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_TEXTURE_MATRIX) / 4;
					/*
					// the vertex shader constant can't be update unconditionaly. it can only be update when in fixed vertex shader program, and not in 192 constant mode.
					// when unpatch all xbox d3d apis, these conditions will be considered by xbox d3d.
					for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        unsigned int tex = slot / 16;
                        unsigned int entry = slot % 16;
                        unsigned int row = NV_IGRAPH_XF_XFCTX_T0MAT + tex * 8 + entry / 4;
                        //pg->vsh_constants[row][entry % 4] = arg0;
                        //pg->vsh_constants_dirty[row] = true;
                    }
					*/
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_TRANSFORM;
					break;
                }
                /* Handles NV097_SET_TEXGEN_PLANE_S,T,R,Q */ //KelvinPrimitive.SetTexgenPlane[4]::{S[4],T[4],R[4],Q[4]}
				// xbox d3d uses KelvinPrimitive.SetTexgenPlane[0~3].S[] only, and sets all 4 S planes with transposed Identity Matrix.
				// the actual texture transform is set via NV097_SET_TEXTURE_MATRIX, KelvinPrimitive.SetTextureMatrix[4][16] includes the transform of texgen.
				CASE_64(NV097_SET_TEXGEN_PLANE_S, 4) : {//done //pg->KelvinPrimitive.SetTexgenPlane[i].S[j] .T[j] .R[j] .Q[j]  tex=i, entry=j
                    //KelvinPrimitive.SetTexgenPlane[4] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_TEXGEN_PLANE_S) / 4;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        unsigned int tex = slot / 16;
                        unsigned int entry = slot % 16;
                        unsigned int row = NV_IGRAPH_XF_XFCTX_TG0MAT + tex * 8 + entry / 4;
                        pg->vsh_constants[row][entry % 4] = arg0;
                        pg->vsh_constants_dirty[row] = true;
                    }
                    */
                    break;
                }

                CASE_3(NV097_SET_FOG_PARAMS, 4) ://done //pg->KelvinPrimitive.SetFogParams[3]
                    //KelvinPrimitive.SetFogParams[3] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_FOG_PARAMS) / 4;
					for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        /* Cxbx note: slot = 2 is right after slot = 1 */
                        //pg->ltctxa[NV_IGRAPH_XF_LTCTXA_FOG_K][slot] = arg0;
                        //pg->ltctxa_dirty[NV_IGRAPH_XF_LTCTXA_FOG_K] = true;
                    }
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
                    break;

                case NV097_SET_TEXGEN_VIEW_MODEL://done //pg->KelvinPrimitive.SetTexgenViewModel
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4], NV_PGRAPH_CSV0_D_TEXGEN_REF,
                    //	arg0);
                    break;

                CASE_4(NV097_SET_FOG_PLANE, 4) ://done //pg->KelvinPrimitive.SetFogPlane[4]
                    //KelvinPrimitive.SetFogPlane[4] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_FOG_PLANE) / 4;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];                        
                        pg->vsh_constants[NV_IGRAPH_XF_XFCTX_FOG][slot] = arg0;
                        pg->vsh_constants_dirty[NV_IGRAPH_XF_XFCTX_FOG] = true;
                    }
                    */
                    break;

                CASE_6(NV097_SET_SPECULAR_PARAMS, 4) ://done //pg->KelvinPrimitive.SetSpecularParams[6]
                    //KelvinPrimitive.SetSpecularParams[6] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_SPECULAR_PARAMS) / 4;
					for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                    // this state is not implemented yet.
                        //pg->ltctxa[NV_IGRAPH_XF_LTCTXA_FR_AMB][slot] = arg0;
                        //pg->ltctxa_dirty[NV_IGRAPH_XF_LTCTXA_FR_AMB] = true;
                    }
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
                    break;

                case NV097_SET_SWATH_WIDTH://not implement //pg->KelvinPrimitive.SetSwathWidth
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_SWATHWIDTH, pg->KelvinPrimitive.SetSwathWidth);
                    break;

                case NV097_SET_FLAT_SHADE_OP: break;//not implement //pg->KelvinPrimitive.SetFlatShadeOp
                    break;

                CASE_3(NV097_SET_SCENE_AMBIENT_COLOR, 4) ://done //pg->KelvinPrimitive.SetSceneAmbientColor[3]
                    //KelvinPrimitive.SetSceneAmbientColor[3] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_SCENE_AMBIENT_COLOR) / 4;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        pg->ltctxa[NV_IGRAPH_XF_LTCTXA_FR_AMB][slot] = arg0;
                        pg->ltctxa_dirty[NV_IGRAPH_XF_LTCTXA_FR_AMB] = true;
                    }
                    */
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;

                CASE_4(NV097_SET_VIEWPORT_OFFSET, 4) ://done //pg->KelvinPrimitive.SetViewportOffset[4]
                    //KelvinPrimitive.SetViewportOffset[4] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_VIEWPORT_OFFSET) / 4;
                    // reserved vertex shader constant -37
					for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
						// vertex shader constant register -37
						pg->vsh_constants[NV_IGRAPH_XF_XFCTX_VPOFF][slot] = arg0;
                        pg->vsh_constants_dirty[NV_IGRAPH_XF_XFCTX_VPOFF] = true;
                    }
                    
					NV2A_viewport_dirty = true;
					break;

                CASE_8(NV097_SET_POINT_PARAMS, 4) ://done //pg->KelvinPrimitive.SetPointParams[8]
                    //KelvinPrimitive.SetPointParams[8] is update already. we update the vertex shader contant as well.
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_POINTPARAMS;
					slot = (method - NV097_SET_POINT_PARAMS) / 4;
					//for (int argc = 0; argc < method_count; argc++, slot++) {
                        //arg0 = argv[argc];
                        //pg->vsh_constants[NV_IGRAPH_XF_XFCTX_VPOFF][slot] = arg0;
                        //pg->vsh_constants_dirty[NV_IGRAPH_XF_XFCTX_VPOFF] = true;
                    //}
                break;

                CASE_4(NV097_SET_EYE_POSITION, 4) ://done //pg->KelvinPrimitive.SetEyePosition[4]
                    //KelvinPrimitive.SetEyePosition[4] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_EYE_POSITION) / 4;
                    /* //reg -40
					for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        pg->vsh_constants[NV_IGRAPH_XF_XFCTX_EYEP][slot] = arg0;
                        pg->vsh_constants_dirty[NV_IGRAPH_XF_XFCTX_EYEP] = true;
                    }
                    */
                    break;

                CASE_8(NV097_SET_COMBINER_FACTOR0, 4) ://done D3DRS_TEXTUREFACTOR //pg->KelvinPrimitive.SetCombinerFactor0[8]
                    //KelvinPrimitive.SetCombinerFactor0[8] is update already. 
                    //	pg->pgraph_regs[NV_PGRAPH_COMBINEFACTOR0/ 4 + slot * 4] = arg0;
                    //}
                    // when fixed mode pixel shader in place, all NV097_SET_COMBINER_FACTOR0 and NV097_SET_COMBINER_FACTOR1 will be set to the same value when D3DRS_TEXTUREFACTOR was set to the value.
                {
                    if (method_count == 16) {
                        bool allTheSame = true;
                        for (int i = 1; i < 15; i++) {
                            if (argv[0] != argv[i]) {
                                allTheSame = false;                                
                                break;
                            }
                        }
                        if (allTheSame) {
                            NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_TEXTUREFACTOR, argv[0]);
                            NV2A_TextureFactorAllTheSame = true;
                        }
                        else {
                            NV2A_TextureFactorAllTheSame = false;
                        }
                    }
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
                }
                    break;

                CASE_8(NV097_SET_COMBINER_FACTOR1, 4) ://done //pg->KelvinPrimitive.SetCombinerFactor1[8]
                    slot = (method - NV097_SET_COMBINER_FACTOR1) / 4;
                    //pg->pgraph_regs[NV_PGRAPH_COMBINEFACTOR1/ 4 + slot * 4] = arg0;
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
                    break;

                CASE_8(NV097_SET_COMBINER_ALPHA_OCW, 4) ://done //pg->KelvinPrimitive.SetCombinerAlphaOCW[8]
                    slot = (method - NV097_SET_COMBINER_ALPHA_OCW) / 4;
                    //pg->pgraph_regs[NV_PGRAPH_COMBINEALPHAO0/ 4 + slot * 4] = arg0;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
					break;

                CASE_8(NV097_SET_COMBINER_COLOR_ICW, 4) ://done //pg->KelvinPrimitive.SetCombinerColorICW[8]
                    slot = (method - NV097_SET_COMBINER_COLOR_ICW) / 4;
                    //pg->pgraph_regs[NV_PGRAPH_COMBINECOLORI0/ 4 + slot * 4] = arg0;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
				    break;

                CASE_4(NV097_SET_COLOR_KEY_COLOR, 4) ://done D3DTSS_COLORKEYCOLOR//pg->KelvinPrimitive.SetColorKeyColor[4]
                    slot = (method - NV097_SET_COLOR_KEY_COLOR) / 4;
                    //pg->pgraph_regs[NV_PGRAPH_COMBINECOLORI0/ 4 + slot * 4] = arg0;
                //this state is not implement yet.
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
                    NV2ATextureStates.Set(slot, xbox::X_D3DTSS_COLORKEYCOLOR, pg->KelvinPrimitive.SetColorKeyColor[slot]);
                    break;

                CASE_4(NV097_SET_VIEWPORT_SCALE, 4) ://done //pg->KelvinPrimitive.SetViewportScale[4]
                    //KelvinPrimitive.SetViewportScale[4] is update already. we update the vertex shader contant as well.
					slot = (method - NV097_SET_VIEWPORT_SCALE) / 4;
                    
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
						// vertex shader constant register -38 D3DVS_XBOX_RESERVEDCONSTANT1, -37 D3DVS_XBOX_RESERVEDCONSTANT2
						pg->vsh_constants[NV_IGRAPH_XF_XFCTX_VPSCL][slot] = arg0;
                        pg->vsh_constants_dirty[NV_IGRAPH_XF_XFCTX_VPSCL] = true;
                    }
                    
					NV2A_viewport_dirty = true;
					break;

				CASE_32(NV097_SET_TRANSFORM_PROGRAM, 4) : {//done // pg->KelvinPrimitive.SetTransformProgram[32]
                        //KelvinPrimitive.SetTransformProgram[32] is update already. we update the extra vertex shader program as well.
						//KelvinPrimitive.SetTransformProgram[32] holds only 32 slots. for program larger than 32 slots, must be split to batches.
					    //before the first batch, NV097_SET_TRANSFORM_PROGRAM_LOAD must be called to set the beginning slot of loading.
					    //NV097_SET_TRANSFORM_PROGRAM will advanced after each execution of NV097_SET_TRANSFORM_PROGRAM.
					    //for continuous batch NV097_SET_TRANSFORM_PROGRAM methods, it will not have NV097_SET_TRANSFORM_PROGRAM_LOAD in between.
					slot = (method - NV097_SET_TRANSFORM_PROGRAM) / 4;
                    // ONLY INCREMENT IN BELOW COPY LOOP : pg->KelvinPrimitive.SetTransformProgramLoad += slot/4;
                    /*
                    for (int argc = 0; argc < method_count; argc++, slot++) {
                        arg0 = argv[argc];
                        //target program register address is prestored in KelvinPrimitive.SetTransformProgramLoad
                        assert(pg->KelvinPrimitive.SetTransformProgramLoad < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH);
						// TODO : Since this data is also copied outside Kelvin, we could bypass setting KelvinPrimitive fields
						//pg->KelvinPrimitive.SetTransformProgram[32] is not enough for xbox d3d, pgraph uses vsh_program_slots[136][4] to store vertex shader program
                        pg->vsh_program_slots[pg->KelvinPrimitive.SetTransformProgramLoad][slot % 4] = arg0;
                        if (slot % 4 == 3) {
                            pg->KelvinPrimitive.SetTransformProgramLoad++;
							//KelvinPrimitive.SetTransformProgramLoad must be advanced
                            //for single time method program set, we may leave the SET_TRANSFORM_PROGRAM_LOAD along,
                            //but if the program is large and requires multiple times of method set, the SET_TRANSFORM_PROGRAM_LOAD might need to be update.
                            //need to verify the actual vertex shader data pushbuffer snapshot.
                            // TODO : Figure out if the actual NV2A increments SET_TRANSFORM_PROGRAM_LOAD / SetTransformProgramLoad per fully written slot, or only when the final slot is written to?
                            // Note : Given how it's technically possible to write to a slot above zero and stop before the end, which
                            // would allow writing another subset at a later time, it makes sense just process this as a partial write
                            // and thus only increase the load index when the final slot is written to.
                            // All Xbox D3D APIs we've analysed, allow for this potential NV2A behavior to be true (similar to how
                            // the entire set of 16 vertix attributes only get processed after the position slot was written to).
                        }
                    }
					// safe guard to make sure vertex shader program token parser won't went over the end of final slot.
					pg->vsh_program_slots[X_VSH_MAX_INSTRUCTION_COUNT][3] = 1; // TODO : Move this to immediately prior to parsing
                    */
                    // use CxbxSetVertexShaderSlots() directly, these codes come from CxbxrImpl_LoadVertexShaderProgram(). update pg->KelvinPrimitive.SetTransformProgramLoad accrodingly.
                    extern void CxbxSetVertexShaderSlots(DWORD * pTokens, DWORD Address, DWORD NrInstructions);
                    CxbxSetVertexShaderSlots((DWORD*) & argv[0], pg->KelvinPrimitive.SetTransformProgramLoad, (method_count / 4));
                    extern bool g_VertexShader_dirty; // tmp glue
                    // set vertex shader dirty flag
                    g_VertexShader_dirty = true;
                    pg->KelvinPrimitive.SetTransformProgramLoad += (method_count / 4);
                    break;
                }

                CASE_32(NV097_SET_TRANSFORM_CONSTANT, 4) : {//done //pg->KelvinPrimitive.SetTransformConstant[32]
                    //KelvinPrimitive.SetTransformConstant[32] is update already. we update the extra vertex shader contant array as well.
                    //because KelvinPrimitive.SetTransformConstant[32] can only hold 32 constant slots, so the max. constant slots can be set in one method is 32.
                    //for constants more than 32, must be split in batches. that is hardware limit.
                    //before calling the first NV097_SET_TRANSFORM_CONSTANT, NV097_SET_TRANSFORM_CONSTANT_LOAD must be called, or it will be used with the last set value,
                    //NV097_SET_TRANSFORM_CONSTANT_LOAD will be advanced when NV097_SET_TRANSFORM_CONSTANT was called with the the constant slots set.
                    //for continuous batch NV097_SET_TRANSFORM_CONSTANT_LOAD operation, NV097_SET_TRANSFORM_CONSTANT_LOAD only has to be set in the very first beginning.
                    slot = (method - NV097_SET_TRANSFORM_CONSTANT) / 4;
                    //slot is sopposed to be 0 here.
                    // ONLY INCREMENT IN BELOW COPY LOOP : pg->KelvinPrimitive.SetTransformConstantLoad += slot/4;
                    /*
                    for (int argc = 0; argc < method_count; argc++,slot++) {
                        arg0 = argv[argc];
                        //the target constant register address is prestored in NV097_SET_TRANSFORM_CONSTANT_LOAD  KelvinPrimitive.SetTransformConstantLoad
                        assert(pg->KelvinPrimitive.SetTransformConstantLoad < NV2A_VERTEXSHADER_CONSTANTS);
						// TODO : Since this data is also copied outside Kelvin, we could bypass setting KelvinPrimitive fields
						// VertexShaderConstant *vsh_constant = &pg->vsh_constants[const_load];
                        if ((arg0 != pg->vsh_constants[pg->KelvinPrimitive.SetTransformConstantLoad][slot % 4])) {
                            pg->vsh_constants_dirty[pg->KelvinPrimitive.SetTransformConstantLoad] |= 1;
                            pg->vsh_constants[pg->KelvinPrimitive.SetTransformConstantLoad][slot % 4] = arg0;
                        }
                        //pg->KelvinPrimitive.SetTransformConstant[32] is not enough for xbox d3d, pgraph uses vsh_constants[192][4] to store vertex shader program
                        if (slot % 4 == 3) {
                            pg->KelvinPrimitive.SetTransformConstantLoad++; //pg->KelvinPrimitive.SetTransformConstantLoad must be advanced.
                            // TODO : Figure out if the actual NV2A increments NV097_SET_TRANSFORM_CONSTANT_LOAD / SetTransformConstantLoad per fully written slot, or only when the final slot is written to?
                        }
                    }
                    */
                    // use CxbxrImpl_SetVertexShaderConstant() directly and update KelvinPrimitive.SetTransformConstantLoad accrodingly.
                    CxbxrImpl_SetVertexShaderConstant(pg->KelvinPrimitive.SetTransformConstantLoad- X_D3DSCM_CORRECTION,&argv[0],method_count/4);
                    
                    
                    //pick up SuperSampleScaleX/SuperSampleScaleY/ZScale/wScale after calling CommonSetPassthroughProgram().
                    if (g_VertexShader_dirty == true && g_Xbox_VertexShaderMode == VertexShaderMode::Passthrough && pg->KelvinPrimitive.SetTransformConstantLoad==0) {
                        //float tempConstant[4];
                        // read constant register 0, CommonSetPassThroughProgram() sets register 0 constant with SuperSampleScaleX/Y
                        //CxbxrImpl_GetVertexShaderConstant(0 - X_D3DSCM_CORRECTION, tempConstant, 1);
                        extern void CxbxrSetSuperSampleScaleXY(float x, float y);
                        extern void CxbxrSetScreenSpaceOffsetXY(float x, float y);
                        extern void CxbxrSetZScale(float z);
                        extern void CxbxrSetWScale(float w);
                        CxbxrSetSuperSampleScaleXY(DWtoF(argv[0]), DWtoF(argv[1]));
                        CxbxrSetZScale(DWtoF(argv[2]));
                        CxbxrSetWScale(DWtoF(argv[3]));
                        float multiSampleOffset = ((pg->KelvinPrimitive.SetAntiAliasingControl & NV097_SET_ANTI_ALIASING_CONTROL_ENABLE_TRUE) != 0)? 0.5f:0.0f;

                        CxbxrSetScreenSpaceOffsetXY(DWtoF(argv[4])- multiSampleOffset, DWtoF(argv[5])- multiSampleOffset);

                    }

                    pg->KelvinPrimitive.SetTransformConstantLoad += (method_count / 4);
                }
                    break;
                

                /* Handles NV097_SET_BACK_LIGHT* */
                CASE_128(NV097_SET_BACK_LIGHT_AMBIENT_COLOR, 4): { //done  //pg->KelvinPrimitive.SetBackLight[8]. {AmbientColor[3],DiffuseColor[3],SpecularColor[3],Rev_0c24[7]}
					slot = (method - NV097_SET_BACK_LIGHT_AMBIENT_COLOR) / 4;
                    /* disable LLE code
                    for (size_t arg_c = 0; arg_c < method_count; arg_c++,slot++) {
                        arg0 = argv[arg_c];
                        unsigned int part =  slot % 16;
                        unsigned int light_index = slot / 16; // [Light index], we have 8 lights, each light holds 16 dwords 
                        assert(light_index < 8);
                        switch(part) {//check the definition of pg->ltctxb, then correlate to KelvinPrimitive.SetBackLight.???
                            //CASE_3(NV097_SET_BACK_LIGHT_AMBIENT_COLOR, 4):
                            CASE_3(0, 1) ://NV097_SET_BACK_LIGHT_AMBIENT_COLOR
                                //part -= NV097_SET_BACK_LIGHT_AMBIENT_COLOR / 4;
                                pg->ltctxb[NV_IGRAPH_XF_LTCTXB_L0_BAMB + light_index *6][part] = arg0;
                                pg->ltctxb_dirty[NV_IGRAPH_XF_LTCTXB_L0_BAMB + light_index *6] = true;
                                break;
                            //CASE_3(NV097_SET_BACK_LIGHT_DIFFUSE_COLOR, 4):
                            CASE_3(3, 1) ://NV097_SET_BACK_LIGHT_DIFFUSE_COLOR
                                //part -= NV097_SET_BACK_LIGHT_DIFFUSE_COLOR / 4;
                                pg->ltctxb[NV_IGRAPH_XF_LTCTXB_L0_BDIF + light_index *6][part] = arg0;
                                pg->ltctxb_dirty[NV_IGRAPH_XF_LTCTXB_L0_BDIF + light_index *6] = true;
                                break;
                            //CASE_3(NV097_SET_BACK_LIGHT_SPECULAR_COLOR, 4):
                            CASE_3(6, 1):
                                //part -= NV097_SET_BACK_LIGHT_SPECULAR_COLOR / 4;
                                pg->ltctxb[NV_IGRAPH_XF_LTCTXB_L0_BSPC + light_index *6][part] = arg0;
                                pg->ltctxb_dirty[NV_IGRAPH_XF_LTCTXB_L0_BSPC + light_index *6] = true;
                                break;
                            default:
                                assert(false);
                                break;
                        }
                    }
                    */
					//not implement //pg->KelvinPrimitive.SetBackSceneAmbientColor[3]
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                }
                /* Handles all the light source props except for NV097_SET_BACK_LIGHT_* */
                CASE_256(NV097_SET_LIGHT_AMBIENT_COLOR, 4): {//pg->KelvinPrimitive.SetLight[8].{AmbientColor[3],DiffuseColor[3],SpecularColor[3],LocalRange,InfiniteHalfVector[3],InfiniteDirection[3],SpotFalloff[3],SpotDirection[4],LocalPosition[3],LocalAttenuation[3],Rev_1074[3]}
					slot = (method - NV097_SET_LIGHT_AMBIENT_COLOR) / 4;

                    for (size_t arg_count = 0; arg_count < method_count; arg_count++,slot++) {
                        arg0 = ldl_le_p(argv + arg_count);

                        unsigned int part = slot % 32;
                        unsigned int light_index = slot / 32; /* [Light index] */ //each light holds 32 dwords 
                        assert(light_index < 8);
                        /*  //disable LLE OpenGL code
                        switch(part) {//check the definition of pg->ltctxb, then correlate to KelvinPrimitive.SetBackLight.???
                            CASE_3(0, 1)://NV097_SET_LIGHT_AMBIENT_COLOR
                                //part -= NV097_SET_LIGHT_AMBIENT_COLOR / 4;
                                pg->ltctxb[NV_IGRAPH_XF_LTCTXB_L0_AMB + light_index *6][part] = arg0;
                                pg->ltctxb_dirty[NV_IGRAPH_XF_LTCTXB_L0_AMB + light_index *6] = true;
                                break;
                            CASE_3(3, 1)://NV097_SET_LIGHT_DIFFUSE_COLOR
                                //part -= NV097_SET_LIGHT_DIFFUSE_COLOR / 4;
                                pg->ltctxb[NV_IGRAPH_XF_LTCTXB_L0_DIF + light_index *6][part] = arg0;
                                pg->ltctxb_dirty[NV_IGRAPH_XF_LTCTXB_L0_DIF + light_index *6] = true;
                                break;
                            CASE_3(6, 1)://NV097_SET_LIGHT_SPECULAR_COLOR
                                //part -= NV097_SET_LIGHT_SPECULAR_COLOR / 4;
                                pg->ltctxb[NV_IGRAPH_XF_LTCTXB_L0_SPC + light_index *6][part] = arg0;
                                pg->ltctxb_dirty[NV_IGRAPH_XF_LTCTXB_L0_SPC + light_index *6] = true;
                                break;
                            case 9://NV097_SET_LIGHT_LOCAL_RANGE:
                                pg->ltc1[NV_IGRAPH_XF_LTC1_r0 + light_index][0] = arg0;
                                pg->ltc1_dirty[NV_IGRAPH_XF_LTC1_r0 + light_index] = true;
                                break;
                            CASE_3(10,1)://NV097_SET_LIGHT_INFINITE_HALF_VECTOR
                                //part -= NV097_SET_LIGHT_INFINITE_HALF_VECTOR / 4;
                                //KelvinPrimitive.SetLight[8].InfiniteHalfVector[3]
                                //pg->light_infinite_half_vector[light_index][part] = *(float*)&arg0;
                                break;
                            CASE_3(13,1)://NV097_SET_LIGHT_INFINITE_DIRECTION
                                //part -= NV097_SET_LIGHT_INFINITE_DIRECTION / 4;
                                //KelvinPrimitive.SetLight[8].InfiniteDirection[3]
                                //pg->light_infinite_direction[light_index][part] = *(float*)&arg0;
                                break;
                            CASE_3(16,1)://NV097_SET_LIGHT_SPOT_FALLOFF
                                //part -= NV097_SET_LIGHT_SPOT_FALLOFF / 4;
                                pg->ltctxa[NV_IGRAPH_XF_LTCTXA_L0_K + light_index *2][part] = arg0;
                                pg->ltctxa_dirty[NV_IGRAPH_XF_LTCTXA_L0_K + light_index *2] = true;
                                break;
                            CASE_4(19,1)://NV097_SET_LIGHT_SPOT_DIRECTION
                                //part -= NV097_SET_LIGHT_SPOT_DIRECTION / 4;
                                //KelvinPrimitive.SetLight[8].SpotDirection[4]
                                pg->ltctxa[NV_IGRAPH_XF_LTCTXA_L0_SPT + light_index *2][part] = arg0;
                                pg->ltctxa_dirty[NV_IGRAPH_XF_LTCTXA_L0_SPT + light_index *2] = true;
                                break;
                            CASE_3(23,1)://NV097_SET_LIGHT_LOCAL_POSITION
                                //part -= NV097_SET_LIGHT_LOCAL_POSITION / 4;
                                //KelvinPrimitive.SetLight[8].LocalPosition[]
                                //pg->light_local_position[light_index][part] = *(float*)&arg0;
                                break;
                            CASE_3(26,1)://NV097_SET_LIGHT_LOCAL_ATTENUATION
                                //part -= NV097_SET_LIGHT_LOCAL_ATTENUATION / 4;
                                //pg->KelvinPrimitive.SetLight[8].LocalAttenuation[3]
                                //pg->light_local_attenuation[light_index][part] = *(float*)&arg0;
                                break;
                            default:
                                //assert(false);
								//Rev_1074[3]
								break;
                        }
                        */
                    }
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                }
            //this state is not implement yet. may not be used in xbox
                case NV097_SET_STIPPLE_CONTROL: //not implement //pg->KelvinPrimitive.SetStippleControl
                    break;
                    
            //this state is not implement yet. may not be used in xbox
                CASE_32(NV097_SET_STIPPLE_PATTERN, 4):
                    slot = (method - NV097_SET_STIPPLE_PATTERN) / 4;
                    break;

				CASE_3(NV097_SET_VERTEX3F, 4) : { //pg->KelvinPrimitive.SetVertex3f[3]: 
					assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
					//assuming the same usage pattern as SET_VERTEX_DATA4f, multiple argv[] in one method call, assert if not
					assert(method_count == 3);
					slot = NV2A_VERTEX_ATTR_POSITION; // Countrary to method NV097_SET_VERTEX_DATA*, NV097_SET_VERTEX[34]F always target the first slot (index zero : the vertex position attribute)
					for (unsigned argc = 0; argc < method_count; argc++) {
						arg0 = argv[argc];
						int part = (method - NV097_SET_VERTEX3F + argc) % 4;
						float *inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
						inline_value[part] = pg->KelvinPrimitive.SetVertex3f[part];
						if (part == 2) { // Note : no check needed on (slot == NV2A_VERTEX_ATTR_POSITION), as it can't differ here
							inline_value[3] = 1.0f;
							pgraph_finish_inline_buffer_vertex(pg);
						}
					}
					break;
				}

				CASE_4(NV097_SET_VERTEX4F, 4) :{ //pg->KelvinPrimitive.SetVertex4f[4]
					assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
					//assuming the same usage pattern as SET_VERTEX_DATA4f, multiple argv[] in one method call, assert if not
					assert(method_count == 4);
					slot = NV2A_VERTEX_ATTR_POSITION; // Countrary to method NV097_SET_VERTEX_DATA*, NV097_SET_VERTEX[34]F always target the first slot (index zero : the vertex position attribute)
					for (unsigned argc = 0; argc < method_count; argc++) {
						int part = (method - NV097_SET_VERTEX4F + argc) % 4;
						float *inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
						inline_value[part] = pg->KelvinPrimitive.SetVertex4f[part];
						if (part == 3) { // Note : no check needed on (slot == NV2A_VERTEX_ATTR_POSITION), as it can't differ here
							//vertex completed, push all attributes to vertex buffer.
							pgraph_finish_inline_buffer_vertex(pg);
						}
					}
					break;
			    }
#if(0)  //missing state implememtations, need verifications.

#   define NV097_SET_VERTEX4S                                 0x00001528 // [2]
#   define NV097_SET_NORMAL3F                                 0x00001530 // [3]
#   define NV097_SET_NORMAL3S                                 0x00001540 // [2]
#   define NV097_SET_DIFFUSE_COLOR4F                          0x00001550 // [4]
#   define NV097_SET_DIFFUSE_COLOR3F                          0x00001560 // [3]
#   define NV097_SET_DIFFUSE_COLOR4UB                         0x0000156C
#   define NV097_SET_SPECULAR_COLOR4F                         0x00001570 // [4]
#   define NV097_SET_SPECULAR_COLOR3F                         0x00001580 // [3]
#   define NV097_SET_SPECULAR_COLOR4UB                        0x0000158C
#   define NV097_SET_TEXCOORD0_2F                             0x00001590 // [2]
#   define NV097_SET_TEXCOORD0_2S                             0x00001598
#   define NV097_SET_TEXCOORD0_4F                             0x000015A0 // [4]
#   define NV097_SET_TEXCOORD0_4S                             0x000015B0 // [2]
#   define NV097_SET_TEXCOORD1_2F                             0x000015B8 // [2]
#   define NV097_SET_TEXCOORD1_2S                             0x000015C0
#   define NV097_SET_TEXCOORD1_4F                             0x000015C8 // [4]
#   define NV097_SET_TEXCOORD1_4S                             0x000015D8 // [2]
#   define NV097_SET_TEXCOORD2_2F                             0x000015E0 // [2]
#   define NV097_SET_TEXCOORD2_2S                             0x000015E8
#   define NV097_SET_TEXCOORD2_4F                             0x000015F0 // [4]
#   define NV097_SET_TEXCOORD2_4S                             0x00001600 // [2]
#   define NV097_SET_TEXCOORD3_2F                             0x00001608 // [2]
#   define NV097_SET_TEXCOORD3_2S                             0x00001610
#   define NV097_SET_TEXCOORD3_4F                             0x00001620 // [4]
#   define NV097_SET_TEXCOORD3_4S                             0x00001630 // [2]
#   define NV097_SET_FOG1F                                    0x00001698
#   define NV097_SET_WEIGHT1F                                 0x0000169C
#   define NV097_SET_WEIGHT2F                                 0x000016A0 // [2]
#   define NV097_SET_WEIGHT3F                                 0x000016B0 // [3]
#   define NV097_SET_EDGE_FLAG                                0x000016BC
#   define NV097_SET_WEIGHT4F                                 0x000016C0 // [4]
#   define NV097_SET_TRANSFORM_FIXED_CONST3                   0x000016D0 // [4]
#   define NV097_SET_TRANSFORM_FIXED_CONST0                   0x000016E0 // [4]
#   define NV097_SET_TRANSFORM_FIXED_CONST1                   0x000016F0 // [4]
#   define NV097_SET_TRANSFORM_FIXED_CONST2                   0x00001700 // [4]
#   define NV097_INVALIDATE_VERTEX_CACHE_FILE                 0x00001710
#   define NV097_INVALIDATE_VERTEX_FILE                       0x00001714
#   define NV097_TL_NOP                                       0x00001718
#   define NV097_TL_SYNC                                      0x0000171C


#endif

				CASE_16(NV097_SET_VERTEX_DATA_ARRAY_OFFSET, 4) : //pg->KelvinPrimitive.SetVertexDataArrayOffset[16]
				//pg->KelvinPrimitive.SetVertexDataArrayOffset[i] = vertex buffer address start + offset of Attribute [i]
					slot = (method - NV097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4;
                    for (size_t argc = 0; argc < method_count; argc++, slot ++) {
                            arg0 = argv[argc];
                            pg->vertex_attributes[slot].dma_select =
                                arg0 & 0x80000000;
                            pg->vertex_attributes[slot].offset =
                                arg0 & 0x7fffffff;
                            pg->vertex_attributes[slot].converted_elements = 0;
                    }
					break;

                CASE_16(NV097_SET_VERTEX_DATA_ARRAY_FORMAT, 4):{ //done //pg->KelvinPrimitive.SetVertexDataArrayFormat[16]
					//pg->KelvinPrimitive.SetVertexDataArrayFormat[i] = Attribute [i].Format (SizeAndType) &0xFF + if (draw up method?)Stride << 8 : 0
					slot = (method - NV097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4;
					for (size_t argc = 0; argc < method_count; argc++,slot++) {
                        arg0 = argv[argc];
						VertexAttribute *vertex_attribute = &pg->vertex_attributes[slot];
                        vertex_attribute->format =
                            GET_MASK(arg0, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE);
                        vertex_attribute->count =
                            GET_MASK(arg0, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE);
                        vertex_attribute->stride =
                            GET_MASK(arg0, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE);

                        NV2A_DPRINTF("vertex data array format=%d, count=%d, stride=%d\n",
                            vertex_attribute->format,
                            vertex_attribute->count,
                            vertex_attribute->stride);

                        vertex_attribute->gl_count = vertex_attribute->count;

                        switch (vertex_attribute->format) {
                            case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
                                vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
                                vertex_attribute->gl_normalize = GL_TRUE;
                                vertex_attribute->size = 1;
                                assert(vertex_attribute->count == 4);
                                // https://www.opengl.org/registry/specs/ARB/vertex_array_bgra.txt
                                vertex_attribute->gl_count = GL_BGRA;
                                vertex_attribute->needs_conversion = false;
                                break;
                            case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
                                vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
                                vertex_attribute->gl_normalize = GL_TRUE;
                                vertex_attribute->size = 1;
                                vertex_attribute->needs_conversion = false;
                                break;
                            case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
                                vertex_attribute->gl_type = GL_SHORT;
                                vertex_attribute->gl_normalize = GL_TRUE;
                                vertex_attribute->size = 2;
                                vertex_attribute->needs_conversion = false;
                                break;
                            case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
                                vertex_attribute->gl_type = GL_FLOAT;
                                vertex_attribute->gl_normalize = GL_FALSE;
                                vertex_attribute->size = 4;
                                vertex_attribute->needs_conversion = false;
                                break;
                            case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K:
                                vertex_attribute->gl_type = GL_SHORT;
                                vertex_attribute->gl_normalize = GL_FALSE;
                                vertex_attribute->size = 2;
                                vertex_attribute->needs_conversion = false;
                                break;
                            case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
                                /* 3 signed, normalized components packed in 32-bits. (11,11,10) */
                                vertex_attribute->size = 4;
                                vertex_attribute->gl_type = GL_FLOAT;
                                vertex_attribute->gl_normalize = GL_FALSE;
                                vertex_attribute->needs_conversion = true;
                                vertex_attribute->converted_size = sizeof(float);
                                vertex_attribute->converted_count = 3 * vertex_attribute->count;
                                break;
                            default:
                                fprintf(stderr, "Unknown vertex type: 0x%x\n", vertex_attribute->format);
                                assert(false);
                                break;
                        }
						if (vertex_attribute->needs_conversion) {
                            vertex_attribute->converted_elements = 0;
                        } else {
                            if (vertex_attribute->converted_buffer) {
                                g_free(vertex_attribute->converted_buffer);
                                vertex_attribute->converted_buffer = NULL;
                            }
                        }
                    }
                    break;
				}
                CASE_3(NV097_SET_BACK_SCENE_AMBIENT_COLOR,4)://not implement //pg->KelvinPrimitive.SetBackSceneAmbientColor[3]
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;
                case NV097_SET_BACK_MATERIAL_ALPHA://not implement //pg->KelvinPrimitive.SetBackMaterialAlpha
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break;          
                CASE_3(NV097_SET_BACK_MATERIAL_EMISSIONR,4)://not implement //pg->KelvinPrimitive.SetBackMaterialEmission[3]
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;
					break; 

                case NV097_SET_LOGIC_OP_ENABLE://done, D3DRS_LOGICOP //pg->KelvinPrimitive.SetLogicOpEnable, set to false when D3D__RenderState[D3DRS_LOGICOP] == D3DLOGICOP_NONE, 0x0. set to true when !=D3DLOGICOP_NONE. actual Logic_OP stored in KelvinPrimitive.SetLogicOp
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_BLEND / 4],
                    //		 NV_PGRAPH_BLEND_LOGICOP_ENABLE, arg0);
                    /*
                    if (pg->KelvinPrimitive.SetLogicOp == 0)
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_LOGICOP, 0);
                    else if (method_count == 2)//xbox sets NV097_SET_LOGIC_OP together with NV097_SET_LOGIC_OP_ENABLE in one method with 2 method counts.
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_LOGICOP, argv[1]);
                    break;
                    */
                case NV097_SET_LOGIC_OP://done, D3DRS_LOGICOP //pg->KelvinPrimitive.SetLogicOp
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_BLEND / 4],
                    //		 NV_PGRAPH_BLEND_LOGICOP, arg0 & 0xF);
                    if (pg->KelvinPrimitive.SetLogicOp != 0) {
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_LOGICOP, pg->KelvinPrimitive.SetLogicOp);
                    }
                    else//D3DLOGICOP_NONE)
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_LOGICOP, 0);
                    break;

                case NV097_SET_TWO_SIDED_LIGHT_EN://not implement, D3D9 not support //pg->KelvinPrimitive.SetTwoSidedLightEn
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_TWOSIDEDLIGHTING, pg->KelvinPrimitive.SetTwoSidedLightEn);
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_LIGHTS;

					break;  

                case NV097_CLEAR_REPORT_VALUE://done //pg->KelvinPrimitive.ClearReportValue

                    /* FIXME: Does this have a value in parameter? Also does this (also?) modify
                     *        the report memory block?
                     */
                    if (pg->gl_zpass_pixel_count_query_count) {
                        if (pg->opengl_enabled) {
                            glDeleteQueries(pg->gl_zpass_pixel_count_query_count,
                                            pg->gl_zpass_pixel_count_queries);
                        }
                        pg->gl_zpass_pixel_count_query_count = 0;
                    }
                    pg->zpass_pixel_count_result = 0;

                    break;

                case NV097_SET_ZPASS_PIXEL_COUNT_ENABLE://done //pg->KelvinPrimitive.SetZpassPixelCountEnable
                    //pg->KelvinPrimitive.SetZpassPixelCountEnable
                    //pg->zpass_pixel_count_enable = arg0;
                    break;

                case NV097_GET_REPORT: {//done //pg->KelvinPrimitive.GetReport
                    /* FIXME: This was first intended to be watchpoint-based. However,
                     *        qemu / kvm only supports virtual-address watchpoints.
                     *        This'll do for now, but accuracy and performance with other
                     *        approaches could be better
                     */
                    uint8_t type = GET_MASK(arg0, NV097_GET_REPORT_TYPE);
                    assert(type == NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT);
                    hwaddr offset = GET_MASK(arg0, NV097_GET_REPORT_OFFSET);

                    uint64_t timestamp = 0x0011223344556677; /* FIXME: Update timestamp?! */
                    uint32_t done = 0;

                    if (pg->opengl_enabled) {
                        /* FIXME: Multisampling affects this (both: OGL and Xbox GPU),
                         *        not sure if CLEARs also count
                         */
                        /* FIXME: What about clipping regions etc? */
                        for(i = 0; i < pg->gl_zpass_pixel_count_query_count; i++) {
                            GLuint gl_query_result;
                            glGetQueryObjectuiv(pg->gl_zpass_pixel_count_queries[i],
                                                GL_QUERY_RESULT,
                                                &gl_query_result);
                            pg->zpass_pixel_count_result += gl_query_result;
                        }
                        if (pg->gl_zpass_pixel_count_query_count) {
                            glDeleteQueries(pg->gl_zpass_pixel_count_query_count,
                                            pg->gl_zpass_pixel_count_queries);
                        }
                        pg->gl_zpass_pixel_count_query_count = 0;

                        hwaddr report_dma_len;
                        uint8_t *report_data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaReport,
                                                                    &report_dma_len);
                        assert(offset < report_dma_len);
                        report_data += offset;

                        stq_le_p((uint64_t*)&report_data[0], timestamp);
                        stl_le_p((uint32_t*)&report_data[8], pg->zpass_pixel_count_result);
                        stl_le_p((uint32_t*)&report_data[12], done);
                    }

                    break;
                }

                CASE_3(NV097_SET_TL_CONST_ZERO, 4) :break;//not implement yet //pg->KelvinPrimitive.SetTLConstZero[3]

                CASE_3(NV097_SET_EYE_DIRECTION, 4)://done //pg->KelvinPrimitive.SetEyeDirection[3]
					slot = (method - NV097_SET_EYE_DIRECTION) / 4;
                    for (size_t argc = 0; argc < method_count; argc++, slot += 4) {
                        arg0 = argv[argc];

                        pg->ltctxa[NV_IGRAPH_XF_LTCTXA_EYED][slot] = arg0;
                        pg->ltctxa_dirty[NV_IGRAPH_XF_LTCTXA_EYED] = true;
                    }
                    break;

                CASE_3(NV097_SET_LINEAR_FOG_CONST, 4) :break;//not implement yet //pg->KelvinPrimitive.SetLinearFogConst[3]
                case NV097_SET_SHADER_CLIP_PLANE_MODE:
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SHADER_STAGE_PROGRAM;
                    break;  //not implement //pg->KelvinPrimitive.SetShaderClipPlaneMode

//**********************this case is very complicate, need more time to verify it.
                case NV097_SET_BEGIN_END: {//consider done  //pg->KelvinPrimitive.SetBeginEnd
        //this case is critical, xbox d3d calls this case twice to enclose the DrawVerticesUp/DrawVert.... calls.
                    //uint32_t control_0 = pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4];
                    //uint32_t control_1 = pg->pgraph_regs[NV_PGRAPH_CONTROL_1 / 4];

                    bool depth_test = pg->KelvinPrimitive.SetCullFaceEnable!=0;
                    bool stencil_test = pg->KelvinPrimitive.SetStencilTestEnable!=0;

                    if (arg0 == NV097_SET_BEGIN_END_OP_END) { // the DrawXXX call completes the pushbuffer operation. we can process the rendering.
						// D3DDevice_PrimeVertexCache() will call NV097_SET_BEGIN_END twice in series, with pritive type in 1st call, OP_END in 2nd call, nothing in between.
                        // this is to flush out any vertices cached in the transform/lighting stages.
                        // then it will begin passing in indexed buffer data with NV097_ARRAY_ELEMENT16/NV097_ARRAY_ELEMENT32 without calling NV097_SET_BEGIN_END with primitive type setting.
                        // this is to fill transform/lighting stages with vertex first.
                        if (pg->draw_mode == DrawMode::None|| pg->draw_mode == DrawMode::PrimeVertexCache) {
                            pg->draw_mode = DrawMode::PrimeVertexCache;
                            pg->primitive_mode = arg0;
                            break;
                        }
                        switch (pg->draw_mode) {
                        case  DrawMode::DrawArrays:  {
                            NV2A_GL_DPRINTF(false, "Draw Arrays");

                            assert(pg->draw_arrays_length > 0);
                            assert(pg->inline_array_length == 0);
                            assert(pg->inline_buffer_length == 0);
                            assert(pg->inline_elements_length == 0);

                            pgraph_use_NV2A_Kelvin();
                            // set vertex declaration override, will be used for the next draw :
                            //set_IVB_DECL_override();

                            //we shall update the pgraph_draw_state_update(d) before we are really calling the HLE draw calls.
                            //because the vertex attr comes from different sources, depends on which how the vertex data are transfefred to NV2A. and it's not decided yet in this moment.
                            if (pgraph_draw_state_update != nullptr) {
                                pgraph_draw_state_update(d);
                            }

                            if (pgraph_draw_arrays != nullptr) {
                                pgraph_draw_arrays(d);
                            }

                            //reset the vertex declaration after we finish the draw call
                            //reset_IVB_DECL_override();
                            pgraph_notuse_NV2A_Kelvin();
                            break;
                        }
                        case DrawMode::InlineBuffer: { // for draw calls using SET_BEGIN_ENG(primitive)/SET_VERTEX_DATAXXX ... /SET_BEGIN_ENG(0)
                            NV2A_GL_DPRINTF(false, "Inline Buffer");

                            assert(pg->draw_arrays_length == 0);
                            assert(pg->inline_array_length == 0);
                            assert(pg->inline_buffer_length > 0);
                            assert(pg->inline_elements_length == 0);


                            pgraph_use_NV2A_Kelvin();

                            // set vertex declaration override, will be used for the next draw :
                            //set_IVB_DECL_override();

                            //we shall update the pgraph_draw_state_update(d) before we are really calling the HLE draw calls.
                            //because the vertex attr comes from different sources, depends on which how the vertex data are transfefred to NV2A. and it's not decided yet in this moment.
                            if (pgraph_draw_state_update != nullptr) {
                                pgraph_draw_state_update(d);
                            }

                            if (pgraph_draw_inline_buffer != nullptr) {
                                pgraph_draw_inline_buffer(d);
                            }

                            //reset the vertex declaration after we finish the draw call
                            //reset_IVB_DECL_override();

                            pgraph_notuse_NV2A_Kelvin();
                            break;
                        }
                        case DrawMode::InlineArray: {// d3d DrawVerticesUP()
                            NV2A_GL_DPRINTF(false, "Inline Array");

                            assert(pg->draw_arrays_length == 0);
                            assert(pg->inline_array_length > 0);
                            assert(pg->inline_buffer_length == 0);
                            assert(pg->inline_elements_length == 0);

                            pgraph_use_NV2A_Kelvin();
                            // set vertex declaration override, will be used for the next draw :
                            //set_IVB_DECL_override();

                            //we shall update the pgraph_draw_state_update(d) before we are really calling the HLE draw calls.
                            //because the vertex attr comes from different sources, depends on which how the vertex data are transfefred to NV2A. and it's not decided yet in this moment.
                            if (pgraph_draw_state_update != nullptr) {
                                pgraph_draw_state_update(d);
                            }

                            if (pgraph_draw_inline_array != nullptr && pg->inline_array_length > 0) {
                                pgraph_draw_inline_array(d);
                            }

                            //reset the vertex declaration after we finish the draw call
                            //reset_IVB_DECL_override();
                            pgraph_notuse_NV2A_Kelvin();
                            break;
                        }
                        case DrawMode::InlineElements: {
                            NV2A_GL_DPRINTF(false, "Inline Elements");

                            assert(pg->draw_arrays_length == 0);
                            assert(pg->inline_array_length == 0);
                            assert(pg->inline_buffer_length == 0);
                            assert(pg->inline_elements_length > 0);

                            pgraph_use_NV2A_Kelvin();
                            // set vertex declaration override, will be used for the next draw :
                            // set_IVB_DECL_override();

                            //we shall update the pgraph_draw_state_update(d) before we are really calling the HLE draw calls.
                            //because the vertex attr comes from different sources, depends on which how the vertex data are transfefred to NV2A. and it's not decided yet in this moment.
                            if (pgraph_draw_state_update != nullptr) {
                                pgraph_draw_state_update(d);
                            }

                            if (pgraph_draw_inline_elements != nullptr) {
                                pgraph_draw_inline_elements(d);
                            }

                            //reset the vertex declaration after we finish the draw call
                            //reset_IVB_DECL_override();
                            pgraph_notuse_NV2A_Kelvin();
                            break;
                        }
                        default:
                            NV2A_GL_DPRINTF(true, "EMPTY NV097_SET_BEGIN_END");
                            assert(false);
                        } // switch
						
						// Reset draw_mode
						pg->draw_mode = DrawMode::None;

                        // Only clear primitive_mode after we finish draw call
                        pg->primitive_mode = NV097_SET_BEGIN_END_OP_END;

                    } else {

                        assert(arg0 == pg->KelvinPrimitive.SetBeginEnd); // Verify pg->regs[NV097_SET_BEGIN_END] is reflected in union KelvinPrimitive.SetBeginEnd
                        assert(arg0 <= NV097_SET_BEGIN_END_OP_POLYGON); // Verify the specified primitive mode is inside the valid range

                        // Copy arg0/KelvinPrimitive.SetBeginEnd, because we still need this value when
                        // it gets overwritten by NV097_SET_BEGIN_END_OP_END (which triggers the draw) :
                        pg->primitive_mode = arg0; // identical to reading pg->KelvinPrimitive.SetBeginEnd
                        //only initialize these states when we're not in PrimeVertexCache mode.
                        if (pg->draw_mode == DrawMode::PrimeVertexCache)
                            // use DrawMode::PrimeVertexCache (0x10)as bit flag.
                            pg->draw_mode = DrawMode::None;

                        //init in inline_elements_length for indexed draw calls, which vertex buffers are set in KelvinPrimitive.SetVertexDataOffset[16], vertex attrs are set in KelvinPrimitive.SetVertexDataFormat[16]
                        pg->inline_elements_length = 0;
                        //init in inline_array_length for drawUP draw calls, which vertices are pushed to pushbuffer, vertex attrs are set in KelvinPrimitive.SetVertexDataFormat[16]
                        pg->inline_array_length = 0;
                        //init in inline_elements_length for non indexed draw calls, which vertex buffers are set in KelvinPrimitive.SetVertexDataOffset[16], vertex attrs are set in KelvinPrimitive.SetVertexDataFormat[16]
                        pg->draw_arrays_length = 0;
                        //pg->draw_arrays_min_start = -1;
                        pg->draw_arrays_max_count = 0;
                        //init in inline_buffer_length for draw calls using BeginEng()/SetVertexDataColor()/SetVertexData4f(), which vertices are pushed to pushbuffer, vertex attrs must be collected during each SetVertexDataXX() calls.
                        pg->inline_buffer_length = 0;//this counts the total vertex count
                        pg->inline_buffer_attr_length = 0;//this counts the total attr counts. let's say if we have i vertices, and a attrs for each vertex, and inline_buffer_attr_length == i * a; this is for the ease of vertex setup process.
                        //reset attribute flag for in_line_buffer
                        for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
                            //reset the attribute flag for next draw call.
                            pg->vertex_attributes[i].set_by_inline_buffer = false;
                        }
                        
                    }

                    //pgraph_set_surface_dirty(pg, true, depth_test || stencil_test);
					break;
                }

                case NV097_ARRAY_ELEMENT16://xbox D3DDevice_DrawIndexedVertices() calls this
					//assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
                    //we do nothing with PrimeVertexCache
                    if (pg->draw_mode == DrawMode::PrimeVertexCache)
                        break;
                    if (pg->draw_mode == DrawMode::None)
						pg->draw_mode = DrawMode::InlineElements;
					else
						assert(pg->draw_mode == DrawMode::InlineElements);

					//NV2A hardware limit 2048 pair of index16 max., xbox d3d block size 511 dword (1022 vertices)max.verified with Otogi
					//  (NV097_SET_BEGIN_END, PrimitiveType)
					//->if(alignment required) (NV097_ARRAY_ELEMENT16,alignment)
					//->if(block transfer required)loop block transfer (NV097_ARRAY_ELEMENT16,511)
					//->if(vertices remained) (NV097_ARRAY_ELEMENT16,vertices_remained)
					//->if(last_odd_vertex) (NV097_ARRAY_ELEMENT32,vertices_remained)
					//NV097_ARRAY_ELEMENT16 must always send out vertices in pair. it can't send single vertex witl mask 0x0000FFFF, that will introduce bug.
					//LOG_TEST_CASE("NV2A_VB_ELEMENT_U16");	
                    // Test-case : Turok (in main menu)	
                    // Test-case : Hunter Redeemer	
                    // Test-case : Otogi (see https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/pull/1113#issuecomment-385593814)
                    for (size_t argc = 0; argc < method_count; argc++) {
                        arg0 = argv[argc];
                        assert(pg->inline_elements_length < NV2A_MAX_BATCH_LENGTH);
                        pg->inline_elements[
                            pg->inline_elements_length++] = arg0 & 0xFFFF;
                        pg->inline_elements[
                            pg->inline_elements_length++] = arg0 >> 16;
                    }
					break;

                case NV097_ARRAY_ELEMENT32://xbox D3DDevice_DrawIndexedVertices() calls this
					//assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
                    //we do nothing with PrimeVertexCache
                    if (pg->draw_mode == DrawMode::PrimeVertexCache)
                        break;
                    if (pg->draw_mode == DrawMode::None)
						pg->draw_mode = DrawMode::InlineElements;
					else
						assert(pg->draw_mode == DrawMode::InlineElements);

					// xbox d3d uses NV097_ARRAY_ELEMENT32 to send the very last odd vertex of index vertex stream.
	                //LOG_TEST_CASE("NV2A_VB_ELEMENT_U32");	
                    // Test-case : Turok (in main menu)
                    for (size_t argc = 0; argc < method_count; argc++) {
                        arg0 = argv[argc];
                        assert(pg->inline_elements_length < NV2A_MAX_BATCH_LENGTH);
                        pg->inline_elements[
                            pg->inline_elements_length++] = arg0;

                    }

                    break;
                case NV097_DRAW_ARRAYS: {
					assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
					if (pg->draw_mode == DrawMode::None)
						pg->draw_mode = DrawMode::DrawArrays;
					else
						assert(pg->draw_mode == DrawMode::DrawArrays);

					/*
					D3DDevice_DrawVertices(
						D3DPRIMITIVETYPE PrimitiveType,
						UINT StartVertex,
						UINT VertexCount)
					(NV097_SET_BEGIN_END, PrimitiveType)1
					BlockSize=256
					ArraysCount= total vertex count / BlockSize  + 1, min. 1 array
					(PUSHER_NOINC(NV097_DRAW_ARRAYS), ArraysCount) 1
					while (VertexCount > BlockSize){
						(PUSHER_NOINC(NV097_DRAW_ARRAYS), BlockSize - 1) 1
						VertexCount -= BlockSize
					}
					(PUSHER_NOINC(NV097_DRAW_ARRAYS), VertexCount - 1) 1
					(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END)1

					*/

					/* 
					//set_vertex_data_array_foramt with stride 0, then begin(triangle_list)/NV097_INLINE_ARRAY with 3 vertex with stride 0x10/end,
					//then set_vertex_data_array_offset, set_vertex_data_array_foramt with stride 0x10,begin(triangle_list)/NV097_DRAW_ARRAYS with arg0=0x02000000, method count=1, count=2, start offset =0/end.
					//the actual proper result should be 2 exact triangles reandered side by side.
					//used in xbox D3DDevice_DrawVertices
					//  (NV097_SET_BEGIN_END, PrimitiveType)
					//->(NV097_DRAW_ARRAYS,Method_Count=block_count)
					//-> loop append each block to pushbuffer
					//->(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END)
                     */
					//start is supposed to be the start vertex index
                    unsigned int start = GET_MASK(arg0, NV097_DRAW_ARRAYS_START_INDEX);
					//count is supposed to be the block count of vertex to be drawed. NV2A can draw up to 256 vertices in one submission.
					//for vertices more then 256, it must be processed with sequencial blocks.
					//each args in the argv[] represnt each block. the count stands for how many block left. the start means the starting vertex of each block.
					//for very last block, the count becomes vertices left in the final block.
					//we simply loops the argv[] with method_count, then we can know how many vertices we have to process.

					unsigned int block_count = method_count;//method_count is supposed to be block count staring from 1, including final block //GET_MASK(arg0, NV097_DRAW_ARRAYS_COUNT)+1;
					
					//each block submits 256 vertices. not sure we need to add start here or not.
					unsigned int last_block_vertex_count = GET_MASK(argv[method_count - 1], NV097_DRAW_ARRAYS_COUNT)+1;//?? shall we add 1, or leave it just as? currently keep it as is original code.
					unsigned int total_vertex_count = (block_count - 1) * 256 + last_block_vertex_count;
					pg->draw_arrays_max_count = MAX(pg->draw_arrays_max_count, start + total_vertex_count);

                    assert(pg->draw_arrays_length < ARRAY_SIZE(pg->gl_draw_arrays_start));
					if (pg->draw_arrays_length > 0) {
						unsigned int last_start =
						pg->gl_draw_arrays_start[pg->draw_arrays_length - 1];
						GLsizei* plast_count =
							&pg->gl_draw_arrays_count[pg->draw_arrays_length - 1];
						if (start == (last_start + *plast_count)) {
							*plast_count += total_vertex_count;
							break;
						}
					}
					pg->gl_draw_arrays_start[pg->draw_arrays_length] = start;
					pg->gl_draw_arrays_count[pg->draw_arrays_length] = total_vertex_count;
					pg->draw_arrays_length++;
					break;
                }

				case NV097_INLINE_ARRAY://xbox D3DDevice_DrawVerticesUP() D3DDevice_DrawIndexedVerticesUP calls this
					assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
					if (pg->draw_mode == DrawMode::None)
						pg->draw_mode = DrawMode::InlineArray;
					else
						assert(pg->draw_mode == DrawMode::InlineArray);

					//we only know how many DWORDs of data is coming.
					//D3DDevice_DrawVerticesUP() Otogi: max. 16 vertices each batch, but data per vertex is not fixed.
					//D3DDevice_DrawIndexedVerticesUP:
					//  (NV097_SET_BEGIN_END, PrimitiveType)
					//->if(remained vertex count >16) loop batch (NV097_INLINE_ARRAY, count), count= 16* dwords/vertex.
					//->last batch (NV097_INLINE_ARRAY, count), count= last remained vertex count * dwords/vertex.
					//->(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END)
                    for (size_t argc = 0; argc < method_count; argc++) {
                        arg0 = argv[argc];
                        assert(pg->inline_array_length < NV2A_MAX_BATCH_LENGTH);
                        pg->inline_array[
                            pg->inline_array_length++] = arg0;
                    }

                    break;

                CASE_3(NV097_SET_EYE_VECTOR, 4) :break;//not implement //pg->KelvinPrimitive.SetEyeVector[3]

                case NV097_INLINE_VERTEX_REUSE:break;//not implement //pg->KelvinPrimitive.InlineVertexReuse

                CASE_32(NV097_SET_VERTEX_DATA2F_M, 4): {//done //pg->KelvinPrimitive.SetVertexData2f[16].M[2]
					//test case HALO: use this to setup slot 0xA default value
                    if (pg->KelvinPrimitive.SetBeginEnd == NV097_SET_BEGIN_END_OP_END) {
                        //we're out side of Begin/End block, should be setting fix function vertex shader color persist attribute value
                        // 4 bytes for each float
                        slot = (method - NV097_SET_VERTEX_DATA2F_M) / 4;
                        // we have 2 floats for each slot
                        slot /= 2;

                        // preserve persist color value in R/G/B/A float4 format in KelvinPrimitive.SetVertexData4f[slot]
                        // D3DCOLOR format persiste in KelvinPrimitive.SetVertexData4ub[slot]
                        float* fm_inline_value = &pg->KelvinPrimitive.SetVertexData4f[slot].M[0];
                        // We set color in float4 in R/G/B/A. no need to swap R/B here.
                        // these value are never used after this. should be disabled.
                        fm_inline_value[0] = pg->KelvinPrimitive.SetVertexData2f[16].M[0];
                        fm_inline_value[1] = pg->KelvinPrimitive.SetVertexData2f[16].M[1];
                        fm_inline_value[2] = 0.0f;
                        fm_inline_value[3] = 1.0f;

                        // we set the color value to each vertex attribute becaue xbox uses this method to set default vertex colors.
                        float* inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
                        // We set color in float4 in R/G/B/A. no need to swap R/B here.
                        inline_value[0] = pg->KelvinPrimitive.SetVertexData2f[16].M[0];
                        inline_value[1] = pg->KelvinPrimitive.SetVertexData2f[16].M[1];
                        inline_value[2] = 0.0f;
                        inline_value[3] = 1.0f;

                        extern void CxbxSetVertexAttribute(int Register, FLOAT a, FLOAT b, FLOAT c, FLOAT d);
                        // sets default register value to host d3d constant.
                        CxbxSetVertexAttribute(slot, inline_value[0], inline_value[1], inline_value[2], inline_value[3]);

                    }
                    else {//in Begin/End block. data transferred are vertices.
                        for (size_t argc = 0; argc < method_count; argc++) {
                            //register is set one at a time per method, for loop should be redundant.
                            slot = (method - NV097_SET_VERTEX_DATA2F_M) / 4;
                            slot += argc;
                            unsigned int part = slot % 2;// 0:a or 1:b
                            slot /= 2;//register
                            float* inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
                            inline_value[part] = pg->KelvinPrimitive.SetVertexData2f[slot].M[part];
                            if (part == 1) {
                                inline_value[2] = 0.0f;
                                inline_value[3] = 1.0f;
                                if (slot == NV2A_VERTEX_ATTR_POSITION) {
                                    pgraph_finish_inline_buffer_vertex(pg);
                                }
                            }
                        }
                    }
					break;
                }

                CASE_64(NV097_SET_VERTEX_DATA4F_M, 4): {//done //pg->KelvinPrimitive.SetVertexData4f[16].M[4]
					//assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
                    //test case HALO 2 : use this to setup slot 0xF default value
                    if (pg->KelvinPrimitive.SetBeginEnd == NV097_SET_BEGIN_END_OP_END) {
                        //we're out side of Begin/End block, should be setting fix function vertex shader color persist attribute value
                        // 4 bytes for each float
                        slot = (method - NV097_SET_VERTEX_DATA2F_M) / 4;
                        // we have 2 floats for each slot
                        slot /= 2;

                        // preserve persist color value in R/G/B/A float4 format in KelvinPrimitive.SetVertexData4f[slot]
                        // D3DCOLOR format persiste in KelvinPrimitive.SetVertexData4ub[slot]
                        float* fm_inline_value = &pg->KelvinPrimitive.SetVertexData4f[slot].M[0];
                        // We set color in float4 in R/G/B/A. no need to swap R/B here.
                        // these value are never used after this. should be disabled.
                        fm_inline_value[0] = pg->KelvinPrimitive.SetVertexData4f[16].M[0];
                        fm_inline_value[1] = pg->KelvinPrimitive.SetVertexData4f[16].M[1];
                        fm_inline_value[2] = pg->KelvinPrimitive.SetVertexData4f[16].M[2];
                        fm_inline_value[3] = pg->KelvinPrimitive.SetVertexData4f[16].M[3];

                        // we set the color value to each vertex attribute becaue xbox uses this method to set default vertex colors.
                        float* inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
                        // We set color in float4 in R/G/B/A. no need to swap R/B here.
                        inline_value[0] = pg->KelvinPrimitive.SetVertexData4f[16].M[0];
                        inline_value[1] = pg->KelvinPrimitive.SetVertexData4f[16].M[1];
                        inline_value[2] = pg->KelvinPrimitive.SetVertexData4f[16].M[2];
                        inline_value[3] = pg->KelvinPrimitive.SetVertexData4f[16].M[3];

                        extern void CxbxSetVertexAttribute(int Register, FLOAT a, FLOAT b, FLOAT c, FLOAT d);
                        // sets default register value to host d3d constant.
                        CxbxSetVertexAttribute(slot, inline_value[0], inline_value[1], inline_value[2], inline_value[3]);

                    }
                    else {//in Begin/End block. data transferred are vertices.
                        //register is set one at a time per method, for loop should be redundant.
                        for (size_t argc = 0; argc < method_count; argc++) {
                            slot = (method - NV097_SET_VERTEX_DATA4F_M) / 4;
                            slot += argc;
                            unsigned int part = slot % 4;//index in M[]
                            slot /= 4;//register
                            float* inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
                            inline_value[part] = pg->KelvinPrimitive.SetVertexData4f[slot].M[part]; // *(float*)&arg0;
                            if ((part == 3) && (slot == NV2A_VERTEX_ATTR_POSITION)) { // D3DVSDE_POSITION
                                //shall we consider the color state setting in NV097_SET_VERTEX_DATA4UB? we should, to be done.
                                pgraph_finish_inline_buffer_vertex(pg);
                            }
                        }
                    }
					break;
                }
				CASE_16(NV097_SET_VERTEX_DATA2S, 4): {//done //pg->KelvinPrimitive.SetVertexData2s[16]
					assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
					for (size_t argc = 0; argc < method_count; argc++) {
						//register is set one at a time per method, for loop should be redundant.
						slot = (method - NV097_SET_VERTEX_DATA2S) / 4;
						slot += argc;
						//assert(false); /* FIXME: Untested! */
						float *inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
						inline_value[0] = (float)(int16_t)(arg0 & 0xFFFF);
						inline_value[1] = (float)(int16_t)(arg0 >> 16);
						if (slot == NV2A_VERTEX_ATTR_POSITION) {
							pgraph_finish_inline_buffer_vertex(pg);
						}
					}
					break;
				}
				CASE_16(NV097_SET_VERTEX_DATA4UB, 4) : {//pg->KelvinPrimitive.SetVertexData4ub[16]
					
					if (pg->KelvinPrimitive.SetBeginEnd == NV097_SET_BEGIN_END_OP_END) {
						//we're out side of Begin/End block, should be setting fix function vertex shader color persist attribute value

						slot = (method - NV097_SET_VERTEX_DATA4UB) / 4;

						// preserve persist color value in R/G/B/A float4 format in KelvinPrimitive.SetVertexData4f[slot]
						// D3DCOLOR format persiste in KelvinPrimitive.SetVertexData4ub[slot]
						float *fm_inline_value = &pg->KelvinPrimitive.SetVertexData4f[slot].M[0];
						// We set color in float4 in R/G/B/A. no need to swap R/B here.
                        // these value are never used after this. should be disabled.
                        fm_inline_value[0] = ( arg0 & 0xFF) / 255.0f;
                        fm_inline_value[1] = ((arg0 >> 8) & 0xFF) / 255.0f;
                        fm_inline_value[2] = ((arg0 >> 16) & 0xFF) / 255.0f;
                        fm_inline_value[3] = ((arg0 >> 24) & 0xFF) / 255.0f;

                        // we set the color value to each vertex attribute becaue xbox uses this method to set default vertex colors.
                        float *inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
                        // We set color in float4 in R/G/B/A. no need to swap R/B here.
                        inline_value[0] = (arg0 & 0xFF) / 255.0f;
                        inline_value[1] = ((arg0 >> 8) & 0xFF) / 255.0f;
                        inline_value[2] = ((arg0 >> 16) & 0xFF) / 255.0f;
                        inline_value[3] = ((arg0 >> 24) & 0xFF) / 255.0f;

                        extern void CxbxSetVertexAttribute(int Register, FLOAT a, FLOAT b, FLOAT c, FLOAT d);
                        // sets default register value to host d3d constant.
                        CxbxSetVertexAttribute(slot, inline_value[0], inline_value[1], inline_value[2], inline_value[3]);
                        
					}
					else {//in Begin/End block. data transferred are vertices.
						for (size_t argc = 0; argc < method_count; argc++) {
							slot = (method - NV097_SET_VERTEX_DATA4UB) / 4;
							slot += argc;
							float *inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
                            // We set color in float4 in R/G/B/A. no need to swap R/B here.
							inline_value[0] = (argv[argc] & 0xFF) / 255.0f;
							inline_value[1] = ((argv[argc] >> 8) & 0xFF) / 255.0f;
							inline_value[2] = ((argv[argc] >> 16) & 0xFF) / 255.0f;
							inline_value[3] = ((argv[argc] >> 24) & 0xFF) / 255.0f;
							if (slot == NV2A_VERTEX_ATTR_POSITION) {
								pgraph_finish_inline_buffer_vertex(pg);
							}
						}
					}
					break;
				}
				CASE_32(NV097_SET_VERTEX_DATA4S_M, 4) : {//done //pg->KelvinPrimitive.SetVertexData4s[16].M[2]
					assert(pg->KelvinPrimitive.SetBeginEnd > NV097_SET_BEGIN_END_OP_END);
					for (size_t argc = 0; argc < method_count; argc++) {
						//register is set one at a time per method, for loop should be redundant.
						slot = (method - NV097_SET_VERTEX_DATA4S_M) / 4;
						slot += argc;
						unsigned int part = slot % 2;
						slot /= 2;//register
						float *inline_value = pgraph_allocate_inline_buffer_vertices(pg, slot);
						/* FIXME: Is mapping to [-1,+1] correct? */
						inline_value[0 + part*2] = ((int16_t)(argv[argc+part] & 0xFFFF)		* 2.0f + 1) / 65535.0f;
						inline_value[1 + part*2] = ((int16_t)(argv[argc+part] >> 16)		* 2.0f + 1) / 65535.0f;
						if (part == 1) {
							if (slot == NV2A_VERTEX_ATTR_POSITION) {
								pgraph_finish_inline_buffer_vertex(pg);
							}
						}
					}
					break;
				}

		/* begin of SetTexture**************************************************** */

				CASE_4(NV097_SET_TEXTURE_OFFSET, 64) :{//KelvinPrimitive.SetTexture[4].Offset , sizeof(SetTexture[])==64
					//get texture[] index
					slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
					//pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0<<slot;
					bool bPreviousTexture = (pg->KelvinPrimitive.SetTexture[slot].Control0 & NV097_SET_TEXTURE_CONTROL0_ENABLE) != 0;

                    // FIXME!!!We can't reset all members right now, because in SwitchTexture() api, it only change Offset and Format, and use other members with persist values.
					// ***but SwitchTexture only valid for nonlinear texture, and Control1 and ImageRect will only be update if the texture is linear. so we can always reset Contro1 and ImageRect when Offset got set
					// reset other texture members in the same stage, because not all members will be set.
					// Format normally be set together with Offset in the same method call. so skip the reset call.
					// Control1 and ImageRect are set conditionaly, so must be reset.
					pg->KelvinPrimitive.SetTexture[slot].Control1 = 0;
					pg->KelvinPrimitive.SetTexture[slot].ImageRect = 0;
					// regenerate shader stage program since texture stage texure changed
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SHADER_STAGE_PROGRAM;
					// reset combiners since texture stage changed
					if(bPreviousTexture==false)
						NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
					// populate to next method handler if method count >1. this happened in xbox d3d SetTexture()/SwitchTexture()
                    /*
                    if (method_count > 1) {
						method_count -= 1;
						argv += 1;
						arg0 = argv[0];
						method += (NV097_SET_TEXTURE_FORMAT - NV097_SET_TEXTURE_OFFSET);
						goto SETTEXTUREFORMAT;
					}
                    */
					break;
				}
				CASE_4(NV097_SET_TEXTURE_FORMAT, 64) : {//KelvinPrimitive.SetTexture[4].Format , sizeof(SetTexture[])==64
					SETTEXTUREFORMAT:
					//get texture[] index
					slot = (method - NV097_SET_TEXTURE_FORMAT) / 64;
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
                    /*
                    bool dma_select =
					GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA) == 2;
                    bool cubemap =
                        arg0 & NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE;
                    unsigned int border_source =
                        arg0 & NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE;
                    unsigned int dimensionality =
                        GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY);
                    unsigned int color_format =
                        GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_COLOR);
                    unsigned int levels =
                        GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS);
                    unsigned int log_width =
                        GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U);
                    unsigned int log_height =
                        GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V);
                    unsigned int log_depth =
                        GET_MASK(arg0, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P);

                    uint32_t *reg = &pg->KelvinPrimitive.SetTexture[slot].Format;
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_CONTEXT_DMA, dma_select);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_CUBEMAPENABLE, cubemap);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BORDER_SOURCE, border_source);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_DIMENSIONALITY, dimensionality);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_COLOR, color_format);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS, levels);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_U, log_width);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_V, log_height);
                    SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_P, log_depth);
                    */
                    //each texture contents 16 dowrds
                    break;
				}

				CASE_4(NV097_SET_TEXTURE_ADDRESS, 64) :////KelvinPrimitive.SetTexture[4].Address , sizeof(SetTexture[])==64
                    //get texture[] index
                    slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
					//pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					// populate to next method handler if method count >1. this happened in xbox d3d lazy update.
                    /*
                    if (method_count > 1) {
						method_count -= 1;
						argv += 1;
						arg0 = argv[0];
						method += (NV097_SET_TEXTURE_CONTROL0 - NV097_SET_TEXTURE_ADDRESS);
						goto SETTEXTURECONTROL0;
					}
                    */
                    break;

                CASE_4(NV097_SET_TEXTURE_CONTROL0, 64) :////KelvinPrimitive.SetTexture[4].Control0 , sizeof(SetTexture[])==64
					SETTEXTURECONTROL0:
					//get texture[] index
                    slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
					//pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					// regenerate shader stage program and recalculate final combiners when texture set to NULL, also reset other texture members.
					if (pg->KelvinPrimitive.SetTexture[slot].Control0 == 0) {
						//pg->KelvinPrimitive.SetTexture[slot].Offset = 0;
						//pg->KelvinPrimitive.SetTexture[slot].Format = 0;
						//pg->KelvinPrimitive.SetTexture[slot].Address = 0;
						//pg->KelvinPrimitive.SetTexture[slot].Control0 = 0;
						//pg->KelvinPrimitive.SetTexture[slot].Control1 = 0;
						//pg->KelvinPrimitive.SetTexture[slot].Filter = 0;
						//pg->KelvinPrimitive.SetTexture[slot].ImageRect = 0;
						//pg->KelvinPrimitive.SetTexture[slot].Palette = 0;
						//pg->KelvinPrimitive.SetTexture[slot].BorderColor = 0;

						// regenerate shader stage program since texture stage texure changed
						NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SHADER_STAGE_PROGRAM;
						// reset combiners since texture stage changed
						NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
					}
					break;

                CASE_4(NV097_SET_TEXTURE_CONTROL1, 64) :////KelvinPrimitive.SetTexture[4].Control1 , sizeof(SetTexture[])==64
                    //get texture[] index
                    slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
					//pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					break;

                CASE_4(NV097_SET_TEXTURE_FILTER, 64) :////KelvinPrimitive.SetTexture[4].Filter , sizeof(SetTexture[])==64
                    //get texture[] index
                    slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
					//pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					break;

                CASE_4(NV097_SET_TEXTURE_IMAGE_RECT, 64) :////KelvinPrimitive.SetTexture[4].ImageRect , sizeof(SetTexture[])==64
                    //get texture[] index
                    slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
				    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
                    //pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					break;

                    //NV097_SET_TEXTURE_PALETTE(Stage) //pPalette->Data | (pPalette->Common >> D3DPALETTE_COMMON_PALETTESET_SHIFT) & D3DPALETTE_COMMON_PALETTESET_MASK
                    //HLE stores the palette data in g_pXbox_Palette_Data[],g_Xbox_Palette_Size[]
				CASE_4(NV097_SET_TEXTURE_PALETTE, 64) :{ //KelvinPrimitive.SetTexture[4].Palette , sizeof(SetTexture[])==64
					//get texture[] index
					slot = (method - NV097_SET_TEXTURE_PALETTE) / 64;
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
					/*
					bool dma_select =
						GET_MASK(arg0, NV097_SET_TEXTURE_PALETTE_CONTEXT_DMA) == 1;
					unsigned int length =
						GET_MASK(arg0, NV097_SET_TEXTURE_PALETTE_LENGTH);
					unsigned int offset =
						GET_MASK(arg0, NV097_SET_TEXTURE_PALETTE_OFFSET);

					uint32_t *reg = &pg->KelvinPrimitive.SetTexture[slot].Palette;
					SET_MASK(*reg, NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA, dma_select);
					SET_MASK(*reg, NV_PGRAPH_TEXPALETTE0_LENGTH, length);
					SET_MASK(*reg, NV_PGRAPH_TEXPALETTE0_OFFSET, offset);
					*/
					//double check required.
                    extern xbox::X_D3DPalette           g_NV2A_Palette_Data[xbox::X_D3DTS_STAGECOUNT];
                    extern xbox::PVOID                  g_pNV2A_Palette_Data[xbox::X_D3DTS_STAGECOUNT];
                    extern unsigned                     g_NV2A_Palette_Size[xbox::X_D3DTS_STAGECOUNT];
                    extern int XboxD3DPaletteSizeToBytes(const xbox::X_D3DPALETTESIZE Size);
                    extern inline xbox::X_D3DPALETTESIZE GetXboxPaletteSize(const xbox::X_D3DPalette* pPalette);

                    g_pNV2A_Palette_Data[slot] = &g_NV2A_Palette_Data[slot];
                    g_NV2A_Palette_Data[slot].Data = pg->KelvinPrimitive.SetTexture[4].Palette & 0xFFFFFFFC;// X_D3DPALETTE_COMMON_PALETTESET_MASK=0xC0000000
                    //setup xbox resource.common for palette. could hack to use cached palette from HLE
                    DWORD common = (pg->KelvinPrimitive.SetTexture[4].Palette & 0x3) << 30 | 1;// X_D3DPALETTE_COMMON_PALETTESET_SHIFT=30, setup the palette size and add one ref count.
                    g_NV2A_Palette_Data[slot].Common = common;
                    g_NV2A_Palette_Data[slot].Lock = 0;
                    g_NV2A_Palette_Size[slot] = XboxD3DPaletteSizeToBytes(GetXboxPaletteSize(&g_NV2A_Palette_Data[slot]));
					break;
				}
                CASE_4(NV097_SET_TEXTURE_BORDER_COLOR, 64) :// D3DTSS_BORDERCOLOR, KelvinPrimitive.SetTexture[slot].SetTextureBorderColor
                    slot = (method - NV097_SET_TEXTURE_BORDER_COLOR) / 64;
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_TEXTURE_STATE_0 << slot;
                    NV2ATextureStates.Set(slot, xbox::X_D3DTSS_BORDERCOLOR, pg->KelvinPrimitive.SetTexture[slot].BorderColor);
                    break;
        // Bumpenv is seperate from texture stage texture, use seperate dirty flags.
                //these NV097_SET_TEXTURE_SET_BUMP_ENV_MAT are no longer needed. leave these code here for short term reference only. shall be cleared later.
                CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0x0, 64) ://KevlinPrimitive.SetTexture[4].SetBumpEnvMat00 SetBumpEnvMat01 SetBumpEnvMat10 SetBumpEnvMat11 SetBumpEnvScale SetBumpEnvOffset
                    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0x4, 64) :
                    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0x8, 64) :
                    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0xc, 64) :
                        //xbox d3d update 6 bump state vars in the same time, including SetBumpEnvScale SetBumpEnvOffset
                        //pg->bump_env_matrix[4][4] was remapped to KevlinPrimitive.SetTexture[4].SetBumpEnvMat00~11
                {
					//get texture[] index
					slot = (method - NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE) / 64;
					//pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
					pg->bumpenv_dirty[slot] = true;

					//find which SetTexture[] element we're setting.
                    //int tex_index = (method - NV097_SET_TEXTURE_SET_BUMP_ENV_MAT) / 64;

                    //bump_env_matrix[tex_index][], there are 4 elements
                    //for (size_t argc = 0; argc < 4; argc++) {
                        //arg0 = ldl_le_p(argv + argc);
                        //slot = (method - NV097_SET_TEXTURE_SET_BUMP_ENV_MAT) / 4;
                        //assert((argc / 16) > 0);
                        //argc -= 16;

                        //this bump_env_matrix[][] is redundat. use pg->KelvinPrimitive.SetTexture[tex_index].SetBumpEnvMat00
                        //pg->bump_env_matrix[tex_index][argc % 4] = *(float*)&pg->KelvinPrimitive.SetTexture[tex_index].SetBumpEnvMat00;

                    //}
                    break;
                }
                //xbox d3d won't update NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE independently, but we leave the case here.
                CASE_4( NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE,64) :
                    //get texture[] index
                    slot = (method - NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE) / 64;
                    //pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
                    pg->bumpenv_dirty[slot] = true;
                    break;
                //xbox d3d won't update NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET independently, but we leave the case here.
                CASE_4( NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET,64):
                    //get texture[] index
                    slot = (method - NV097_SET_TEXTURE_OFFSET) / 64;
                    //pg->pgraph_regs[NV_PGRAPH_TEXOFFSET0 / 4 + slot * 4] = arg0;
                    pg->bumpenv_dirty[slot] = true;
                    break;

        /* end of SetTexture**************************************************** */


#if (0) //state not implement yet
#   define NV097_PARK_ATTRIBUTE                               0x00001D64
#   define NV097_UNPARK_ATTRIBUTE                             0x00001D68
#endif
				case NV097_SET_SEMAPHORE_OFFSET: {
					pg->regs[NV_PGRAPH_SEMAPHOREOFFSET/4] = arg0;
					break;
				}

                case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE: {
                    pgraph_update_surface(d, false, true, true);

                    //qemu_mutex_unlock(&pg->pgraph_lock);
                    //qemu_mutex_lock_iothread();
					// ***the semaphore relese is to update the CDevice.GpuTime, which will change the state of CDevice_IsBusy(). we have to implement this.
					
                    uint32_t semaphore_offset = pg->pgraph_regs[NV_PGRAPH_SEMAPHOREOFFSET/4];

                    xbox::addr_xt semaphore_dma_len;
                    uint8_t *semaphore_data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaSemaphore,
                        &semaphore_dma_len);
                    assert(semaphore_offset < semaphore_dma_len);
                    semaphore_data += semaphore_offset;

                    stl_le_p((uint32_t*)semaphore_data, arg0);
					
                    //qemu_mutex_lock(&pg->pgraph_lock);
                    //qemu_mutex_unlock_iothread();

                    break;
                }

                case NV097_TEXTURE_READ_SEMAPHORE_RELEASE:               break;

                case NV097_SET_ZMIN_MAX_CONTROL://pg->KelvinPrimitive.SetZMinMaxControl
                    if(pg->KelvinPrimitive.SetDepthTestEnable!= D3DZB_FALSE)
                        if((pg->KelvinPrimitive.SetZMinMaxControl& NV097_SET_ZMIN_MAX_CONTROL_CULL_NEAR_FAR_EN_TRUE)==0)
                            NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ZENABLE, D3DZB_USEW);
                        else
                            NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_ZENABLE, D3DZB_TRUE);
                    break;

                case NV097_SET_ANTI_ALIASING_CONTROL://D3DRS_MULTISAMPLEANTIALIAS D3DRS_MULTISAMPLEMASK//pg->KelvinPrimitive.SetAntiAliasingControl
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_MULTISAMPLEANTIALIAS, pg->KelvinPrimitive.SetAntiAliasingControl& NV097_SET_ANTI_ALIASING_CONTROL_ENABLE_TRUE);
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_MULTISAMPLEMASK, (pg->KelvinPrimitive.SetAntiAliasingControl& NV097_SET_ANTI_ALIASING_CONTROL_SAMPLE_MASK)>>16);
                    break;

                case NV097_SET_COMPRESS_ZBUFFER_EN:                      break;

                case NV097_SET_OCCLUDE_ZSTENCIL_EN://D3DRS_OCCLUSIONCULLENABLE D3DRS_STENCILCULLENABLE    // pg->KelvinPrimitive.SetOccludeZStencilEn
                   
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_STENCILCULLENABLE, (pg->KelvinPrimitive.SetOccludeZStencilEn & NV097_SET_OCCLUDE_ZSTENCIL_EN_OCCLUDE_STENCIL_EN_ENABLE));
                    if ((pg->KelvinPrimitive.SetOccludeZStencilEn & NV097_SET_OCCLUDE_ZSTENCIL_EN_OCCLUDE_ZEN_ENABLE) != 0)
                        NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_OCCLUSIONCULLENABLE, 1);
                    else
                        // if ((!D3D__RenderState[D3DRS_STENCILENABLE]) || (D3D__RenderState[D3DRS_STENCILFAIL] == D3DSTENCILOP_KEEP))
                        if((pg->KelvinPrimitive.SetStencilTestEnable==0) || (pg->KelvinPrimitive.SetStencilOpFail == 0x1e00))
                            NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_OCCLUSIONCULLENABLE, 0);
                    break;
				// xbox d3d D3DDevice_Clear() calls NV097_SET_ZSTENCIL_CLEAR_VALUE with method count == 3, sets NV097_SET_ZSTENCIL_CLEAR_VALUE,NV097_SET_COLOR_CLEAR_VALUE, then trigger clear(). 
				case NV097_SET_ZSTENCIL_CLEAR_VALUE://done //pg->KelvinPrimitive.SetZStencilClearValue
					//popular method handler to NV097_SET_COLOR_CLEAR_VALUE if method count>1
					if (method_count > 1) {
						method_count -= 1;
						argv += 1;
						arg0 = argv[0];
						method += (NV097_SET_COLOR_CLEAR_VALUE - NV097_SET_ZSTENCIL_CLEAR_VALUE);
						goto SETCOLORCLEARVALUE;
					}
					break;
				case NV097_SET_COLOR_CLEAR_VALUE://done //pg->KelvinPrimitive.SetColorClearValue
					SETCOLORCLEARVALUE:
					//populate method handler to NV097_CLEAR_SURFACE if method count>1
					if (method_count > 1) {
						method_count -= 1;
						argv += 1;
						arg0 = argv[0];
						method += (NV097_CLEAR_SURFACE - NV097_SET_COLOR_CLEAR_VALUE);
						goto CLEARSURFACE;
					}
					break;

                case NV097_CLEAR_SURFACE: {//done //pg->KelvinPrimitive.ClearSurface
					CLEARSURFACE:
					//NV097_CLEAR_SURFACE triggers NV2A to start clear right away.
					// clear target Rect if we're in pushbuffer replay
					if (pgraph_GetNV2AStateFlag(X_STATE_RUNPUSHBUFFERWASCALLED)) {
						// call pgraph_draw_state_update() to update all necessary d3d resources
						if (pgraph_draw_state_update != nullptr) {
							//pgraph_draw_state_update(d);
						}
						// clear target Rect
						if (pgraph_draw_clear != nullptr) {
							pgraph_draw_clear(d);
						}
					}
                    break;
                }
                // xbox d3d clear() calls NV097_SET_CLEAR_RECT_HORIZONTAL with method count 2, set NV097_SET_CLEAR_RECT_HORIZONTAL and NV097_SET_CLEAR_RECT_VERTICAL.
				// then calls NV097_SET_ZSTENCIL_CLEAR_VALUE with method count 3, sets NV097_SET_ZSTENCIL_CLEAR_VALUE,NV097_SET_COLOR_CLEAR_VALUE, then trigger clear(). 
                case NV097_SET_CLEAR_RECT_HORIZONTAL://done //pg->KelvinPrimitive.SetClearRectHorizontal
					break;
                case NV097_SET_CLEAR_RECT_VERTICAL://done //pg->KelvinPrimitive.SetClearRectVertical
					break;
#if(0)
#   define NV097_SET_BEGIN_PATCH0                             0x00001DE0
#   define NV097_SET_BEGIN_PATCH1                             0x00001DE4
#   define NV097_SET_BEGIN_PATCH2                             0x00001DE8
#   define NV097_SET_BEGIN_PATCH3                             0x00001DEC
#   define NV097_SET_END_PATCH                                0x00001DF0
#   define NV097_SET_BEGIN_END_SWATCH                         0x00001DF4
#   define NV097_SET_BEGIN_END_CURVE                          0x00001DF8
#   define NV097_SET_CURVE_COEFFICIENTS                       0x00001E00 // [4]
#   define NV097_SET_BEGIN_TRANSITION0                        0x00001E10
#   define NV097_SET_BEGIN_TRANSITION1                        0x00001E14
#   define NV097_SET_BEGIN_TRANSITION2                        0x00001E18
#   define NV097_SET_END_TRANSITION                           0x00001E1C
#endif
				CASE_2(NV097_SET_SPECULAR_FOG_FACTOR,4):// [2]
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
                    break;
				CASE_6(NV097_SET_BACK_SPECULAR_PARAMS,4)://[6]
					NV2A_DirtyFlags|=X_D3DDIRTYFLAG_LIGHTS;
					break;
				CASE_8( NV097_SET_COMBINER_COLOR_OCW , 4):// [8]
					NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
				    break;
				case NV097_SET_COMBINER_CONTROL:
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
                    break;
				case NV097_SET_SHADOW_ZSLOPE_THRESHOLD://done //pg->KelvinPrimitive.SetShadowZSlopeThreshold
                    //pg->pgraph_regs[NV_PGRAPH_SHADOWZSLOPETHRESHOLD/4] = arg0;
                    assert(arg0 == 0x7F800000); /* FIXME: Unimplemented */
                    break;

                case NV097_SET_SHADOW_DEPTH_FUNC://not implement //pg->KelvinPrimitive.SetShadowDepthFunc
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_SHADOWFUNC, pg->KelvinPrimitive.SetShadowDepthFunc + 0x200); // xbox substracts 0x200 before push the value to pushbuffer
                    break;

				case NV097_SET_SHADER_STAGE_PROGRAM://pg->KelvinPrimitive.SetShaderStageProgram
					// this is a dirty hack, if NV097_SET_SHADER_OTHER_STAGE_INPUT was called and set with 0x00210000, and all 16 texture factors are the same, then we're in fixed mode pixel shader
                    // there is no simple way to tell whether we're in fixed mode or program mode pixel shader.
                    // hack remove this condition(NV2A_ShaderOtherStageInputDirty == true) && 
                    DWORD shaderMode;
                    shaderMode = pg->KelvinPrimitive.SetShaderStageProgram;
                    if((pg->KelvinPrimitive.SetShaderOtherStageInput == 0x00210000)&&(NV2A_TextureFactorAllTheSame==true)){
						pgraph_use_FixedPixelShader();
                        // reset NV2A_ShaderOtherStageInputDirty dirty flag
                        NV2A_ShaderOtherStageInputDirty = false;
                        NV2A_DirtyFlags |= X_D3DDIRTYFLAG_COMBINERS;
                        
                    // else we're in user mode pixel program
					}else{
						pgraph_use_UserPixelShader();
					}
                    if(pgraph_GetNV2AStateFlag(X_STATE_COMBINERNEEDSSPECULAR))
                        NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SPECFOG_COMBINER;
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SHADER_STAGE_PROGRAM;
					break;//done //pg->KelvinPrimitive.SetShaderStageProgram


                case NV097_SET_DOT_RGBMAPPING:{//not implement  //pg->KelvinPrimitive.SetDotRGBMapping
                    NV2ARenderStates.SetXboxRenderState(xbox::X_D3DRS_PSDOTMAPPING, pg->KelvinPrimitive.SetDotRGBMapping);
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SHADER_STAGE_PROGRAM;
                    break;
                }

                case NV097_SET_SHADER_OTHER_STAGE_INPUT://done  //pg->KelvinPrimitive.SetShaderOtherStageInput
                    //pg->pgraph_regs[NV_PGRAPH_SHADERCTL/4] = arg0;
					// set NV2A_ShaderOtherStageInputDirty, so later in NV097_SET_SHADER_STAGE_PROGRAM we could tell this is fixed mode pixel shader.
					NV2A_ShaderOtherStageInputDirty = true;
					//pgraph_use_FixedPixelShader();
                    NV2A_DirtyFlags |= X_D3DDIRTYFLAG_SHADER_STAGE_PROGRAM;
                    break;

                CASE_4(NV097_SET_TRANSFORM_DATA,4):break;//not implement //pg->KelvinPrimitive.SetTransformData[4]

                case NV097_LAUNCH_TRANSFORM_PROGRAM://not implement //pg->KelvinPrimitive.LaunchTransformProgram
					// D3DDevice_RunVertexStateShader() calls NV097_LAUNCH_TRANSFORM_PROGRAM to launch vertex state shader
					// it loads vertex shader constants to constant slot 0 first.
					assert(0);
					break;

				extern bool g_bUsePassthroughHLSL;//TMP glue

                case NV097_SET_TRANSFORM_EXECUTION_MODE://done //pg->KelvinPrimitive.SetTransformExecutionMode
                    // Test-case : Whiplash
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4], NV_PGRAPH_CSV0_D_MODE,  //GET_MASK(pg->KelvinPrimitive.SetTransformExecutionMode,NV097_SET_TRANSFORM_EXECUTION_MODE_MODE)
                    //	GET_MASK(arg0,
                    //		NV097_SET_TRANSFORM_EXECUTION_MODE_MODE));
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4], NV_PGRAPH_CSV0_D_RANGE_MODE,  ////GET_MASK(pg->KelvinPrimitive.SetTransformExecutionMode,NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE)
                    //	GET_MASK(arg0,
                    //		NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE));
					/*
					user program start program using
					push (NV097_SET_TRANSFORM_PROGRAM_START, addr , method count 1)
					after program setup. we don't start user program here.

					*/
					slot=arg0 & NV097_SET_TRANSFORM_EXECUTION_MODE_MODE;
					if (slot == NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM ) {//program mode, user program or pass through program
						/*
						xbox d3d calls NV097_SET_TRANSFORM_EXECUTION_MODE with method count =2, 
						         (NV097_SET_TRANSFORM_EXECUTION_MODE,
								  // NV097_SET_TRANSFORM_EXECUTION_MODE:
									(DRF_DEF(097, _SET_TRANSFORM_EXECUTION_MODE, _MODE, _PROGRAM) |  DRF_DEF(097, _SET_TRANSFORM_EXECUTION_MODE, _RANGE_MODE, _PRIV)),
								 // NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN:
								      VertexShader.Flags & VERTEXSHADER_WRITE, method count 2)
						*/
						if (method_count==2){
                            // dirty hack. for program shader, there will be a NV097_SET_TRANSFORM_PROGRAM_START right after NV097_SET_TRANSFORM_EXECUTION_MODE in SelectVertexShader().
                            // but for passthrough shader, it won't call SelectVertexShader(), but only use NV097_SET_TRANSFORM_PROGRAM_START right before NV097_SET_TRANSFORM_EXECUTION_MODE
                            if ((argv[1] == NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN_V_READ_ONLY)&& ((argv[2]& COMMAND_WORD_MASK_METHOD)== NV097_SET_TRANSFORM_PROGRAM_START)) {
								// for passthrough, argv[1] is always 0:NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN_V_READ_ONLY
								// for program, argv[1] is vertexshader.flags & VERTEXSHADER_WRITE:1
								// ** the only way to tell whether a vertexh shader is a program or a pass through,
								// ** is the sequence of call to NV097_SET_TRANSFORM_EXECUTION_MODE and NV097_SET_TRANSFORM_PROGRAM_START
								// ** program uses D3DDevice_SelectVertexShader() which calls NV097_SET_TRANSFORM_EXECUTION_MODE first then calls NV097_SET_TRANSFORM_PROGRAM_START
								// ** passthrough in SelectVertexShader() calls NV097_SET_TRANSFORM_PROGRAM_START first, then calls NV097_SET_TRANSFORM_EXECUTION_MODE


                                g_Xbox_VertexShaderMode = VertexShaderMode::ShaderProgram;
                                //g_UseFixedFunctionVertexShader = false;

                                // for shader program, here we set it to default register 0, later when we reach NV097_SET_TRANSFORM_PROGRAM_START, we'll use the register addr passed in.
                                //g_Xbox_VertexShader_FunctionSlots_StartAddress = 0;

                                // set vertex shader dirty flag
                                g_VertexShader_dirty = true;

                            }
                            else {
                                // if we hit here with g_Xbox_VertexShaderMode==FixedFunction, then we're in Passthrough
                                //if (g_VertexShader_dirty == false) {

                                    g_Xbox_VertexShaderMode = VertexShaderMode::Passthrough;
                                    //g_UseFixedFunctionVertexShader = false;

                                    // for shader program, here we set it to default register 0, later when we reach NV097_SET_TRANSFORM_PROGRAM_START, we'll use the register addr passed in.
                                    g_Xbox_VertexShader_FunctionSlots_StartAddress = 0;

                                    // set vertex shader dirty flag
                                    g_VertexShader_dirty = true;

                                    // funtion key F7 flips this variable
                                    g_bUsePassthroughHLSL = true;
                                    //float tempConstant[4];
                                    // read constant register 0, CommonSetPassThroughProgram() sets register 0 constant with SuperSampleScaleX/Y
                                    //CxbxrImpl_GetVertexShaderConstant(0 - X_D3DSCM_CORRECTION, tempConstant, 1);
                                    //extern void CxbxrSetSuperSampleScaleXY(float x, float y);
                                    //CxbxrSetSuperSampleScaleXY(tempConstant[0], tempConstant[1]);
                                //}
                            }
						}
						/*
						fix function setup finished using 
						push (NV097_SET_TRANSFORM_EXECUTION_MODE,
								 NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_FIXED|NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV, method count 1)
						to start fix function vertex shader program
						*/
					}else if (slot== NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_FIXED){//fix function mode

						//Call CxbxrImpl_SetVertexShaderInput(pg->vsh_FVF_handle) here? or set the global xbox vertex attribute []
						//or g_Xbox_SetVertexShaderInput_Attributes = *CxbxGetVertexShaderAttributes(pXboxVertexShader); ??
						//to set vertex format info, but wihthout stream info.

						// set fixed function vertex shader program mode
						g_Xbox_VertexShaderMode = VertexShaderMode::FixedFunction;

						// enable g_UseFixedFunctionVertexShader
						g_UseFixedFunctionVertexShader = true;

                        g_Xbox_VertexShader_FunctionSlots_StartAddress = 0;

						// set vertex shader dirty flag
						g_VertexShader_dirty = true;

					}
					break;


			    //this is set when Starting pass through vertex shaders.
				//it's not called directly, it set by calling NV097_SET_TRANSFORM_EXECUTION_MODE with method count=2, argv[1]=NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN_V_READ_ONLY

				case NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN://done //pg->KelvinPrimitive.SetTransformProgramCxtWriteEn
                    // Test-case : Whiplash
                    //pg->KelvinPrimitive.SetTransformProgramCxtWriteEn
                    //pg->enable_vertex_program_write = arg0;
					//if (arg0 == NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN_V_READ_ONLY) {
					// for passthrough, arg0 is always 0:NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN_V_READ_ONLY
					// for program, arg0 is vertexshader.flags & VERTEXSHADER_WRITE:1
					// ** the only way to tell whether a vertexh shader is a program or a pass through,
					// ** is the sequence of call to NV097_SET_TRANSFORM_EXECUTION_MODE and NV097_SET_TRANSFORM_PROGRAM_START
					// ** program uses D3DDevice_SelectVertexShader() which calls NV097_SET_TRANSFORM_EXECUTION_MODE first then calls NV097_SET_TRANSFORM_PROGRAM_START
					// ** passthrough in SelectVertexShader() calls NV097_SET_TRANSFORM_PROGRAM_START first, then calls NV097_SET_TRANSFORM_EXECUTION_MODE

					// xbox d3d is supposed to call NV097_SET_TRANSFORM_EXECUTION_MODE with method_cound = 2 to set this var, but we copy the code here in case guest code use two methods.
					//}

					break;
                case NV097_SET_TRANSFORM_PROGRAM_LOAD:
					//pg->KelvinPrimitive.SetTransformProgramLoad set the target program slot to load the shader program.
                    assert(arg0 < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CHEOPS_OFFSET / 4],
                    //	NV_PGRAPH_CHEOPS_OFFSET_PROG_LD_PTR, arg0);
                    break;
				/*
			    this is where xbox d3d SetVertexShader(Handle, address) kick off shader, argv[0]=address, starting program slot.
				we can call
				set g_Xbox_VertexShaderMode == VertexShaderMode::FixedFunction/Passthrough/ShaderProgram
				then call CxbxUpdateHostVertexShader(), which will generate host vertex function program and create/set host vertex shader.
				but we need to set vertex attribute format first. using KelvinPrimitive.SetVertexDataArrayFormat[16]& 0xFF
				user program start program using
				push (NV097_SET_TRANSFORM_PROGRAM_START, addr , method count 1)
				after program setup.
				*/

				case NV097_SET_TRANSFORM_PROGRAM_START: {
					//pg->KelvinPrimitive.SetTransformProgramStart
					//assert(parameter < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH);
					//SET_MASK(pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4],
					//	NV_PGRAPH_CSV0_C_CHEOPS_PROGRAM_START, arg0);

					// ** the only way to tell whether a vertexh shader is a program or a pass through,
					// ** is the sequence of call to NV097_SET_TRANSFORM_EXECUTION_MODE and NV097_SET_TRANSFORM_PROGRAM_START
					// ** program uses D3DDevice_SelectVertexShader() which calls NV097_SET_TRANSFORM_EXECUTION_MODE first then calls NV097_SET_TRANSFORM_PROGRAM_START
					// ** passthrough in SelectVertexShader() calls NV097_SET_TRANSFORM_PROGRAM_START first, then calls NV097_SET_TRANSFORM_EXECUTION_MODE

					// if we hit here with g_Xbox_VertexShaderMode==FixedFunction, then we're in Passthrough


					//reset fix fuction handle.
					//pg->vsh_FVF_handle = 0;

					//set starting program slot
					//if (g_Xbox_VertexShaderMode == VertexShaderMode::ShaderProgram) {
					g_Xbox_VertexShader_FunctionSlots_StartAddress = arg0;

                    //}
                    // set vertex shader dirty flag
                    g_VertexShader_dirty = true;

					break;
				}
                case NV097_SET_TRANSFORM_CONSTANT_LOAD://pg->KelvinPrimitive.SetTransformConstantLoad
                    assert(arg0 < NV2A_VERTEXSHADER_CONSTANTS);
					assert(arg0 >=0);
                    //SET_MASK(pg->pgraph_regs[NV_PGRAPH_CHEOPS_OFFSET / 4],
                    //	NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR, arg0);
                    NV2A_DPRINTF("load to %d\n", arg0);
                    break;

				CASE_10(NV097_DEBUG_INIT, 4) ://pg->KelvinPrimitive.SetDebugInit[10]
						break;

                default://default case of KELVIN_PRIME method
                    //reset num_words_consumed indicates the method is not handled.
                    num_words_consumed = 0;
                    NV2A_GL_DPRINTF(true, "    unhandled  (0x%02x 0x%08x)",
                            graphics_class, method);
                    break;
			}//end of KELVIN_PRIMITIVE switch(method)

			//normal code flow of case NV_KELVIN_PRIMITIVE
			//check if the method was handled.
			//if (num_words_consumed > 0){
			//	;
			//}
			//num_processed = method_count;//num_processed should always be method_count. if not, then must be something wrong.
			break;//break for KELVIN_PRIMITIVE case    
        }//	end of graphic_class KELVIN_PRIMITIVE case
		    
        default://graphics_class default case
            NV2A_GL_DPRINTF(true, "Unknown Graphics Class/Method 0x%08X/0x%08X",
                            graphics_class, method);
            break;
    }//end of switch(graphics_class)

    return num_processed;	//return the words processed here so the caller can advance the dma_get pointer of the pushbuffer
                            //num_processed default to 1, which represent the first parameter passed in this call.
                            //but that word is advanced by the caller already. it's the caller's duty to subtract that word from the num_processed;
}

static void pgraph_switch_context(NV2AState *d, unsigned int channel_id)
{
    bool channel_valid =
        d->pgraph.pgraph_regs[NV_PGRAPH_CTX_CONTROL / 4] & NV_PGRAPH_CTX_CONTROL_CHID;
    unsigned pgraph_channel_id = GET_MASK(d->pgraph.pgraph_regs[NV_PGRAPH_CTX_USER / 4], NV_PGRAPH_CTX_USER_CHID);
    // Cxbx Note : This isn't present in xqemu / OpenXbox : d->pgraph.pgraph_lock.lock();
    bool valid = channel_valid && pgraph_channel_id == channel_id;
    if (!valid) {
        SET_MASK(d->pgraph.pgraph_regs[NV_PGRAPH_TRAPPED_ADDR / 4],
                 NV_PGRAPH_TRAPPED_ADDR_CHID, channel_id);

        NV2A_DPRINTF("pgraph switching to ch %d\n", channel_id);

        /* TODO: hardware context switching */
        //assert(!(d->pgraph.pgraph_regs[NV_PGRAPH_DEBUG_3 / 4]
        //        & NV_PGRAPH_DEBUG_3_HW_CONTEXT_SWITCH));

        qemu_mutex_unlock(&d->pgraph.pgraph_lock);
        qemu_mutex_lock_iothread();
        d->pgraph.pending_interrupts |= NV_PGRAPH_INTR_CONTEXT_SWITCH; // TODO : Should this be done before unlocking pgraph_lock?
        update_irq(d);

        qemu_mutex_lock(&d->pgraph.pgraph_lock);
        qemu_mutex_unlock_iothread();

        // wait for the interrupt to be serviced
        while (d->pgraph.pending_interrupts & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
            qemu_cond_wait(&d->pgraph.interrupt_cond, &d->pgraph.pgraph_lock);
        }
    }
}

static void pgraph_wait_fifo_access(NV2AState *d) {
    while (!(d->pgraph.pgraph_regs[NV_PGRAPH_FIFO / 4] & NV_PGRAPH_FIFO_ACCESS)) {
        qemu_cond_wait(&d->pgraph.fifo_access_cond, &d->pgraph.pgraph_lock);
    }
}

static void pgraph_log_method(unsigned int subchannel,
                                unsigned int graphics_class,
                                unsigned int method, uint32_t parameter)
{
    static unsigned int last = 0;
    static unsigned int count = 0;

    extern const char *NV2AMethodToString(DWORD dwMethod); // implemented in PushBuffer.cpp

    if (last == 0x1800 && method != last) {
        const char* method_name = NV2AMethodToString(last); // = 'NV2A_VB_ELEMENT_U16'
        NV2A_GL_DPRINTF(true, "d->pgraph method (%d) 0x%08X %s * %d",
                        subchannel, last, method_name, count);
    }
    if (method != 0x1800) {
        // const char* method_name = NV2AMethodToString(method);
        // unsigned int nmethod = 0;
        // switch (graphics_class) {
        // case NV_KELVIN_PRIMITIVE:
        // 	nmethod = method | (0x5c << 16);
        // 	break;
        // case NV_CONTEXT_SURFACES_2D:
        // 	nmethod = method | (0x6d << 16);
        // 	break;
        // case NV_CONTEXT_PATTERN:
        // 	nmethod = method | (0x68 << 16);
        // 	break;
        // default:
        // 	break;
        // }
        // if (method_name) {
        // 	NV2A_DPRINTF("d->pgraph method (%d): %s (0x%x)\n",
        // 		subchannel, method_name, parameter);
        // } else {
            NV2A_DPRINTF("pgraph method (%d): 0x%08X -> 0x%04x (0x%x)\n",
                subchannel, graphics_class, method, parameter);
        // }

    }
    if (method == last) { count++; }
    else { count = 0; }
    last = method;
}

static float *pgraph_allocate_inline_buffer_vertices(PGRAPHState *pg,
                                                   unsigned int attr)
{
    unsigned int i;
    VertexAttribute *vertex_attribute = &pg->vertex_attributes[attr];

	//set flag so when each vertex is completed, it can know which attribute is set and require to be pushed to vertex buffer.
	vertex_attribute->set_by_inline_buffer = true;

	float *inline_value = vertex_attribute->inline_value;
	//if (vertex_attribute->inline_buffer || pg->inline_buffer_length == 0) {
	//return if the buffer is already allocated.
	if (!vertex_attribute->inline_buffer) {

		//allocate the inline buffer for vertex attribute,
		vertex_attribute->inline_buffer = (float*)g_malloc(NV2A_MAX_BATCH_LENGTH
			* sizeof(float) * 4);

		/* Now upload the previous vertex_attribute value */
		/* don't upload the whole inline buffer of attribute here. this routine is only for buffer allocation. */
		// this code is assuming that attribute could be set/inserted not in the very first vertex. so when the attribute is set, we must duplicate it's value from the beginning of vertex buffer to the current vertex.
		for (int i = 0; i < pg->inline_buffer_length; i++) {
			memcpy(&vertex_attribute->inline_buffer[i * 4],
					inline_value,
					sizeof(float) * 4);
		}
	}

	return inline_value;
}

float *pgraph_get_vertex_attribute_inline_value(PGRAPHState *pg, int attribute_index)
{
	// See CASE_16(NV097_SET_VERTEX_DATA4UB, 4) in LLE pgraph_handle_method()
	VertexAttribute *vertex_attribute = &pg->vertex_attributes[attribute_index];
	return vertex_attribute->inline_value;
}

static void pgraph_finish_inline_buffer_vertex(PGRAPHState *pg)
{
	unsigned int i;

	if (pg->draw_mode == DrawMode::None) {
		pg->draw_mode = DrawMode::InlineBuffer;
	}
	else
		assert(pg->draw_mode == DrawMode::InlineBuffer);

	assert(pg->inline_buffer_length < NV2A_MAX_BATCH_LENGTH);

	for (i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
		VertexAttribute *vertex_attribute = &pg->vertex_attributes[i];
		//process the attribute data if it's been set
		if (vertex_attribute->set_by_inline_buffer) {
			float *inline_value = pgraph_get_vertex_attribute_inline_value(pg, i);
			memcpy(&vertex_attribute->inline_buffer[
				pg->inline_buffer_length * 4],
				inline_value,
					sizeof(float) * 4);
			//currently we composed all input attributes into one vertex buffer. this is slow but it's working and verified with HLE already.
			/* //disabled, attributes could possibly inserted anytime, not from the very beginning. so the vertex buffer can only be put together after all vertices are finished.
			memcpy(&pg->inline_buffer[
				pg->inline_buffer_attr_length * 4],
				inline_value,
					sizeof(float) * 4);
			*/
			// this var might not be useful since attributes are not set in every vertex.
			pg->inline_buffer_attr_length++;
		}
	}

	pg->inline_buffer_length++;
}

void pgraph_init(NV2AState *d)
{
    int i;

    PGRAPHState *pg = &d->pgraph;

    qemu_mutex_init(&pg->pgraph_lock);
    qemu_cond_init(&pg->interrupt_cond);
    qemu_cond_init(&pg->fifo_access_cond);
    qemu_cond_init(&pg->flip_3d);
	pg->opengl_enabled = false;
    if (!(pg->opengl_enabled))
        return;

    /* attach OpenGL render plugins */
    OpenGL_init_pgraph_plugins();

    /* fire up opengl */

    pg->gl_context = glo_context_create();
    assert(pg->gl_context);

#ifdef DEBUG_NV2A_GL
    glEnable(GL_DEBUG_OUTPUT);
#endif

    glextensions_init();

    // Set up ImGui's render backend
    //ImGui_ImplSDL2_InitForOpenGL(window, pg->gl_context);
    ImGui_ImplOpenGL3_Init();
    g_renderbase->SetDeviceRelease([] {
        ImGui_ImplOpenGL3_Shutdown();
    });

    /* DXT textures */
    assert(glo_check_extension("GL_EXT_texture_compression_s3tc"));
    /*  Internal RGB565 texture format */
    assert(glo_check_extension("GL_ARB_ES2_compatibility"));

    GLint max_vertex_attributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attributes);
    assert(max_vertex_attributes >= NV2A_VERTEXSHADER_ATTRIBUTES);

    glGenFramebuffers(1, &pg->gl_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, pg->gl_framebuffer);

    /* need a valid framebuffer to start with */
    glGenTextures(1, &pg->gl_color_buffer);
    glBindTexture(GL_TEXTURE_2D, pg->gl_color_buffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, pg->gl_color_buffer, 0);

    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)
            == GL_FRAMEBUFFER_COMPLETE);

    //glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

#ifdef USE_TEXTURE_CACHE
    pg->texture_cache = g_lru_cache_new_full(
        0,
        NULL,
        texture_key_destroy,
        0,
        NULL,
        texture_binding_destroy,
        texture_key_hash,
        texture_key_equal,
        texture_key_retrieve,
        NULL,
        NULL
        );

    g_lru_cache_set_max_size(pg->texture_cache, 512);
#endif

#ifdef USE_SHADER_CACHE
    pg->shader_cache = g_hash_table_new(shader_hash, shader_equal);
#endif

    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        glGenBuffers(1, &pg->vertex_attributes[i].gl_converted_buffer);
        glGenBuffers(1, &pg->vertex_attributes[i].gl_inline_buffer);
    }
    glGenBuffers(1, &pg->gl_inline_array_buffer);
    glGenBuffers(1, &pg->gl_element_buffer);

    glGenBuffers(1, &pg->gl_memory_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, pg->gl_memory_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 d->vram_size,
                 NULL,
                 GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &pg->gl_vertex_array);
    glBindVertexArray(pg->gl_vertex_array);

//    assert(glGetError() == GL_NO_ERROR);

    glo_set_current(NULL);
}

void pgraph_destroy(PGRAPHState *pg)
{

    qemu_mutex_destroy(&pg->pgraph_lock);
    qemu_cond_destroy(&pg->interrupt_cond);
    qemu_cond_destroy(&pg->fifo_access_cond);
    qemu_cond_destroy(&pg->flip_3d);

    if (pg->opengl_enabled) {
        glo_set_current(pg->gl_context);

        if (pg->gl_color_buffer) {
            glDeleteTextures(1, &pg->gl_color_buffer);
        }
        if (pg->gl_zeta_buffer) {
            glDeleteTextures(1, &pg->gl_zeta_buffer);
        }
        glDeleteFramebuffers(1, &pg->gl_framebuffer);

        // TODO: clear out shader cached
        // TODO: clear out texture cache

        glo_set_current(NULL);

        glo_context_destroy(pg->gl_context);
    }
}

static void pgraph_update_shader_constants(PGRAPHState *pg,
                                           ShaderBinding *binding,
                                           bool binding_changed,
                                           bool vertex_program,
                                           bool fixed_function)
{
    assert(pg->opengl_enabled);

    unsigned int i, j;

    /* update combiner constants */
    for (i = 0; i<= 8; i++) {
        uint32_t constant[2];
        if (i == 8) {
            /* final combiner */
            constant[0] = pg->KelvinPrimitive.SetSpecularFogFactor[0];
            constant[1] = pg->KelvinPrimitive.SetSpecularFogFactor[1];
        } else {
            constant[0] = pg->KelvinPrimitive.SetCombinerFactor0[i];
            constant[1] = pg->KelvinPrimitive.SetCombinerFactor1[i];
        }

        for (j = 0; j < 2; j++) {
            GLint loc = binding->psh_constant_loc[i][j];
            if (loc != -1) {
                float value[4];
                value[0] = (float) ((constant[j] >> 16) & 0xFF) / 255.0f;
                value[1] = (float) ((constant[j] >> 8) & 0xFF) / 255.0f;
                value[2] = (float) (constant[j] & 0xFF) / 255.0f;
                value[3] = (float) ((constant[j] >> 24) & 0xFF) / 255.0f;

                glUniform4fv(loc, 1, value);
            }
        }
    }
    if (binding->alpha_ref_loc != -1) {
        float alpha_ref = (pg->KelvinPrimitive.SetAlphaRef & 0xFF) / 255.0f;
        glUniform1f(binding->alpha_ref_loc, alpha_ref);
    }


    /* For each texture stage */
    for (i = 0; i < NV2A_MAX_TEXTURES; i++) {
        // char name[32];
        GLint loc;

        /* Bump luminance only during stages 1 - 3 */
        if (i > 0) {
            loc = binding->bump_mat_loc[i];
            if (loc != -1) {
                glUniformMatrix2fv(loc, 1, GL_FALSE, &pg->KelvinPrimitive.SetTexture[i].SetBumpEnvMat00); //KelvinPrimitive has 4 SetTexture[], no need to use i-1
            }
            loc = binding->bump_scale_loc[i];
            if (loc != -1) {
                glUniform1f(loc, pg->KelvinPrimitive.SetTexture[i].SetBumpEnvScale);//KelvinPrimitive has 4 SetTexture[], no need to use i-1
            }
            loc = binding->bump_offset_loc[i];
            if (loc != -1) {
                glUniform1f(loc, pg->KelvinPrimitive.SetTexture[i].SetBumpEnvOffset);//KelvinPrimitive has 4 SetTexture[], no need to use i-1
            }
        }

    }

    if (binding->fog_color_loc != -1) {
        /* NV097 Kelvin fog color channels are ABGR, PGRAPH channels are ARGB */
        uint32_t kelvin_fog_color_ABGR = pg->KelvinPrimitive.SetFogColor;
        uint8_t alpha = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_ALPHA);
        uint8_t blue = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_BLUE);
        uint8_t green = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_GREEN);
        uint8_t red = GET_MASK(kelvin_fog_color_ABGR, NV097_SET_FOG_COLOR_RED);
        glUniform4f(binding->fog_color_loc,
                    red / 255.0f, // NV_PGRAPH_FOGCOLOR_RED
                    green / 255.0f, // NV_PGRAPH_FOGCOLOR_GREEN
                    blue / 255.0f, // NV_PGRAPH_FOGCOLOR_BLUE
                    alpha / 255.0f); // NV_PGRAPH_FOGCOLOR_ALPHA
    }
    if (binding->fog_param_loc[0] != -1) {
        glUniform1f(binding->fog_param_loc[0],
			pg->KelvinPrimitive.SetFogParams[0] / 4);
    }
    if (binding->fog_param_loc[1] != -1) {
        glUniform1f(binding->fog_param_loc[1],
			pg->KelvinPrimitive.SetFogParams[1] / 4);
    }

    float zclip_max = pg->KelvinPrimitive.SetClipMax;
    float zclip_min = pg->KelvinPrimitive.SetClipMin;

    if (fixed_function) {
        /* update lighting constants */
        struct {
            uint32_t* v;
            bool* dirty;
            GLint* locs;
            size_t len;
        } lighting_arrays[] = {
            // TODO : Change pointers into offset_of(), so this variable can become static
            {&pg->ltctxa[0][0], &pg->ltctxa_dirty[0], binding->ltctxa_loc, NV2A_LTCTXA_COUNT},
            {&pg->ltctxb[0][0], &pg->ltctxb_dirty[0], binding->ltctxb_loc, NV2A_LTCTXB_COUNT},
            {&pg->ltc1[0][0], &pg->ltc1_dirty[0], binding->ltc1_loc, NV2A_LTC1_COUNT},
        };

        for (i=0; i<ARRAY_SIZE(lighting_arrays); i++) {
            uint32_t *lighting_v = lighting_arrays[i].v;
            bool *lighting_dirty = lighting_arrays[i].dirty;
            GLint *lighting_locs = lighting_arrays[i].locs;
            size_t lighting_len = lighting_arrays[i].len;
            for (j=0; j<lighting_len; j++) {
                if (!lighting_dirty[j] && !binding_changed) continue;
                GLint loc = lighting_locs[j];
                if (loc != -1) {
                    glUniform4fv(loc, 1, (const GLfloat*)&lighting_v[j*4]);
                }
                lighting_dirty[j] = false;
            }
        }


        for (i = 0; i < NV2A_MAX_LIGHTS; i++) {
            GLint loc;
            loc = binding->light_infinite_half_vector_loc[i];
            if (loc != -1) {
                glUniform3fv(loc, 1, pg->KelvinPrimitive.SetLight[i].InfiniteHalfVector);
            }
            loc = binding->light_infinite_direction_loc[i];
            if (loc != -1) {
                glUniform3fv(loc, 1, pg->KelvinPrimitive.SetLight[i].InfiniteDirection);
            }

            loc = binding->light_local_position_loc[i];
            if (loc != -1) {
                glUniform3fv(loc, 1, pg->KelvinPrimitive.SetLight[i].LocalPosition);
            }
            loc = binding->light_local_attenuation_loc[i];
            if (loc != -1) {
                glUniform3fv(loc, 1, pg->KelvinPrimitive.SetLight[i].LocalAttenuation);
            }
        }

        /* estimate the viewport by assuming it matches the surface ... */
        //FIXME: Get surface dimensions?
        float m11 = 0.5f * pg->surface_shape.clip_width;
        float m22 = -0.5f * pg->surface_shape.clip_height;
        float m33 = zclip_max - zclip_min;
        //float m41 = m11;
        //float m42 = -m22;
        float m43 = zclip_min;
        //float m44 = 1.0f;

        if (m33 == 0.0f) {
            m33 = 1.0f;
        }
        float invViewport[16] = {
            1.0f/m11, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f/m22, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f/m33, 0.0f,
            -1.0f, 1.0f, -m43/m33, 1.0f
        };

        if (binding->inv_viewport_loc != -1) {
            glUniformMatrix4fv(binding->inv_viewport_loc,
                               1, GL_FALSE, &invViewport[0]);
        }

    }

    /* update vertex program constants */
    for (i=0; i<NV2A_VERTEXSHADER_CONSTANTS; i++) {
        if (!pg->vsh_constants_dirty[i] && !binding_changed) continue;

        GLint loc = binding->vsh_constant_loc[i];
        //assert(loc != -1);
        if (loc != -1) {
            glUniform4fv(loc, 1, (const GLfloat*)pg->vsh_constants[i]);
        }
        pg->vsh_constants_dirty[i] = false;
    }

    if (binding->surface_size_loc != -1) {
        glUniform2f(binding->surface_size_loc, (GLfloat)pg->surface_shape.clip_width,
                    (GLfloat)pg->surface_shape.clip_height);
    }

    if (binding->clip_range_loc != -1) {
        glUniform2f(binding->clip_range_loc, zclip_min, zclip_max);
    }

}

static void pgraph_bind_shaders(PGRAPHState *pg)
{
    assert(pg->opengl_enabled);

    unsigned int i, j;

    //uint32_t csv0_d = pg->pgraph_regs[NV_PGRAPH_CSV0_D / 4];
    bool vertex_program = GET_MASK(pg->KelvinPrimitive.SetTransformExecutionMode, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE) == 2;

    bool fixed_function = GET_MASK(pg->KelvinPrimitive.SetTransformExecutionMode, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE) == 0;

    //uint32_t csv0_c = pg->pgraph_regs[NV_PGRAPH_CSV0_C / 4];
    int program_start = pg->KelvinPrimitive.SetTransformProgramStart;

    NV2A_GL_DGROUP_BEGIN("%s (VP: %s FFP: %s)", __func__,
                         vertex_program ? "yes" : "no",
                         fixed_function ? "yes" : "no");

    ShaderBinding* old_binding = pg->shader_binding;

    ShaderState state;
    /* register combiner stuff */
	state.psh.window_clip_exclusive = pg->KelvinPrimitive.SetWindowClipType & 0x1;
    state.psh.combiner_control = pg->KelvinPrimitive.SetCombinerControl;
    state.psh.shader_stage_program = pg->KelvinPrimitive.SetShaderStageProgram;
    state.psh.other_stage_input = pg->KelvinPrimitive.SetShaderOtherStageInput;
    state.psh.final_inputs_0 = pg->KelvinPrimitive.SetCombinerSpecularFogCW0;
    state.psh.final_inputs_1 = pg->KelvinPrimitive.SetCombinerSpecularFogCW1;
    //uint32_t control0 = pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4];

    state.psh.alpha_test = pg->KelvinPrimitive.SetAlphaTestEnable !=0;
    state.psh.alpha_func = (enum PshAlphaFunc)(pg->KelvinPrimitive.SetAlphaFunc & 0xF);

    /* fixed function stuff */
    state.skinning = (enum VshSkinning)pg->KelvinPrimitive.SetSkinMode;
    state.lighting = pg->KelvinPrimitive.SetDitherEnable!=0;
    state.normalization = pg->KelvinPrimitive.SetNormalizationEnable!=0;

    state.fixed_function = fixed_function;

    /* vertex program stuff */
    state.vertex_program = vertex_program;
    state.z_perspective = GET_MASK(pg->KelvinPrimitive.SetControl0 , NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE);
    //state.z_perspective = GET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4], NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE);

    /* geometry shader stuff */
    state.primitive_mode = (enum ShaderPrimitiveMode)pg->primitive_mode;
    state.polygon_front_mode = (enum ShaderPolygonMode)kelvin_map_polygon_mode(pg->KelvinPrimitive.SetFrontPolygonMode);
    state.polygon_back_mode = (enum ShaderPolygonMode)kelvin_map_polygon_mode(pg->KelvinPrimitive.SetBackPolygonMode);

    state.program_length = 0;
    memset(state.vsh_program_copy, 0, sizeof(state.vsh_program_copy));

    if (vertex_program) {
        // copy in vertex program tokens
        for (i = program_start; i < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH; i++) {
            uint32_t *cur_token = (uint32_t*)&pg->vsh_program_slots[i];
            memcpy(&state.vsh_program_copy[state.program_length],
                   cur_token,
                   VSH_TOKEN_SIZE * sizeof(uint32_t));
            state.program_length++;

            if (vsh_get_field(cur_token, FLD_FINAL)) {
                break;
            }
        }
    }

    /* Texgen */
    for (i = 0; i < 4; i++) {
        //NV_PGRAPH_CSV1_B was never used because there are only 2 Texgen in KelvinPrimitive, which are used as T0_S T R Q, T1_S T R Q
        //unsigned int reg = NV_PGRAPH_CSV1_A / 4;//(i < 2) ? NV_PGRAPH_CSV1_A / 4 : NV_PGRAPH_CSV1_B / 4;
        for (j = 0; j < 4; j++) {
            unsigned int masks[] = {
                // NOTE: For some reason, Visual Studio thinks NV_PGRAPH_xxxx is signed integer. (possible bug?)
                (i % 2U) ? (unsigned int)pg->KelvinPrimitive.SetTexgen[1].S : (unsigned int)pg->KelvinPrimitive.SetTexgen[0].S,
                (i % 2U) ? (unsigned int)pg->KelvinPrimitive.SetTexgen[1].T : (unsigned int)pg->KelvinPrimitive.SetTexgen[0].T,
                (i % 2U) ? (unsigned int)pg->KelvinPrimitive.SetTexgen[1].R : (unsigned int)pg->KelvinPrimitive.SetTexgen[0].R,
                (i % 2U) ? (unsigned int)pg->KelvinPrimitive.SetTexgen[1].Q : (unsigned int)pg->KelvinPrimitive.SetTexgen[0].Q
            };
            state.texgen[i][j] = (enum VshTexgen) masks[j];
        }
    }

    /* Fog */
    state.fog_enable = pg->KelvinPrimitive.SetFogEnable!=0;
    if (state.fog_enable) {
        /*FIXME: Use CSV0_D? */
        state.fog_mode = (enum VshFogMode)pg->KelvinPrimitive.SetFogMode;
        state.foggen = (enum VshFoggen)pg->KelvinPrimitive.SetFogGenMode;
    } else {
        /* FIXME: Do we still pass the fogmode? */
        state.fog_mode = (enum VshFogMode)0;
        state.foggen = (enum VshFoggen)0;
    }

    /* Texture matrices */
    for (i = 0; i < 4; i++) {
        state.texture_matrix_enable[i] = pg->KelvinPrimitive.SetTextureMatrixEnable[4];
    }

    /* Lighting */
    if (state.lighting) {
        for (i = 0; i < NV2A_MAX_LIGHTS; i++) {
            state.light[i] = (enum VshLight)GET_MASK(pg->KelvinPrimitive.SetLightEnableMask,
                                      NV_PGRAPH_CSV0_D_LIGHT0 << (i * 2));
        }
    }

    /* Window clip
     *
     * Optimization note: very quickly check to ignore any repeated or zero-size
     * clipping regions. Note that if region number 7 is valid, but the rest are
     * not, we will still add all of them. Clip regions seem to be typically
     * front-loaded (meaning the first one or two regions are populated, and the
     * following are zeroed-out), so let's avoid adding any more complicated
     * masking or copying logic here for now unless we discover a valid case.
     */
    assert(!state.psh.window_clip_exclusive); /* FIXME: Untested */
    state.psh.window_clip_count = 0;
    uint32_t last_x = 0, last_y = 0;

    for (i = 0; i < 8; i++) {
        const uint32_t x = pg->KelvinPrimitive.SetWindowClipHorizontal[i];
        const uint32_t y = pg->KelvinPrimitive.SetWindowClipVertical[i];
        const uint32_t x_min = GET_MASK(x, NV_PGRAPH_WINDOWCLIPX0_XMIN);
        const uint32_t x_max = GET_MASK(x, NV_PGRAPH_WINDOWCLIPX0_XMAX);
        const uint32_t y_min = GET_MASK(y, NV_PGRAPH_WINDOWCLIPY0_YMIN);
        const uint32_t y_max = GET_MASK(y, NV_PGRAPH_WINDOWCLIPY0_YMAX);

        /* Check for zero width or height clipping region */
        if ((x_min == x_max) || (y_min == y_max)) {
            continue;
        }

        /* Check for in-order duplicate regions */
        if ((x == last_x) && (y == last_y)) {
            continue;
        }

        NV2A_DPRINTF("Clipping Region %d: min=(%d, %d) max=(%d, %d)\n",
            i, x_min, y_min, x_max, y_max);

        state.psh.window_clip_count = i + 1;
        last_x = x;
        last_y = y;
    }

    /* FIXME: We should memset(state, 0x00, sizeof(state)) instead */
    memset(state.psh.rgb_inputs, 0, sizeof(state.psh.rgb_inputs));
    memset(state.psh.rgb_outputs, 0, sizeof(state.psh.rgb_outputs));
    memset(state.psh.alpha_inputs, 0, sizeof(state.psh.alpha_inputs));
    memset(state.psh.alpha_outputs, 0, sizeof(state.psh.alpha_outputs));

    /* Copy content of enabled combiner stages */
    unsigned int num_stages = pg->KelvinPrimitive.SetCombinerControl & 0xFF;
    for (i = 0; i < num_stages; i++) {
        state.psh.rgb_inputs[i] = pg->KelvinPrimitive.SetCombinerColorICW[i];
        state.psh.rgb_outputs[i] = pg->KelvinPrimitive.SetCombinerColorOCW[i];
        state.psh.alpha_inputs[i] = pg->KelvinPrimitive.SetCombinerAlphaICW[i];
        state.psh.alpha_outputs[i] = pg->KelvinPrimitive.SetCombinerAlphaOCW[i];
        //constant_0[i] = pg->pgraph_regs[NV_PGRAPH_COMBINEFACTOR0 + i * 4];
        //constant_1[i] = pg->pgraph_regs[NV_PGRAPH_COMBINEFACTOR1 + i * 4];
    }

    for (i = 0; i < 4; i++) {
        state.psh.rect_tex[i] = false;
        bool enabled = pg->KelvinPrimitive.SetTexture[i].Control0
                         & NV_PGRAPH_TEXCTL0_0_ENABLE;
        unsigned int color_format =
            GET_MASK(pg->KelvinPrimitive.SetTexture[i].Format,
                     NV_PGRAPH_TEXFMT0_COLOR);

        if (enabled && kelvin_color_format_map[color_format].encoding == linear) {
            state.psh.rect_tex[i] = true;
        }

        for (j = 0; j < 4; j++) {
            state.psh.compare_mode[i][j] =
                (pg->KelvinPrimitive.SetShaderClipPlaneMode >> (4 * i + j)) & 1;
        }
        state.psh.alphakill[i] = pg->KelvinPrimitive.SetTexture[i].Control0
                               & NV_PGRAPH_TEXCTL0_0_ALPHAKILLEN;
    }

#ifdef USE_SHADER_CACHE
    ShaderBinding* cached_shader = (ShaderBinding*)g_hash_table_lookup(pg->shader_cache, &state);

    if (cached_shader) {
        pg->shader_binding = cached_shader;
    } else {
#endif
        pg->shader_binding = generate_shaders(state);

#ifdef USE_SHADER_CACHE
        /* cache it */
        ShaderState *cache_state = (ShaderState *)g_malloc(sizeof(*cache_state));
        memcpy(cache_state, &state, sizeof(*cache_state));
        g_hash_table_insert(pg->shader_cache, cache_state,
                            (gpointer)pg->shader_binding);
    }
#endif

    bool binding_changed = (pg->shader_binding != old_binding);

    glUseProgram(pg->shader_binding->gl_program);

    /* Clipping regions */
    for (i = 0; i < state.psh.window_clip_count; i++) {
        if (pg->shader_binding->clip_region_loc[i] == -1) {
            continue;
        }

        uint32_t x   = pg->KelvinPrimitive.SetWindowClipHorizontal[i];
        GLuint x_min = GET_MASK(x, NV_PGRAPH_WINDOWCLIPX0_XMIN);
        GLuint x_max = GET_MASK(x, NV_PGRAPH_WINDOWCLIPX0_XMAX);

        /* Adjust y-coordinates for the OpenGL viewport: translate coordinates
         * to have the origin at the bottom-left of the surface (as opposed to
         * top-left), and flip y-min and y-max accordingly.
         */
        uint32_t y   = pg->KelvinPrimitive.SetWindowClipVertical[i];
        GLuint y_min = (pg->surface_shape.clip_height - 1) -
                       GET_MASK(y, NV_PGRAPH_WINDOWCLIPY0_YMAX);
        GLuint y_max = (pg->surface_shape.clip_height - 1) -
                       GET_MASK(y, NV_PGRAPH_WINDOWCLIPY0_YMIN);

        pgraph_apply_anti_aliasing_factor(pg, &x_min, &y_min);
        pgraph_apply_anti_aliasing_factor(pg, &x_max, &y_max);

        glUniform4i(pg->shader_binding->clip_region_loc[i],
                    x_min, y_min, x_max + 1, y_max + 1);
    }

    pgraph_update_shader_constants(pg, pg->shader_binding, binding_changed,
                                   vertex_program, fixed_function);

    NV2A_GL_DGROUP_END();
}

static bool pgraph_get_framebuffer_dirty(PGRAPHState *pg)
{
    bool shape_changed = memcmp(&pg->surface_shape, &pg->last_surface_shape,
                                sizeof(SurfaceShape)) != 0;
    if (!shape_changed || (!pg->surface_shape.color_format
            && !pg->surface_shape.zeta_format)) {
        return false;
    }
    return true;
}

static bool pgraph_get_color_write_enabled(PGRAPHState *pg)
{
//	return pg->pgraph_regs[NV_PGRAPH_CONTROL_0] & (
//		NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE
//		| NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE
//		| NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE
//		| NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE);

	return (pg->KelvinPrimitive.SetColorMask & (
        NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE
        | NV097_SET_COLOR_MASK_RED_WRITE_ENABLE
        | NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE
        | NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE))!=0;
}

static bool pgraph_get_zeta_write_enabled(PGRAPHState *pg)
{
	//return pg->pgraph_regs[NV_PGRAPH_CONTROL_0] & (		NV_PGRAPH_CONTROL_0_ZWRITEENABLE	|  NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE);

	//return ((GET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0 / 4], NV_PGRAPH_CONTROL_0_ZWRITEENABLE) !=0)|| GET_MASK(pg->pgraph_regs[NV_PGRAPH_CONTROL_0/4],NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE)!=0);
    return ((GET_MASK(pg->KelvinPrimitive.SetControl0, NV_PGRAPH_CONTROL_0_ZWRITEENABLE) != 0) || GET_MASK(pg->KelvinPrimitive.SetControl0, NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE) != 0);
}

static void pgraph_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    NV2A_DPRINTF("pgraph_set_surface_dirty(%d, %d) -- %d %d\n",
                 color, zeta,
                 pgraph_get_color_write_enabled(pg), pgraph_get_zeta_write_enabled(pg));
    /* FIXME: Does this apply to CLEARs too? */
    color = color && pgraph_get_color_write_enabled(pg);
    zeta = zeta && pgraph_get_zeta_write_enabled(pg);
    pg->surface_color.draw_dirty |= color;
    pg->surface_zeta.draw_dirty |= zeta;
}

static void pgraph_update_surface_part(NV2AState *d, bool upload, bool color) {
    PGRAPHState *pg = &d->pgraph;

    unsigned int width, height;
    pgraph_get_surface_dimensions(pg, &width, &height);
    pgraph_apply_anti_aliasing_factor(pg, &width, &height);

    Surface *surface;
    hwaddr dma_address;
    GLuint *gl_buffer;
    unsigned int bytes_per_pixel;
    GLint gl_internal_format;
    GLenum gl_format, gl_type, gl_attachment;

    if (color) {
        surface = &pg->surface_color;
        dma_address = pg->KelvinPrimitive.SetContextDmaColor;
        gl_buffer = &pg->gl_color_buffer;

        assert(pg->surface_shape.color_format != 0);
        assert(pg->surface_shape.color_format
                < ARRAY_SIZE(kelvin_surface_color_format_map));
        SurfaceColorFormatInfo f =
            kelvin_surface_color_format_map[pg->surface_shape.color_format];
        if (f.bytes_per_pixel == 0) {
            fprintf(stderr, "nv2a: unimplemented color surface format 0x%x\n",
                    pg->surface_shape.color_format);
            abort();
        }

        bytes_per_pixel = f.bytes_per_pixel;
        gl_internal_format = f.gl_internal_format;
        gl_format = f.gl_format;
        gl_type = f.gl_type;
        gl_attachment = GL_COLOR_ATTACHMENT0;

    } else {
        surface = &pg->surface_zeta;
        dma_address = pg->KelvinPrimitive.SetContextDmaZeta;
        gl_buffer = &pg->gl_zeta_buffer;

        assert(pg->surface_shape.zeta_format != 0);
        switch (pg->surface_shape.zeta_format) {
        case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
            bytes_per_pixel = 2;
            gl_format = GL_DEPTH_COMPONENT;
            gl_attachment = GL_DEPTH_ATTACHMENT;
            if (pg->surface_shape.z_format) {
                gl_type = GL_HALF_FLOAT;
                gl_internal_format = GL_DEPTH_COMPONENT32F;
            } else {
                gl_type = GL_UNSIGNED_SHORT;
                gl_internal_format = GL_DEPTH_COMPONENT16;
            }
            break;
        case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
            bytes_per_pixel = 4;
            gl_format = GL_DEPTH_STENCIL;
            gl_attachment = GL_DEPTH_STENCIL_ATTACHMENT;
            if (pg->surface_shape.z_format) {
                assert(false);
                gl_type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
                gl_internal_format = GL_DEPTH32F_STENCIL8;
            } else {
                gl_type = GL_UNSIGNED_INT_24_8;
                gl_internal_format = GL_DEPTH24_STENCIL8;
            }
            break;
        default:
            assert(false);
            break;
        }
    }


    DMAObject dma = nv_dma_load(d, dma_address);
    /* There's a bunch of bugs that could cause us to hit this function
     * at the wrong time and get a invalid dma object.
     * Check that it's sane. */
    assert(dma.dma_class == NV_DMA_IN_MEMORY_CLASS);

    assert(dma.address + surface->offset != 0);
    assert(surface->offset <= dma.limit);
    assert(surface->offset + surface->pitch * height <= dma.limit + 1);

    hwaddr data_len;
    uint8_t *data = (uint8_t*)nv_dma_map(d, dma_address, &data_len);

    /* TODO */
    // assert(pg->surface_clip_x == 0 && pg->surface_clip_y == 0);

    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);

    uint8_t *buf = data + surface->offset;
    if (swizzle) {
        buf = (uint8_t*)g_malloc(height * surface->pitch);
    }

    bool dirty = surface->buffer_dirty;
    if (color) {
#if 1
        // HACK: Always mark as dirty
        dirty |= 1;
#else
        dirty |= memory_region_test_and_clear_dirty(d->vram,
                                               dma.address + surface->offset,
                                               surface->pitch * height,
                                               DIRTY_MEMORY_NV2A);
#endif
    }
    if (upload && dirty) {
        /* surface modified (or moved) by the cpu.
         * copy it into the opengl renderbuffer */
        // TODO: Why does this assert?
        //assert(!surface->draw_dirty);
        assert(surface->pitch % bytes_per_pixel == 0);

        if (swizzle) {
            unswizzle_rect(data + surface->offset,
                           width, height,
                           buf,
                           surface->pitch,
                           bytes_per_pixel);
        }

        if (pg->opengl_enabled) {
            if (!color) {
                /* need to clear the depth_stencil and depth attachment for zeta */
                glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       GL_DEPTH_ATTACHMENT,
                                       GL_TEXTURE_2D,
                                       0, 0);
                glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       GL_DEPTH_STENCIL_ATTACHMENT,
                                       GL_TEXTURE_2D,
                                       0, 0);
            }

            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   gl_attachment,
                                   GL_TEXTURE_2D,
                                   0, 0);

            if (*gl_buffer) {
                glDeleteTextures(1, gl_buffer);
                *gl_buffer = 0;
            }

            glGenTextures(1, gl_buffer);
            glBindTexture(GL_TEXTURE_2D, *gl_buffer);

            /* This is VRAM so we can't do this inplace! */
            uint8_t *flipped_buf = (uint8_t*)g_malloc(width * height * bytes_per_pixel);
            unsigned int irow;
            for (irow = 0; irow < height; irow++) {
                memcpy(&flipped_buf[width * (height - irow - 1)
                                         * bytes_per_pixel],
                       &buf[surface->pitch * irow],
                       width * bytes_per_pixel);
            }

            glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format,
                         width, height, 0,
                         gl_format, gl_type,
                         flipped_buf);

            g_free(flipped_buf);

            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   gl_attachment,
                                   GL_TEXTURE_2D,
                                   *gl_buffer, 0);

            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)
                == GL_FRAMEBUFFER_COMPLETE);
        }

        if (color) {
            pgraph_update_memory_buffer(d, dma.address + surface->offset,
                                        surface->pitch * height, true);
        }
        surface->buffer_dirty = false;

#ifdef DEBUG_NV2A
        uint8_t *out = data + surface->offset + 64;
        NV2A_DPRINTF("upload_surface %s 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                      "(0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                        "%d %d, %d %d, %d) - %x %x %x %x\n",
            color ? "color" : "zeta",
            dma.address, dma.address + dma.limit,
            dma.address + surface->offset,
            dma.address + surface->pitch * height,
            pg->surface_shape.clip_x, pg->surface_shape.clip_y,
            pg->surface_shape.clip_width,
            pg->surface_shape.clip_height,
            surface->pitch,
            out[0], out[1], out[2], out[3]);
#endif
    }

    if (!upload && surface->draw_dirty) {
        if (pg->opengl_enabled) {
            /* read the opengl framebuffer into the surface */

            glo_readpixels(gl_format, gl_type,
                           bytes_per_pixel, surface->pitch,
                           width, height,
                           buf);

//			assert(glGetError() == GL_NO_ERROR);
        }

        if (swizzle) {
            swizzle_rect(buf,
                         width, height,
                         data + surface->offset,
                         surface->pitch,
                         bytes_per_pixel);
        }

        // memory_region_set_client_dirty(d->vram,
        //                                dma.address + surface->offset,
        //                                surface->pitch * height,
        //                                DIRTY_MEMORY_VGA);

        if (color) {
            pgraph_update_memory_buffer(d, dma.address + surface->offset,
                                        surface->pitch * height, true);
        }

        surface->draw_dirty = false;
        surface->write_enabled_cache = false;

#ifdef DEBUG_NV2A
        uint8_t *out = data + surface->offset + 64;
        NV2A_DPRINTF("read_surface %s 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                      "(0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                        "%d %d, %d %d, %d) - %x %x %x %x\n",
            color ? "color" : "zeta",
            dma.address, dma.address + dma.limit,
            dma.address + surface->offset,
            dma.address + surface->pitch * pg->surface_shape.clip_height,
            pg->surface_shape.clip_x, pg->surface_shape.clip_y,
            pg->surface_shape.clip_width, pg->surface_shape.clip_height,
            surface->pitch,
            out[0], out[1], out[2], out[3]);
#endif
    }

    if (swizzle) {
        g_free(buf);
    }
}

static void pgraph_update_surface(NV2AState *d, bool upload,
    bool color_write, bool zeta_write)
{
    PGRAPHState *pg = &d->pgraph;

    if (!pg->opengl_enabled) {
        return;
    }

    pg->surface_shape.z_format = GET_MASK(pg->KelvinPrimitive.SetControl0, NV097_SET_CONTROL0_Z_FORMAT); //GET_MASK(pg->pgraph_regs[NV_PGRAPH_SETUPRASTER / 4], NV_PGRAPH_SETUPRASTER_Z_FORMAT);

    /* FIXME: Does this apply to CLEARs too? */
    color_write = color_write && pgraph_get_color_write_enabled(pg);
    zeta_write = zeta_write && pgraph_get_zeta_write_enabled(pg);

    if (upload && pgraph_get_framebuffer_dirty(pg)) {
        assert(!pg->surface_color.draw_dirty);
        assert(!pg->surface_zeta.draw_dirty);

        pg->surface_color.buffer_dirty = true;
        pg->surface_zeta.buffer_dirty = true;

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D,
                                0, 0);

        if (pg->gl_color_buffer) {
            glDeleteTextures(1, &pg->gl_color_buffer);
            pg->gl_color_buffer = 0;
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                                GL_DEPTH_ATTACHMENT,
                                GL_TEXTURE_2D,
                                0, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                                GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_TEXTURE_2D,
                                0, 0);

        if (pg->gl_zeta_buffer) {
            glDeleteTextures(1, &pg->gl_zeta_buffer);
            pg->gl_zeta_buffer = 0;
        }

        memcpy(&pg->last_surface_shape, &pg->surface_shape,
            sizeof(SurfaceShape));
    }
    if ((color_write || (!upload && pg->surface_color.write_enabled_cache))
        && (upload || pg->surface_color.draw_dirty)) {
        pgraph_update_surface_part(d, upload, true);
    }


    if ((zeta_write || (!upload && pg->surface_zeta.write_enabled_cache))
        && (upload || pg->surface_zeta.draw_dirty)) {
        pgraph_update_surface_part(d, upload, false);
    }
}

static void pgraph_bind_textures(NV2AState *d)
{
    int i;
    PGRAPHState *pg = &d->pgraph;

    if (!(pg->opengl_enabled))
        return;

    NV2A_GL_DGROUP_BEGIN("%s", __func__);

    for (i=0; i<NV2A_MAX_TEXTURES; i++) {

        uint32_t ctl_0 = pg->KelvinPrimitive.SetTexture[i].Control0;
        uint32_t ctl_1 = pg->KelvinPrimitive.SetTexture[i].Control1;
        uint32_t fmt = pg->KelvinPrimitive.SetTexture[i].Format;
        uint32_t filter = pg->KelvinPrimitive.SetTexture[i].Filter;
        uint32_t address = pg->KelvinPrimitive.SetTexture[i].Address;
        uint32_t palette =  pg->KelvinPrimitive.SetTexture[i].Palette;

        bool enabled = ctl_0 & NV_PGRAPH_TEXCTL0_0_ENABLE;
        unsigned int min_mipmap_level =
            GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_MIN_LOD_CLAMP);
        unsigned int max_mipmap_level =
            GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_MAX_LOD_CLAMP);

        unsigned int pitch =
            GET_MASK(ctl_1, NV_PGRAPH_TEXCTL1_0_IMAGE_PITCH);

        bool dma_select =
            fmt & NV_PGRAPH_TEXFMT0_CONTEXT_DMA;
        bool cubemap =
            fmt & NV_PGRAPH_TEXFMT0_CUBEMAPENABLE;
        unsigned int dimensionality =
            GET_MASK(fmt, NV_PGRAPH_TEXFMT0_DIMENSIONALITY);
        unsigned int color_format = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_COLOR);
        unsigned int levels = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS);
        unsigned int log_width = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_U);
        unsigned int log_height = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_V);
        unsigned int log_depth = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_P);

        unsigned int rect_width =
            GET_MASK(pg->KelvinPrimitive.SetTexture[i].ImageRect,
                     NV_PGRAPH_TEXIMAGERECT0_WIDTH);
        unsigned int rect_height =
            GET_MASK(pg->KelvinPrimitive.SetTexture[i].ImageRect,
                     NV_PGRAPH_TEXIMAGERECT0_HEIGHT);
#ifdef DEBUG_NV2A
        unsigned int lod_bias =
            GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS);
#endif
        unsigned int min_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
        unsigned int mag_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);

        unsigned int addru = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU);
        unsigned int addrv = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV);
        unsigned int addrp = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRP);

        bool border_source_color = (fmt & NV_PGRAPH_TEXFMT0_BORDER_SOURCE); // != NV_PGRAPH_TEXFMT0_BORDER_SOURCE_TEXTURE;
        uint32_t border_color = pg->KelvinPrimitive.SetTexture[i].BorderColor;

        unsigned int offset = pg->KelvinPrimitive.SetTexture[i].Offset;

        bool palette_dma_select =
            palette & NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA;
        unsigned int palette_length_index =
            GET_MASK(palette, NV_PGRAPH_TEXPALETTE0_LENGTH);
        unsigned int palette_offset =
            palette & NV_PGRAPH_TEXPALETTE0_OFFSET;

        unsigned int palette_length = 0;
        switch (palette_length_index) {
        case NV_PGRAPH_TEXPALETTE0_LENGTH_256: palette_length = 256; break;
        case NV_PGRAPH_TEXPALETTE0_LENGTH_128: palette_length = 128; break;
        case NV_PGRAPH_TEXPALETTE0_LENGTH_64: palette_length = 64; break;
        case NV_PGRAPH_TEXPALETTE0_LENGTH_32: palette_length = 32; break;
        default: assert(false); break;
        }

        /* Check for unsupported features */
        assert(!(filter & NV_PGRAPH_TEXFILTER0_ASIGNED));
        assert(!(filter & NV_PGRAPH_TEXFILTER0_RSIGNED));
        assert(!(filter & NV_PGRAPH_TEXFILTER0_GSIGNED));
        assert(!(filter & NV_PGRAPH_TEXFILTER0_BSIGNED));

        glActiveTexture(GL_TEXTURE0 + i);
        if (!enabled) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);
            glBindTexture(GL_TEXTURE_1D, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);
            continue;
        }

        if (!pg->bumpenv_dirty[i] && pg->texture_binding[i]) {
            glBindTexture(pg->texture_binding[i]->gl_target,
                          pg->texture_binding[i]->gl_texture);
            continue;
        }

        NV2A_DPRINTF(" texture %d is format 0x%x, off 0x%x (r %d, %d or %d, %d, %d; %d%s),"
                        " filter %x %x, levels %d-%d %d bias %d\n",
                     i, color_format, offset,
                     rect_width, rect_height,
                     1 << log_width, 1 << log_height, 1 << log_depth,
                     pitch,
                     cubemap ? "; cubemap" : "",
                     min_filter, mag_filter,
                     min_mipmap_level, max_mipmap_level, levels,
                     lod_bias);

        assert(color_format < ARRAY_SIZE(kelvin_color_format_map));
        ColorFormatInfo f = kelvin_color_format_map[color_format];
        if (f.bytes_per_pixel == 0) {
            fprintf(stderr, "nv2a: unimplemented texture color format 0x%x\n",
                    color_format);
            abort();
        }

        unsigned int width, height, depth;
        if (f.encoding == linear) {
            assert(dimensionality == 2);
            width = rect_width;
            height = rect_height;
            depth = 1;
        } else {
            width = 1 << log_width;
            height = 1 << log_height;
            depth = 1 << log_depth;

            /* FIXME: What about 3D mipmaps? */
            levels = MIN(levels, max_mipmap_level + 1);
            if (f.encoding == swizzled) {
                /* Discard mipmap levels that would be smaller than 1x1.
                 * FIXME: Is this actually needed?
                 *
                 * >> Level 0: 32 x 4
                 *    Level 1: 16 x 2
                 *    Level 2: 8 x 1
                 *    Level 3: 4 x 1
                 *    Level 4: 2 x 1
                 *    Level 5: 1 x 1
                 */
                levels = MIN(levels, MAX(log_width, log_height) + 1);
            } else {
                /* OpenGL requires DXT textures to always have a width and
                 * height a multiple of 4. The Xbox and DirectX handles DXT
                 * textures smaller than 4 by padding the reset of the block.
                 *
                 * See:
                 * https://msdn.microsoft.com/en-us/library/windows/desktop/bb204843(v=vs.85).aspx
                 * https://msdn.microsoft.com/en-us/library/windows/desktop/bb694531%28v=vs.85%29.aspx#Virtual_Size
                 *
                 * Work around this for now by discarding mipmap levels that
                 * would result in too-small textures. A correct solution
                 * will be to decompress these levels manually, or add texture
                 * sampling logic.
                 *
                 * >> Level 0: 64 x 8
                 *    Level 1: 32 x 4
                 *    Level 2: 16 x 2 << Ignored
                 * >> Level 0: 16 x 16
                 *    Level 1: 8 x 8
                 *    Level 2: 4 x 4 << OK!
                 */
                if (log_width < 2 || log_height < 2) {
                    /* Base level is smaller than 4x4... */
                    levels = 1;
                } else {
                    levels = MIN(levels, MIN(log_width, log_height) - 1);
                }
            }
            assert(levels > 0);
        }

        hwaddr dma_len;
        uint8_t *texture_data;
        if (dma_select) {
            texture_data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaB, &dma_len);
        } else {
            texture_data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaA, &dma_len);
        }
        assert(offset < dma_len);
        texture_data += offset;

        hwaddr palette_dma_len;
        uint8_t *palette_data;
        if (palette_dma_select) {
            palette_data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaB, &palette_dma_len);
        } else {
            palette_data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaA, &palette_dma_len);
        }
        assert(palette_offset < palette_dma_len);
        palette_data += palette_offset;

        NV2A_DPRINTF(" - 0x%tx\n", texture_data - d->vram_ptr);

        size_t length = 0;
        if (f.encoding == linear) {
            assert(cubemap == false);
            assert(dimensionality == 2);
            length = height * pitch;
        } else {
            if (dimensionality >= 2) {
                unsigned int w = width, h = height;
                unsigned int level;
                if (f.encoding == swizzled) {
                    for (level = 0; level < levels; level++) {
                        w = MAX(w, 1); h = MAX(h, 1);
                        length += w * h * f.bytes_per_pixel;
                        w /= 2;
                        h /= 2;
                    }
                } else {
                    /* Compressed textures are a bit different */
                    unsigned int block_size;
                    if (f.gl_internal_format ==
                            GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                        block_size = 8;
                    } else {
                        block_size = 16;
                    }

                    for (level = 0; level < levels; level++) {
                        w = MAX(w, 4); h = MAX(h, 4);
                        length += w/4 * h/4 * block_size;
                        w /= 2; h /= 2;
                    }
                }
                if (cubemap) {
                    assert(dimensionality == 2);
                    length *= 6;
                }
                if (dimensionality >= 3) {
                    length *= depth;
                }
            }
        }

        TextureShape state;
        state.cubemap = cubemap;
        state.dimensionality = dimensionality;
        state.color_format = color_format;
        state.levels = levels;
        state.width = width;
        state.height = height;
        state.depth = depth;
        state.min_mipmap_level = min_mipmap_level;
        state.max_mipmap_level = max_mipmap_level;
        state.pitch = pitch;

#ifdef USE_TEXTURE_CACHE
        TextureKey key;
        key.state = state;
        key.data_hash = fast_hash(texture_data, length, 5003)
            ^ fnv_hash(palette_data, palette_length);
        key.texture_data = texture_data;
        key.palette_data = palette_data;

        gpointer cache_key = g_malloc(sizeof(TextureKey));
        memcpy(cache_key, &key, sizeof(TextureKey));

        GError *err;
        TextureBinding *binding = (TextureBinding *)g_lru_cache_get(pg->texture_cache, cache_key, &err);
        assert(binding);
        binding->refcnt++;
#else
        TextureBinding *binding = generate_texture(state,
                                                   texture_data, palette_data);
#endif

        glBindTexture(binding->gl_target, binding->gl_texture);


        if (f.encoding == linear) {
            /* sometimes games try to set mipmap min filters on linear textures.
             * this could indicate a bug... */
            switch (min_filter) {
            case NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD:
            case NV_PGRAPH_TEXFILTER0_MIN_BOX_TENT_LOD:
                min_filter = NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0;
                break;
            case NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD:
            case NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD:
                min_filter = NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0;
                break;
            }
        }

        glTexParameteri(binding->gl_target, GL_TEXTURE_MIN_FILTER,
            pgraph_texture_min_filter_map[min_filter]);
        glTexParameteri(binding->gl_target, GL_TEXTURE_MAG_FILTER,
            pgraph_texture_mag_filter_map[mag_filter]);

        /* Texture wrapping */
        assert(addru < ARRAY_SIZE(pgraph_texture_addr_map));
        glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_S,
            pgraph_texture_addr_map[addru]);
        if (dimensionality > 1) {
            assert(addrv < ARRAY_SIZE(pgraph_texture_addr_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_T,
                pgraph_texture_addr_map[addrv]);
        }
        if (dimensionality > 2) {
            assert(addrp < ARRAY_SIZE(pgraph_texture_addr_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_R,
                pgraph_texture_addr_map[addrp]);
        }

        /* FIXME: Only upload if necessary? [s, t or r = GL_CLAMP_TO_BORDER] */
        if (border_source_color) {
            GLfloat gl_border_color[] = {
                /* FIXME: Color channels might be wrong order */
                ((border_color >> 16) & 0xFF) / 255.0f, /* red */
                ((border_color >> 8) & 0xFF) / 255.0f,  /* green */
                (border_color & 0xFF) / 255.0f,         /* blue */
                ((border_color >> 24) & 0xFF) / 255.0f  /* alpha */
            };
            glTexParameterfv(binding->gl_target, GL_TEXTURE_BORDER_COLOR,
                gl_border_color);
        }

        if (pg->texture_binding[i]) {
            texture_binding_destroy(pg->texture_binding[i]);
        }
        pg->texture_binding[i] = binding;
        pg->bumpenv_dirty[i] = false;
    }
    NV2A_GL_DGROUP_END();
}

static void pgraph_apply_anti_aliasing_factor(PGRAPHState *pg,
                                              unsigned int *width,
                                              unsigned int *height)
{
    switch (pg->surface_shape.anti_aliasing) {
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1:
        break;
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2:
        if (width) { *width *= 2; }
        break;
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_SQUARE_OFFSET_4:
        if (width) { *width *= 2; }
        if (height) { *height *= 2; }
        break;
    default:
        assert(false);
        break;
    }
}

static void pgraph_get_surface_dimensions(PGRAPHState *pg,
                                          unsigned int *width,
                                          unsigned int *height)
{
    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);
    if (swizzle) {
        *width = 1 << pg->surface_shape.log_width;
        *height = 1 << pg->surface_shape.log_height;
    } else {
        *width = pg->surface_shape.clip_width;
        *height = pg->surface_shape.clip_height;
    }
}

static void pgraph_update_memory_buffer(NV2AState *d, hwaddr addr, hwaddr size,
                                        bool f)
{
    glBindBuffer(GL_ARRAY_BUFFER, d->pgraph.gl_memory_buffer);

    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;

    assert(end < d->vram_size);

    // if (f || memory_region_test_and_clear_dirty(d->vram,
    //                                             addr,
    //                                             end - addr,
    //                                             DIRTY_MEMORY_NV2A)) {
        glBufferSubData(GL_ARRAY_BUFFER, addr, end - addr, d->vram_ptr + addr);
    // }

//		auto error = glGetError();
//		assert(error == GL_NO_ERROR);
}

static void pgraph_bind_vertex_attributes(NV2AState *d,
                                          unsigned int num_elements,
                                          bool inline_data,
                                          unsigned int inline_stride)
{
    unsigned int i, j;
    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);

    if (inline_data) {
        NV2A_GL_DGROUP_BEGIN("%s (num_elements: %d inline stride: %d)",
                             __func__, num_elements, inline_stride);
    } else {
        NV2A_GL_DGROUP_BEGIN("%s (num_elements: %d)", __func__, num_elements);
    }

    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *vertex_attribute = &pg->vertex_attributes[i];
        if (vertex_attribute->count == 0) {
            glDisableVertexAttribArray(i);
			float *inline_value = pgraph_get_vertex_attribute_inline_value(pg, i);
            glVertexAttrib4fv(i, inline_value);
            continue;
        }

        uint8_t *data;
        unsigned int in_stride;
        if (inline_data && vertex_attribute->needs_conversion) {
            data = (uint8_t*)pg->inline_array
                    + vertex_attribute->inline_array_offset;
            in_stride = inline_stride;
        } else {
            hwaddr dma_len;
            if (vertex_attribute->dma_select) {
                data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaVertexB, &dma_len);
            } else {
                data = (uint8_t*)nv_dma_map(d, pg->KelvinPrimitive.SetContextDmaVertexA, &dma_len);
            }

            assert(vertex_attribute->offset < dma_len);
            data += vertex_attribute->offset;

            in_stride = vertex_attribute->stride;
        }

        if (vertex_attribute->needs_conversion) {
            NV2A_DPRINTF("converted %d\n", i);

            unsigned int out_stride = vertex_attribute->converted_size
                                    * vertex_attribute->converted_count;

            if (num_elements > vertex_attribute->converted_elements) {
                vertex_attribute->converted_buffer = (uint8_t*)g_realloc(
                    vertex_attribute->converted_buffer,
                    num_elements * out_stride);
            }

            for (j=vertex_attribute->converted_elements; j<num_elements; j++) {
                uint8_t *in = data + j * in_stride;
                uint8_t *out = vertex_attribute->converted_buffer + j * out_stride;

                switch (vertex_attribute->format) {
                case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP: {
                    uint32_t p = ldl_le_p((uint32_t*)in);
                    float *xyz = (float*)out;
                    xyz[0] = ((int32_t)(((p >>  0) & 0x7FF) << 21) >> 21)
                                                                    / 1023.0f;
                    xyz[1] = ((int32_t)(((p >> 11) & 0x7FF) << 21) >> 21)
                                                                    / 1023.0f;
                    xyz[2] = ((int32_t)(((p >> 22) & 0x3FF) << 22) >> 22)
                                                                    / 511.0f;
                    break;
                }
                default:
                    assert(false);
                    break;
                }
            }


            glBindBuffer(GL_ARRAY_BUFFER, vertex_attribute->gl_converted_buffer);
            if (num_elements != vertex_attribute->converted_elements) {
                glBufferData(GL_ARRAY_BUFFER,
                                num_elements * out_stride,
                                vertex_attribute->converted_buffer,
                                GL_DYNAMIC_DRAW);
                vertex_attribute->converted_elements = num_elements;
            }


            glVertexAttribPointer(i,
                vertex_attribute->converted_count,
                vertex_attribute->gl_type,
                vertex_attribute->gl_normalize,
                out_stride,
                0);
        } else if (inline_data) {
            glBindBuffer(GL_ARRAY_BUFFER, pg->gl_inline_array_buffer);
            glVertexAttribPointer(i,
                                    vertex_attribute->gl_count,
                                    vertex_attribute->gl_type,
                                    vertex_attribute->gl_normalize,
                                    inline_stride,
                                    (void*)(uintptr_t)vertex_attribute->inline_array_offset);
        } else {
            hwaddr addr = data - d->vram_ptr;
            pgraph_update_memory_buffer(d, addr,
                                        num_elements * vertex_attribute->stride,
                                        false);
            glVertexAttribPointer(i,
                vertex_attribute->gl_count,
                vertex_attribute->gl_type,
                vertex_attribute->gl_normalize,
                vertex_attribute->stride,
                (void*)(uint64_t)(addr));
        }

        glEnableVertexAttribArray(i);
    }
    NV2A_GL_DGROUP_END();
}

static unsigned int pgraph_bind_inline_array(NV2AState *d)
{
    int i;

    PGRAPHState *pg = &d->pgraph;

    assert(pg->opengl_enabled);

    unsigned int offset = 0;
    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *vertex_attribute = &pg->vertex_attributes[i];
        if (vertex_attribute->count) {
            vertex_attribute->inline_array_offset = offset;

            NV2A_DPRINTF("bind inline vertex_attribute %d size=%d, count=%d\n",
                i, vertex_attribute->size, vertex_attribute->count);
            offset += vertex_attribute->size * vertex_attribute->count;
            assert(offset % 4 == 0);
        }
    }

    unsigned int vertex_size = offset;


    unsigned int index_count = pg->inline_array_length*4 / vertex_size;

    NV2A_DPRINTF("draw inline array %d, %d\n", vertex_size, index_count);

    glBindBuffer(GL_ARRAY_BUFFER, pg->gl_inline_array_buffer);
    glBufferData(GL_ARRAY_BUFFER, pg->inline_array_length*4, pg->inline_array,
                 GL_DYNAMIC_DRAW);

    pgraph_bind_vertex_attributes(d, index_count, true, vertex_size);

    return index_count;
}

/* 16 bit to [0.0, F16_MAX = 511.9375] */
static float convert_f16_to_float(uint16_t f16) {
    if (f16 == 0x0000) { return 0.0f; }
    uint32_t i = (f16 << 11) + 0x3C000000;
    return *(float*)&i;
}

/* 24 bit to [0.0, F24_MAX] */
static float convert_f24_to_float(uint32_t f24) {
    assert(!(f24 >> 24));
    f24 &= 0xFFFFFF;
    if (f24 == 0x000000) { return 0.0f; }
    uint32_t i = f24 << 7;
    return *(float*)&i;
}

extern void __R6G5B5ToARGBRow_C(const uint8_t* src_r6g5b5, uint8_t* dst_argb, int width);
extern void ____YUY2ToARGBRow_C(const uint8_t* src_yuy2, uint8_t* rgb_buf, int width);
extern void ____UYVYToARGBRow_C(const uint8_t* src_uyvy, uint8_t* rgb_buf, int width);

/* 'converted_format' indicates the format that results when convert_texture_data() returns non-NULL converted_data. */
static const int converted_format = NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8;

static uint8_t* convert_texture_data(const unsigned int color_format,
                                     const uint8_t *data,
                                     const uint8_t *palette_data,
                                     const unsigned int width,
                                     const unsigned int height,
                                     const unsigned int depth,
                                     const unsigned int row_pitch,
                                     const unsigned int slice_pitch)
{
    // Note : Unswizzle is already done when entering here
    switch (color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8: {
        // Test-case : WWE RAW2
        assert(depth == 1); /* FIXME */
        uint8_t* converted_data = (uint8_t*)g_malloc(width * height * 4);
        unsigned int x, y;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint8_t index = data[y * row_pitch + x];
                uint32_t color = *(uint32_t*)(palette_data + index * 4);
                *(uint32_t*)(converted_data + y * width * 4 + x * 4) = color;
            }
        }
        return converted_data;
    }
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X7SY9: {
        assert(false); /* FIXME */
        return NULL;
    }
    case NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8: {
        // Test-case : WWE RAW2
        assert(depth == 1); /* FIXME */
        uint8_t* converted_data = (uint8_t*)g_malloc(width * height * 4);
        unsigned int y;
        for (y = 0; y < height; y++) {
            const uint8_t* line = &data[y * width * 2];
            uint8_t* pixel = &converted_data[(y * width) * 4];
            ____YUY2ToARGBRow_C(line, pixel, width);
            // Note : LC_IMAGE_CR8YB8CB8YA8 suggests UYVY format,
            // but for an unknown reason, the actual encoding is YUY2
        }
        return converted_data;
    }	
    case NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8: {
        assert(depth == 1); /* FIXME */
        uint8_t* converted_data = (uint8_t*)g_malloc(width * height * 4);
        unsigned int y;
        for (y = 0; y < height; y++) {
            const uint8_t* line = &data[y * width * 2];
            uint8_t* pixel = &converted_data[(y * width) * 4];
            ____UYVYToARGBRow_C(line, pixel, width); // TODO : Validate LC_IMAGE_YB8CR8YA8CB8 indeed requires ____UYVYToARGBRow_C()
        }
        return converted_data;
    }
    case NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_A4V6YB6A4U6YA6: {
        assert(false); /* FIXME */
        return NULL;
    }
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8CR8CB8Y8: {
        assert(false); /* FIXME */
        return NULL;
    }
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R6G5B5: {
        assert(depth == 1); /* FIXME */
        uint8_t *converted_data = (uint8_t*)g_malloc(width * height * 4);
        unsigned int y;
        for (y = 0; y < height; y++) {
            uint16_t rgb655 = *(uint16_t*)(data + y * row_pitch);
            int8_t *pixel = (int8_t*)&converted_data[(y * width) * 4];
            __R6G5B5ToARGBRow_C((const uint8_t*)rgb655, (uint8_t*)pixel, width);
        }
        return converted_data;
    }
    default:
        return NULL;
    }
}

/* returns the format of the output, either identical to the input format, or the converted format - see converted_format */
static int upload_gl_texture(GLenum gl_target,
                              const TextureShape s,
                              const uint8_t *texture_data,
                              const uint8_t *palette_data)
{
    //assert(pg->opengl_enabled);
    int resulting_format = s.color_format;
    ColorFormatInfo f = kelvin_color_format_map[s.color_format];

    switch(gl_target) {
    case GL_TEXTURE_1D:
        assert(false);
        break;
    case GL_TEXTURE_RECTANGLE: {
        /* Can't handle strides unaligned to pixels */
        assert(s.pitch % f.bytes_per_pixel == 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,
                      s.pitch / f.bytes_per_pixel);

        uint8_t *unswizzled = NULL;
        if (f.encoding == swizzled) { // TODO : Verify this works correctly
            unswizzled = (uint8_t*)g_malloc(s.height * s.pitch);
            unswizzle_rect(texture_data, s.width, s.height,
                            unswizzled, s.pitch, f.bytes_per_pixel);
        }
        uint8_t *converted = convert_texture_data(s.color_format, unswizzled ? unswizzled : texture_data,
                                                  palette_data,
                                                  s.width, s.height, 1,
                                                  s.pitch, 0);

        resulting_format = converted ? converted_format : s.color_format;
        ColorFormatInfo cf = kelvin_color_format_map[resulting_format];
        glTexImage2D(gl_target, 0, cf.gl_internal_format,
                     s.width, s.height, 0,
                     cf.gl_format, cf.gl_type,
                     converted ? converted : unswizzled ? unswizzled : texture_data);

        if (converted) {
          g_free(converted);
        }
        if (unswizzled) {
            g_free(unswizzled);
        }

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        break;
    }
    case GL_TEXTURE_2D:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: {

        unsigned int width = s.width, height = s.height;

        unsigned int level;
        for (level = 0; level < s.levels; level++) {
            if (f.encoding == compressed) {

                width = MAX(width, 4); height = MAX(height, 4);

                unsigned int block_size;
                if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }

                glCompressedTexImage2D(gl_target, level, f.gl_internal_format,
                                       width, height, 0,
                                       width/4 * height/4 * block_size,
                                       texture_data);

                texture_data += width/4 * height/4 * block_size;
            } else {

                width = MAX(width, 1); height = MAX(height, 1);

                unsigned int pitch = width * f.bytes_per_pixel;
                uint8_t *unswizzled = NULL;
                if (f.encoding == swizzled) {
                    unswizzled = (uint8_t*)g_malloc(height * pitch);
                    unswizzle_rect(texture_data, width, height,
                                   unswizzled, pitch, f.bytes_per_pixel);
                }

                uint8_t *converted = convert_texture_data(s.color_format, unswizzled ? unswizzled : texture_data,
                                                          palette_data,
                                                          width, height, 1,
                                                          pitch, 0);

                resulting_format = converted ? converted_format : s.color_format;
                ColorFormatInfo cf = kelvin_color_format_map[resulting_format];
                glTexImage2D(gl_target, level, cf.gl_internal_format,
                             width, height, 0,
                             cf.gl_format, cf.gl_type,
                             converted ? converted : unswizzled ? unswizzled : texture_data);

                if (converted) {
                    g_free(converted);
                }
                if (unswizzled) {
                    g_free(unswizzled);
                }

                texture_data += pitch * height;
            }

            width /= 2;
            height /= 2;
        }

        break;
    }
    case GL_TEXTURE_3D: {

        unsigned int width = s.width, height = s.height, depth = s.depth;

        unsigned int level;
        for (level = 0; level < s.levels; level++) {

            unsigned int row_pitch = width * f.bytes_per_pixel;
            unsigned int slice_pitch = row_pitch * height;
            uint8_t *unswizzled = NULL;
            if (f.encoding == swizzled) {
                unswizzled = (uint8_t*)g_malloc(slice_pitch * depth);
                unswizzle_box(texture_data, width, height, depth, unswizzled,
                               row_pitch, slice_pitch, f.bytes_per_pixel);
            }
            uint8_t *converted = convert_texture_data(s.color_format, unswizzled ? unswizzled : texture_data,
                                                      palette_data,
                                                      width, height, depth,
                                                      row_pitch, slice_pitch);

            resulting_format = converted ? converted_format : s.color_format;
            ColorFormatInfo cf = kelvin_color_format_map[resulting_format];
            glTexImage3D(gl_target, level, cf.gl_internal_format,
                         width, height, depth, 0,
                         cf.gl_format, cf.gl_type,
                         converted ? converted : unswizzled ? unswizzled : texture_data);

            if (converted) {
                g_free(converted);
            }
            if (unswizzled) {
                g_free(unswizzled);
            }

            texture_data += width * height * depth * f.bytes_per_pixel;

            width /= 2;
            height /= 2;
            depth /= 2;
        }
        break;
    }
    default:
        assert(false);
        break;
    }
    return resulting_format;
}

static TextureBinding* generate_texture(const TextureShape s,
                                        const uint8_t *texture_data,
                                        const uint8_t *palette_data)
{
    // assert(pg->opengl_enabled);

    ColorFormatInfo f = kelvin_color_format_map[s.color_format];

    /* Create a new opengl texture */
    GLuint gl_texture;
    glGenTextures(1, &gl_texture);

    GLenum gl_target;
    if (s.cubemap) {
        assert(f.encoding != linear);
        assert(s.dimensionality == 2);
        gl_target = GL_TEXTURE_CUBE_MAP;
    } else {
        if (f.encoding == linear) { /* FIXME : Include compressed too? (!= swizzled) */
            /* linear textures use unnormalised texcoords.
             * GL_TEXTURE_RECTANGLE_ARB conveniently also does, but
             * does not allow repeat and mirror wrap modes.
             *  (or mipmapping, but xbox d3d says 'Non swizzled and non
             *   compressed textures cannot be mip mapped.')
             * Not sure if that'll be an issue. */

            /* FIXME: GLSL 330 provides us with textureSize()! Use that? */
            gl_target = GL_TEXTURE_RECTANGLE;
            assert(s.dimensionality == 2);
        } else {
            switch(s.dimensionality) {
            case 1: gl_target = GL_TEXTURE_1D; break;
            case 2: gl_target = GL_TEXTURE_2D; break;
            case 3: gl_target = GL_TEXTURE_3D; break;
            default:
                assert(false);
                break;
            }
        }
    }

    glBindTexture(gl_target, gl_texture);

    NV2A_GL_DLABEL(GL_TEXTURE, gl_texture,
                   "format: 0x%02X%s, %d dimensions%s, width: %d, height: %d, depth: %d",
                   s.color_format, (f.encoding == linear) ? "" : (f.encoding == swizzled) ? " (SZ)" : " (DXT)", // compressed
                   s.dimensionality, s.cubemap ? " (Cubemap)" : "",
                   s.width, s.height, s.depth);

    /* Linear textures don't support mipmapping */
    if (f.encoding != linear) {
        glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
            s.min_mipmap_level);
        glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
            s.levels - 1);
    }

    /* Set this before calling upload_gl_texture() to prevent potential conversions */
    if (f.gl_swizzle_mask) {
        glTexParameteriv(gl_target, GL_TEXTURE_SWIZZLE_RGBA,
                         f.gl_swizzle_mask);
    }

    if (gl_target == GL_TEXTURE_CUBE_MAP) {

        size_t length = 0;
        unsigned int w = s.width, h = s.height;
        unsigned int level;
        for (level = 0; level < s.levels; level++) {
            /* FIXME: This is wrong for compressed textures and textures with 1x? non-square mipmaps */
            length += w * h * f.bytes_per_pixel;
            w /= 2;
            h /= 2;
        }

        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                          s, texture_data + 0 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                          s, texture_data + 1 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                          s, texture_data + 2 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                          s, texture_data + 3 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                          s, texture_data + 4 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
                          s, texture_data + 5 * length, palette_data);
    } else {
        upload_gl_texture(gl_target, s, texture_data, palette_data);
    }

    TextureBinding* ret = (TextureBinding *)g_malloc(sizeof(TextureBinding));
    ret->gl_target = gl_target;
    ret->gl_texture = gl_texture;
    ret->refcnt = 1;
    return ret;
}

// NOTE: Might want to change guint to guint64 for return.
/* functions for texture LRU cache */
static guint texture_key_hash(gconstpointer key)
{
    const TextureKey *k = (const TextureKey *)key;
    uint64_t state_hash = fnv_hash(
        (const uint8_t*)&k->state, sizeof(TextureShape));
    return guint(state_hash ^ k->data_hash);
}
static gboolean texture_key_equal(gconstpointer a, gconstpointer b)
{
    const TextureKey *ak = (const TextureKey *)a, *bk = (const TextureKey *)b;
    return memcmp(&ak->state, &bk->state, sizeof(TextureShape)) == 0
            && ak->data_hash == bk->data_hash;
}
static gpointer texture_key_retrieve(gpointer key, gpointer user_data, GError **error)
{
    const TextureKey *k = (const TextureKey *)key;
    TextureBinding *v = generate_texture(k->state,
                                         k->texture_data,
                                         k->palette_data);
    if (error != NULL) {
        *error = NULL;
    }
    return v;
}
static void texture_key_destroy(gpointer data)
{
    g_free(data);
}
static void texture_binding_destroy(gpointer data)
{
    TextureBinding *binding = (TextureBinding *)data;

    // assert(pg->opengl_enabled);

    assert(binding->refcnt > 0);
    binding->refcnt--;
    if (binding->refcnt == 0) {
        glDeleteTextures(1, &binding->gl_texture);
        g_free(binding);
    }
}

// NOTE: Might want to change guint to guint64 for return.
/* hash and equality for shader cache hash table */
static guint shader_hash(gconstpointer key)
{
    return (guint)fnv_hash((const uint8_t *)key, sizeof(ShaderState));
}
static gboolean shader_equal(gconstpointer a, gconstpointer b)
{
    const ShaderState *as = (const ShaderState *)a, *bs = (const ShaderState *)b;
    return memcmp(as, bs, sizeof(ShaderState)) == 0;
}

static unsigned int kelvin_map_stencil_op(uint32_t parameter)
{
    unsigned int op;
    switch (parameter) {
    case NV097_SET_STENCIL_OP_V_KEEP:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_KEEP; break;
    case NV097_SET_STENCIL_OP_V_ZERO:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_ZERO; break;
    case NV097_SET_STENCIL_OP_V_REPLACE:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_REPLACE; break;
    case NV097_SET_STENCIL_OP_V_INCRSAT:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INCRSAT; break;
    case NV097_SET_STENCIL_OP_V_DECRSAT:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_DECRSAT; break;
    case NV097_SET_STENCIL_OP_V_INVERT:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INVERT; break;
    case NV097_SET_STENCIL_OP_V_INCR:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INCR; break;
    case NV097_SET_STENCIL_OP_V_DECR:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_DECR; break;
    default:
        assert(false);
        break;
    }
    return op;
}

static unsigned int kelvin_map_polygon_mode(uint32_t parameter)
{
    unsigned int mode;
    switch (parameter) {
    case NV097_SET_FRONT_POLYGON_MODE_V_POINT:
        mode = NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_POINT; break;
    case NV097_SET_FRONT_POLYGON_MODE_V_LINE:
        mode = NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_LINE; break;
    case NV097_SET_FRONT_POLYGON_MODE_V_FILL:
        mode = NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_FILL; break;
    default:
        assert(false);
        break;
    }
    return mode;
}

static unsigned int kelvin_map_texgen(uint32_t parameter, unsigned int channel)
{
    assert(channel < 4);
    unsigned int texgen;
    switch (parameter) {
    case NV097_SET_TEXGEN_S_DISABLE:
        texgen = NV_PGRAPH_CSV1_A_T0_S_DISABLE; break;
    case NV097_SET_TEXGEN_S_EYE_LINEAR:
        texgen = NV_PGRAPH_CSV1_A_T0_S_EYE_LINEAR; break;
    case NV097_SET_TEXGEN_S_OBJECT_LINEAR:
        texgen = NV_PGRAPH_CSV1_A_T0_S_OBJECT_LINEAR; break;
    case NV097_SET_TEXGEN_S_SPHERE_MAP:
        assert(channel < 2);
        texgen = NV_PGRAPH_CSV1_A_T0_S_SPHERE_MAP; break;
    case NV097_SET_TEXGEN_S_REFLECTION_MAP:
        assert(channel < 3);
        texgen = NV_PGRAPH_CSV1_A_T0_S_REFLECTION_MAP; break;
    case NV097_SET_TEXGEN_S_NORMAL_MAP:
        assert(channel < 3);
        texgen = NV_PGRAPH_CSV1_A_T0_S_NORMAL_MAP; break;
    default:
        assert(false);
        break;
    }
    return texgen;
}
unsigned int kelvin_to_xbox_map_texgen(uint32_t parameter)
{
    
    unsigned int texgen;
    switch (parameter) {
    case NV097_SET_TEXGEN_S_DISABLE:
        texgen = D3DTSS_TCI_PASSTHRU; break;
    case NV097_SET_TEXGEN_S_EYE_LINEAR:
        texgen = D3DTSS_TCI_CAMERASPACEPOSITION; break;
    //case NV097_SET_TEXGEN_S_OBJECT_LINEAR:
    //   texgen = D3DTSS_TCI_OBJECT; break;
    //case NV097_SET_TEXGEN_S_SPHERE_MAP:
    //    texgen = D3DTSS_TCI_SPHERE; break;
    case NV097_SET_TEXGEN_S_REFLECTION_MAP:
        texgen = D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR; break;
    case NV097_SET_TEXGEN_S_NORMAL_MAP:
        texgen = D3DTSS_TCI_CAMERASPACENORMAL; break;
    default:
        assert(false);
        texgen = D3DTSS_TCI_PASSTHRU;
        break;
    }
    return texgen;
}

static uint64_t fnv_hash(const uint8_t *data, size_t len)
{
    /* 64 bit Fowler/Noll/Vo FNV-1a hash code */
    uint64_t hval = 0xcbf29ce484222325ULL;
    const uint8_t *dp = data;
    const uint8_t *de = data + len;
    while (dp < de) {
        hval ^= (uint64_t) *dp++;
        hval += (hval << 1) + (hval << 4) + (hval << 5) +
            (hval << 7) + (hval << 8) + (hval << 40);
    }

    return hval;
}

static uint64_t fast_hash(const uint8_t *data, size_t len, unsigned int samples)
{
#ifdef __SSE4_2__
    uint64_t h[4] = {len, 0, 0, 0};
    assert(samples > 0);

    if (len < 8 || len % 8) {
        return fnv_hash(data, len);
    }

    assert(len >= 8 && len % 8 == 0);
    const uint64_t *dp = (const uint64_t*)data;
    const uint64_t *de = dp + (len / 8);
    size_t step = len / 8 / samples;
    if (step == 0) step = 1;

    while (dp < de - step * 3) {
        h[0] = __builtin_ia32_crc32di(h[0], dp[step * 0]);
        h[1] = __builtin_ia32_crc32di(h[1], dp[step * 1]);
        h[2] = __builtin_ia32_crc32di(h[2], dp[step * 2]);
        h[3] = __builtin_ia32_crc32di(h[3], dp[step * 3]);
        dp += step * 4;
    }
    if (dp < de - step * 0)
        h[0] = __builtin_ia32_crc32di(h[0], dp[step * 0]);
    if (dp < de - step * 1)
        h[1] = __builtin_ia32_crc32di(h[1], dp[step * 1]);
    if (dp < de - step * 2)
        h[2] = __builtin_ia32_crc32di(h[2], dp[step * 2]);

    return h[0] + (h[1] << 10) + (h[2] << 21) + (h[3] << 32);
#else
    return fnv_hash(data, len);
#endif
}
