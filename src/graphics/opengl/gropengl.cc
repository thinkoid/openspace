// -*- mode: c++; -*-

#include "gropengl.hh"

#include "defs.hh"

#include "assert/assert.hh"
#include "bmpman/bmpman.hh"
#include "cfile/cfile.hh"
#include "cmdline/cmdline.hh"
#include "ddsutils/ddsutils.hh"
#include "debugconsole/console.hh"
#include "graphics/2d.hh"
#include "graphics/line.hh"
#include "graphics/matrix.hh"
#include "graphics/paths/PathRenderer.hh"
#include "gropenglbmpman.hh"
#include "gropengldeferred.hh"
#include "gropengldraw.hh"
#include "gropenglpostprocessing.hh"
#include "gropenglquery.hh"
#include "gropenglshader.hh"
#include "gropenglstate.hh"
#include "gropenglsync.hh"
#include "gropengltexture.hh"
#include "gropengltnl.hh"
#include "io/mouse.hh"
#include "io/timer.hh"
#include "libs/renderdoc/renderdoc.hh"
#include "log/log.hh"
#include "math/floating.hh"
#include "model/model.hh"
#include "nebula/neb.hh"
#include "osapi/osapi.hh"
#include "osapi/osregistry.hh"
#include "pngutils/pngutils.hh"
#include "popup/popup.hh"
#include "render/3d.hh"
#include "shared/globals.hh"
#include "tracing/tracing.hh"

#include "glad/glad.h"

// minimum GL version we can reliably support is 3.2
static const int MIN_REQUIRED_GL_VERSION = 32;

// minimum GLSL version we can reliably support is 110
static const int MIN_REQUIRED_GLSL_VERSION = 150;

int GL_version = 0;
int GLSL_version = 0;

bool GL_initted = 0;

// 0==no fog
// 1==linear
// 2==fog coord EXT
// 3==NV Radial
int OGL_fogmode = 0;

static ushort *GL_original_gamma_ramp = NULL;

int Use_PBOs = 0;

static ubyte *GL_saved_screen = NULL;
static int GL_saved_screen_id = -1;
static GLuint GL_screen_pbo = 0;

float GL_alpha_threshold = 0.0f;

extern std::string Window_title;

extern GLfloat GL_anisotropy;

extern float FreeSpace_gamma;
void gr_opengl_set_gamma(float gamma);

extern float FreeSpace_gamma;
void gr_opengl_set_gamma(float gamma);

static int GL_fullscreen = 0;
static int GL_windowed = 0;
static int GL_minimized = 0;

static GLenum GL_read_format = GL_BGRA;

GLuint GL_vao = 0;

std::string GL_implementation_id;
std::vector< GLint > GL_binary_formats;

static std::unique_ptr< fs2::os::OpenGLContext > GL_context = nullptr;

static std::unique_ptr< fs2::os::GraphicsOperations > graphic_operations = nullptr;
static fs2::os::Viewport *current_viewport = nullptr;

void opengl_go_fullscreen()
{
        if (Cmdline_fullscreen_window || Cmdline_window || GL_fullscreen)
                return;

        gr_opengl_set_gamma(FreeSpace_gamma);

        GL_fullscreen = 1;
        GL_minimized = 0;
        GL_windowed = 0;
}

void opengl_go_windowed()
{
        if (!Cmdline_fullscreen_window && !Cmdline_window)
                return;

        GL_windowed = 1;
        GL_minimized = 0;
        GL_fullscreen = 0;
}

void opengl_minimize()
{
        GL_minimized = 1;
        GL_windowed = 0;
        GL_fullscreen = 0;
}

void gr_opengl_activate(int active)
{
        if (active) {
                if (Cmdline_fullscreen_window || Cmdline_window)
                        opengl_go_windowed();
                else
                        opengl_go_fullscreen();
        } else {
                opengl_minimize();
        }
}

void gr_opengl_clear()
{
        float red = gr_screen.current_clear_color.red / 255.0f;
        float green = gr_screen.current_clear_color.green / 255.0f;
        float blue = gr_screen.current_clear_color.blue / 255.0f;
        float alpha = gr_screen.current_clear_color.alpha / 255.0f;

        if (High_dynamic_range) {
                const float SRGB_GAMMA = 2.2f;

                red = pow(red, SRGB_GAMMA);
                green = pow(green, SRGB_GAMMA);
                blue = pow(blue, SRGB_GAMMA);
        }

        // Make sure that all channels are enabled before doing this.
        GL_state.ColorMask(true, true, true, true);

        glClearColor(red, green, blue, alpha);

        glClear(GL_COLOR_BUFFER_BIT);
}

void gr_opengl_flip()
{
        if (!GL_initted)
                return;

        TRACE_SCOPE(tracing::PageFlip);

        gr_reset_clip();

        mouse_reset_deltas();

        if (Cmdline_gl_finish)
                glFinish();

        current_viewport->swapBuffers();

        opengl_tcache_frame();

#ifndef NDEBUG
        int ic = opengl_check_for_errors();

        if (ic) {
                WARNINGF(LOCATION, "!!DEBUG!! OpenGL Errors this frame: %i", ic);
        }
#endif
}

void gr_opengl_set_clip(int x, int y, int w, int h, int resize_mode)
{
        // check for sanity of parameters
        if (x < 0) {
                x = 0;
        }

        if (y < 0) {
                y = 0;
        }

        int to_resize = (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1)));

        int max_w = ((to_resize) ? gr_screen.max_w_unscaled : gr_screen.max_w);
        int max_h = ((to_resize) ? gr_screen.max_h_unscaled : gr_screen.max_h);

        if ((gr_screen.rendering_to_texture != -1) && to_resize) {
                gr_unsize_screen_pos(&max_w, &max_h);
        }

        if (x >= max_w) {
                x = max_w - 1;
        }

        if (y >= max_h) {
                y = max_h - 1;
        }

        if (x + w > max_w) {
                w = max_w - x;
        }

        if (y + h > max_h) {
                h = max_h - y;
        }

        if (w > max_w) {
                w = max_w;
        }

        if (h > max_h) {
                h = max_h;
        }

        gr_screen.offset_x_unscaled = x;
        gr_screen.offset_y_unscaled = y;
        gr_screen.clip_left_unscaled = 0;
        gr_screen.clip_right_unscaled = w - 1;
        gr_screen.clip_top_unscaled = 0;
        gr_screen.clip_bottom_unscaled = h - 1;
        gr_screen.clip_width_unscaled = w;
        gr_screen.clip_height_unscaled = h;

        if (to_resize) {
                gr_resize_screen_pos(&x, &y, &w, &h, resize_mode);
        } else {
                gr_unsize_screen_pos(
                        &gr_screen.offset_x_unscaled, &gr_screen.offset_y_unscaled);
                gr_unsize_screen_pos(
                        &gr_screen.clip_right_unscaled, &gr_screen.clip_bottom_unscaled);
                gr_unsize_screen_pos(
                        &gr_screen.clip_width_unscaled, &gr_screen.clip_height_unscaled);
        }

        gr_screen.offset_x = x;
        gr_screen.offset_y = y;
        gr_screen.clip_left = 0;
        gr_screen.clip_right = w - 1;
        gr_screen.clip_top = 0;
        gr_screen.clip_bottom = h - 1;
        gr_screen.clip_width = w;
        gr_screen.clip_height = h;

        gr_screen.clip_aspect = float(w) / float(h);
        gr_screen.clip_center_x = (gr_screen.clip_left + gr_screen.clip_right) * 0.5f;
        gr_screen.clip_center_y = (gr_screen.clip_top + gr_screen.clip_bottom) * 0.5f;

        // just return early if we aren't actually going to need the scissor test
        if ((x == 0) && (y == 0) && (w == max_w) && (h == max_h)) {
                GL_state.ScissorTest(GL_FALSE);
                return;
        }

        GL_state.ScissorTest(GL_TRUE);
        if (GL_rendering_to_texture) {
                glScissor(x, y, w, h);
        } else {
                glScissor(x, gr_screen.max_h - y - h, w, h);
        }
}

void gr_opengl_reset_clip()
{
        gr_screen.offset_x = gr_screen.offset_x_unscaled = 0;
        gr_screen.offset_y = gr_screen.offset_y_unscaled = 0;
        gr_screen.clip_left = gr_screen.clip_left_unscaled = 0;
        gr_screen.clip_top = gr_screen.clip_top_unscaled = 0;
        gr_screen.clip_right = gr_screen.clip_right_unscaled = gr_screen.max_w - 1;
        gr_screen.clip_bottom = gr_screen.clip_bottom_unscaled = gr_screen.max_h - 1;
        gr_screen.clip_width = gr_screen.clip_width_unscaled = gr_screen.max_w;
        gr_screen.clip_height = gr_screen.clip_height_unscaled = gr_screen.max_h;

        if (gr_screen.custom_size) {
                gr_unsize_screen_pos(
                        &gr_screen.clip_right_unscaled, &gr_screen.clip_bottom_unscaled);
                gr_unsize_screen_pos(
                        &gr_screen.clip_width_unscaled, &gr_screen.clip_height_unscaled);
        }

        gr_screen.clip_aspect = float(gr_screen.clip_width) / float(gr_screen.clip_height);
        gr_screen.clip_center_x = (gr_screen.clip_left + gr_screen.clip_right) * 0.5f;
        gr_screen.clip_center_y = (gr_screen.clip_top + gr_screen.clip_bottom) * 0.5f;

        GL_state.ScissorTest(GL_FALSE);
}

void gr_opengl_print_screen(const char *filename)
{
        char tmp[MAX_PATH_LEN];
        GLubyte *pixels = NULL;
        GLuint pbo = 0;

        // save to a "screenshots" directory and tack on the filename
        snprintf(tmp, MAX_PATH_LEN - 1, "screenshots/%s.png", filename);

        mkdir(fs2::os::get_config_path("screenshots").c_str(), 0777);

        // glReadBuffer(GL_FRONT);

        // now for the data
        if (Use_PBOs) {
                ASSERT(!pbo);
                glGenBuffers(1, &pbo);

                if (!pbo) {
                        return;
                }

                glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
                glBufferData(
                        GL_PIXEL_PACK_BUFFER, (gr_screen.max_w * gr_screen.max_h * 4),
                        NULL, GL_STATIC_READ);

                glReadBuffer(GL_FRONT);
                glReadPixels(
                        0, 0, gr_screen.max_w, gr_screen.max_h, GL_RGBA,
                        GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

                // map the image data so that we can save it to file
                pixels = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        } else {
                pixels = (GLubyte *)malloc(gr_screen.max_w * gr_screen.max_h * 4);

                if (pixels == NULL) {
                        return;
                }

                glReadPixels(
                        0, 0, gr_screen.max_w, gr_screen.max_h, GL_RGBA,
                        GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
                glFlush();
        }

        if (!png_write_bitmap(
                    fs2::os::get_config_path(tmp).c_str(), gr_screen.max_w,
                    gr_screen.max_h, true, pixels)) {
                RELEASE_WARNINGF(
                        LOCATION, "Failed to write screenshot to \"%s\".",
                        fs2::os::get_config_path(tmp).c_str());
        }

        if (pbo) {
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                pixels = NULL;
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glDeleteBuffers(1, &pbo);
        }

        if (pixels != NULL) {
                free(pixels);
        }
}

void gr_opengl_shutdown()
{
        opengl_tcache_shutdown();
        opengl_tnl_shutdown();
        opengl_scene_texture_shutdown();
        opengl_post_process_shutdown();
        opengl_shader_shutdown();

        GL_initted = false;

        glDeleteVertexArrays(1, &GL_vao);
        GL_vao = 0;

        if (GL_original_gamma_ramp != NULL && fs2::os::getSDLMainWindow() != nullptr) {
                SDL_SetWindowGammaRamp(
                        fs2::os::getSDLMainWindow(), GL_original_gamma_ramp,
                        (GL_original_gamma_ramp + 256), (GL_original_gamma_ramp + 512));
        }

        if (GL_original_gamma_ramp != NULL) {
                free(GL_original_gamma_ramp);
                GL_original_gamma_ramp = NULL;
        }

        graphic_operations->makeOpenGLContextCurrent(nullptr, nullptr);
        GL_context = nullptr;
}

void gr_opengl_cleanup(bool closing, int /*minimize*/)
{
        if (!GL_initted) {
                return;
        }

        if (!closing) {
                gr_reset_clip();
                gr_clear();
                gr_flip();
                gr_clear();
                gr_flip();
                gr_clear();
        }

        GL_initted = false;

        opengl_tcache_flush();

        current_viewport = nullptr;

        // All windows have to be closed before we destroy the OpenGL context
        fs2::os::closeAllViewports();

        gr_opengl_shutdown();

        graphic_operations.reset();
}

int gr_opengl_set_cull(int cull)
{
        GLboolean enabled = GL_FALSE;

        if (cull) {
                enabled = GL_state.CullFace(GL_TRUE);
                GL_state.FrontFaceValue(GL_CCW);
                GL_state.CullFaceValue(GL_BACK);
        } else {
                enabled = GL_state.CullFace(GL_FALSE);
        }

        return (enabled) ? 1 : 0;
}

void gr_opengl_set_clear_color(int r, int g, int b)
{
        gr_init_color(&gr_screen.current_clear_color, r, g, b);
}

int gr_opengl_set_color_buffer(int mode)
{
        bvec4 enabled;

        if (mode) {
                enabled = GL_state.ColorMask(true, true, true, true);
        } else {
                enabled = GL_state.ColorMask(false, false, false, false);
        }

        GL_state.SetAlphaBlendMode(ALPHA_BLEND_ALPHA_BLEND_ALPHA);

        // Check if all channels are active
        return enabled.x && enabled.y && enabled.z && enabled.w;
}

int gr_opengl_zbuffer_get()
{
        if (!gr_global_zbuffering) {
                return GR_ZBUFF_NONE;
        }

        return gr_zbuffering_mode;
}

int gr_opengl_zbuffer_set(int mode)
{
        int tmp = gr_zbuffering_mode;

        gr_zbuffering_mode = mode;

        if (gr_zbuffering_mode == GR_ZBUFF_NONE) {
                gr_zbuffering = 0;
                GL_state.SetZbufferType(ZBUFFER_TYPE_NONE);
        } else if (gr_zbuffering_mode == GR_ZBUFF_READ) {
                gr_zbuffering = 1;
                GL_state.SetZbufferType(ZBUFFER_TYPE_READ);
        } else {
                gr_zbuffering = 1;
                GL_state.SetZbufferType(ZBUFFER_TYPE_FULL);
        }

        return tmp;
}

void gr_opengl_zbuffer_clear(int mode)
{
        if (mode) {
                gr_zbuffering = 1;
                gr_zbuffering_mode = GR_ZBUFF_FULL;
                gr_global_zbuffering = 1;

                GL_state.SetAlphaBlendMode(ALPHA_BLEND_NONE);
                GL_state.SetZbufferType(ZBUFFER_TYPE_FULL);

                glClear(GL_DEPTH_BUFFER_BIT);
        } else {
                gr_zbuffering = 0;
                gr_zbuffering_mode = GR_ZBUFF_NONE;
                gr_global_zbuffering = 0;

                GL_state.DepthTest(GL_FALSE);
        }
}

int gr_opengl_stencil_set(int mode)
{
        int tmp = gr_stencil_mode;

        gr_stencil_mode = mode;

        if (mode == GR_STENCIL_READ) {
                GL_state.StencilTest(1);
                GL_state.StencilFunc(GL_NOTEQUAL, 1, 0xFFFF);
                GL_state.StencilOpSeparate(
                        GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
        } else if (mode == GR_STENCIL_WRITE) {
                GL_state.StencilTest(1);
                GL_state.StencilFunc(GL_ALWAYS, 1, 0xFFFF);
                GL_state.StencilOpSeparate(
                        GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_REPLACE);
        } else {
                GL_state.StencilTest(0);
                GL_state.StencilFunc(GL_NEVER, 1, 0xFFFF);
                GL_state.StencilOpSeparate(
                        GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
        }

        return tmp;
}

void gr_opengl_stencil_clear() { glClear(GL_STENCIL_BUFFER_BIT); }

int gr_opengl_alpha_mask_set(int mode, float alpha)
{
        if (mode) {
                GL_alpha_threshold = alpha;
        } else {
                GL_alpha_threshold = 0.0f;
        }

        // alpha masking is deprecated
        return mode;
}

// I feel dirty...
static void opengl_make_gamma_ramp(float gamma, ushort *ramp)
{
        ushort x, y;
        ushort base_ramp[256];

        ASSERT(ramp != NULL);

        // generate the base ramp values first off

        // if no gamma set then just do this quickly
        if (gamma <= 0.0f) {
                memset(ramp, 0, 3 * 256 * sizeof(ushort));
                return;
        }
        // identity gamma, avoid all of the math
        else if ((gamma == 1.0f) || (GL_original_gamma_ramp == NULL)) {
                if (GL_original_gamma_ramp != NULL) {
                        memcpy(ramp, GL_original_gamma_ramp, 3 * 256 * sizeof(ushort));
                }
                // set identity if no original ramp
                else {
                        for (x = 0; x < 256; x++) {
                                ramp[x] = (x << 8) | x;
                                ramp[x + 256] = (x << 8) | x;
                                ramp[x + 512] = (x << 8) | x;
                        }
                }

                return;
        }
        // for everything else we need to actually figure it up
        else {
                double g = 1.0 / (double)gamma;
                double val;

                ASSERT(GL_original_gamma_ramp != NULL);

                for (x = 0; x < 256; x++) {
                        val = (pow(x / 255.0, g) * 65535.0 + 0.5);
                        CLAMP(val, 0, 65535);

                        base_ramp[x] = (ushort)val;
                }

                for (y = 0; y < 3; y++) {
                        for (x = 0; x < 256; x++) {
                                val = (base_ramp[x] * 2) - GL_original_gamma_ramp[x + y * 256];
                                CLAMP(val, 0, 65535);

                                ramp[x + y * 256] = (ushort)val;
                        }
                }
        }
}

void gr_opengl_set_gamma(float gamma)
{
        ushort *gamma_ramp = NULL;

        Gr_gamma = gamma;
        Gr_gamma_int = int(Gr_gamma * 10);

        if (!Cmdline_no_set_gamma && fs2::os::getSDLMainWindow()) {
                gamma_ramp = (ushort *)malloc(3 * 256 * sizeof(ushort));

                if (gamma_ramp == NULL) {
                        ASSERT(0);
                        return;
                }

                memset(gamma_ramp, 0, 3 * 256 * sizeof(ushort));

                // Create the Gamma lookup table
                opengl_make_gamma_ramp(gamma, gamma_ramp);

                SDL_SetWindowGammaRamp(
                        fs2::os::getSDLMainWindow(), gamma_ramp, (gamma_ramp + 256),
                        (gamma_ramp + 512));

                free(gamma_ramp);
        }
}

void gr_opengl_get_region(int /*front*/, int w, int h, ubyte *data)
{
        // if (front) {
        // glReadBuffer(GL_FRONT);
        // } else {
        glReadBuffer(GL_BACK);
        // }

        GL_state.SetAlphaBlendMode(ALPHA_BLEND_NONE);
        GL_state.SetZbufferType(ZBUFFER_TYPE_NONE);

        if (gr_screen.bits_per_pixel == 16) {
                glReadPixels(
                        0, gr_screen.max_h - h, w, h, GL_BGRA,
                        GL_UNSIGNED_SHORT_1_5_5_5_REV, data);
        } else if (gr_screen.bits_per_pixel == 32) {
                glReadPixels(
                        0, gr_screen.max_h - h, w, h, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                        data);
        }
}

int gr_opengl_save_screen()
{
        int i;
        ubyte *sptr = NULL, *dptr = NULL;
        ubyte *opengl_screen_tmp = NULL;
        int width_times_pixel;

        gr_opengl_reset_clip();

        if (GL_saved_screen || GL_screen_pbo) {
                // already have a screen saved so just bail...
                return -1;
        }

        GL_saved_screen = (ubyte *)malloc(gr_screen.max_w * gr_screen.max_h * 4);

        if (!GL_saved_screen) {
                WARNINGF(LOCATION, "Couldn't get memory for saved screen!");
                return -1;
        }

        GLboolean save_state = GL_state.DepthTest(GL_FALSE);
        glReadBuffer(GL_FRONT_LEFT);

        if (Use_PBOs) {
                GLubyte *pixels = NULL;

                glGenBuffers(1, &GL_screen_pbo);

                if (!GL_screen_pbo) {
                        if (GL_saved_screen) {
                                free(GL_saved_screen);
                                GL_saved_screen = NULL;
                        }

                        return -1;
                }

                glBindBuffer(GL_PIXEL_PACK_BUFFER, GL_screen_pbo);
                glBufferData(
                        GL_PIXEL_PACK_BUFFER, gr_screen.max_w * gr_screen.max_h * 4, NULL,
                        GL_STATIC_READ);

                glReadPixels(
                        0, 0, gr_screen.max_w, gr_screen.max_h, GL_read_format,
                        GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

                pixels = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);

                width_times_pixel = (gr_screen.max_w * 4);

                sptr = (ubyte *)pixels;
                dptr = (ubyte *)&GL_saved_screen[gr_screen.max_w * gr_screen.max_h * 4];

                for (i = 0; i < gr_screen.max_h; i++) {
                        dptr -= width_times_pixel;
                        memcpy(dptr, sptr, width_times_pixel);
                        sptr += width_times_pixel;
                }

                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                glDeleteBuffers(1, &GL_screen_pbo);
                GL_screen_pbo = 0;

                GL_saved_screen_id = bm_create(
                        32, gr_screen.max_w, gr_screen.max_h, GL_saved_screen, 0);
        } else {
                opengl_screen_tmp = (ubyte *)malloc(
                        gr_screen.max_w * gr_screen.max_h * 4);

                if (!opengl_screen_tmp) {
                        if (GL_saved_screen) {
                                free(GL_saved_screen);
                                GL_saved_screen = NULL;
                        }

                        WARNINGF(LOCATION, "Couldn't get memory for temporary saved screen!");
                        GL_state.DepthTest(save_state);
                        return -1;
                }

                glReadPixels(
                        0, 0, gr_screen.max_w, gr_screen.max_h, GL_read_format,
                        GL_UNSIGNED_INT_8_8_8_8_REV, opengl_screen_tmp);

                sptr = (ubyte *)&opengl_screen_tmp[gr_screen.max_w * gr_screen.max_h * 4];
                dptr = (ubyte *)GL_saved_screen;

                width_times_pixel = (gr_screen.max_w * 4);

                for (i = 0; i < gr_screen.max_h; i++) {
                        sptr -= width_times_pixel;
                        memcpy(dptr, sptr, width_times_pixel);
                        dptr += width_times_pixel;
                }

                free(opengl_screen_tmp);

                GL_saved_screen_id = bm_create(
                        32, gr_screen.max_w, gr_screen.max_h, GL_saved_screen, 0);
        }

        GL_state.DepthTest(save_state);

        return GL_saved_screen_id;
}

void gr_opengl_restore_screen(int bmp_id)
{
        gr_reset_clip();

        if (!GL_saved_screen) {
                gr_clear();
                return;
        }

        ASSERT((bmp_id < 0) || (bmp_id == GL_saved_screen_id));

        if (GL_saved_screen_id < 0)
                return;

        gr_set_bitmap(GL_saved_screen_id);
        gr_bitmap(0, 0, GR_RESIZE_NONE); // don't scale here since we already have
                                         // real screen size
}

void gr_opengl_free_screen(int bmp_id)
{
        if (!GL_saved_screen)
                return;

        free(GL_saved_screen);
        GL_saved_screen = NULL;

        ASSERT((bmp_id < 0) || (bmp_id == GL_saved_screen_id));

        if (GL_saved_screen_id < 0)
                return;

        bm_release(GL_saved_screen_id);
        GL_saved_screen_id = -1;
}

// fill mode, solid/wire frame
void gr_opengl_set_fill_mode(int mode)
{
        if (mode == GR_FILL_MODE_SOLID) {
                GL_state.SetPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                return;
        }

        if (mode == GR_FILL_MODE_WIRE) {
                GL_state.SetPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                return;
        }

        // default setting
        GL_state.SetPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void gr_opengl_zbias(int bias)
{
        if (bias) {
                GL_state.PolygonOffsetFill(GL_TRUE);
                if (bias < 0) {
                        GL_state.SetPolygonOffset(1.0, -float(bias));
                } else {
                        GL_state.SetPolygonOffset(0.0, -float(bias));
                }
        } else {
                GL_state.PolygonOffsetFill(GL_FALSE);
        }
}

void gr_opengl_push_texture_matrix(int /*unit*/) { }

void gr_opengl_pop_texture_matrix(int /*unit*/) { }

void gr_opengl_translate_texture_matrix(
        int /*unit*/, const vec3d * /*shift*/) { }

void gr_opengl_set_line_width(float width)
{
        if (width <= 1.0f) {
                GL_state.SetLineWidth(width);
        }
        gr_screen.line_width = width;
}

int opengl_check_for_errors(const char *err_at)
{
#ifdef NDEBUG
        return 0;
#endif
        int num_errors = 0;

        auto err = glGetError();

        if (err != GL_NO_ERROR) {
                if (err_at != NULL) {
                        WARNINGF(LOCATION, "OpenGL Error from %s: %#x", err_at, err);
                } else {
                        WARNINGF(LOCATION, "OpenGL Error:  %#x", err);
                }

                num_errors++;
        }

        return num_errors;
}

void opengl_set_vsync(int status)
{
        if ((status < 0) || (status > 1)) {
                ASSERT(0);
                return;
        }

        GL_context->setSwapInterval(status);

        GL_CHECK_FOR_ERRORS("end of set_vsync()");
}

std::unique_ptr< fs2::os::Viewport >
gr_opengl_create_viewport(const fs2::os::ViewPortProperties &props)
{
        fs2::os::ViewPortProperties attrs = props;
        attrs.pixel_format.red_size = Gr_red.bits;
        attrs.pixel_format.green_size = Gr_green.bits;
        attrs.pixel_format.blue_size = Gr_blue.bits;
        attrs.pixel_format.alpha_size = (gr_screen.bits_per_pixel == 32) ? Gr_alpha.bits : 0;
        attrs.pixel_format.depth_size = (gr_screen.bits_per_pixel == 32) ? 24 : 16;
        attrs.pixel_format.stencil_size = (gr_screen.bits_per_pixel == 32) ? 8 : 1;

        attrs.pixel_format.multi_samples = fs2::registry::read("Default.OGL_AntiAliasSamples", 0);

        attrs.enable_opengl = true;
        attrs.gl_attributes.profile = fs2::os::OpenGLProfile::Core;

        return graphic_operations->createViewport(attrs);
}

void gr_opengl_use_viewport(fs2::os::Viewport *view)
{
        graphic_operations->makeOpenGLContextCurrent(view, GL_context.get());
        current_viewport = view;

        auto size = view->getSize();
        gr_screen_resize(size.first, size.second);
}

int opengl_init_display_device()
{
        int bpp = gr_screen.bits_per_pixel;

        ASSERTX(
                (bpp == 16) || (bpp == 32), "Invalid bits-per-pixel value %d!", bpp);

        // screen format
        switch (bpp) {
        case 16: {
                Gr_red.bits = 5;
                Gr_red.shift = 11;
                Gr_red.scale = 8;
                Gr_red.mask = 0xF800;

                Gr_green.bits = 6;
                Gr_green.shift = 5;
                Gr_green.scale = 4;
                Gr_green.mask = 0x7E0;

                Gr_blue.bits = 5;
                Gr_blue.shift = 0;
                Gr_blue.scale = 8;
                Gr_blue.mask = 0x1F;

                break;
        }

        case 32: {
                Gr_red.bits = 8;
                Gr_red.shift = 16;
                Gr_red.scale = 1;
                Gr_red.mask = 0xff0000;

                Gr_green.bits = 8;
                Gr_green.shift = 8;
                Gr_green.scale = 1;
                Gr_green.mask = 0x00ff00;

                Gr_blue.bits = 8;
                Gr_blue.shift = 0;
                Gr_blue.scale = 1;
                Gr_blue.mask = 0x0000ff;

                Gr_alpha.bits = 8;
                Gr_alpha.shift = 24;
                Gr_alpha.mask = 0xff000000;
                Gr_alpha.scale = 1;

                break;
        }
        }

        // texture format
        Gr_t_red.bits = 5;
        Gr_t_red.mask = 0x7c00;
        Gr_t_red.shift = 10;
        Gr_t_red.scale = 8;

        Gr_t_green.bits = 5;
        Gr_t_green.mask = 0x03e0;
        Gr_t_green.shift = 5;
        Gr_t_green.scale = 8;

        Gr_t_blue.bits = 5;
        Gr_t_blue.mask = 0x001f;
        Gr_t_blue.shift = 0;
        Gr_t_blue.scale = 8;

        Gr_t_alpha.bits = 1;
        Gr_t_alpha.mask = 0x8000;
        Gr_t_alpha.scale = 255;
        Gr_t_alpha.shift = 15;

        // alpha-texture format
        Gr_ta_red.bits = 4;
        Gr_ta_red.mask = 0x0f00;
        Gr_ta_red.shift = 8;
        Gr_ta_red.scale = 17;

        Gr_ta_green.bits = 4;
        Gr_ta_green.mask = 0x00f0;
        Gr_ta_green.shift = 4;
        Gr_ta_green.scale = 17;

        Gr_ta_blue.bits = 4;
        Gr_ta_blue.mask = 0x000f;
        Gr_ta_blue.shift = 0;
        Gr_ta_blue.scale = 17;

        Gr_ta_alpha.bits = 4;
        Gr_ta_alpha.mask = 0xf000;
        Gr_ta_alpha.shift = 12;
        Gr_ta_alpha.scale = 17;

        // allocate storage for original gamma settings
        if (!Cmdline_no_set_gamma && (GL_original_gamma_ramp == NULL)) {
                GL_original_gamma_ramp = (ushort *)malloc(
                        3 * 256 * sizeof(ushort));

                if (GL_original_gamma_ramp == NULL) {
                        WARNINGF(LOCATION, "  Unable to allocate memory for gamma ramp!  Disabling...");
                        Cmdline_no_set_gamma = 1;
                } else {
                        // assume identity ramp by default, to be overwritten by true ramp
                        // later
                        for (ushort x = 0; x < 256; x++) {
                                GL_original_gamma_ramp[x] = GL_original_gamma_ramp[x + 256] = GL_original_gamma_ramp[x + 512] = (x << 8) | x;
                        }
                }
        }

        fs2::os::ViewPortProperties attrs;
        attrs.enable_opengl = true;

        attrs.gl_attributes.major_version = MIN_REQUIRED_GL_VERSION / 10;
        attrs.gl_attributes.minor_version = MIN_REQUIRED_GL_VERSION % 10;

#ifndef NDEBUG
        attrs.gl_attributes.flags.set(fs2::os::OpenGLContextFlags::Debug);
#endif

        attrs.gl_attributes.profile = fs2::os::OpenGLProfile::Core;

        attrs.display = fs2::registry::read("Video.Display", 0);
        attrs.width = (uint32_t)gr_screen.max_w;
        attrs.height = (uint32_t)gr_screen.max_h;

        attrs.title = "FreeSpace 2";

        if (!Window_title.empty()) {
                attrs.title = Window_title;
        }

        if (!Cmdline_window && !Cmdline_fullscreen_window) {
                attrs.flags.set(fs2::os::ViewPortFlags::Fullscreen);
        } else if (Cmdline_fullscreen_window) {
                attrs.flags.set(fs2::os::ViewPortFlags::Borderless);
        }

        auto viewport = gr_opengl_create_viewport(attrs);
        if (!viewport) {
                return 1;
        }

        const int gl_versions[] = { 45, 44, 43, 42, 41, 40, 33, 32 };

        // find the latest and greatest OpenGL context
        for (auto ver : gl_versions) {
                auto gl_attrs = attrs.gl_attributes;
                gl_attrs.major_version = ver / 10;
                gl_attrs.minor_version = ver % 10;

                GL_context = graphic_operations->createOpenGLContext(
                        viewport.get(), gl_attrs);

                if (GL_context != nullptr) {
                        break;
                }
        }

        if (GL_context == nullptr) {
                return 1;
        }

        auto port = fs2::os::addViewport(std::move(viewport));
        fs2::os::setMainViewPort(port);

        // We can't use gr_use_viewport because that tries to use OpenGL which
        // hasn't been initialized yet
        graphic_operations->makeOpenGLContextCurrent(port, GL_context.get());
        current_viewport = port;

        if (GL_original_gamma_ramp != NULL && fs2::os::getSDLMainWindow() != nullptr) {
                SDL_GetWindowGammaRamp(
                        fs2::os::getSDLMainWindow(), GL_original_gamma_ramp,
                        (GL_original_gamma_ramp + 256), (GL_original_gamma_ramp + 512));
        }

        return 0;
}

void opengl_setup_function_pointers()
{
        gr_screen.gf_flip = gr_opengl_flip;
        gr_screen.gf_set_clip = gr_opengl_set_clip;
        gr_screen.gf_reset_clip = gr_opengl_reset_clip;

        gr_screen.gf_clear = gr_opengl_clear;
        // gr_screen.gf_bitmap                             = gr_opengl_bitmap;

        // gr_screen.gf_rect                               = gr_opengl_rect;

        gr_screen.gf_print_screen = gr_opengl_print_screen;

        gr_screen.gf_zbuffer_get = gr_opengl_zbuffer_get;
        gr_screen.gf_zbuffer_set = gr_opengl_zbuffer_set;
        gr_screen.gf_zbuffer_clear = gr_opengl_zbuffer_clear;

        gr_screen.gf_stencil_set = gr_opengl_stencil_set;
        gr_screen.gf_stencil_clear = gr_opengl_stencil_clear;

        gr_screen.gf_alpha_mask_set = gr_opengl_alpha_mask_set;

        gr_screen.gf_save_screen = gr_opengl_save_screen;
        gr_screen.gf_restore_screen = gr_opengl_restore_screen;
        gr_screen.gf_free_screen = gr_opengl_free_screen;

        gr_screen.gf_set_gamma = gr_opengl_set_gamma;

        // UnknownPlayer : Don't recognize this - MAY NEED DEBUGGING
        gr_screen.gf_get_region = gr_opengl_get_region;

        // now for the bitmap functions
        gr_screen.gf_bm_free_data = gr_opengl_bm_free_data;
        gr_screen.gf_bm_create = gr_opengl_bm_create;
        gr_screen.gf_bm_init = gr_opengl_bm_init;
        gr_screen.gf_bm_page_in_start = gr_opengl_bm_page_in_start;
        gr_screen.gf_bm_data = gr_opengl_bm_data;
        gr_screen.gf_bm_make_render_target = gr_opengl_bm_make_render_target;
        gr_screen.gf_bm_set_render_target = gr_opengl_bm_set_render_target;

        gr_screen.gf_set_cull = gr_opengl_set_cull;
        gr_screen.gf_set_color_buffer = gr_opengl_set_color_buffer;

        gr_screen.gf_set_clear_color = gr_opengl_set_clear_color;

        gr_screen.gf_preload = gr_opengl_preload;

        gr_screen.gf_push_texture_matrix = gr_opengl_push_texture_matrix;
        gr_screen.gf_pop_texture_matrix = gr_opengl_pop_texture_matrix;
        gr_screen.gf_translate_texture_matrix = gr_opengl_translate_texture_matrix;

        gr_screen.gf_set_texture_addressing = gr_opengl_set_texture_addressing;
        gr_screen.gf_zbias = gr_opengl_zbias;
        gr_screen.gf_set_fill_mode = gr_opengl_set_fill_mode;

        gr_screen.gf_create_buffer = gr_opengl_create_buffer;
        gr_screen.gf_delete_buffer = gr_opengl_delete_buffer;
        gr_screen.gf_update_buffer_data = gr_opengl_update_buffer_data;
        gr_screen.gf_update_buffer_data_offset = gr_opengl_update_buffer_data_offset;
        gr_screen.gf_bind_uniform_buffer = gr_opengl_bind_uniform_buffer;

        gr_screen.gf_update_transform_buffer = gr_opengl_update_transform_buffer;

        gr_screen.gf_post_process_set_effect = gr_opengl_post_process_set_effect;
        gr_screen.gf_post_process_set_defaults = gr_opengl_post_process_set_defaults;

        gr_screen.gf_post_process_begin = gr_opengl_post_process_begin;
        gr_screen.gf_post_process_end = gr_opengl_post_process_end;
        gr_screen.gf_post_process_save_zbuffer = gr_opengl_post_process_save_zbuffer;
        gr_screen.gf_post_process_restore_zbuffer = gr_opengl_post_process_restore_zbuffer;

        gr_screen.gf_scene_texture_begin = gr_opengl_scene_texture_begin;
        gr_screen.gf_scene_texture_end = gr_opengl_scene_texture_end;
        gr_screen.gf_copy_effect_texture = gr_opengl_copy_effect_texture;

        gr_screen.gf_deferred_lighting_begin = gr_opengl_deferred_lighting_begin;
        gr_screen.gf_deferred_lighting_end = gr_opengl_deferred_lighting_end;
        gr_screen.gf_deferred_lighting_finish = gr_opengl_deferred_lighting_finish;

        gr_screen.gf_set_line_width = gr_opengl_set_line_width;

        gr_screen.gf_sphere = gr_opengl_sphere;

        gr_screen.gf_maybe_create_shader = gr_opengl_maybe_create_shader;
        gr_screen.gf_recompile_all_shaders = gr_opengl_recompile_all_shaders;
        gr_screen.gf_shadow_map_start = gr_opengl_shadow_map_start;
        gr_screen.gf_shadow_map_end = gr_opengl_shadow_map_end;

        gr_screen.gf_render_shield_impact = gr_opengl_render_shield_impact;

        gr_screen.gf_update_texture = gr_opengl_update_texture;
        gr_screen.gf_get_bitmap_from_texture = gr_opengl_get_bitmap_from_texture;

        gr_screen.gf_clear_states = gr_opengl_clear_states;

        gr_screen.gf_start_decal_pass = gr_opengl_start_decal_pass;
        gr_screen.gf_stop_decal_pass = gr_opengl_stop_decal_pass;

        gr_screen.gf_render_model = gr_opengl_render_model;
        gr_screen.gf_render_primitives = gr_opengl_render_primitives;
        gr_screen.gf_render_primitives_particle = gr_opengl_render_primitives_particle;
        gr_screen.gf_render_primitives_batched = gr_opengl_render_primitives_batched;
        gr_screen.gf_render_primitives_distortion = gr_opengl_render_primitives_distortion;
        gr_screen.gf_render_movie = gr_opengl_render_movie;
        gr_screen.gf_render_nanovg = gr_opengl_render_nanovg;
        gr_screen.gf_render_decals = gr_opengl_render_decals;

        gr_screen.gf_is_capable = gr_opengl_is_capable;
        gr_screen.gf_get_property = gr_opengl_get_property;

        gr_screen.gf_push_debug_group = gr_opengl_push_debug_group;
        gr_screen.gf_pop_debug_group = gr_opengl_pop_debug_group;

        gr_screen.gf_create_query_object = gr_opengl_create_query_object;
        gr_screen.gf_query_value = gr_opengl_query_value;
        gr_screen.gf_query_value_available = gr_opengl_query_value_available;
        gr_screen.gf_get_query_value = gr_opengl_get_query_value;
        gr_screen.gf_delete_query_object = gr_opengl_delete_query_object;

        gr_screen.gf_create_viewport = gr_opengl_create_viewport;
        gr_screen.gf_use_viewport = gr_opengl_use_viewport;

        gr_screen.gf_sync_fence = gr_opengl_sync_fence;
        gr_screen.gf_sync_wait = gr_opengl_sync_wait;
        gr_screen.gf_sync_delete = gr_opengl_sync_delete;

        gr_screen.gf_set_viewport = gr_opengl_set_viewport;

        // NOTE: All function pointers here should have a Cmdline_nohtl check at
        // the top
        // if they shouldn't be run in non-HTL mode, Don't keep separate
        // entries.
        // *****************************************************************************
}

#ifndef NDEBUG
static void APIENTRY debug_callback(
        GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
        const GLchar *message, const void * /*userParam*/)
{
        if (source == GL_DEBUG_SOURCE_APPLICATION_ARB) {
                // Ignore application messages
                return;
        }

        const char *sourceStr;
        const char *typeStr;
        const char *severityStr;

        switch (source) {
        case GL_DEBUG_SOURCE_API_ARB: sourceStr = "OpenGL"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB: sourceStr = "WindowSys"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
                sourceStr = "shader compiler";
                break;
        case GL_DEBUG_SOURCE_THIRD_PARTY_ARB: sourceStr = "third-party"; break;
        case GL_DEBUG_SOURCE_APPLICATION_ARB: sourceStr = "application"; break;
        case GL_DEBUG_SOURCE_OTHER_ARB: sourceStr = "other"; break;
        default: sourceStr = "(unknown)"; break;
        }

        switch (type) {
        case GL_DEBUG_TYPE_ERROR_ARB: typeStr = "error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
                typeStr = "deprecated";
                break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
                typeStr = "undefined";
                break;
        case GL_DEBUG_TYPE_PORTABILITY_ARB: typeStr = "non-portable"; break;
        case GL_DEBUG_TYPE_PERFORMANCE_ARB: typeStr = "non-performant"; break;
        case GL_DEBUG_TYPE_OTHER_ARB: typeStr = "other"; break;
        default: typeStr = "(unknown)"; break;
        }

        switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH_ARB: severityStr = "high"; break;
        case GL_DEBUG_SEVERITY_MEDIUM_ARB: severityStr = "medium"; break;
        case GL_DEBUG_SEVERITY_LOW_ARB: severityStr = "low"; break;
        default: severityStr = "(unknown)"; break;
        }

        II << "OpenGL debug : " << sourceStr << " : " << typeStr << " : "
           << id << " : " << severityStr << " : " << message;
}

static bool hasPendingDebugMessage()
{
        GLint numMsgs = 0;
        glGetIntegerv(GL_DEBUG_LOGGED_MESSAGES_ARB, &numMsgs);

        return numMsgs > 0;
}

static bool printNextDebugMessage()
{
        if (!hasPendingDebugMessage()) {
                return false;
        }

        GLint msgLen = 0;
        glGetIntegerv(GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_ARB, &msgLen);

        std::vector< GLchar > msg;
        msg.resize(
                msgLen + 1); // Includes null character, needs to be removed later

        GLenum source;
        GLenum type;
        GLenum severity;
        GLuint id;
        GLsizei length;

        GLuint numFound = glGetDebugMessageLogARB(
                1, static_cast< GLsizei >(msg.size()), &source, &type, &id,
                &severity, &length, &msg[0]);

        if (numFound < 1) {
                return false;
        }

        debug_callback(source, type, id, severity, length, msg.data(), nullptr);

        return true;
}
#endif

static void init_extensions()
{
        // if S3TC compression is found, then "GL_ARB_texture_compression" must be
        // an extension
        Use_compressed_textures = GLAD_GL_EXT_texture_compression_s3tc;
        Texture_compression_available = true;
        // Swifty put this in, but it's not doing anything. Once he uses it, he can
        // uncomment it.
        // int use_base_vertex =
        // Is_Extension_Enabled(OGL_ARB_DRAW_ELEMENTS_BASE_VERTEX);

        if (!Cmdline_no_pbo) {
                Use_PBOs = 1;
        }

        int ver = 0, major = 0, minor = 0;
        const char *glsl_ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

        sscanf(glsl_ver, "%d.%d", &major, &minor);
        ver = (major * 100) + minor;

        GLSL_version = ver;

        // we require a minimum GLSL version
        if (GLSL_version < MIN_REQUIRED_GLSL_VERSION) {
                ASSERTX(0, "Current GL Shading Langauge Version of %d is less than the required version of %d. Switch video modes or update your drivers.", GLSL_version, MIN_REQUIRED_GLSL_VERSION);
        }

        GLint max_texture_units;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);

        // we need enough texture slots for this stuff to work

        if (max_texture_units < 6) {
                WARNINGF(LOCATION, "Not enough texture units for height map support. We need at least 6, we found %d.", max_texture_units);
                Cmdline_height = 0;
        } else if (max_texture_units < 5) {
                WARNINGF(LOCATION, "Not enough texture units for height and normal map support. We need at least 5, we found %d.", max_texture_units);
                Cmdline_normal = 0;
                Cmdline_height = 0;
        } else if (max_texture_units < 4) {
                ASSERTX(0, "Not enough texture units found for proper rendering support! We need at least 4, we found %d.", max_texture_units);
        }
}

bool gr_opengl_init(std::unique_ptr< fs2::os::GraphicsOperations > &&graphicsOps)
{
        if (GL_initted) {
                gr_opengl_cleanup(false);
                GL_initted = false;
        }

        WARNINGF(LOCATION, "Initializing OpenGL graphics device at %ix%i with %i-bit color...", gr_screen.max_w, gr_screen.max_h, gr_screen.bits_per_pixel);

        // Load the RenderDoc API if available before doing anything with OpenGL
        renderdoc::loadApi();

        graphic_operations = std::move(graphicsOps);

        if (opengl_init_display_device()) {
                ASSERTX(0, "Unable to initialize display device!\nThis most likely means that your graphics drivers do not support the minimum required OpenGL version which is %d.%d.\n", (MIN_REQUIRED_GL_VERSION / 10), (MIN_REQUIRED_GL_VERSION % 10));
        }

        // Initialize function pointers
        if (!gladLoadGLLoader(GL_context->getLoaderFunction())) {
                ASSERTX(0, "Failed to load OpenGL!");
        }

        // version check
        GL_version = (GLVersion.major * 10) + GLVersion.minor;

        if (GL_version < MIN_REQUIRED_GL_VERSION) {
                ASSERTX(0, "Current GL Version of %d.%d is less than the required version of %d.%d.\nSwitch video modes or update your drivers.", GLVersion.major, GLVersion.minor, (MIN_REQUIRED_GL_VERSION / 10), (MIN_REQUIRED_GL_VERSION % 10));
        }

        GL_initted = true;

#ifndef NDEBUG
        // Set up the debug extension if present
        if (GLAD_GL_ARB_debug_output) {
                WARNINGF(LOCATION, "Using OpenGL debug extension");
                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
                GLuint unusedIds = 0;
                glDebugMessageControlARB(
                        GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, &unusedIds, true);

                glDebugMessageCallbackARB(debug_callback, nullptr);

                // Now print all pending log messages
                while (hasPendingDebugMessage()) { printNextDebugMessage(); }
        }
#endif

        // this MUST be done before any other gr_opengl_* or
        // opengl_* function calls!!
        opengl_setup_function_pointers();

        WARNINGF(LOCATION, "  OpenGL Vendor    : %s", glGetString(GL_VENDOR));
        WARNINGF(LOCATION, "  OpenGL Renderer  : %s", glGetString(GL_RENDERER));
        WARNINGF(LOCATION, "  OpenGL Version   : %s", glGetString(GL_VERSION));

        // Build a string identifier for this OpenGL implementation
        GL_implementation_id.clear();
        GL_implementation_id += reinterpret_cast< const char * >(glGetString(GL_VENDOR));
        GL_implementation_id += "\n";
        GL_implementation_id += reinterpret_cast< const char * >(glGetString(GL_RENDERER));
        GL_implementation_id += "\n";
        GL_implementation_id += reinterpret_cast< const char * >(glGetString(GL_VERSION));
        GL_implementation_id += "\n";
        GL_implementation_id += reinterpret_cast< const char * >(
                glGetString(GL_SHADING_LANGUAGE_VERSION));

        GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        GL_binary_formats.resize(formats);
        glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, GL_binary_formats.data());

        if (Cmdline_fullscreen_window || Cmdline_window) {
                opengl_go_windowed();
        } else {
                opengl_go_fullscreen();
        }

        init_extensions();

        // init state system (must come AFTER light is set up)
        GL_state.init();

        GLint max_texture_units = GL_supported_texture_units;
        GLint max_texture_coords = GL_supported_texture_units;

        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        max_texture_coords = 1;

        // create vertex array object to make OpenGL Core happy
        glGenVertexArrays(1, &GL_vao);
        GL_state.BindVertexArray(GL_vao);

        GL_state.Texture.init(max_texture_units);
        GL_state.Array.init(max_texture_coords);
        GL_state.Constants.init();

        opengl_set_texture_target();
        GL_state.Texture.SetActiveUnit(0);
        GL_state.Texture.SetTarget(GL_TEXTURE_2D);
        GL_state.Texture.Enable();

        // ready the texture system
        opengl_tcache_init();

        opengl_tnl_init();

        // setup default shaders, and shader related items
        opengl_shader_init();

        // post processing effects, after shaders are initialized
        opengl_setup_scene_textures();
        opengl_post_process_init();

        // must be called after extensions are setup
        opengl_set_vsync(!Cmdline_no_vsync);

        gr_reset_matrices();
        gr_setup_viewport();

        glClear(GL_DEPTH_BUFFER_BIT);
        glClear(GL_STENCIL_BUFFER_BIT);

        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        glDepthRange(0.0, 1.0);

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glFlush();

        Gr_current_red = &Gr_red;
        Gr_current_blue = &Gr_blue;
        Gr_current_green = &Gr_green;
        Gr_current_alpha = &Gr_alpha;

        gr_opengl_reset_clip();
        gr_opengl_clear();
        gr_opengl_flip();
        gr_opengl_clear();
        gr_opengl_flip();
        gr_opengl_clear();

        glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &GL_max_elements_vertices);
        glGetIntegerv(GL_MAX_ELEMENTS_INDICES, &GL_max_elements_indices);

        WARNINGF(LOCATION, "  Max texture units: %i (%i)", GL_supported_texture_units, max_texture_units);
        WARNINGF(LOCATION, "  Max client texture states: %i (%i)", GL_supported_texture_units, max_texture_coords);
        WARNINGF(LOCATION, "  Max elements vertices: %i", GL_max_elements_vertices);
        WARNINGF(LOCATION, "  Max elements indices: %i", GL_max_elements_indices);
        WARNINGF(LOCATION, "  Max texture size: %ix%i", GL_max_texture_width, GL_max_texture_height);

        WARNINGF(LOCATION, "  Max render buffer size: %ix%i", GL_max_renderbuffer_size, GL_max_renderbuffer_size);

        WARNINGF(LOCATION, "  Can use compressed textures: %s", Use_compressed_textures ? NOX("YES") : NOX("NO"));
        WARNINGF(LOCATION, "  Texture compression available: %s", Texture_compression_available ? NOX("YES") : NOX("NO"));
        WARNINGF(LOCATION, "  Post-processing enabled: %s", (Cmdline_postprocess) ? "YES" : "NO");
        WARNINGF(LOCATION, "  Using %s texture filter.", (GL_mipmap_filter) ? NOX("trilinear") : NOX("bilinear"));

        WARNINGF(LOCATION, "  OpenGL Shader Version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

        WARNINGF(LOCATION, "  Max uniform block size: %d", GL_state.Constants.GetMaxUniformBlockSize());
        WARNINGF(LOCATION, "  Max uniform buffer bindings: %d", GL_state.Constants.GetMaxUniformBlockBindings());
        WARNINGF(LOCATION, "  Uniform buffer byte offset alignment: %d", GL_state.Constants.GetUniformBufferOffsetAlignment());

        // This stops fred crashing if no textures are set
        gr_screen.current_bitmap = -1;

        WARNINGF(LOCATION, "... OpenGL init is complete!");

        if (Cmdline_ati_color_swap)
                GL_read_format = GL_RGBA;

        return true;
}

bool gr_opengl_is_capable(gr_capability capability)
{
        switch (capability) {
        case CAPABILITY_ENVIRONMENT_MAP: return true;
        case CAPABILITY_NORMAL_MAP: return Cmdline_normal ? true : false;
        case CAPABILITY_HEIGHT_MAP: return Cmdline_height ? true : false;
        case CAPABILITY_SOFT_PARTICLES:
        case CAPABILITY_DISTORTION:
                return Cmdline_softparticles && !Cmdline_no_fbo;
        case CAPABILITY_POST_PROCESSING:
                return Cmdline_postprocess && !Cmdline_no_fbo;
        case CAPABILITY_DEFERRED_LIGHTING:
                return !Cmdline_no_fbo && !Cmdline_no_deferred_lighting;
        case CAPABILITY_SHADOWS: return true;
        case CAPABILITY_BATCHED_SUBMODELS: return true;
        case CAPABILITY_POINT_PARTICLES: return !Cmdline_no_geo_sdr_effects;
        case CAPABILITY_TIMESTAMP_QUERY:
                return GLAD_GL_ARB_timer_query != 0; // Timestamp queries are available from 3.3 onwards
        case CAPABILITY_SEPARATE_BLEND_FUNCTIONS:
                return GLAD_GL_ARB_draw_buffers_blend != 0; // We need an OpenGL extension for this
        }

        return false;
}

bool gr_opengl_get_property(gr_property prop, void *dest)
{
        switch (prop) {
        case gr_property::UNIFORM_BUFFER_OFFSET_ALIGNMENT:
                *((int *)dest) = GL_state.Constants.GetUniformBufferOffsetAlignment();
                return true;
        case gr_property::UNIFORM_BUFFER_MAX_SIZE:
                *((int *)dest) = GL_state.Constants.GetMaxUniformBlockSize();
                return true;
        default: return false;
        }
}

void gr_opengl_push_debug_group(const char *name)
{
        if (GLAD_GL_KHR_debug) {
                glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION_KHR, 0, -1, name);
        }
}
void gr_opengl_pop_debug_group()
{
        if (GLAD_GL_KHR_debug) {
                glPopDebugGroup();
        }
}
#if !defined(NDEBUG) || defined(FS_OPENGL_DEBUG) || defined(DOXYGEN)
void opengl_set_object_label(
        GLenum type, GLuint handle, const std::string &name)
{
        if (GLAD_GL_KHR_debug) {
                glObjectLabel(type, handle, (GLsizei)name.size(), name.c_str());
        }
}
#endif

uint opengl_data_type_size(GLenum data_type)
{
        switch (data_type) {
        case GL_BYTE: return sizeof(GLbyte);
        case GL_UNSIGNED_BYTE: return sizeof(GLubyte);
        case GL_SHORT: return sizeof(GLshort);
        case GL_UNSIGNED_SHORT: return sizeof(GLushort);
        case GL_INT: return sizeof(GLint);
        case GL_UNSIGNED_INT: return sizeof(GLuint);
        case GL_FLOAT: return sizeof(GLfloat);
        case GL_DOUBLE: return sizeof(GLdouble);
        }

        return 0;
}

DCF(ogl_minimize, "Minimizes opengl")
{
        bool minimize_ogl = false;

        if (gr_screen.mode != GR_OPENGL) {
                dc_printf("Command only available in OpenGL mode.\n");
                return;
        }

        if (dc_optional_string_either("help", "--help")) {
                dc_printf(
                        "[bool] If true is passed, then the OpenGL window will "
                        "minimize.\n");
                return;
        }
        dc_stuff_boolean(&minimize_ogl);

        if (minimize_ogl) {
                opengl_minimize();
        }
}

DCF(ogl_anisotropy, "toggles anisotropic filtering")
{
        bool process = true;
        int value;

        if (gr_screen.mode != GR_OPENGL) {
                dc_printf("Can only set anisotropic filter in OpenGL mode.\n");
                return;
        }

        if (dc_optional_string_either("help", "--help")) {
                dc_printf("Sets OpenGL anisotropic filtering level.\n");
                dc_printf(
                        "GL_anisotropy [int]  Valid values are 0 to %i. 0 turns off "
                        "anisotropic filtering.\n",
                        (int)opengl_get_max_anisotropy());
                process = false;
        }

        if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
                dc_printf(
                        "Current anisotropic filter value is %i\n", (int)GL_anisotropy);
                process = false;
        }

        if (!process) {
                return;
        }

        if (!GLAD_GL_EXT_texture_filter_anisotropic) {
                dc_printf("Error: Anisotropic filter is not settable!\n");
                return;
        }

        if (!dc_maybe_stuff_int(&value)) {
                // No arg passed, set to default
                GL_anisotropy = 1.0f;
                // opengl_set_anisotropy();
                dc_printf("Anisotropic filter value reset to default level.\n");
        } else {
                GL_anisotropy = (GLfloat)value;
                // opengl_set_anisotropy( (float)Dc_arg_float );
        }
}