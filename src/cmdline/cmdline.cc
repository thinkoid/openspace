// -*- mode: c++; -*-

#include "cmdline/cmdline.hh"

#include "defs.hh"

#include <dirent.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "assert/assert.hh"
#include "camera/camera.hh" //VIEWER_ZOOM_DEFAULT
#include "cfile/cfilesystem.hh"
#include "hud/hudconfig.hh"
#include "io/joy.hh"
#include "log/log.hh"
#include "osapi/osapi.hh"
#include "parse/sexp.hh"
#include "shared/globals.hh"
#include "shared/types.hh"
#include "shared/version.hh"
#include "sound/openal.hh"
#include "util/list.hh"

enum cmdline_arg_type { AT_NONE = 0,
                        AT_INT,
                        AT_FLOAT,
                        AT_STRING };

// values and order MUST match cmdline_arg_type
const char *cmdline_arg_types[] = {
        "NONE",
        "INT",
        "FLOAT",
        "STRING",
};

// variables
class cmdline_parm {
public:
        cmdline_parm *next, *prev;
        const char *name;   // name of parameter, must start with '-' char
        const char *help;   // help text for this parameter
        const bool stacks;  // whether this arg stacks with each use or is replaced
                            // by newest use (should only be used for strings!!)
        char *args;         // string value for parameter arguments (NULL if no arguments)
        int name_found;     // true if parameter on command line, otherwise false
        const int arg_type; // from enum cmdline_arg_type; used for help

        cmdline_parm(
                const char *name, const char *help, const int arg_type,
                const bool stacks = false);
        ~cmdline_parm();
        int found();
        int get_int();
        float get_float();
        char *str();
        bool check_if_args_is_valid();
        bool has_param();
};

static cmdline_parm Parm_list(NULL, NULL, AT_NONE);
static int Parm_list_inited = 0;

extern int Show_framerate; // from freespace.cpp

enum {
        // DO NOT CHANGE ANYTHING ABOUT THESE FIRST TWO OR WILL MESS UP THE
        // LAUNCHER
        EASY_DEFAULT = 1 << 1,
        EASY_ALL_ON = 1 << 2,

        EASY_MEM_ON = 1 << 3,
        EASY_MEM_OFF = 1 << 4,

        // Add new flags here

        // Combos
        EASY_MEM_ALL_ON = EASY_ALL_ON | EASY_MEM_ON,
        EASY_DEFAULT_MEM = EASY_DEFAULT | EASY_MEM_OFF
};

enum BuildCaps {
        BUILD_CAPS_OPENAL = (1 << 0),
        BUILD_CAPS_NO_D3D = (1 << 1),
        BUILD_CAPS_NEW_SND = (1 << 2),
        BUILD_CAPS_SDL = (1 << 3)
};

#define PARSE_COMMAND_LINE_STRING "-parse_cmdline_only"

typedef struct {
        // DO NOT CHANGE THE SIZE OF THIS AT_STRING!
        char name[32];

} EasyFlag;

EasyFlag easy_flags[] = { { "Custom" },
                          { "Default FS2 (All features off)" },
                          { "All features on" },
                          { "High memory usage features on" },
                          { "High memory usage features off" } };

// DO NOT CHANGE **ANYTHING** ABOUT THIS STRUCTURE AND ITS CONTENT
typedef struct {
        char name[20];     // The actual flag
        char desc[40];     // The text that will appear in the launcher (unless its
                           // blank, other name is shown)
        bool fso_only;     // true if this is a fs2_open only feature
        int on_flags;      // Easy flag which will turn this feature on
        int off_flags;     // Easy flag which will turn this feature off
        char type[16];     // Launcher uses this to put flags under different headings
        char web_url[256]; // Link to documentation of feature (please use wiki or
                           // somewhere constant)

} Flag;

// Please group them by type, ie graphics, gameplay etc, maximum 20 different
// types
Flag exe_params[] = {
        {
                "-nospec",
                "Disable specular",
                true,
                EASY_DEFAULT_MEM,
                EASY_MEM_ALL_ON,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-spec",
        },
        {
                "-noglow",
                "Disable glow maps",
                true,
                EASY_DEFAULT_MEM,
                EASY_MEM_ALL_ON,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-glow",
        },
        {
                "-noenv",
                "Disable environment maps",
                true,
                EASY_DEFAULT_MEM,
                EASY_MEM_ALL_ON,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/Command-Line_Reference#-env",
        },
        {
                "-nomotiondebris",
                "Disable motion debris",
                true,
                EASY_ALL_ON,
                EASY_DEFAULT,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-nomotiondebris",
        },
        {
                "-noscalevid",
                "Disable scale-to-window for movies",
                true,
                0,
                EASY_DEFAULT,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-noscalevid",
        },
        { "-nonormal", "Disable normal maps", true, EASY_DEFAULT_MEM,
          EASY_MEM_ALL_ON, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-normal" },
        { "-no_emissive_light", "Disable emissive light from ships", true, 0,
          EASY_DEFAULT, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-no_emissive_light" },
        { "-noheight", "Disable height/parallax maps", true, EASY_DEFAULT_MEM,
          EASY_MEM_ALL_ON, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-height" },
        { "-3dshockwave", "Enable 3D shockwaves", true, EASY_MEM_ALL_ON,
          EASY_DEFAULT_MEM, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-3dshockwave" },
        { "-post_process", "Enable post processing", true, EASY_ALL_ON,
          EASY_DEFAULT, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-post_process" },
        { "-soft_particles", "Enable soft particles", true, EASY_ALL_ON,
          EASY_DEFAULT, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-soft_particles" },
        { "-fxaa", "Enable FXAA anti-aliasing", true, EASY_MEM_ALL_ON,
          EASY_DEFAULT, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-fxaa" },
        { "-nolightshafts", "Disable lightshafts", true, EASY_DEFAULT,
          EASY_DEFAULT, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-flightshaftsoff" },
        {
                "-fb_explosions",
                "Enable Framebuffer Shockwaves",
                true,
                EASY_ALL_ON,
                EASY_DEFAULT,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-fb_explosions",
        },
        {
                "-fb_thrusters",
                "Enable Framebuffer Thrusters",
                true,
                EASY_ALL_ON,
                EASY_DEFAULT,
                "Graphics",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-fb_thrusters",
        },
        { "-no_deferred", "Disable Deferred Lighting", true, EASY_DEFAULT_MEM,
          EASY_DEFAULT, "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-no_deferred" },
        { "-enable_shadows", "Enable Shadows", true, EASY_MEM_ALL_ON, EASY_DEFAULT,
          "Graphics",
          "http://www.hard-light.net/wiki/index.php/"
          "Command-Line_Reference#-no_shadows" },
        {
                "-no_vsync",
                "Disable vertical sync",
                true,
                0,
                EASY_DEFAULT,
                "Game Speed",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-no_vsync",
        },

        {
                "-dualscanlines",
                "Add another pair of scanning lines",
                true,
                0,
                EASY_DEFAULT,
                "HUD",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-dualscanlines",
        },
        {
                "-targetinfo",
                "Enable info next to target",
                true,
                0,
                EASY_DEFAULT,
                "HUD",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-targetinfo",
        },
        {
                "-orbradar",
                "Enable 3D radar",
                true,
                0,
                EASY_DEFAULT,
                "HUD",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-orbradar",
        },
        {
                "-rearm_timer",
                "Enable rearm/repair completion timer",
                true,
                0,
                EASY_DEFAULT,
                "HUD",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-rearm_timer",
        },
        {
                "-ballistic_gauge",
                "Enable analog ballistic ammo gauge",
                true,
                0,
                EASY_DEFAULT,
                "HUD",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-ballistic_gauge",
        },

        {
                "-ship_choice_3d",
                "Use 3D models for ship selection",
                true,
                0,
                EASY_DEFAULT,
                "Gameplay",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-ship_choice_3d",
        },
        {
                "-weapon_choice_3d",
                "Use 3D models for weapon selection",
                true,
                0,
                EASY_DEFAULT,
                "Gameplay",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-weapon_choice_3d",
        },
        {
                "-3dwarp",
                "Enable 3D warp",
                true,
                0,
                EASY_DEFAULT,
                "Gameplay",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-3dwarp",
        },
        {
                "-warp_flash",
                "Enable flash upon warp",
                true,
                0,
                EASY_DEFAULT,
                "Gameplay",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-warp_flash",
        },
        {
                "-no_ap_interrupt",
                "Disable interrupting autopilot",
                true,
                0,
                EASY_DEFAULT,
                "Gameplay",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-no_ap_interrupt",
        },
        {
                "-stretch_menu",
                "Stretch interface to fill screen",
                true,
                0,
                EASY_DEFAULT,
                "Gameplay",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-stretch_menu",
        },

        {
                "-nosound",
                "Disable all sound",
                false,
                0,
                EASY_DEFAULT,
                "Audio",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-nosound",
        },
        {
                "-nomusic",
                "Disable music",
                false,
                0,
                EASY_DEFAULT,
                "Audio",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-nomusic",
        },
        {
                "-no_enhanced_sound",
                "Disable enhanced sound",
                false,
                0,
                EASY_DEFAULT,
                "Audio",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-no_enhanced_sound",
        },

        {
                "-portable_mode",
                "Store config in portable location",
                false,
                0,
                EASY_DEFAULT,
                "Launcher",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-portable_mode",
        },

        {
                "-standalone",
                "Run as standalone server",
                false,
                0,
                EASY_DEFAULT,
                "Multiplayer",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-standalone",
        },
        {
                "-clientdamage",
                "",
                false,
                0,
                EASY_DEFAULT,
                "Multiplayer",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-clientdamage",
        },
        {
                "-mpnoreturn",
                "Disable flight deck option",
                true,
                0,
                EASY_DEFAULT,
                "Multiplayer",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-mpnoreturn",
        },

        {
                "-no_set_gamma",
                "Disable setting of gamma",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-no_set_gamma",
        },
        {
                "-nomovies",
                "Disable video playback",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-nomovies",
        },
        {
                "-noparseerrors",
                "Disable parsing errors",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-noparseerrors",
        },
        {
                "-query_speech",
                "Check if this build has speech",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-query_speech",
        },
        {
                "-loadallweps",
                "Load all weapons, even those not used",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-loadallweps",
        },
        {
                "-disable_fbo",
                "Disable OpenGL RenderTargets",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-disable_fbo",
        },
        {
                "-disable_pbo",
                "Disable OpenGL Pixel Buffer Objects",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-disable_pbo",
        },
        {
                "-ati_swap",
                "Fix colour issues on some ATI cards",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-ati_swap",
        },
        {
                "-no_3d_sound",
                "Use only 2D/stereo for sound effects",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-no_3d_sound",
        },
        {
                "-mipmap",
                "Enable mipmapping",
                true,
                0,
                EASY_DEFAULT_MEM,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-mipmap",
        },
        {
                "-use_gldrawelements",
                "Don't use glDrawRangeElements",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-use_gldrawelements",
        },
        {
                "-old_collision",
                "Use old collision detection system",
                true,
                EASY_DEFAULT,
                EASY_ALL_ON,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-old_collision",
        },
        {
                "-gl_finish",
                "Fix input lag on some ATI+Linux systems",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-gl_finish",
        },
        {
                "-no_geo_effects",
                "Disable geometry shader for effects",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "",
        },
        {
                "-set_cpu_affinity",
                "Sets processor affinity to config value",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "",
        },
        {
                "-nograb",
                "Disables mouse grabbing",
                true,
                0,
                EASY_DEFAULT,
                "Troubleshoot",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-nograb",
        },
        {
                "-ingame_join",
                "Allow in-game joining",
                true,
                0,
                EASY_DEFAULT,
                "Experimental",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-ingame_join",
        },
        {
                "-voicer",
                "Enable voice recognition",
                true,
                0,
                EASY_DEFAULT,
                "Experimental",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-voicer",
        },

        {
                "-fps",
                "Show frames per second on HUD",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/Command-Line_Reference#-fps",
        },
        {
                "-bmpmanusage",
                "Show how many BMPMAN slots are in use",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-bmpmanusage",
        },
        {
                "-pos",
                "Show position of camera",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/Command-Line_Reference#-pos",
        },
        {
                "-window",
                "Run in window",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-window",
        },
        {
                "-fullscreen_window",
                "Run in fullscreen window",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-fullscreen_window",
        },
        {
                "-stats",
                "Show statistics",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-stats",
        },
        {
                "-coords",
                "Show coordinates",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-coords",
        },
        {
                "-show_mem_usage",
                "Show memory usage",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-show_mem_usage",
        },
        {
                "-pofspew",
                "Generate all ibx files immediately",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-pofspew",
        },
        {
                "-tablecrcs",
                "Dump table CRCs for multi validation",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-tablecrcs",
        },
        {
                "-missioncrcs",
                "Dump mission CRCs for multi validation",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-missioncrcs",
        },
        {
                "-dis_collisions",
                "Disable collisions",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-dis_collisions",
        },
        {
                "-dis_weapons",
                "Disable weapon rendering",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-dis_weapons",
        },
        {
                "-output_sexps",
                "Output SEXPs to sexps.html",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-output_sexps",
        },
        {
                "-save_render_target",
                "Save render targets to file",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-save_render_target",
        },
        {
                "-verify_vps",
                "Spew VP CRCs to vp_crcs.txt",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-verify_vps",
        },
        {
                "-reparse_mainhall",
                "Reparse mainhall.tbl when loading halls",
                false,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-reparse_mainhall",
        },
        {
                "-profile_write_file",
                "Write profiling information to file",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-profile_write_file",
        },
        {
                "-no_unfocused_pause",
                "Don't pause if the window isn't focused",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-no_unfocused_pause",
        },
        {
                "-benchmark_mode",
                "Puts the game into benchmark mode",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-benchmark_mode",
        },
        {
                "-noninteractive",
                "Disables interactive dialogs",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-noninteractive",
        },
        {
                "-profile_frame_time",
                "Profile engine subsystems",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-profile_frame_timings",
        },
        {
                "-debug_window",
                "Enable the debug window",
                true,
                0,
                EASY_DEFAULT,
                "Dev Tool",
                "http://www.hard-light.net/wiki/index.php/"
                "Command-Line_Reference#-debug_window",
        },
};

// forward declaration
const char *get_param_desc(const char *flag_name);

// here are the command line parameters that we will be using for FreeSpace

// RETAIL options ----------------------------------------------
cmdline_parm allowabove_arg(
        "-allowabove", "Ranks above this can join multi",
        AT_STRING); // Cmdline_rank_above
cmdline_parm allowbelow_arg(
        "-allowbelow", "Ranks below this can join multi",
        AT_STRING); // Cmdline_rank_below
cmdline_parm
        nosound_arg("-nosound", NULL, AT_NONE); // Cmdline_freespace_no_sound
cmdline_parm
        nomusic_arg("-nomusic", NULL, AT_NONE); // Cmdline_freespace_no_music
cmdline_parm noenhancedsound_arg(
        "-no_enhanced_sound", NULL, AT_NONE); // Cmdline_no_enhanced_sound

cmdline_parm client_dodamage(
        "-clientdamage", NULL, AT_NONE);             // Cmdline_client_dodamage
cmdline_parm pof_spew("-pofspew", NULL, AT_NONE);    // Cmdline_spew_pof_info
cmdline_parm mouse_coords("-coords", NULL, AT_NONE); // Cmdline_mouse_coords
cmdline_parm timeout(
        "-timeout", "Multiplayer network timeout (secs)",
        AT_INT); // Cmdline_timeout
cmdline_parm bit32_arg(
        "-32bit", "Deprecated", AT_NONE); // (only here for retail compatibility
                                          // reasons, doesn't actually do anything)

char *Cmdline_rank_above = NULL;
char *Cmdline_rank_below = NULL;
int Cmdline_cd_check = 1;
int Cmdline_client_dodamage = 0;
int Cmdline_freespace_no_music = 0;
int Cmdline_freespace_no_sound = 0;
int Cmdline_gimme_all_medals = 0;
int Cmdline_mouse_coords = 0;
int Cmdline_multi_stream_chat_to_file = 0;
int Cmdline_spew_pof_info = 0;
int Cmdline_timeout = -1;
int Cmdline_use_last_pilot = 0;

// FSO options -------------------------------------------------

// Graphics related
cmdline_parm fov_arg(
        "-fov", "Vertical field-of-view factor",
        AT_FLOAT); // Cmdline_fov  -- comand line FOV -Bobboau
cmdline_parm clip_dist_arg(
        "-clipdist",
        "Changes the distance from the viewpoint for the near-clipping plane",
        AT_FLOAT); // Cmdline_clip_dist
cmdline_parm spec_exp_arg(
        "-spec_exp", "Adjusts the size of shiny spots on ships", AT_FLOAT);
cmdline_parm ogl_spec_arg(
        "-ogl_spec", "Shininess of specular light", AT_FLOAT); // Cmdline_ogl_spec
cmdline_parm spec_static_arg(
        "-spec_static", "Adjusts suns contribution to specular highlights",
        AT_FLOAT);
cmdline_parm spec_point_arg(
        "-spec_point", "Adjusts laser weapons contribution to specular highlights",
        AT_FLOAT);
cmdline_parm spec_tube_arg(
        "-spec_tube", "Adjusts beam weapons contribution to specular highlights",
        AT_FLOAT);
cmdline_parm ambient_factor_arg(
        "-ambient_factor", "Adjusts ambient light applied to all parts of a ship",
        AT_INT);                           // Cmdline_ambient_factor
cmdline_parm env("-noenv", NULL, AT_NONE); // Cmdline_env
cmdline_parm
        glow_arg("-noglow", NULL, AT_NONE); // Cmdline_glow  -- use Bobs glow code
cmdline_parm nomotiondebris_arg(
        "-nomotiondebris", NULL, AT_NONE); // Cmdline_nomotiondebris  -- Removes
                                           // those ugly floating rocks -C
cmdline_parm noscalevid_arg(
        "-noscalevid", NULL, AT_NONE);           // Cmdline_noscalevid  -- disable video
                                                 // scaling that fits to window
cmdline_parm spec_arg("-nospec", NULL, AT_NONE); // Cmdline_spec  --
cmdline_parm noemissive_arg(
        "-no_emissive_light", "Disable emissive light from ships",
        AT_NONE); // Cmdline_no_emissive  -- don't use emissive light in OGL
cmdline_parm normal_arg(
        "-nonormal", NULL, AT_NONE); // Cmdline_normal  -- disable normal mapping
cmdline_parm height_arg(
        "-noheight", NULL,
        AT_NONE); // Cmdline_height  -- enable support for parallax mapping
cmdline_parm enable_3d_shockwave_arg("-3dshockwave", NULL, AT_NONE);
cmdline_parm softparticles_arg("-soft_particles", NULL, AT_NONE);
cmdline_parm postprocess_arg("-post_process", NULL, AT_NONE);
cmdline_parm bloom_intensity_arg(
        "-bloom_intensity", "Set bloom intensity, requires -post_process", AT_INT);
cmdline_parm fxaa_arg("-fxaa", NULL, AT_NONE);
cmdline_parm fxaa_preset_arg(
        "-fxaa_preset", "FXAA quality (0-9), requires -post_process and -fxaa",
        AT_INT);
cmdline_parm fb_explosions_arg("-fb_explosions", NULL, AT_NONE);
cmdline_parm fb_thrusters_arg("-fb_thrusters", NULL, AT_NONE);
cmdline_parm flightshaftsoff_arg("-nolightshafts", NULL, AT_NONE);
cmdline_parm shadow_quality_arg("-shadow_quality", NULL, AT_INT);
cmdline_parm enable_shadows_arg("-enable_shadows", NULL, AT_NONE);
cmdline_parm no_deferred_lighting_arg(
        "-no_deferred", NULL, AT_NONE); // Cmdline_no_deferred
cmdline_parm anisotropy_level_arg("-anisotropic_filter", NULL, AT_INT);

float Cmdline_clip_dist = Default_min_draw_distance;
float Cmdline_fov = 0.75f;
float Cmdline_ogl_spec = 80.0f;
int Cmdline_ambient_factor = 128;
int Cmdline_env = 1;
int Cmdline_mipmap = 0;
int Cmdline_glow = 1;
int Cmdline_nomotiondebris = 0;
int Cmdline_noscalevid = 0;
int Cmdline_spec = 1;
int Cmdline_no_emissive = 0;
int Cmdline_normal = 1;
int Cmdline_height = 1;
int Cmdline_enable_3d_shockwave = 0;
int Cmdline_softparticles = 0;
int Cmdline_postprocess = 0;
int Cmdline_bloom_intensity = 75;
bool Cmdline_fxaa = false;
int Cmdline_fxaa_preset = 6;
extern int Fxaa_preset_last_frame;
bool Cmdline_fb_explosions = 0;
bool Cmdline_fb_thrusters = false;
extern bool ls_force_off;
int Cmdline_shadow_quality = 0;
int Cmdline_no_deferred_lighting = 0;
int Cmdline_aniso_level = 0;

// Game Speed related
cmdline_parm no_fpscap(
        "-no_fps_capping", "Don't limit frames-per-second",
        AT_NONE);                                      // Cmdline_NoFPSCap
cmdline_parm no_vsync_arg("-no_vsync", NULL, AT_NONE); // Cmdline_no_vsync

int Cmdline_NoFPSCap = 0; // Disable FPS capping - kazan
int Cmdline_no_vsync = 0;

// HUD related
cmdline_parm ballistic_gauge(
        "-ballistic_gauge", NULL, AT_NONE); // Cmdline_ballistic_gauge
cmdline_parm dualscanlines_arg(
        "-dualscanlines", NULL,
        AT_NONE);                                   // Cmdline_dualscanlines  -- Change to phreaks options including
                                                    // new targeting code
cmdline_parm orb_radar("-orbradar", NULL, AT_NONE); // Cmdline_orb_radar
cmdline_parm
        rearm_timer_arg("-rearm_timer", NULL, AT_NONE); // Cmdline_rearm_timer
cmdline_parm targetinfo_arg(
        "-targetinfo", NULL, AT_NONE); // Cmdline_targetinfo  -- Adds ship
                                       // name/class to right of target box -C

int Cmdline_ballistic_gauge = 0; // WMCoolmon's gauge thingy
int Cmdline_dualscanlines = 0;
int Cmdline_orb_radar = 0;
int Cmdline_rearm_timer = 0;
int Cmdline_targetinfo = 0;

// Gameplay related
cmdline_parm use_3dwarp("-3dwarp", NULL, AT_NONE); // Cmdline_3dwarp
cmdline_parm ship_choice_3d_arg(
        "-ship_choice_3d", NULL, AT_NONE); // Cmdline_ship_choice_3d
cmdline_parm weapon_choice_3d_arg(
        "-weapon_choice_3d", NULL, AT_NONE); // Cmdline_weapon_choice_3d
cmdline_parm
        use_warp_flash("-warp_flash", NULL, AT_NONE); // Cmdline_warp_flash
cmdline_parm allow_autpilot_interrupt("-no_ap_interrupt", NULL, AT_NONE);
cmdline_parm
        stretch_menu("-stretch_menu", NULL, AT_NONE); // Cmdline_stretch_menu

int Cmdline_3dwarp = 0;
int Cmdline_ship_choice_3d = 0;
int Cmdline_weapon_choice_3d = 0;
int Cmdline_warp_flash = 0;
int Cmdline_autopilot_interruptable = 1;
int Cmdline_stretch_menu = 0;

// Audio related
cmdline_parm
        query_speech_arg("-query_speech", NULL, AT_NONE); // Cmdline_query_speech
cmdline_parm voice_recognition_arg(
        "-voicer", NULL, AT_NONE); // Cmdline_voice_recognition

int Cmdline_query_speech = 0;
int Cmdline_voice_recognition = 0;
int Cmdline_no_enhanced_sound = 0;

// MOD related
cmdline_parm mod_arg(
        "-mod", "List of folders to overwrite/add-to the default data", AT_STRING,
        true); // Cmdline_mod  -- DTP modsupport

char *Cmdline_mod = NULL; // DTP for mod argument

// Multiplayer/Network related
cmdline_parm almission_arg(
        "-almission", "Autoload multiplayer mission",
        AT_STRING); // Cmdline_almission  -- DTP for autoload Multi mission
cmdline_parm
        ingamejoin_arg("-ingame_join", NULL, AT_NONE); // Cmdline_ingamejoin
cmdline_parm mpnoreturn_arg(
        "-mpnoreturn", NULL, AT_NONE); // Cmdline_mpnoreturn  -- Removes 'Return to
                                       // Flight Deck' in respawn dialog -C
cmdline_parm objupd_arg(
        "-cap_object_update", "Multiplayer object update cap (0-3)", AT_INT);

char *Cmdline_almission = NULL; // DTP for autoload multi mission.
int Cmdline_ingamejoin = 0;
int Cmdline_mpnoreturn = 0;
int Cmdline_objupd = 3; // client object updates on LAN by default

// Launcher related options
cmdline_parm portable_mode("-portable_mode", NULL, AT_NONE);

// Troubleshooting
cmdline_parm loadallweapons_arg(
        "-loadallweps", NULL, AT_NONE); // Cmdline_load_all_weapons
cmdline_parm nomovies_arg(
        "-nomovies", NULL, AT_NONE); // Cmdline_nomovies  -- Allows video streaming
cmdline_parm
        no_set_gamma_arg("-no_set_gamma", NULL, AT_NONE); // Cmdline_no_set_gamma
cmdline_parm no_fbo_arg("-disable_fbo", NULL, AT_NONE);   // Cmdline_no_fbo
cmdline_parm no_pbo_arg("-disable_pbo", NULL, AT_NONE);   // Cmdline_no_pbo
cmdline_parm mipmap_arg("-mipmap", NULL, AT_NONE);        // Cmdline_mipmap
cmdline_parm atiswap_arg(
        "-ati_swap", NULL,
        AT_NONE); // Cmdline_atiswap - Fix ATI color swap issue for screenshots.
cmdline_parm no3dsound_arg(
        "-no_3d_sound", NULL,
        AT_NONE); // Cmdline_no_3d_sound - Disable use of full 3D sounds
cmdline_parm no_drawrangeelements(
        "-use_gldrawelements", NULL,
        AT_NONE); // Cmdline_drawelements -- Uses glDrawElements instead of
                  // glDrawRangeElements
cmdline_parm keyboard_layout(
        "-keyboard_layout", "Specify keyboard layout (qwertz or azerty)",
        AT_STRING);
cmdline_parm old_collision_system(
        "-old_collision", NULL, AT_NONE); // Cmdline_old_collision_sys
cmdline_parm gl_finish("-gl_finish", NULL, AT_NONE);
cmdline_parm no_geo_sdr_effects("-no_geo_effects", NULL, AT_NONE);
cmdline_parm set_cpu_affinity("-set_cpu_affinity", NULL, AT_NONE);
cmdline_parm nograb_arg("-nograb", NULL, AT_NONE);

int Cmdline_load_all_weapons = 0;
int Cmdline_nomovies = 0;
int Cmdline_no_set_gamma = 0;
int Cmdline_no_fbo = 0;
int Cmdline_no_pbo = 0;
int Cmdline_ati_color_swap = 0;
int Cmdline_no_3d_sound = 0;
int Cmdline_drawelements = 0;
char *Cmdline_keyboard_layout = NULL;
bool Cmdline_gl_finish = false;
bool Cmdline_no_geo_sdr_effects = false;
bool Cmdline_set_cpu_affinity = false;
bool Cmdline_nograb = false;

// Developer/Testing related
cmdline_parm start_mission_arg(
        "-start_mission", "Skip mainhall and run this mission",
        AT_STRING); // Cmdline_start_mission
cmdline_parm dis_collisions(
        "-dis_collisions", NULL, AT_NONE); // Cmdline_dis_collisions
cmdline_parm
        dis_weapons("-dis_weapons", NULL, AT_NONE); // Cmdline_dis_weapons
cmdline_parm noparseerrors_arg(
        "-noparseerrors", NULL,
        AT_NONE); // Cmdline_noparseerrors  -- turns off parsing errors -C
cmdline_parm extra_warn_arg(
        "-extra_warn", "Enable 'extra' warnings", AT_NONE); // Cmdline_extra_warn
cmdline_parm fps_arg("-fps", NULL, AT_NONE);                // Cmdline_show_fps
cmdline_parm
        bmpmanusage_arg("-bmpmanusage", NULL, AT_NONE); // Cmdline_bmpman_usage
cmdline_parm pos_arg("-pos", NULL, AT_NONE);            // Cmdline_show_pos
cmdline_parm stats_arg("-stats", NULL, AT_NONE);        // Cmdline_show_stats
cmdline_parm save_render_targets_arg(
        "-save_render_target", NULL, AT_NONE);     // Cmdline_save_render_targets
cmdline_parm window_arg("-window", NULL, AT_NONE); // Cmdline_window
cmdline_parm fullscreen_window_arg(
        "-fullscreen_window", "Fullscreen/borderless window (Windows only)",
        AT_NONE);
cmdline_parm
        res_arg("-res", "Resolution, formatted like 1600x900", AT_STRING);
cmdline_parm center_res_arg(
        "-center_res", "Resolution of center monitor, formatted like 1600x900",
        AT_STRING);
cmdline_parm verify_vps_arg(
        "-verify_vps", NULL,
        AT_NONE); // Cmdline_verify_vps  -- spew VP crcs to vp_crcs.txt
cmdline_parm parse_cmdline_only(
        PARSE_COMMAND_LINE_STRING, "Ignore any cmdline_fso.cfg files", AT_NONE);
cmdline_parm reparse_mainhall_arg(
        "-reparse_mainhall", NULL, AT_NONE); // Cmdline_reparse_mainhall
cmdline_parm frame_profile_write_file(
        "-profile_write_file", NULL, AT_NONE); // Cmdline_profile_write_file
cmdline_parm benchmark_mode_arg(
        "-benchmark_mode", NULL, AT_NONE); // Cmdline_benchmark_mode
cmdline_parm noninteractive_arg(
        "-noninteractive", NULL, AT_NONE); // Cmdline_noninteractive
cmdline_parm show_video_info(
        "-show_video_info", NULL, AT_NONE); // Cmdline_show_video_info
cmdline_parm frame_profile_arg(
        "-profile_frame_time", NULL, AT_NONE); // Cmdline_frame_profile

char *Cmdline_start_mission = NULL;
int Cmdline_old_collision_sys = 0;
int Cmdline_dis_collisions = 0;
int Cmdline_dis_weapons = 0;
bool Cmdline_output_sexp_info = false;
int Cmdline_noparseerrors = 0;
#ifdef Allow_NoWarn
int Cmdline_nowarn = 0; // turn warnings off in FRED
#endif
int Cmdline_extra_warn = 0;
int Cmdline_bmpman_usage = 0;
int Cmdline_show_pos = 0;
int Cmdline_show_stats = 0;
int Cmdline_save_render_targets = 0;
int Cmdline_window = 0;
int Cmdline_fullscreen_window = 0;
char *Cmdline_res = 0;
char *Cmdline_center_res = 0;
int Cmdline_verify_vps = 0;
int Cmdline_reparse_mainhall = 0;
bool Cmdline_profile_write_file = false;
bool Cmdline_benchmark_mode = false;
bool Cmdline_noninteractive = false;
bool Cmdline_frame_profile = false;
bool Cmdline_show_video_info = false;

// Other
cmdline_parm output_sexp_arg(
        "-output_sexps", NULL, AT_NONE); // WMC - outputs all SEXPs to sexps.html

// Deprecated flags - CommanderDJ
cmdline_parm deprecated_spec_arg("-spec", "Deprecated", AT_NONE);
cmdline_parm deprecated_glow_arg("-glow", "Deprecated", AT_NONE);
cmdline_parm deprecated_normal_arg("-normal", "Deprecated", AT_NONE);
cmdline_parm deprecated_env_arg("-env", "Deprecated", AT_NONE);
cmdline_parm deprecated_tbp_arg("-tbp", "Deprecated", AT_NONE);
cmdline_parm deprecated_jpgtga_arg("-jpgtga", "Deprecated", AT_NONE);
cmdline_parm deprecated_htl_arg("-nohtl", "Deprecated", AT_NONE);
cmdline_parm
        deprecated_brieflighting_arg("-brief_lighting", "Deprecated", AT_NONE);
cmdline_parm deprecated_sndpreload_arg("-snd_preload", "Deprecated", AT_NONE);
cmdline_parm deprecated_missile_lighting_arg(
        "-missile_lighting", "Deprecated", AT_NONE);
cmdline_parm
        deprecated_cache_bitmaps_arg("-cache_bitmaps", "Deprecated", AT_NONE);

int Cmdline_deprecated_spec = 0;
int Cmdline_deprecated_glow = 0;
int Cmdline_deprecated_normal = 0;
int Cmdline_deprecated_env = 0;
int Cmdline_deprecated_tbp = 0;
int Cmdline_deprecated_jpgtga = 0;
int Cmdline_deprecated_nohtl = 0;
bool Cmdline_deprecated_brief_lighting = 0;
bool Cmdline_deprecated_missile_lighting = false;
bool Cmdline_deprecated_cache_bitmaps = false;

// Return true if this character is an extra char (white space and quotes)
int is_extra_space(char ch)
{
        return (
                (ch == ' ') || (ch == '\t') || (ch == 0x0a) || (ch == '\'') || (ch == '\"'));
}

// eliminates all leading and trailing extra chars from a string.  Returns
// pointer passed in.
char *drop_extra_chars(char *str)
{
        size_t s;
        size_t e;

        s = 0;
        while (str[s] && is_extra_space(str[s])) s++;

        e = strlen(str);

        if (e > 0) {
                // we already account for NULL later on, so the -1 is here to make
                // sure we do our math without taking it into consideration
                e -= 1;
        }

        while (e > s) {
                if (!is_extra_space(str[e])) {
                        break;
                }

                e--;
        }

        if (e >= s && e != 0) {
                memmove(str, str + s, e - s + 1);
        }

        str[e - s + 1] = 0;
        return str;
}

/*
 * @brief Processes one argument for the given parameter
 *
 * @param param The parameter to check
 * @param argc The argument count
 * @param argc The argument values
 * @param argc The current index
 * @return @c true when an extra parameter was found, @c false otherwise
 */
bool parm_stuff_args(cmdline_parm *parm, int argc, char *argv[], int index)
{
        ASSERT(index < argc);

        if (index + 1 < argc) {
                char *value = argv[index + 1];
                if (value[0] == '-') {
                        // Found another argument, just return
                        return false;
                } else {
                        char *saved_args = NULL;

                        if (parm->args != NULL) {
                                if (parm->stacks) {
                                        saved_args = parm->args;
                                } else {
                                        delete[] parm->args;
                                }

                                parm->args = NULL;
                        }

                        size_t argsize = strlen(argv[index + 1]);
                        size_t buffersize = argsize;

                        if (saved_args != NULL) {
                                // Add one for the , separating args
                                buffersize += strlen(saved_args) + 1;
                        }

                        buffersize += 1; // Null-terminator

                        parm->args = new char[buffersize];
                        memset(parm->args, 0, buffersize);

                        if (saved_args != NULL) {
                                // saved args go first, then new arg
                                strcpy(parm->args, saved_args);
                                // add a separator too, so that we can tell the args apart
                                strcat(parm->args, ",");
                                // now the new arg
                                strcat(parm->args, argv[index + 1]);

                                delete[] saved_args;
                        } else {
                                strcpy(parm->args, argv[index + 1]);
                        }

                        return true;
                }
        } else {
                // Last argument, can't have any values
                return false;
        }
}

// internal function - parse the command line, extracting parameter arguments
// if they exist cmdline - command line string passed to the application
void os_parse_parms(int argc, char *argv[])
{
        // locate command line parameters
        cmdline_parm *parmp;

        for (int i = 0; i < argc; i++) {
                // On OS X this gets passed if the application was launched by
                // double-clicking in the Finder
                if (i == 1 && strncmp(argv[i], "-psn", 4) == 0) {
                        continue;
                }

                for (parmp = GET_FIRST(&Parm_list); parmp != END_OF_LIST(&Parm_list);
                     parmp = GET_NEXT(parmp)) {
                        if (!strcasecmp(parmp->name, argv[i])) {
                                parmp->name_found = 1;
                                if (parm_stuff_args(parmp, argc, argv, i)) {
                                        i++;
                                }
                        }
                }
        }
}

// validate the command line parameters.  Display an error if an unrecognized
// parameter is located.
void os_validate_parms(int argc, char *argv[])
{
        cmdline_parm *parmp;
        char *token;
        int parm_found;

        for (int i = 0; i < argc; i++) {
                token = argv[i];

                // On OS X this gets passed if the application was launched by
                // double-clicking in the Finder
                if (i == 1 && strncmp(token, "-psn", 4) == 0) {
                        continue;
                }

                if (token[0] == '-') {
                        parm_found = 0;

                        for (parmp = GET_FIRST(&Parm_list);
                             parmp != END_OF_LIST(&Parm_list); parmp = GET_NEXT(parmp)) {
                                if (!strcasecmp(parmp->name, token)) {
                                        parm_found = 1;
                                        break;
                                }
                        }

                        if (parm_found == 0) {
                                // if we got a -help, --help, -h, or -? then show the help
                                // text, otherwise show unknown option
                                if (!strcasecmp(token, "-help") || !strcasecmp(token, "--help") || !strcasecmp(token, "-h") || !strcasecmp(token, "-?")) {
                                        printf("FreeSpace 2 Open, version %s\n", FS_VERSION_FULL);
                                        printf("Website: http://scp.indiegames.us\n");
                                        printf(
                                                "Mantis (bug reporting): "
                                                "http://scp.indiegames.us/mantis/\n\n");
                                        printf("Usage: fs2_open [options]\n");

                                        // not the prettiest thing but the job gets done
                                        static const int STR_SIZE = 25; // max len of exe_params.name + 5 spaces
                                        const int AT_SIZE = 8;          // max len of cmdline_arg_types[] + 2 spaces
                                        size_t atp = 0;
                                        size_t sp = 0;
                                        for (parmp = GET_FIRST(&Parm_list);
                                             parmp != END_OF_LIST(&Parm_list);
                                             parmp = GET_NEXT(parmp)) {
                                                // don't output deprecated flags
                                                if (strcasecmp("deprecated", parmp->help) != 0) {
                                                        sp = strlen(parmp->name);
                                                        if (parmp->arg_type != AT_NONE) {
                                                                atp = strlen(
                                                                        cmdline_arg_types[parmp->arg_type]);
                                                                printf(
                                                                        "    [ %s ]%*s[ %s ]%*s- %s\n",
                                                                        parmp->name, (int)(STR_SIZE - sp - 1),
                                                                        NOX(" "),
                                                                        cmdline_arg_types[parmp->arg_type],
                                                                        (int)(AT_SIZE - atp), NOX(" "),
                                                                        parmp->help);
                                                        } else {
                                                                printf(
                                                                        "    [ %s ]%*s- %s\n", parmp->name,
                                                                        (int)(STR_SIZE - sp - 1 + AT_SIZE + 4),
                                                                        NOX(" "), parmp->help);
                                                        }
                                                }
                                        }

                                        printf("\n");
                                        exit(0);
                                } else {
                                        II
                                                << "unrecognized command line argument: " << token;
                                }
                        }
                }
        }
}

int parse_cmdline_string(char *cmdline, char **argv)
{
        size_t length = strlen(cmdline);

        bool start_found = false;
        bool quoted = false;

        size_t argc = 0;
        char *current_argv = NULL;
        for (size_t i = 0; i < length; i++) {
                if (!start_found && !isspace(cmdline[i])) {
                        start_found = true;
                        current_argv = (cmdline + i);
                } else if (start_found) {
                        if (cmdline[i] == '"') {
                                quoted = !quoted;

                                if (!quoted && current_argv != NULL) {
                                        if (argv != NULL) {
                                                // Terminate string at quote
                                                cmdline[i] = '\0';
                                                argv[argc] = current_argv;
                                                current_argv = NULL;
                                        }

                                        argc++;
                                }
                        } else if (isspace(cmdline[i]) && !quoted) {
                                // Parameter terminated by space
                                if (current_argv != NULL) // == NULL means that we currently
                                                          // don't have a parameter
                                {
                                        if (argv != NULL) {
                                                // Terminate string at quote
                                                cmdline[i] = '\0';
                                                argv[argc] = current_argv;
                                                current_argv = NULL;
                                        }

                                        argc++;
                                }
                        } else if (current_argv == NULL) {
                                current_argv = cmdline + i;
                        }
                }
        }

        if (current_argv != NULL) {
                if (argv != NULL) {
                        // Terminate string at quote
                        argv[argc] = current_argv;
                        current_argv = NULL;
                }

                argc++;
        }

        return (int)argc;
}

void os_process_cmdline(char *cmdline)
{
        int argc = parse_cmdline_string(cmdline, NULL);

        char **argv = new char *[argc];

        argc = parse_cmdline_string(cmdline, argv);

        os_parse_parms(argc, argv);
        os_validate_parms(argc, argv);

        delete[] argv;
}

bool has_cmdline_only_flag(int argc, char *argv[])
{
        for (int i = 0; i < argc; ++i) {
                if (!strcmp(argv[i], PARSE_COMMAND_LINE_STRING)) {
                        return true;
                }
        }

        return false;
}

// remove old parms - needed for tests
static void reset_cmdline_parms()
{
        for (cmdline_parm *parmp = GET_FIRST(&Parm_list);
             parmp != END_OF_LIST(&Parm_list); parmp = GET_NEXT(parmp)) {
                if (parmp->args != NULL) {
                        delete[] parmp->args;
                        parmp->args = NULL;
                }
                parmp->name_found = 0;
        }
}

// Call once to initialize the command line system
//
// cmdline - command line string passed to the application
void os_init_cmdline(int argc, char *argv[])
{
        // Tests call this multiple times, so reset the params here.
        // Otherwise e.g. the modlist just grows and grows...
        reset_cmdline_parms();

        FILE *fp;

        if (!has_cmdline_only_flag(argc, argv)) {
                // Only parse the config file in the current directory if we are in
                // legacy config mode parse user specific cmdline_fso config file
                // (will supersede options in global file)
                fp = fopen(fs2::os::get_config_path("data/cmdline_fso.cfg").c_str(), "rt");

                // if the file exists, get a single line, and deal with it
                if (fp) {
                        char *buf, *p;

                        auto len = static_cast< int >(cfilelength(fileno(fp))) + 2;
                        buf = new char[len];

                        if (fgets(buf, len - 1, fp) != nullptr) {
                                // replace the newline character with a NULL
                                if ((p = strrchr(buf, '\n')) != NULL) {
                                        *p = '\0';
                                }

                                // append a space for the os_parse_parms() check
                                strcat(buf, " ");

                                os_process_cmdline(buf);
                        }

                        delete[] buf;

                        fclose(fp);
                }
        } // If cmdline included PARSE_COMMAND_LINE_STRING

        // By parsing cmdline last, anything actually on the command line will take
        // precedence.
        os_parse_parms(argc, argv);
        os_validate_parms(argc, argv);
}

/*
 * arg constructor
 *
 * @param name_    name of the parameter, must start with '-' character
 * @param help_    help text for this parameter
 * @param arg_type_    parameters arguement type (if any)
 * @param stacks_    can the parameter be stacked
 */
cmdline_parm::cmdline_parm(
        const char *name_, const char *help_, const int arg_type_,
        const bool stacks_)
        : name(name_), help(help_), stacks(stacks_), arg_type(arg_type_)
{
        args = NULL;
        name_found = 0;

        if (Parm_list_inited == 0) {
                ASSERTX(
                        &Parm_list == this,
                        "Coding error! 1st initialised cmdline_parm must be static "
                        "Parm_list\n");
                list_init(this);
                Parm_list_inited = 1;
        } else {
                ASSERTX(
                        name, "Coding error! cmdline_parm's must have a non-NULL name\n");
                ASSERTX(
                        name[0] == '-',
                        "Coding error! cmdline_parm's must start with a '-'\n");
                // not in the static Parm_list init, so lookup the NULL help args
                if (help == NULL) {
                        help = get_param_desc(name);
                }
                list_append(&Parm_list, this);
        }
}

// destructor - frees any allocated memory
cmdline_parm::~cmdline_parm()
{
        if (args) {
                delete[] args;
                args = 0;
        }
}

// checks if the objects args variable is valid
// returns true if it is, shows an error box and returns false if not valid.
bool cmdline_parm::check_if_args_is_valid()
{
        if (0 == args) {
                ASSERTX(0, "missing command line argument (%s) value", name);
                return false;
        } else
                return true;
}

// returns - true if the parameter exists on the command line, otherwise false
int cmdline_parm::found() { return name_found; }

// returns - the interger representation for the parameter argument
int cmdline_parm::get_int()
{
        ASSERTX(
                arg_type == AT_INT,
                "Coding error! Cmdline arg (%s) called cmdline_parm::get_int() with "
                "invalid arg_type (%s)",
                name, cmdline_arg_types[arg_type]);
        check_if_args_is_valid();

        size_t offset = 0;

        if (stacks) {
                // first off, DON'T STACK NON-STRINGS!!
                ASSERT(0);

                // secondly, we still need to get it right for the user's sake...
                char *moron = strstr(args, ",");

                if (moron && ((strlen(moron) + 1) < strlen(args))) {
                        // we get the last arg, since it's the newest one
                        offset = strlen(args) - strlen(moron) + 1;
                }
        }

        return atoi(args + offset);
}

// returns - the float representation for the parameter argument
float cmdline_parm::get_float()
{
        ASSERTX(
                arg_type == AT_FLOAT,
                "Coding error! Cmdline arg (%s) called cmdline_parm::get_float() with "
                "invalid arg_type (%s)",
                name, cmdline_arg_types[arg_type]);
        check_if_args_is_valid();

        size_t offset = 0;

        if (stacks) {
                // first off, DON'T STACK NON-STRINGS!!
                ASSERT(0);

                // secondly, we still need to get it right for the user's sake
                char *moron = strstr(args, ",");

                if (moron && ((strlen(moron) + 1) < strlen(args))) {
                        // we get the last arg, since it's the newest one
                        offset = strlen(args) - strlen(moron) + 1;
                }
        }

        return (float)atof(args + offset);
}

// returns - the string value for the parameter argument
char *cmdline_parm::str()
{
        ASSERTX(
                arg_type == AT_STRING,
                "Coding error! Cmdline arg (%s) called cmdline_parm::str() with "
                "invalid arg_type (%s)",
                name, cmdline_arg_types[arg_type]);
        check_if_args_is_valid();

        return args;
}
bool cmdline_parm::has_param() { return args != nullptr; }

// Return a vector with all filesystem names of "parent/dir" relative to
// parent. dir must not contain a slash.
static std::vector< std::string >
unix_get_single_dir_names(const std::string &parent, const std::string &dir)
{
        std::vector< std::string > ret;

        DIR *dp;
        if ((dp = opendir(parent.c_str())) == NULL) {
                WARNINGF(LOCATION, "Can't open directory '%s' when searching mod paths. Ignoring. errno=%d", parent.c_str(), errno);
                return ret;
        }

        dirent *dirp;
        while ((dirp = readdir(dp)) != NULL) {
                if (!strcasecmp(dirp->d_name, dir.c_str())) {
                        ret.push_back(dirp->d_name);
                }
        }
        (void)closedir(dp);

        return ret;
}

// Return a vector with all filesystem names of "parent/dir" relative to
// parent. Recurses to deal with slashes in dir.
static std::vector< std::string >
unix_get_dir_names(const std::string &parent, const std::string &dir)
{
        size_t slash = dir.find_first_of("/\\");

        // no subdirectories, no need to recurse
        if (slash == std::string::npos) {
                return unix_get_single_dir_names(parent, dir);
        }

        // get the names of the first component of dir
        std::vector< std::string > this_dir_names = unix_get_single_dir_names(parent, dir.substr(0, slash));

        std::string rest = dir.substr(slash + 1);

        std::vector< std::string > ret;

        // search for the rest of dir in each of these
        std::vector< std::string >::iterator ii, end = this_dir_names.end();
        for (ii = this_dir_names.begin(); ii != end; ++ii) {
                std::string this_dir_path = parent + "/" + *ii;
                std::vector< std::string > mod_path = unix_get_dir_names(this_dir_path, rest);

                // add all found paths relative to parent
                std::vector< std::string >::iterator ii2, end2 = mod_path.end();
                for (ii2 = mod_path.begin(); ii2 != end2; ++ii2) {
                        ret.push_back(*ii + "/" + *ii2);
                }
        }

        return ret;
}

// For case sensitive filesystems (e.g. Linux/BSD) perform case-insensitive dir
// matches.
static void handle_unix_modlist(char **modlist, size_t *len)
{
        // search filesystem for given paths
        std::vector< std::string > mod_paths;
        for (char *cur_mod = strtok(*modlist, ","); cur_mod != NULL;
             cur_mod = strtok(NULL, ",")) {
                std::vector< std::string > this_mod_paths = unix_get_dir_names(".", cur_mod);
                // Ignore non-existing mods for unit tests
                if (!running_unittests && this_mod_paths.empty()) {
                        RELEASE_WARNINGF(
                                LOCATION, "Can't find mod '%s'. Ignoring.", cur_mod);
                }
                mod_paths.insert(
                        mod_paths.end(), this_mod_paths.begin(), this_mod_paths.end());
        }

        // create new char[] to replace modlist
        size_t total_len = 0;
        std::vector< std::string >::iterator ii, end = mod_paths.end();
        for (ii = mod_paths.begin(); ii != end; ++ii) {
                total_len += ii->length() + 1;
        }

        char *new_modlist = new char[total_len + 1];
        memset(new_modlist, 0, total_len + 1);
        end = mod_paths.end();
        for (ii = mod_paths.begin(); ii != end; ++ii) {
                strcat(new_modlist, ii->c_str());
                strcat(new_modlist, ","); // replace later with NUL
        }

        // make the rest of the modlist manipulation unaware that anything happened
        // here
        delete[] * modlist;
        *modlist = new_modlist;
        *len = total_len;
}

// Sets externed variables used for communication cmdline information
bool SetCmdlineParams()
{
        // getcwd(FreeSpace_Directory, 256); // set the directory to our fs2 root

        // DO THIS FIRST to avoid unrecognized flag warnings when just getting flag
        // file

        if (no_fpscap.found()) {
                Cmdline_NoFPSCap = 1;
        }

        if (loadallweapons_arg.found()) {
                Cmdline_load_all_weapons = 1;
        }

        if (voice_recognition_arg.found()) {
                Cmdline_voice_recognition = 1;
        }

#ifdef Allow_NoWarn
        if (nowarn_arg.found()) {
                Cmdline_nowarn = 1;
        }
#endif

        if (extra_warn_arg.found()) {
                Cmdline_extra_warn = 1;
        }

        // object update control
        if (objupd_arg.found()) {
                Cmdline_objupd = objupd_arg.get_int();
                if (Cmdline_objupd < 0) {
                        Cmdline_objupd = 0;
                }
                if (Cmdline_objupd > 3) {
                        Cmdline_objupd = 3;
                }
        }

        if (mpnoreturn_arg.found()) {
                Cmdline_mpnoreturn = 1;
        }

        // run with no sound
        if (nosound_arg.found()) {
                Cmdline_freespace_no_sound = 1;
                // and since music is automatically unusable...
                Cmdline_freespace_no_music = 1;
        }

        // run with no music
        if (nomusic_arg.found()) {
                Cmdline_freespace_no_music = 1;
        }

        // Disable enhanced sound
        if (noenhancedsound_arg.found()) {
                Cmdline_no_enhanced_sound = 1;
        }

        // set the rank above/below arguments
        if (allowabove_arg.found()) {
                Cmdline_rank_above = allowabove_arg.str();
        }
        if (allowbelow_arg.found()) {
                Cmdline_rank_below = allowbelow_arg.str();
        }

        // maybe use old-school client damage
        if (client_dodamage.found()) {
                Cmdline_client_dodamage = 1;
        }

        // spew pof info
        if (pof_spew.found()) {
                Cmdline_spew_pof_info = 1;
        }

        // mouse coords
        if (mouse_coords.found()) {
                Cmdline_mouse_coords = 1;
        }

        // net timeout
        if (timeout.found()) {
                Cmdline_timeout = timeout.get_int();
        }

        // d3d windowed
        if (window_arg.found()) {
                Cmdline_window = 1;
        }

        if (fullscreen_window_arg.found()) {
                Cmdline_fullscreen_window = 1;
                Cmdline_window = 0; /* Make sure no-one sets both */
        }

        if (res_arg.found()) {
                Cmdline_res = res_arg.str();
        }
        if (center_res_arg.found()) {
                Cmdline_center_res = center_res_arg.str();
        }
        if (almission_arg.found()) {
                // DTP for autoload mission
                Cmdline_almission = almission_arg.str();
                Cmdline_use_last_pilot = 1;
        }

        if (dualscanlines_arg.found()) {
                Cmdline_dualscanlines = 1;
        }

        if (targetinfo_arg.found()) {
                Cmdline_targetinfo = 1;
        }

        if (nomovies_arg.found()) {
                Cmdline_nomovies = 1;
        }

        if (noscalevid_arg.found()) {
                Cmdline_noscalevid = 1;
        }

        if (noparseerrors_arg.found()) {
                Cmdline_noparseerrors = 1;
        }

        if (mod_arg.found()) {
                Cmdline_mod = mod_arg.str();

                // strip off blank space it it's there
                if (Cmdline_mod[strlen(Cmdline_mod) - 1] == ' ') {
                        Cmdline_mod[strlen(Cmdline_mod) - 1] = '\0';
                }

                // Ok - mod stacking support
                size_t len = strlen(Cmdline_mod);
                char *modlist = new char[len + 2];
                memset(modlist, 0, len + 2);
                strcpy(modlist, Cmdline_mod);

                // handle case-insensitive searching
                handle_unix_modlist(&modlist, &len);

                // null terminate each individual
                for (size_t i = 0; i < len; i++) {
                        if (modlist[i] == ',')
                                modlist[i] = '\0';
                }

                // copy over - we don't have to delete[] Cmdline_mod because it's a
                // pointer to an automatic global char*
                Cmdline_mod = modlist;
        }

        if (fps_arg.found()) {
                Show_framerate = 1;
        }

        if (bmpmanusage_arg.found()) {
                Cmdline_bmpman_usage = 1;
        }

        if (pos_arg.found()) {
                Cmdline_show_pos = 1;
        }

        if (nomotiondebris_arg.found()) {
                Cmdline_nomotiondebris = 1;
        }

        if (mipmap_arg.found()) {
                Cmdline_mipmap = 1;
        }

        if (stats_arg.found()) {
                Cmdline_show_stats = 1;
        }

        if (fov_arg.found()) {
                Cmdline_fov = fov_arg.get_float();
                if (Cmdline_fov > 0.1) {
                        VIEWER_ZOOM_DEFAULT = Cmdline_fov;
                } else {
                        VIEWER_ZOOM_DEFAULT = Cmdline_fov = 0.75f;
                }
        }

        if (clip_dist_arg.found()) {
                Min_draw_distance = Cmdline_clip_dist = clip_dist_arg.get_float();
        }

        if (orb_radar.found()) {
                Cmdline_orb_radar = 1;
        }

        if (use_3dwarp.found()) {
                Cmdline_3dwarp = 1;
        }

        if (use_warp_flash.found()) {
                Cmdline_warp_flash = 1;
        }

        if (allow_autpilot_interrupt.found()) {
                Cmdline_autopilot_interruptable = 0;
        }

        if (stretch_menu.found()) {
                Cmdline_stretch_menu = 1;
        }
        // specular comand lines
        if (spec_exp_arg.found()) {
                specular_exponent_value = spec_exp_arg.get_float();
        }

        if (spec_point_arg.found()) {
                static_point_factor = spec_point_arg.get_float();
        }

        if (spec_static_arg.found()) {
                static_light_factor = spec_static_arg.get_float();
        }

        if (spec_tube_arg.found()) {
                static_tube_factor = spec_tube_arg.get_float();
        }

        if (spec_arg.found()) {
                Cmdline_spec = 0;
        }

        if (no_set_gamma_arg.found()) {
                Cmdline_no_set_gamma = 1;
        }

        if (no_vsync_arg.found()) {
                Cmdline_no_vsync = 1;
        }

        if (normal_arg.found()) {
                Cmdline_normal = 0;
        }

        if (height_arg.found()) {
                Cmdline_height = 0;
        }

        if (fxaa_arg.found()) {
                Cmdline_fxaa = true;

                if (fxaa_preset_arg.found()) {
                        Cmdline_fxaa_preset = fxaa_preset_arg.get_int();
                }

                Fxaa_preset_last_frame = Cmdline_fxaa_preset;
        }

        if (glow_arg.found())
                Cmdline_glow = 0;

        if (query_speech_arg.found())
                Cmdline_query_speech = 1;

        if (ship_choice_3d_arg.found())
                Cmdline_ship_choice_3d = 1;

        if (weapon_choice_3d_arg.found())
                Cmdline_weapon_choice_3d = 1;

        if (ingamejoin_arg.found())
                Cmdline_ingamejoin = 1;

        if (start_mission_arg.found()) {
                Cmdline_start_mission = start_mission_arg.str();
        }

        if (ambient_factor_arg.found())
                Cmdline_ambient_factor = ambient_factor_arg.get_int();

        if (output_sexp_arg.found()) {
                Cmdline_output_sexp_info = true;
        }

        if (no_pbo_arg.found()) {
                Cmdline_no_pbo = 1;
        }

        if (no_drawrangeelements.found()) {
                Cmdline_drawelements = 1;
        }

        if (keyboard_layout.found()) {
                Cmdline_keyboard_layout = keyboard_layout.str();
        }

        if (gl_finish.found()) {
                Cmdline_gl_finish = true;
        }

        if (no_geo_sdr_effects.found()) {
                Cmdline_no_geo_sdr_effects = true;
        }

        if (set_cpu_affinity.found()) {
                Cmdline_set_cpu_affinity = true;
        }

        if (nograb_arg.found()) {
                Cmdline_nograb = true;
        }

        if (env.found()) {
                Cmdline_env = 0;
        }

        if (ballistic_gauge.found()) {
                Cmdline_ballistic_gauge = 1;
        }

        if (old_collision_system.found())
                Cmdline_old_collision_sys = 1;

        if (dis_collisions.found())
                Cmdline_dis_collisions = 1;

        if (dis_weapons.found())
                Cmdline_dis_weapons = 1;

        if (no_fbo_arg.found()) {
                Cmdline_no_fbo = 1;
        }

        if (noemissive_arg.found()) {
                Cmdline_no_emissive = 1;
        }

        if (ogl_spec_arg.found()) {
                Cmdline_ogl_spec = ogl_spec_arg.get_float();

                CLAMP(Cmdline_ogl_spec, 0.0f, 128.0f);
        }

        if (rearm_timer_arg.found())
                Cmdline_rearm_timer = 1;

        if (save_render_targets_arg.found())
                Cmdline_save_render_targets = 1;

        if (verify_vps_arg.found())
                Cmdline_verify_vps = 1;

        if (no3dsound_arg.found())
                Cmdline_no_3d_sound = 1;

        if (atiswap_arg.found()) {
                Cmdline_ati_color_swap = 1;
        }

        if (enable_3d_shockwave_arg.found()) {
                Cmdline_enable_3d_shockwave = 1;
        }

        if (softparticles_arg.found()) {
                Cmdline_softparticles = 1;
        }

        if (fb_explosions_arg.found()) {
                Cmdline_fb_explosions = 1;
        }

        if (fb_thrusters_arg.found()) {
                Cmdline_fb_thrusters = true;
        }

        if (postprocess_arg.found()) {
                Cmdline_postprocess = 1;
        }

        if (bloom_intensity_arg.found()) {
                Cmdline_bloom_intensity = bloom_intensity_arg.get_int();
        }

        if (flightshaftsoff_arg.found()) {
                ls_force_off = true;
        }

        if (reparse_mainhall_arg.found()) {
                Cmdline_reparse_mainhall = 1;
        }

        if (enable_shadows_arg.found()) {
                Cmdline_shadow_quality = 2;
        }

        if (shadow_quality_arg.found()) {
                Cmdline_shadow_quality = shadow_quality_arg.get_int();
        }

        if (no_deferred_lighting_arg.found()) {
                Cmdline_no_deferred_lighting = 1;
        }

        if (anisotropy_level_arg.found()) {
                Cmdline_aniso_level = anisotropy_level_arg.get_int();
        }

        if (frame_profile_write_file.found()) {
                Cmdline_profile_write_file = true;
        }

        if (benchmark_mode_arg.found()) {
                Cmdline_benchmark_mode = true;
        }

        if (noninteractive_arg.found()) {
                Cmdline_noninteractive = true;
        }

        if (frame_profile_arg.found()) {
                Cmdline_frame_profile = true;
        }

        if (show_video_info.found()) {
                Cmdline_show_video_info = true;
        }

        // Deprecated flags - CommanderDJ
        if (deprecated_spec_arg.found()) {
                Cmdline_deprecated_spec = 1;
        }

        if (deprecated_glow_arg.found()) {
                Cmdline_deprecated_glow = 1;
        }

        if (deprecated_normal_arg.found()) {
                Cmdline_deprecated_normal = 1;
        }

        if (deprecated_env_arg.found()) {
                Cmdline_deprecated_env = 1;
        }

        if (deprecated_tbp_arg.found()) {
                Cmdline_deprecated_tbp = 1;
        }

        if (deprecated_jpgtga_arg.found()) {
                Cmdline_deprecated_jpgtga = 1;
        }

        if (deprecated_htl_arg.found()) {
                Cmdline_deprecated_nohtl = 1;
        }

        if (deprecated_brieflighting_arg.found()) {
                Cmdline_deprecated_brief_lighting = 1;
        }

        if (deprecated_missile_lighting_arg.found()) {
                Cmdline_deprecated_missile_lighting = true;
        }

        if (deprecated_cache_bitmaps_arg.found()) {
                Cmdline_deprecated_cache_bitmaps = true;
        }

        return true;
}

int parse_cmdline(int argc, char *argv[])
{
        // mprintf(("I got to parse_cmdline()!!\n"));

        os_init_cmdline(argc, argv);

        // --------------- Kazan -------------
        // If you're looking for the list of if (someparam.found()) {
        // cmdline_someparam = something; } look above at this function I did this
        // because of fred2_parse_cmdline()
        return SetCmdlineParams();
}

const char *get_param_desc(const char *flag_name)
{
        int i;
        int flag_size = sizeof(Flag);
        int num_flags = sizeof(exe_params) / flag_size;
        for (i = 0; i < num_flags; ++i) {
                if (!strcmp(flag_name, exe_params[i].name)) {
                        return exe_params[i].desc;
                }
        }
        return "UNKNOWN - FIXME!";
}