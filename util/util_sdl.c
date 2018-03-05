/*
Copyright (c) 2017 Steven Haid

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// XXX review
// XXX mouse flicker problem
// XXX comments on all APIs
// XXX document examples on how to use this

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#include <sys/stat.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>

#include "util_sdl.h"
#include "util_sdl_button_sound.h"
#include "util_png.h"
#include "util_jpeg.h"
#include "util_misc.h"

//
// defines
//

#define MAX_FONT_PTSIZE   200
#define MAX_EVENT_REG_TBL 1000

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

//
// typedefs
//

typedef struct {
    TTF_Font * font;
    int32_t    char_width;
    int32_t    char_height;
} sdl_font_t;

typedef struct {
    int32_t event_id;
    int32_t event_type;
    void * event_cx;
    rect_t disp_loc;
} sdl_event_reg_t;

//
// variables
//

static SDL_Window     * sdl_window;
static SDL_Renderer   * sdl_renderer;
static SDL_RendererInfo sdl_renderer_info;
static int32_t          sdl_win_width;
static int32_t          sdl_win_height;
static bool             sdl_win_minimized;
static bool             sdl_program_quit;

static Mix_Chunk      * sdl_button_sound;

static sdl_font_t       sdl_font[MAX_FONT_PTSIZE];
static char           * sdl_font_path;

static sdl_event_reg_t  sdl_event_reg_tbl[MAX_EVENT_REG_TBL];
static int32_t          sdl_event_max;
static sdl_event_t      sdl_push_ev;

static uint32_t         sdl_color_to_rgba[] = {
                            //    red           green          blue    alpha
                               (127 << 24) | (  0 << 16) | (255 << 8) | 255,     // PURPLE
                               (  0 << 24) | (  0 << 16) | (255 << 8) | 255,     // BLUE
                               (  0 << 24) | (255 << 16) | (255 << 8) | 255,     // LIGHT_BLUE
                               (  0 << 24) | (255 << 16) | (  0 << 8) | 255,     // GREEN
                               (255 << 24) | (255 << 16) | (  0 << 8) | 255,     // YELLOW
                               (255 << 24) | (128 << 16) | (  0 << 8) | 255,     // ORANGE
                               (255 << 24) | (105 << 16) | (180 << 8) | 255,     // PINK 
                               (255 << 24) | (  0 << 16) | (  0 << 8) | 255,     // RED         
                               (224 << 24) | (224 << 16) | (224 << 8) | 255,     // GRAY        
                               (255 << 24) | (255 << 16) | (255 << 8) | 255,     // WHITE       
                               (  0 << 24) | (  0 << 16) | (  0 << 8) | 255,     // BLACK       
                                        };

//
// prototypes
//

static void sdl_exit_handler(void);
static void sdl_set_color(int32_t color); 
static void sdl_font_init(int32_t ptsize);
static void sdl_pane_terminate(struct pane_list_head_s * pane_list_head, pane_cx_t * pane_cx);
static int32_t sdl_pane_move_speed(void);
static char * sdl_print_screen_filename(void);

// 
// inline procedures
//

static inline uint32_t _bswap32(uint32_t a)
{
  a = ((a & 0x000000FF) << 24) |
      ((a & 0x0000FF00) <<  8) |
      ((a & 0x00FF0000) >>  8) |
      ((a & 0xFF000000) >> 24);
  return a;
}

// -----------------  SDL INIT & MISC ROUTINES  ------------------------- 

int32_t sdl_init(int32_t *w, int32_t *h, bool resizeable)
{
    #define SDL_FLAGS (resizeable ? SDL_WINDOW_RESIZABLE : 0)
    #define MAX_FONT_SEARCH_PATH 3

    static char font_search_path[MAX_FONT_SEARCH_PATH][PATH_MAX];

    static const char * font_filename = "FreeMonoBold.ttf";

    // display available and current video drivers
    int num, i;
    num = SDL_GetNumVideoDrivers();
    DEBUG("Available Video Drivers: ");
    for (i = 0; i < num; i++) {
        DEBUG("   %s\n",  SDL_GetVideoDriver(i));
    }

    // initialize Simple DirectMedia Layer  (SDL)
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0) {
        ERROR("SDL_Init failed\n");
        return -1;
    }

    // create SDL Window and Renderer
    if (SDL_CreateWindowAndRenderer(*w, *h, SDL_FLAGS, &sdl_window, &sdl_renderer) != 0) {
        ERROR("SDL_CreateWindowAndRenderer failed\n");
        return -1;
    }

    // the size of the created window may be different than what was requested
    // - call sdl_poll_event to flush all of the initial events,
    //   and especially to process the SDL_WINDOWEVENT_SIZE_CHANGED event
    //   which will update sdl_win_width and sdl_win_height
    // - return the updated window width & height to caller
    sdl_poll_event();
    DEBUG("sdl_win_width=%d sdl_win_height=%d\n", sdl_win_width, sdl_win_height);
    *w = sdl_win_width;
    *h = sdl_win_height;

    // init button_sound
    if (Mix_OpenAudio( 22050, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        WARN("Mix_OpenAudio failed\n");
    } else {
        sdl_button_sound = Mix_QuickLoad_WAV(button_sound_wav);
        if (sdl_button_sound == NULL) {
            ERROR("Mix_QuickLoadWAV failed\n");
            return -1;
        }
        Mix_VolumeChunk(sdl_button_sound,MIX_MAX_VOLUME/2);
    }

    // initialize True Type Font
    if (TTF_Init() < 0) {
        ERROR("TTF_Init failed\n");
        return -1;
    }

    // determine sdl_font_path by searching for FreeMonoBold.ttf font file in possible locations
    // note - fonts can be installed using:
    //   sudo yum install gnu-free-mono-fonts       # rhel,centos,fedora
    //   sudo apt-get install fonts-freefont-ttf    # raspberrypi, ubuntu
    sprintf(font_search_path[0], "%s/%s", "/usr/share/fonts/gnu-free", font_filename);
    sprintf(font_search_path[1], "%s/%s", "/usr/share/fonts/truetype/freefont", font_filename);
    sprintf(font_search_path[2], "%s/%s/%s", getenv("HOME"), "my_fonts", font_filename);
    for (i = 0; i < MAX_FONT_SEARCH_PATH; i++) {
        struct stat buf;
        sdl_font_path = font_search_path[i];
        if (stat(sdl_font_path, &buf) == 0) {
            break;
        }
    }
    if (i == MAX_FONT_SEARCH_PATH) {
        ERROR("failed to locate font file\n");
        return -1;
    }
    DEBUG("using font %s\n", sdl_font_path);

    // currently the SDL Text Input feature is not being used here
    SDL_StopTextInput();

    // register exit handler
    atexit(sdl_exit_handler);

    // return success
    DEBUG("success\n");
    return 0;
}

static void sdl_exit_handler(void)
{
    int32_t i;
    
    if (sdl_button_sound) {
        Mix_FreeChunk(sdl_button_sound);
        Mix_CloseAudio();
    }

    for (i = 0; i < MAX_FONT_PTSIZE; i++) {
        if (sdl_font[i].font != NULL) {
            TTF_CloseFont(sdl_font[i].font);
        }
    }
    TTF_Quit();

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}

void sdl_get_max_texture_dim(int32_t * max_texture_dim)
{
    // return the max texture dimension to caller;
    // - SDL provides us with max_texture_width and max_texture_height, usually the same
    // - the min of max_texture_width/height is returned to caller
    // - this returned max_texture_dim is to be used by the caller to limit the maximum
    //   width and height args passed to sdl_create_texture()
    if (SDL_GetRendererInfo(sdl_renderer, &sdl_renderer_info) != 0) {
        ERROR("SDL_SDL_GetRendererInfo failed\n");
        *max_texture_dim = 0;
    }
    INFO("max_texture_width=%d  max_texture_height=%d\n",
         sdl_renderer_info.max_texture_width, sdl_renderer_info.max_texture_height);
    *max_texture_dim = min(sdl_renderer_info.max_texture_width, 
                           sdl_renderer_info.max_texture_height);
}

static void sdl_set_color(int32_t color)
{
    uint8_t r, g, b, a;
    uint32_t rgba;

    rgba = sdl_color_to_rgba[color];
    r = (rgba >> 24) & 0xff;
    g = (rgba >> 16) & 0xff;
    b = (rgba >>  8) & 0xff;
    a = (rgba      ) & 0xff;
    SDL_SetRenderDrawColor(sdl_renderer, r, g, b, a);
}

static void sdl_font_init(int32_t font_ptsize)
{
    // if this font has already been initialized then return
    if (sdl_font[font_ptsize].font != NULL) {
        return;
    }

    // open font for this font_ptsize,
    assert(sdl_font_path);
    sdl_font[font_ptsize].font = TTF_OpenFont(sdl_font_path, font_ptsize);
    if (sdl_font[font_ptsize].font == NULL) {
        FATAL("failed TTF_OpenFont(%s,%d)\n", sdl_font_path, font_ptsize);
    }

    // and init the char_width / char_height
    TTF_SizeText(sdl_font[font_ptsize].font, "X", &sdl_font[font_ptsize].char_width, &sdl_font[font_ptsize].char_height);
    DEBUG("font_ptsize=%d width=%d height=%d\n",
         font_ptsize, sdl_font[font_ptsize].char_width, sdl_font[font_ptsize].char_height);
}

// -----------------  PANE MANAGER  ------------------------------------- 

void sdl_pane_manager(void *display_cx,                        // optional, context
                      void (*display_start)(void *display_cx), // optional, called prior to pane handlers
                      void (*display_end)(void *display_cx),   // optional, called after pane handlers
                      int64_t redraw_interval_us,              // 0=continuous, -1=never, else us
                      int32_t count,                           // number of pane handler varargs that follow
                      ...)                                     // pane_handler, init_params, x_disp, y_disp, w, h, border_style
{
    #define FG_PANE_CX (TAILQ_LAST(&pane_list_head,pane_list_head_s))

    #define SDL_EVENT_PANE_SELECT            0xffffff01
    #define SDL_EVENT_PANE_MOVE              0xffffff02
    #define SDL_EVENT_PANE_TERMINATE         0xffffff03

    va_list ap; 
    pane_cx_t *pane_cx, *pane_cx_next;
    int32_t ret, i, win_width, win_height;
    bool redraw;
    uint64_t start_us;
    sdl_event_t * event;
    struct pane_list_head_s pane_list_head;
    rect_t loc_full_pane, loc_bar_move, loc_bar_terminate;
    bool call_pane_handler;

    // at least one pane must be provided 
    if (count <= 0) {
        FATAL("count %d must be > 0\n", count);
    }

    // create the pane(s)
    TAILQ_INIT(&pane_list_head);
    va_start(ap, count);
    for (i = 0; i < count; i++) {
        pane_handler_t pane_handler = va_arg(ap, void*);
        void         * init_params  = va_arg(ap, void*);
        int32_t        x_disp       = va_arg(ap, int32_t);
        int32_t        y_disp       = va_arg(ap, int32_t);
        int32_t        w_total      = va_arg(ap, int32_t);
        int32_t        h_total      = va_arg(ap, int32_t);
        int32_t        border_style = va_arg(ap, int32_t);
        sdl_pane_create(&pane_list_head, pane_handler, init_params, x_disp, y_disp, w_total, h_total, 
                        border_style, display_cx);
    }
    va_end(ap);

    // loop 
    while (true) {
        // if no panes or the global program_quit flag is set then break
        if (TAILQ_EMPTY(&pane_list_head) || sdl_program_quit) {
            break;
        }

        // init
        start_us = microsec_timer();
        sdl_display_init(&win_width, &win_height, NULL);  // XXX perhaps win_width/height should be used
        redraw = false;

        // call display_start, if supplied 
        if (display_start) {
            display_start(display_cx);
        }

        // call all pane_handler render
        for (pane_cx = TAILQ_FIRST(&pane_list_head); pane_cx != NULL; pane_cx = pane_cx_next) {
            pane_cx_next = TAILQ_NEXT(pane_cx, entries);

            pane_cx->pane = sdl_init_pane(pane_cx->x_disp, pane_cx->y_disp, pane_cx->w_total, pane_cx->h_total,
                                          pane_cx->border_style, 
                                          pane_cx == FG_PANE_CX ? GREEN : BLUE,  
                                          true,   // clear
                                          &loc_full_pane, &loc_bar_move, &loc_bar_terminate);

            sdl_register_event(&pane_cx->pane, &loc_full_pane, SDL_EVENT_PANE_SELECT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            sdl_register_event(&pane_cx->pane, &loc_bar_move, SDL_EVENT_PANE_MOVE, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
            sdl_register_event(&pane_cx->pane, &loc_bar_terminate, SDL_EVENT_PANE_TERMINATE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

            ret = pane_cx->pane_handler(pane_cx, PANE_HANDLER_REQ_RENDER, NULL, NULL);

            if (ret == PANE_HANDLER_RET_PANE_TERMINATE) {
                sdl_pane_terminate(&pane_list_head, pane_cx);
                redraw = true;
            } else if (ret == PANE_HANDLER_RET_DISPLAY_REDRAW) {
                redraw = true;
            }
        }

        // call display_end, if supplied 
        if (display_end) {
            display_end(display_cx);
        }

        // if redraw has been requested or global program_quit flag is set then continue
        if (redraw || sdl_program_quit) {
            continue;
        }

        // present the display
        sdl_display_present();

        // handle events
        while (true) {
            call_pane_handler = false;
            event = sdl_poll_event();

            if (event->event_cx == NULL) {
                if (IS_KEYBOARD_EVENT_ID(event->event_id)) {
                    if (event->event_id == SDL_EVENT_KEY_ALT + 'r') {         // ALT-r: redraw
                        redraw = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + 'q') {  // ALT-q: program quit
                        sdl_program_quit = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + 'x') {  // ALT-x: terminate pane
                        sdl_pane_terminate(&pane_list_head, FG_PANE_CX);
                        redraw = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + 'p') {  // ALT-p: print screen
                        sdl_print_screen(sdl_print_screen_filename(),true,NULL);
                        redraw = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + SDL_EVENT_KEY_UP_ARROW) {  // ALT+arrow: move pane
                        FG_PANE_CX->y_disp -= sdl_pane_move_speed();
                        redraw = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + SDL_EVENT_KEY_DOWN_ARROW) {
                        FG_PANE_CX->y_disp += sdl_pane_move_speed();
                        redraw = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + SDL_EVENT_KEY_LEFT_ARROW) {
                        FG_PANE_CX->x_disp -= sdl_pane_move_speed();
                        redraw = true;
                    } else if (event->event_id == SDL_EVENT_KEY_ALT + SDL_EVENT_KEY_RIGHT_ARROW) {
                        FG_PANE_CX->x_disp += sdl_pane_move_speed();
                        redraw = true;
                    } else {   // pass keyboard event to the pane_handler
                        call_pane_handler = true;
                    }
                } else if (event->event_id == SDL_EVENT_WIN_SIZE_CHANGE ||  
                           event->event_id == SDL_EVENT_WIN_RESTORED) 
                {
                    redraw = true;
                } else if (event->event_id == SDL_EVENT_QUIT) {
                    sdl_program_quit = true;
                }
            }

            if (event->event_cx != NULL) {
                if ((pane_cx_t*)event->event_cx != FG_PANE_CX) {
                    TAILQ_REMOVE(&pane_list_head, (pane_cx_t*)event->event_cx, entries);
                    TAILQ_INSERT_TAIL(&pane_list_head, (pane_cx_t*)event->event_cx, entries);
                    redraw = true;
                }
                if (event->event_id == SDL_EVENT_PANE_SELECT) {
                    redraw = true;
                } else if (event->event_id == SDL_EVENT_PANE_MOVE) {
                    FG_PANE_CX->x_disp += event->mouse_motion.delta_x;
                    FG_PANE_CX->y_disp += event->mouse_motion.delta_y;
                    redraw = true;
                } else if (event->event_id == SDL_EVENT_PANE_TERMINATE) {
                    sdl_pane_terminate(&pane_list_head, FG_PANE_CX);
                    redraw = true;
                } else {
                    call_pane_handler = true;
                }
            } 

            if (call_pane_handler) {
                ret = FG_PANE_CX->pane_handler(FG_PANE_CX, PANE_HANDLER_REQ_EVENT, NULL, event);
                if (ret == PANE_HANDLER_RET_PANE_TERMINATE) {
                    sdl_pane_terminate(&pane_list_head, FG_PANE_CX);
                    redraw = true;
                } else if (ret == PANE_HANDLER_RET_DISPLAY_REDRAW) {
                    redraw = true;
                }
            }

            if ((redraw) || 
                (redraw_interval_us != -1 && microsec_timer() >= start_us + redraw_interval_us) ||
                (sdl_program_quit))
            {
                break;
            }

            usleep(1000);
        }
    }

    // call all pane_handler terminate, and remove from pane_list
    while ((pane_cx = pane_list_head.tqh_first) != NULL) {
        sdl_pane_terminate(&pane_list_head, pane_cx);
    }
}

void sdl_pane_create(struct pane_list_head_s * pane_list_head, pane_handler_t pane_handler, void * init_params,
                     int32_t x_disp, int32_t y_disp, int32_t w_total, int32_t h_total, int32_t border_style,
                     void * display_cx)
{
    pane_cx_t * pane_cx;

    // alloc and init a pane_cx
    pane_cx = calloc(1, sizeof(pane_cx_t));
    pane_cx->display_cx     = display_cx;
    pane_cx->x_disp         = x_disp;
    pane_cx->y_disp         = y_disp;
    pane_cx->w_total        = w_total;
    pane_cx->h_total        = h_total;
    pane_cx->border_style   = border_style;
    pane_cx->pane           = sdl_get_pane(x_disp, y_disp, w_total, h_total, border_style);
    pane_cx->vars           = NULL;
    pane_cx->pane_handler   = pane_handler;
    pane_cx->pane_list_head = pane_list_head;

    // add the pane to the tail of pane_list
    TAILQ_INSERT_TAIL(pane_list_head, pane_cx, entries);

    // call the pane_handler init
    pane_handler(pane_cx, PANE_HANDLER_REQ_INITIALIZE, init_params, NULL);
}

static void sdl_pane_terminate(struct pane_list_head_s * pane_list_head, pane_cx_t * pane_cx)
{
    // remove from pane_list
    TAILQ_REMOVE(pane_list_head, pane_cx, entries);

    // call pane_handler terminate
    pane_cx->pane_handler(pane_cx, PANE_HANDLER_REQ_TERMINATE, NULL, NULL);

    // free the pane_cx
    free(pane_cx);
}

static int32_t sdl_pane_move_speed(void)
{
    int32_t  speed;
    uint64_t time_now, duration;
    static uint64_t time_last, time_start;

    time_now = microsec_timer();
    if (time_now - time_last > 250000) {
        time_start = time_now;
    }
    time_last = time_now;

    duration = time_now - time_start;
    speed = duration / 500000;
    if (speed < 1) speed = 1;
    if (speed > 20) speed = 20;

    return speed;
}

rect_t sdl_init_pane(int32_t x_disp, int32_t y_disp, int32_t w_total, int32_t h_total, 
                     int32_t border_style, int32_t border_color, bool clear,
                     rect_t * loc_full_pane, rect_t * loc_bar_move, rect_t * loc_bar_terminate)
{
    rect_t locz  = {0, 0, 0, 0};  // zero
    rect_t panez  = {0, 0, 0, 0};  // zero
    rect_t locdummy;

    if (loc_full_pane == NULL) {
        loc_full_pane = &locdummy;
    }
    if (loc_bar_move == NULL) {
        loc_bar_move = &locdummy;
    }
    if (loc_bar_terminate == NULL) {
        loc_bar_terminate = &locdummy;
    }

    if (border_style == PANE_BORDER_STYLE_STANDARD) {
        #define PANE_BORDER_WIDTH        2
        #define PANE_BAR_HEIGHT          20
        #define PANE_BAR_TERMINATE_WIDTH 20
        rect_t pane  = { x_disp+PANE_BORDER_WIDTH, y_disp+PANE_BAR_HEIGHT, w_total-2*PANE_BORDER_WIDTH, h_total-PANE_BAR_HEIGHT-PANE_BORDER_WIDTH};
        rect_t locf  = {-PANE_BORDER_WIDTH, -PANE_BAR_HEIGHT, w_total, h_total};   // full
        rect_t locb  = {-PANE_BORDER_WIDTH, -PANE_BAR_HEIGHT, w_total, PANE_BAR_HEIGHT};  // bar
        rect_t locbm = {-PANE_BORDER_WIDTH, -PANE_BAR_HEIGHT, w_total-PANE_BAR_TERMINATE_WIDTH, PANE_BAR_HEIGHT};  // bar-motion
        rect_t locbt = {-PANE_BORDER_WIDTH+w_total-PANE_BAR_TERMINATE_WIDTH-PANE_BORDER_WIDTH, -PANE_BAR_HEIGHT+PANE_BORDER_WIDTH,  // bar-term
                        PANE_BAR_TERMINATE_WIDTH, PANE_BAR_HEIGHT-2*PANE_BORDER_WIDTH};

        if (clear) {
            sdl_render_fill_rect(&pane, &locf, WHITE);
        }
        sdl_render_rect(&pane, &locf, PANE_BORDER_WIDTH, border_color);
        sdl_render_fill_rect(&pane, &locb, border_color);
        sdl_render_fill_rect(&pane, &locbt, RED);

        *loc_full_pane = locf;
        *loc_bar_move = locbm;
        *loc_bar_terminate = locbt;

        return pane;
    }

    if (border_style == PANE_BORDER_STYLE_MINIMAL) {
        #define PANE_BORDER_WIDTH        2
        rect_t pane  = { x_disp+PANE_BORDER_WIDTH, y_disp+PANE_BORDER_WIDTH, w_total-2*PANE_BORDER_WIDTH, h_total-2*PANE_BORDER_WIDTH};
        rect_t locf  = {-PANE_BORDER_WIDTH, -PANE_BORDER_WIDTH, w_total, h_total};   // full

        if (clear) {
            sdl_render_fill_rect(&pane, &locf, WHITE);
        }
        sdl_render_rect(&pane, &locf, PANE_BORDER_WIDTH, border_color);

        *loc_full_pane = locf;
        *loc_bar_move = locz;
        *loc_bar_terminate = locz;

        return pane;
    }

    if (border_style == PANE_BORDER_STYLE_NONE) {
        rect_t pane  = {x_disp, y_disp, w_total, h_total};
        rect_t locf  = {0, 0, w_total, h_total};   // full

        if (clear) {
            sdl_render_fill_rect(&pane, &locf, WHITE);
        }

        *loc_full_pane = locf;
        *loc_bar_move = locz;
        *loc_bar_terminate = locz;

        return pane;
    }

    FATAL("invalid border_style %d\n", border_style);
    return panez;
}

rect_t sdl_get_pane(int32_t x_disp, int32_t y_disp, int32_t w_total, int32_t h_total, int32_t border_style)
{
    rect_t panez  = {0, 0, 0, 0};  // zero

    switch (border_style) {
    case PANE_BORDER_STYLE_STANDARD: {
        rect_t pane  = { x_disp+PANE_BORDER_WIDTH, y_disp+PANE_BAR_HEIGHT, w_total-2*PANE_BORDER_WIDTH, h_total-PANE_BAR_HEIGHT-PANE_BORDER_WIDTH};
        return pane; }
    case PANE_BORDER_STYLE_MINIMAL: {
        rect_t pane  = { x_disp+PANE_BORDER_WIDTH, y_disp+PANE_BORDER_WIDTH, w_total-2*PANE_BORDER_WIDTH, h_total-2*PANE_BORDER_WIDTH};
        return pane; }
    case PANE_BORDER_STYLE_NONE: {
        rect_t pane  = {x_disp, y_disp, w_total, h_total};
        return pane; }
    }

    FATAL("invalid border_style %d\n", border_style);
    return panez;
}

// -----------------  DISPLAY INIT AND PRESENT  ------------------------- 

void sdl_display_init(int32_t * win_width, int32_t * win_height, bool * win_minimized)
{
    sdl_event_max = 0;

    sdl_set_color(WHITE);
    SDL_RenderClear(sdl_renderer);

    if (win_width) {
        *win_width = sdl_win_width;
    }
    if (win_height) {
        *win_height = sdl_win_height;
    }
    if (win_minimized) {
        *win_minimized = sdl_win_minimized;
    }
}

void sdl_display_present(void)
{
    SDL_RenderPresent(sdl_renderer);
}

// -----------------  FONT SUPPORT ROUTINES  ---------------------------- 

int32_t sdl_pane_cols(rect_t * pane, int32_t font_ptsize)
{
    sdl_font_init(font_ptsize);
    return pane->w / sdl_font[font_ptsize].char_width;
}

int32_t sdl_pane_rows(rect_t * pane, int32_t font_ptsize)
{
    sdl_font_init(font_ptsize);
    return pane->h / sdl_font[font_ptsize].char_height;
}

int32_t sdl_font_char_width(int32_t font_ptsize)
{
    sdl_font_init(font_ptsize);
    return sdl_font[font_ptsize].char_width;
}

int32_t sdl_font_char_height(int32_t font_ptsize)
{
    sdl_font_init(font_ptsize);
    return sdl_font[font_ptsize].char_height;
}

// -----------------  EVENT HANDLING  ----------------------------------- 

void sdl_register_event(rect_t * pane, rect_t * loc, int32_t event_id, int32_t event_type, void * event_cx)
{
    sdl_event_reg_t * e = &sdl_event_reg_tbl[sdl_event_max];

    if (sdl_event_max == MAX_EVENT_REG_TBL) {
        ERROR("sdl_event_reg_tbl is full\n");
        return;
    }

    if (loc->w == 0 || loc->h == 0) {
        return;
    }

    e->event_id = event_id;
    e->event_type = event_type;
    e->disp_loc.x = pane->x + loc->x;
    e->disp_loc.y = pane->y + loc->y;
    e->disp_loc.w = loc->w;
    e->disp_loc.h = loc->h;
    e->event_cx = event_cx;

    sdl_event_max++;
}

void sdl_render_text_and_register_event(rect_t * pane, int32_t x, int32_t y,
        int32_t font_ptsize, char * str, int32_t fg_color, int32_t bg_color, 
        int32_t event_id, int32_t event_type, void * event_cx)
{
    rect_t loc_clipped;
    loc_clipped = sdl_render_text(pane, x, y, font_ptsize, str, fg_color, bg_color);
    sdl_register_event(pane, &loc_clipped, event_id, event_type, event_cx);
}

void sdl_render_texture_and_register_event(rect_t * pane, int32_t x, int32_t y,
        texture_t texture, int32_t event_id, int32_t event_type, void * event_cx)
{
    rect_t loc_clipped;
    loc_clipped = sdl_render_texture(pane, x, y, texture);
    sdl_register_event(pane, &loc_clipped, event_id, event_type, event_cx);
}

sdl_event_t * sdl_poll_event(void)
{
    #define AT_POS(X,Y,pos) (((X) >= (pos).x) && \
                             ((X) < (pos).x + (pos).w) && \
                             ((Y) >= (pos).y) && \
                             ((Y) < (pos).y + (pos).h))

    #define SDL_WINDOWEVENT_STR(x) \
       ((x) == SDL_WINDOWEVENT_SHOWN        ? "SDL_WINDOWEVENT_SHOWN"        : \
        (x) == SDL_WINDOWEVENT_HIDDEN       ? "SDL_WINDOWEVENT_HIDDEN"       : \
        (x) == SDL_WINDOWEVENT_EXPOSED      ? "SDL_WINDOWEVENT_EXPOSED"      : \
        (x) == SDL_WINDOWEVENT_MOVED        ? "SDL_WINDOWEVENT_MOVED"        : \
        (x) == SDL_WINDOWEVENT_RESIZED      ? "SDL_WINDOWEVENT_RESIZED"      : \
        (x) == SDL_WINDOWEVENT_SIZE_CHANGED ? "SDL_WINDOWEVENT_SIZE_CHANGED" : \
        (x) == SDL_WINDOWEVENT_MINIMIZED    ? "SDL_WINDOWEVENT_MINIMIZED"    : \
        (x) == SDL_WINDOWEVENT_MAXIMIZED    ? "SDL_WINDOWEVENT_MAXIMIZED"    : \
        (x) == SDL_WINDOWEVENT_RESTORED     ? "SDL_WINDOWEVENT_RESTORED"     : \
        (x) == SDL_WINDOWEVENT_ENTER        ? "SDL_WINDOWEVENT_ENTER"        : \
        (x) == SDL_WINDOWEVENT_LEAVE        ? "SDL_WINDOWEVENT_LEAVE"        : \
        (x) == SDL_WINDOWEVENT_FOCUS_GAINED ? "SDL_WINDOWEVENT_FOCUS_GAINED" : \
        (x) == SDL_WINDOWEVENT_FOCUS_LOST   ? "SDL_WINDOWEVENT_FOCUS_LOST"   : \
        (x) == SDL_WINDOWEVENT_CLOSE        ? "SDL_WINDOWEVENT_CLOSE"        : \
                                              "????")

    #define MOUSE_BUTTON_STATE_NONE     0
    #define MOUSE_BUTTON_STATE_DOWN     1
    #define MOUSE_BUTTON_STATE_MOTION   2

    #define MOUSE_BUTTON_STATE_RESET \
        do { \
            mouse_button_state = MOUSE_BUTTON_STATE_NONE; \
            mouse_button_motion_event_id = SDL_EVENT_NONE; \
            mouse_button_motion_event_cx = NULL; \
            mouse_button_x = 0; \
            mouse_button_y = 0; \
        } while (0)

    SDL_Event ev;
    int32_t i;

    static sdl_event_t event;
    static struct {
        bool active;
        int32_t event_id;
        void * event_cx;
        int32_t x_start;
        int32_t y_start;
        int32_t x_last;
        int32_t y_last;
    } mouse_motion;

    bzero(&event, sizeof(event));

    // if a push event is pending then return that event;
    // the push event can be used to inject an event from another thread,
    // for example if another thread wishes to terminate the program it could
    // push the SDL_EVENT_QUIT
    if (sdl_push_ev.event_id != SDL_EVENT_NONE) {
        event = sdl_push_ev;
        sdl_push_ev.event_id = SDL_EVENT_NONE;
        __sync_synchronize();
        return &event;
    }

    while (true) {
        // get the next event, break out of loop if no event
        if (SDL_PollEvent(&ev) == 0) {
            break;
        }

        // process the SDL event, this code
        // - sets event and sdl_quit
        // - updates sdl_win_width, sdl_win_height, sdl_win_minimized
        switch (ev.type) {
        case SDL_MOUSEBUTTONDOWN: {
            DEBUG("MOUSE DOWN which=%d button=%s state=%s x=%d y=%d\n",
                   ev.button.which,
                   (ev.button.button == SDL_BUTTON_LEFT   ? "LEFT" :
                    ev.button.button == SDL_BUTTON_MIDDLE ? "MIDDLE" :
                    ev.button.button == SDL_BUTTON_RIGHT  ? "RIGHT" :
                                                            "???"),
                   (ev.button.state == SDL_PRESSED  ? "PRESSED" :
                    ev.button.state == SDL_RELEASED ? "RELEASED" :
                                                      "???"),
                   ev.button.x,
                   ev.button.y);

            // if not the left button then get out
            if (ev.button.button != SDL_BUTTON_LEFT) {
                break;
            }

            // clear mouse_motion 
            memset(&mouse_motion,0,sizeof(mouse_motion));

            // search for matching registered mouse_click or mouse_motion event
            for (i = sdl_event_max-1; i >= 0; i--) {
                if ((sdl_event_reg_tbl[i].event_type == SDL_EVENT_TYPE_MOUSE_CLICK ||
                     sdl_event_reg_tbl[i].event_type == SDL_EVENT_TYPE_MOUSE_MOTION) &&
                    (AT_POS(ev.button.x, ev.button.y, sdl_event_reg_tbl[i].disp_loc))) 
                {
                    break;
                }
            }
            if (i < 0) {
                break;
            }

            // we've found a registered MOUSE_CLICK or MOUSE_MOTION event;
            // if it is a MOUSE_CLICK event then
            //   return the event to caller
            // else
            //   initialize mouse_motion state, which will be used in 
            //     the case SDL_MOUSEMOTION below
            // endif
            if (sdl_event_reg_tbl[i].event_type == SDL_EVENT_TYPE_MOUSE_CLICK) {
                event.event_id = sdl_event_reg_tbl[i].event_id;
                event.event_cx = sdl_event_reg_tbl[i].event_cx;
            } else {
                mouse_motion.active = true;
                mouse_motion.event_id = sdl_event_reg_tbl[i].event_id;
                mouse_motion.event_cx = sdl_event_reg_tbl[i].event_cx;
                mouse_motion.x_start = ev.button.x;
                mouse_motion.y_start = ev.button.y;
                mouse_motion.x_last  = ev.button.x;
                mouse_motion.y_last  = ev.button.y;

                event.event_id = mouse_motion.event_id;
                event.event_cx = mouse_motion.event_cx;
                event.mouse_motion.delta_x = 0;
                event.mouse_motion.delta_y = 0;
            }
            break; }

        case SDL_MOUSEBUTTONUP: {
            DEBUG("MOUSE UP which=%d button=%s state=%s x=%d y=%d\n",
                   ev.button.which,
                   (ev.button.button == SDL_BUTTON_LEFT   ? "LEFT" :
                    ev.button.button == SDL_BUTTON_MIDDLE ? "MIDDLE" :
                    ev.button.button == SDL_BUTTON_RIGHT  ? "RIGHT" :
                                                            "???"),
                   (ev.button.state == SDL_PRESSED  ? "PRESSED" :
                    ev.button.state == SDL_RELEASED ? "RELEASED" :
                                                      "???"),
                   ev.button.x,
                   ev.button.y);

            // if not the left button then get out
            if (ev.button.button != SDL_BUTTON_LEFT) {
                break;
            }

            // clear mouse_motion 
            memset(&mouse_motion,0,sizeof(mouse_motion));
            break; }

        case SDL_MOUSEMOTION: {
            // if mouse_motion is not active then get out
            if (!mouse_motion.active) {
                break;
            }

#if 0 //XXX evaluate why this is ifdefed out
            // search the sdl_event_reg_tbl to ensure the mouse_motion event is still registered
            for (i = sdl_event_max-1; i >= 0; i--) {
                if ((sdl_event_reg_tbl[i].event_type == SDL_EVENT_TYPE_MOUSE_MOTION) &&
                    (AT_POS(mouse_motion.x_start, mouse_motion.y_start, sdl_event_reg_tbl[i].disp_loc))) 
                {
                    break;
                }
            }

            // if the mouse_motion event is no longer registered or 
            //    the registration has changed 
            // then
            //   clear mouse_motion, and get out
            // endif
            if (i < 0 ||
                sdl_event_reg_tbl[i].event_id != mouse_motion.event_id ||
                sdl_event_reg_tbl[i].event_cx != mouse_motion.event_cx)
            {
                memset(&mouse_motion,0,sizeof(mouse_motion));
                break;
            }
#endif
            

            // get all additional pending mouse motion events, and sum the motion
            event.event_id = mouse_motion.event_id;
            event.event_cx = mouse_motion.event_cx;
            event.mouse_motion.delta_x = 0;
            event.mouse_motion.delta_y = 0;
            do {
                event.mouse_motion.delta_x += ev.motion.x - mouse_motion.x_last;
                event.mouse_motion.delta_y += ev.motion.y - mouse_motion.y_last;
                mouse_motion.x_last = ev.motion.x;
                mouse_motion.y_last = ev.motion.y;
            } while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) == 1);
            break; }

        case SDL_MOUSEWHEEL: {
            int32_t mouse_x, mouse_y;

            // check if mouse wheel event is registered for the location of the mouse
            SDL_GetMouseState(&mouse_x, &mouse_y);
            for (i = sdl_event_max-1; i >= 0; i--) {
                if (sdl_event_reg_tbl[i].event_type == SDL_EVENT_TYPE_MOUSE_WHEEL &&
                    AT_POS(mouse_x, mouse_y, sdl_event_reg_tbl[i].disp_loc)) 
                {
                    break;
                }
            }

            // if did not find a registered mouse wheel event then get out
            if (i < 0) {
                break;
            }

            // set return event
            event.event_id = sdl_event_reg_tbl[i].event_id;
            event.event_cx = sdl_event_reg_tbl[i].event_cx;
            event.mouse_wheel.delta_x = ev.wheel.x;
            event.mouse_wheel.delta_y = ev.wheel.y;
            break; }

        case SDL_KEYDOWN: {
            int32_t  key = ev.key.keysym.sym;
            bool     shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            bool     ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
            bool     alt = (ev.key.keysym.mod & KMOD_ALT) != 0;
            int32_t  event_id = SDL_EVENT_NONE;

            // map key to event_id
            if (key < 128) {
                event_id = key;
                if (shift) {  // XXX other keys, try using a table
                    if (event_id >= 'a' && event_id <= 'z') {
                        event_id = toupper(event_id);
                    } else if (event_id >= '0' && event_id <= '9') {
                        event_id = ")!@#$%^&*("[event_id-'0'];
                    } else if (event_id == '-') {
                        event_id = '_';
                    } else if (event_id == '=') {
                        event_id = '+';
                    } else if (event_id == ',') {
                        event_id = '<';
                    } else if (event_id == '.') {
                        event_id = '>';
                    } else if (event_id == '/') {
                        event_id = '?';
                    }
                }
            } else if (key == SDLK_INSERT) {
                event_id = SDL_EVENT_KEY_INSERT;
            } else if (key == SDLK_HOME) {
                event_id = SDL_EVENT_KEY_HOME;
            } else if (key == SDLK_END) {
                event_id = SDL_EVENT_KEY_END;
            } else if (key == SDLK_PAGEUP) {
                event_id= SDL_EVENT_KEY_PGUP;
            } else if (key == SDLK_PAGEDOWN) {
                event_id = SDL_EVENT_KEY_PGDN;
            } else if (key == SDLK_UP) {
                event_id = SDL_EVENT_KEY_UP_ARROW;
            } else if (key == SDLK_DOWN) {
                event_id = SDL_EVENT_KEY_DOWN_ARROW;
            } else if (key == SDLK_LEFT) {
                event_id = SDL_EVENT_KEY_LEFT_ARROW;
            } else if (key == SDLK_RIGHT) {
                event_id = SDL_EVENT_KEY_RIGHT_ARROW;
            }

            // adjust event_id if ctrl and/or alt is active
            if (event_id != SDL_EVENT_NONE && ctrl) {
                event_id += SDL_EVENT_KEY_CTRL;
            }
            if (event_id != SDL_EVENT_NONE && alt) {
                event_id += SDL_EVENT_KEY_ALT;
            }

            // if there is a keyboard event_id then return it
            if (event_id != SDL_EVENT_NONE) {
                event.event_id = event_id;
            }
            break; }

        case SDL_KEYUP:
            break;

       case SDL_TEXTINPUT:
            break;

       case SDL_WINDOWEVENT: {
            DEBUG("got event SDL_WINOWEVENT - %s\n", SDL_WINDOWEVENT_STR(ev.window.event));
            switch (ev.window.event)  {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                sdl_win_width = ev.window.data1;
                sdl_win_height = ev.window.data2;
                event.event_id = SDL_EVENT_WIN_SIZE_CHANGE;
                break;
            case SDL_WINDOWEVENT_MINIMIZED:
                sdl_win_minimized = true;
                event.event_id = SDL_EVENT_WIN_MINIMIZED;
                break;
            case SDL_WINDOWEVENT_RESTORED:
                sdl_win_minimized = false;
                event.event_id = SDL_EVENT_WIN_RESTORED;
                break;
            }
            break; }

        case SDL_QUIT: {
            DEBUG("got event SDL_QUIT\n");
            event.event_id = SDL_EVENT_QUIT;
            break; }

        default: {
            DEBUG("got event %d - not supported\n", ev.type);
            break; }
        }

        // break if event is set
        if (event.event_id != SDL_EVENT_NONE) {
            break; 
        }
    }

    return &event;
}

// this function is thread-safe
void sdl_push_event(sdl_event_t *ev) 
{
    sdl_push_ev = *ev;
    __sync_synchronize();
    while (sdl_push_ev.event_id != SDL_EVENT_NONE) {
        usleep(1000);
    }
}

void sdl_play_event_sound(void)   
{
    if (sdl_button_sound) {
        Mix_PlayChannel(-1, sdl_button_sound, 0);
    }
}

// -----------------  RENDER TEXT  -------------------------------------- 

rect_t sdl_render_text(rect_t * pane, int32_t x, int32_t y, int32_t font_ptsize, char * str, 
                       int32_t fg_color, int32_t bg_color)
{
    texture_t texture;
    int32_t   width, height;
    rect_t    loc, loc_clipped = {0,0,0,0};
    
    // create the text texture
    texture =  sdl_create_text_texture(fg_color, bg_color, font_ptsize, str);
    if (texture == NULL) {
        ERROR("sdl_create_text_texture failed\n");
        return loc_clipped;
    }
    sdl_query_texture(texture, &width, &height);

    // determine the location within the pane that this
    // texture is to be rendered, if x or y is negative then
    // wrap to other end of the pane
#if 0 // XXX
    if (x < 0) {
        x += pane->w;
    }
    if (y < 0) {
        y += pane->h;
    }
#endif
    loc.x = x;
    loc.y = y; 
    loc.w = width;
    loc.h = height;

    // render the texture
    loc_clipped = sdl_render_scaled_texture(pane, &loc, texture);

    // clean up
    sdl_destroy_texture(texture);

    // return the location of the text (possibly clipped), within the pane
    return loc_clipped;
}

// XXX what is str len is zero
void sdl_render_printf(rect_t * pane, int32_t x, int32_t y, int32_t font_ptsize,
                       int32_t fg_color, int32_t bg_color, char * fmt, ...) 
{
    char str[1000];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);
    
    sdl_render_text(pane, x, y, font_ptsize, str, fg_color, bg_color);
}

// -----------------  RENDER RECTANGLES & LINES  ------------------------ 

// XXX sdl_render_rect, and sdl_render_fill_rect needs to support clipping

void sdl_render_rect(rect_t * pane, rect_t * loc, int32_t line_width, int32_t color)
{
    SDL_Rect rect;
    int32_t i;

    sdl_set_color(color);

    rect.x = pane->x + loc->x;
    rect.y = pane->y + loc->y;
    rect.w = loc->w;
    rect.h = loc->h;

    for (i = 0; i < line_width; i++) {
        SDL_RenderDrawRect(sdl_renderer, &rect);
        if (rect.w < 2 || rect.h < 2) {
            break;
        }
        rect.x += 1;
        rect.y += 1;
        rect.w -= 2;
        rect.h -= 2;
    }
}

void sdl_render_fill_rect(rect_t * pane, rect_t * loc, int32_t color)
{
    SDL_Rect rect;

    sdl_set_color(color);

    rect.x = pane->x + loc->x;
    rect.y = pane->y + loc->y;
    rect.w = loc->w;
    rect.h = loc->h;
    SDL_RenderFillRect(sdl_renderer, &rect);
}

void sdl_render_line(rect_t * pane, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color)
{
    point_t points[2] = { {x1,y1}, {x2,y2} };
    sdl_render_lines(pane, points, 2, color);
}

void sdl_render_lines(rect_t * pane, point_t * points, int32_t count, int32_t color)
{
    #define MAX_SDL_POINTS 1000

    SDL_Point sdl_points[MAX_SDL_POINTS];
    int32_t i, max=0;

    if (count <= 1) {
        return;
    }

    sdl_set_color(color);

    for (i = 0; i < count; i++) {
        if (points[i].x < 0 || points[i].x >= pane->w || points[i].y < 0 || points[i].y >= pane->h) {
            // XXX sdl_render_lines should compute intersection with pane border, 
            //     but this is too complicated for now
            if (max) {
                SDL_RenderDrawLines(sdl_renderer, sdl_points, max);
                max = 0;
            }
            continue;
        }

        sdl_points[max].x = points[i].x + pane->x;
        sdl_points[max].y = points[i].y + pane->y;
        max++;

        if (max == MAX_SDL_POINTS) {
            SDL_RenderDrawLines(sdl_renderer, sdl_points, max);
            sdl_points[0].x = sdl_points[MAX_SDL_POINTS-1].x;
            sdl_points[0].y = sdl_points[MAX_SDL_POINTS-1].y;
            max = 1;
        }
    }

    if (max) {
        SDL_RenderDrawLines(sdl_renderer, sdl_points, max);
    }
}

void sdl_render_circle(rect_t * pane, int32_t x_center, int32_t y_center, int32_t radius,
                       int32_t line_width, int32_t color)
{
    int32_t count = 0, i, angle, x, y;
    SDL_Point points[370];

    static int32_t sin_table[370];
    static int32_t cos_table[370];
    static bool first_call = true;

    // on first call make table of sin and cos indexed by degrees
    if (first_call) {
        for (angle = 0; angle < 362; angle++) {
            sin_table[angle] = sin(angle*(2*M_PI/360)) * (1<<18);
            cos_table[angle] = cos(angle*(2*M_PI/360)) * (1<<18);
        }
        first_call = false;
    }

    // set the color
    sdl_set_color(color);

    // loop over line_width
    for (i = 0; i < line_width; i++) {
        // draw circle, and clip to pane dimensions
        for (angle = 0; angle < 362; angle++) {
            x = x_center + (((int64_t)radius * sin_table[angle]) >> 18);
            y = y_center + (((int64_t)radius * cos_table[angle]) >> 18);
            if (x < 0 || x >= pane->w || y < 0 || y >= pane->h) {
                if (count) {
                    SDL_RenderDrawLines(sdl_renderer, points, count);
                    count = 0;
                }
                continue;
            }
            points[count].x = pane->x + x;
            points[count].y = pane->y + y;
            count++;
        }
        if (count) {
            SDL_RenderDrawLines(sdl_renderer, points, count);
            count = 0;
        }

        // reduce radius by 1
        radius--;
        if (radius < 0) {
            break;
        }
    }
}

void sdl_render_point(rect_t * pane, int32_t x, int32_t y, int32_t color, int32_t point_size)
{
    point_t point = {x,y};
    sdl_render_points(pane, &point, 1, color, point_size);
}

void sdl_render_points(rect_t * pane, point_t * points, int32_t count, int32_t color, int32_t point_size)
{
    #define MAX_SDL_POINTS 1000

    static struct point_extend_s {
        int32_t max;
        struct point_extend_offset_s {
            int32_t x;
            int32_t y;
        } offset[300];
    } point_extend[10] = {
    { 1, {
        {0,0}, 
            } },
    { 5, {
        {-1,0}, 
        {0,-1}, {0,0}, {0,1}, 
        {1,0}, 
            } },
    { 21, {
        {-2,-1}, {-2,0}, {-2,1}, 
        {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, 
        {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, 
        {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, 
        {2,-1}, {2,0}, {2,1}, 
            } },
    { 37, {
        {-3,-1}, {-3,0}, {-3,1}, 
        {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, 
        {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, 
        {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, 
        {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, 
        {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, 
        {3,-1}, {3,0}, {3,1}, 
            } },
    { 61, {
        {-4,-1}, {-4,0}, {-4,1}, 
        {-3,-3}, {-3,-2}, {-3,-1}, {-3,0}, {-3,1}, {-3,2}, {-3,3}, 
        {-2,-3}, {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, {-2,3}, 
        {-1,-4}, {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, {-1,4}, 
        {0,-4}, {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, 
        {1,-4}, {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, 
        {2,-3}, {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, {2,3}, 
        {3,-3}, {3,-2}, {3,-1}, {3,0}, {3,1}, {3,2}, {3,3}, 
        {4,-1}, {4,0}, {4,1}, 
            } },
    { 89, {
        {-5,-1}, {-5,0}, {-5,1}, 
        {-4,-3}, {-4,-2}, {-4,-1}, {-4,0}, {-4,1}, {-4,2}, {-4,3}, 
        {-3,-4}, {-3,-3}, {-3,-2}, {-3,-1}, {-3,0}, {-3,1}, {-3,2}, {-3,3}, {-3,4}, 
        {-2,-4}, {-2,-3}, {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, {-2,3}, {-2,4}, 
        {-1,-5}, {-1,-4}, {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, {-1,4}, {-1,5}, 
        {0,-5}, {0,-4}, {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, 
        {1,-5}, {1,-4}, {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, 
        {2,-4}, {2,-3}, {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, {2,3}, {2,4}, 
        {3,-4}, {3,-3}, {3,-2}, {3,-1}, {3,0}, {3,1}, {3,2}, {3,3}, {3,4}, 
        {4,-3}, {4,-2}, {4,-1}, {4,0}, {4,1}, {4,2}, {4,3}, 
        {5,-1}, {5,0}, {5,1}, 
            } },
    { 121, {
        {-6,-1}, {-6,0}, {-6,1}, 
        {-5,-3}, {-5,-2}, {-5,-1}, {-5,0}, {-5,1}, {-5,2}, {-5,3}, 
        {-4,-4}, {-4,-3}, {-4,-2}, {-4,-1}, {-4,0}, {-4,1}, {-4,2}, {-4,3}, {-4,4}, 
        {-3,-5}, {-3,-4}, {-3,-3}, {-3,-2}, {-3,-1}, {-3,0}, {-3,1}, {-3,2}, {-3,3}, {-3,4}, {-3,5}, 
        {-2,-5}, {-2,-4}, {-2,-3}, {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, {-2,3}, {-2,4}, {-2,5}, 
        {-1,-6}, {-1,-5}, {-1,-4}, {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, {-1,4}, {-1,5}, {-1,6}, 
        {0,-6}, {0,-5}, {0,-4}, {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, 
        {1,-6}, {1,-5}, {1,-4}, {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6}, 
        {2,-5}, {2,-4}, {2,-3}, {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, {2,3}, {2,4}, {2,5}, 
        {3,-5}, {3,-4}, {3,-3}, {3,-2}, {3,-1}, {3,0}, {3,1}, {3,2}, {3,3}, {3,4}, {3,5}, 
        {4,-4}, {4,-3}, {4,-2}, {4,-1}, {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, 
        {5,-3}, {5,-2}, {5,-1}, {5,0}, {5,1}, {5,2}, {5,3}, 
        {6,-1}, {6,0}, {6,1}, 
            } },
    { 177, {
        {-7,-2}, {-7,-1}, {-7,0}, {-7,1}, {-7,2}, 
        {-6,-4}, {-6,-3}, {-6,-2}, {-6,-1}, {-6,0}, {-6,1}, {-6,2}, {-6,3}, {-6,4}, 
        {-5,-5}, {-5,-4}, {-5,-3}, {-5,-2}, {-5,-1}, {-5,0}, {-5,1}, {-5,2}, {-5,3}, {-5,4}, {-5,5}, 
        {-4,-6}, {-4,-5}, {-4,-4}, {-4,-3}, {-4,-2}, {-4,-1}, {-4,0}, {-4,1}, {-4,2}, {-4,3}, {-4,4}, {-4,5}, {-4,6}, 
        {-3,-6}, {-3,-5}, {-3,-4}, {-3,-3}, {-3,-2}, {-3,-1}, {-3,0}, {-3,1}, {-3,2}, {-3,3}, {-3,4}, {-3,5}, {-3,6}, 
        {-2,-7}, {-2,-6}, {-2,-5}, {-2,-4}, {-2,-3}, {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, {-2,3}, {-2,4}, {-2,5}, {-2,6}, {-2,7}, 
        {-1,-7}, {-1,-6}, {-1,-5}, {-1,-4}, {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, {-1,4}, {-1,5}, {-1,6}, {-1,7}, 
        {0,-7}, {0,-6}, {0,-5}, {0,-4}, {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7}, 
        {1,-7}, {1,-6}, {1,-5}, {1,-4}, {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6}, {1,7}, 
        {2,-7}, {2,-6}, {2,-5}, {2,-4}, {2,-3}, {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, {2,3}, {2,4}, {2,5}, {2,6}, {2,7}, 
        {3,-6}, {3,-5}, {3,-4}, {3,-3}, {3,-2}, {3,-1}, {3,0}, {3,1}, {3,2}, {3,3}, {3,4}, {3,5}, {3,6}, 
        {4,-6}, {4,-5}, {4,-4}, {4,-3}, {4,-2}, {4,-1}, {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, {4,5}, {4,6}, 
        {5,-5}, {5,-4}, {5,-3}, {5,-2}, {5,-1}, {5,0}, {5,1}, {5,2}, {5,3}, {5,4}, {5,5}, 
        {6,-4}, {6,-3}, {6,-2}, {6,-1}, {6,0}, {6,1}, {6,2}, {6,3}, {6,4}, 
        {7,-2}, {7,-1}, {7,0}, {7,1}, {7,2}, 
            } },
    { 221, {
        {-8,-2}, {-8,-1}, {-8,0}, {-8,1}, {-8,2}, 
        {-7,-4}, {-7,-3}, {-7,-2}, {-7,-1}, {-7,0}, {-7,1}, {-7,2}, {-7,3}, {-7,4}, 
        {-6,-5}, {-6,-4}, {-6,-3}, {-6,-2}, {-6,-1}, {-6,0}, {-6,1}, {-6,2}, {-6,3}, {-6,4}, {-6,5}, 
        {-5,-6}, {-5,-5}, {-5,-4}, {-5,-3}, {-5,-2}, {-5,-1}, {-5,0}, {-5,1}, {-5,2}, {-5,3}, {-5,4}, {-5,5}, {-5,6}, 
        {-4,-7}, {-4,-6}, {-4,-5}, {-4,-4}, {-4,-3}, {-4,-2}, {-4,-1}, {-4,0}, {-4,1}, {-4,2}, {-4,3}, {-4,4}, {-4,5}, {-4,6}, {-4,7}, 
        {-3,-7}, {-3,-6}, {-3,-5}, {-3,-4}, {-3,-3}, {-3,-2}, {-3,-1}, {-3,0}, {-3,1}, {-3,2}, {-3,3}, {-3,4}, {-3,5}, {-3,6}, {-3,7}, 
        {-2,-8}, {-2,-7}, {-2,-6}, {-2,-5}, {-2,-4}, {-2,-3}, {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, {-2,3}, {-2,4}, {-2,5}, {-2,6}, {-2,7}, {-2,8}, 
        {-1,-8}, {-1,-7}, {-1,-6}, {-1,-5}, {-1,-4}, {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, {-1,4}, {-1,5}, {-1,6}, {-1,7}, {-1,8}, 
        {0,-8}, {0,-7}, {0,-6}, {0,-5}, {0,-4}, {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7}, {0,8}, 
        {1,-8}, {1,-7}, {1,-6}, {1,-5}, {1,-4}, {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6}, {1,7}, {1,8}, 
        {2,-8}, {2,-7}, {2,-6}, {2,-5}, {2,-4}, {2,-3}, {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, {2,3}, {2,4}, {2,5}, {2,6}, {2,7}, {2,8}, 
        {3,-7}, {3,-6}, {3,-5}, {3,-4}, {3,-3}, {3,-2}, {3,-1}, {3,0}, {3,1}, {3,2}, {3,3}, {3,4}, {3,5}, {3,6}, {3,7}, 
        {4,-7}, {4,-6}, {4,-5}, {4,-4}, {4,-3}, {4,-2}, {4,-1}, {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, {4,5}, {4,6}, {4,7}, 
        {5,-6}, {5,-5}, {5,-4}, {5,-3}, {5,-2}, {5,-1}, {5,0}, {5,1}, {5,2}, {5,3}, {5,4}, {5,5}, {5,6}, 
        {6,-5}, {6,-4}, {6,-3}, {6,-2}, {6,-1}, {6,0}, {6,1}, {6,2}, {6,3}, {6,4}, {6,5}, 
        {7,-4}, {7,-3}, {7,-2}, {7,-1}, {7,0}, {7,1}, {7,2}, {7,3}, {7,4}, 
        {8,-2}, {8,-1}, {8,0}, {8,1}, {8,2}, 
            } },
    { 277, {
        {-9,-2}, {-9,-1}, {-9,0}, {-9,1}, {-9,2}, 
        {-8,-4}, {-8,-3}, {-8,-2}, {-8,-1}, {-8,0}, {-8,1}, {-8,2}, {-8,3}, {-8,4}, 
        {-7,-6}, {-7,-5}, {-7,-4}, {-7,-3}, {-7,-2}, {-7,-1}, {-7,0}, {-7,1}, {-7,2}, {-7,3}, {-7,4}, {-7,5}, {-7,6}, 
        {-6,-7}, {-6,-6}, {-6,-5}, {-6,-4}, {-6,-3}, {-6,-2}, {-6,-1}, {-6,0}, {-6,1}, {-6,2}, {-6,3}, {-6,4}, {-6,5}, {-6,6}, {-6,7}, 
        {-5,-7}, {-5,-6}, {-5,-5}, {-5,-4}, {-5,-3}, {-5,-2}, {-5,-1}, {-5,0}, {-5,1}, {-5,2}, {-5,3}, {-5,4}, {-5,5}, {-5,6}, {-5,7}, 
        {-4,-8}, {-4,-7}, {-4,-6}, {-4,-5}, {-4,-4}, {-4,-3}, {-4,-2}, {-4,-1}, {-4,0}, {-4,1}, {-4,2}, {-4,3}, {-4,4}, {-4,5}, {-4,6}, {-4,7}, {-4,8}, 
        {-3,-8}, {-3,-7}, {-3,-6}, {-3,-5}, {-3,-4}, {-3,-3}, {-3,-2}, {-3,-1}, {-3,0}, {-3,1}, {-3,2}, {-3,3}, {-3,4}, {-3,5}, {-3,6}, {-3,7}, {-3,8}, 
        {-2,-9}, {-2,-8}, {-2,-7}, {-2,-6}, {-2,-5}, {-2,-4}, {-2,-3}, {-2,-2}, {-2,-1}, {-2,0}, {-2,1}, {-2,2}, {-2,3}, {-2,4}, {-2,5}, {-2,6}, {-2,7}, {-2,8}, {-2,9}, 
        {-1,-9}, {-1,-8}, {-1,-7}, {-1,-6}, {-1,-5}, {-1,-4}, {-1,-3}, {-1,-2}, {-1,-1}, {-1,0}, {-1,1}, {-1,2}, {-1,3}, {-1,4}, {-1,5}, {-1,6}, {-1,7}, {-1,8}, {-1,9}, 
        {0,-9}, {0,-8}, {0,-7}, {0,-6}, {0,-5}, {0,-4}, {0,-3}, {0,-2}, {0,-1}, {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7}, {0,8}, {0,9}, 
        {1,-9}, {1,-8}, {1,-7}, {1,-6}, {1,-5}, {1,-4}, {1,-3}, {1,-2}, {1,-1}, {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6}, {1,7}, {1,8}, {1,9}, 
        {2,-9}, {2,-8}, {2,-7}, {2,-6}, {2,-5}, {2,-4}, {2,-3}, {2,-2}, {2,-1}, {2,0}, {2,1}, {2,2}, {2,3}, {2,4}, {2,5}, {2,6}, {2,7}, {2,8}, {2,9}, 
        {3,-8}, {3,-7}, {3,-6}, {3,-5}, {3,-4}, {3,-3}, {3,-2}, {3,-1}, {3,0}, {3,1}, {3,2}, {3,3}, {3,4}, {3,5}, {3,6}, {3,7}, {3,8}, 
        {4,-8}, {4,-7}, {4,-6}, {4,-5}, {4,-4}, {4,-3}, {4,-2}, {4,-1}, {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, {4,5}, {4,6}, {4,7}, {4,8}, 
        {5,-7}, {5,-6}, {5,-5}, {5,-4}, {5,-3}, {5,-2}, {5,-1}, {5,0}, {5,1}, {5,2}, {5,3}, {5,4}, {5,5}, {5,6}, {5,7}, 
        {6,-7}, {6,-6}, {6,-5}, {6,-4}, {6,-3}, {6,-2}, {6,-1}, {6,0}, {6,1}, {6,2}, {6,3}, {6,4}, {6,5}, {6,6}, {6,7}, 
        {7,-6}, {7,-5}, {7,-4}, {7,-3}, {7,-2}, {7,-1}, {7,0}, {7,1}, {7,2}, {7,3}, {7,4}, {7,5}, {7,6}, 
        {8,-4}, {8,-3}, {8,-2}, {8,-1}, {8,0}, {8,1}, {8,2}, {8,3}, {8,4}, 
        {9,-2}, {9,-1}, {9,0}, {9,1}, {9,2}, 
            } },
                };

    int32_t i, j, x, y;
    SDL_Point sdl_points[MAX_SDL_POINTS];
    int32_t sdl_points_count = 0;
    struct point_extend_s * pe = &point_extend[point_size];
    struct point_extend_offset_s * peo = pe->offset;

    if (count < 0) {
        return;
    }
    if (point_size < 0 || point_size > 9) {
        return;
    }

    sdl_set_color(color);

    for (i = 0; i < count; i++) {
        for (j = 0; j < pe->max; j++) {
            x = points[i].x + peo[j].x;
            y = points[i].y + peo[j].y;
            if (x < 0 || x >= pane->w || y < 0 || y >= pane->h) {
                continue;
            }
            sdl_points[sdl_points_count].x = pane->x + x;
            sdl_points[sdl_points_count].y = pane->y + y;
            sdl_points_count++;

            if (sdl_points_count == MAX_SDL_POINTS) {
                SDL_RenderDrawPoints(sdl_renderer, sdl_points, sdl_points_count);
                sdl_points_count = 0;
            }
        }
    }

    if (sdl_points_count > 0) {
        SDL_RenderDrawPoints(sdl_renderer, sdl_points, sdl_points_count);
        sdl_points_count = 0;
    }
}

// -----------------  RENDER USING TEXTURES  ---------------------------- 

texture_t sdl_create_texture(int32_t w, int32_t h)
{
    SDL_Texture * texture;

    texture = SDL_CreateTexture(sdl_renderer,
                                SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                w, h);
    if (texture == NULL) {
        ERROR("failed to allocate texture, %s\n", SDL_GetError());
        return NULL;
    }

    return (texture_t)texture;
}

texture_t sdl_create_texture_from_pane_pixels(rect_t * pane)
{
    texture_t texture;
    int32_t ret;
    uint8_t * pixels;
    SDL_Rect rect = {pane->x, pane->y, pane->w, pane->h};

    // allocate memory for the pixels
    pixels = calloc(1, rect.h * rect.w * BYTES_PER_PIXEL);
    if (pixels == NULL) {
        ERROR("allocate pixels failed\n");
        return NULL;
    }

    // read the pixels
    ret = SDL_RenderReadPixels(sdl_renderer, 
                               &rect,  
                               SDL_PIXELFORMAT_ABGR8888, 
                               pixels, 
                               rect.w * BYTES_PER_PIXEL);
    if (ret < 0) {
        ERROR("SDL_RenderReadPixels, %s\n", SDL_GetError());
        free(pixels);
        return NULL;
    }

    // create the texture
    texture = sdl_create_texture(pane->w, pane->h);
    if (texture == NULL) {
        ERROR("failed to allocate texture\n");
        free(pixels);
        return NULL;
    }

    // update the texture with the pixels
    sdl_update_texture(texture, pixels, pane->w);

    // free pixels
    free(pixels);

    // return the texture
    return texture;
}

texture_t sdl_create_filled_circle_texture(int32_t radius, int32_t color)
{
    int32_t width = 2 * radius + 1;
    int32_t x = radius;
    int32_t y = 0;
    int32_t radiusError = 1-x;
    int32_t pixels[width][width];
    SDL_Texture * texture;
    uint32_t rgba = _bswap32(sdl_color_to_rgba[color]);

    #define DRAWLINE(Y, XS, XE, V) \
        do { \
            int32_t i; \
            for (i = XS; i <= XE; i++) { \
                pixels[Y][i] = (V); \
            } \
        } while (0)

    // initialize pixels
    bzero(pixels,sizeof(pixels));
    while(x >= y) {
        DRAWLINE(y+radius, -x+radius, x+radius, rgba);
        DRAWLINE(x+radius, -y+radius, y+radius, rgba);
        DRAWLINE(-y+radius, -x+radius, x+radius, rgba);
        DRAWLINE(-x+radius, -y+radius, y+radius, rgba);
        y++;
        if (radiusError<0) {
            radiusError += 2 * y + 1;
        } else {
            x--;
            radiusError += 2 * (y - x) + 1;
        }
    }

    // create the texture and copy the pixels to the texture
    texture = sdl_create_texture(width, width);
    if (texture == NULL) {
        ERROR("failed to allocate texture\n");
        return NULL;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(texture, NULL, pixels, width*BYTES_PER_PIXEL);

    // return texture
    return (texture_t)texture;
}

texture_t sdl_create_text_texture(int32_t fg_color, int32_t bg_color, int32_t font_ptsize, char * str)
{
    SDL_Surface * surface;
    SDL_Texture * texture;
    uint32_t      fg_rgba;
    uint32_t      bg_rgba;
    SDL_Color     fg_sdl_color;
    SDL_Color     bg_sdl_color;

    if (str[0] == '\0') {
        return NULL;
    }

    fg_rgba = sdl_color_to_rgba[fg_color];
    fg_sdl_color.r = (fg_rgba >> 24) & 0xff;
    fg_sdl_color.g = (fg_rgba >> 16) & 0xff;
    fg_sdl_color.b = (fg_rgba >>  8) & 0xff;
    fg_sdl_color.a = (fg_rgba >>  0) & 0xff;

    bg_rgba = sdl_color_to_rgba[bg_color];
    bg_sdl_color.r = (bg_rgba >> 24) & 0xff;
    bg_sdl_color.g = (bg_rgba >> 16) & 0xff;
    bg_sdl_color.b = (bg_rgba >>  8) & 0xff;
    bg_sdl_color.a = (bg_rgba >>  0) & 0xff;

    // if the font has not been initialized then do so
    sdl_font_init(font_ptsize);

    // xxx comments
    surface = TTF_RenderText_Shaded(sdl_font[font_ptsize].font, str, fg_sdl_color, bg_sdl_color);
    if (surface == NULL) {
        ERROR("failed to allocate surface\n");
        return NULL;
    }
    texture = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    if (texture == NULL) {
        ERROR("failed to allocate texture\n");
        SDL_FreeSurface(surface);
        return NULL;
    }
    SDL_FreeSurface(surface);

    return (texture_t)texture;
}

void sdl_update_texture(texture_t texture, uint8_t * pixels, int32_t pitch) 
{
    int32_t width, height;

    sdl_query_texture(texture, &width, &height);

    SDL_UpdateTexture((SDL_Texture*)texture,
                      NULL,                   // update entire texture
                      pixels,                 // pixels
                      pitch*BYTES_PER_PIXEL); // pitch  
}

void sdl_query_texture(texture_t texture, int32_t * width, int32_t * height)
{
    if (texture == NULL) {
        *width = 0;
        *height = 0;
        return;
    }

    SDL_QueryTexture((SDL_Texture *)texture, NULL, NULL, width, height);
}

rect_t sdl_render_texture(rect_t * pane, int32_t x, int32_t y, texture_t texture)
{
    int32_t width, height;
    rect_t loc, loc_clipped = {0,0,0,0};

    // verify texture arg
    if (texture == NULL) {
        return loc_clipped;
    }

    // construct loc from caller supplied x,y and from the texture width,height
    sdl_query_texture(texture, &width, &height);
    loc.x = x;
    loc.y = y;
    loc.w = width;
    loc.h = height;
    
    // render the texture at pane/loc
    loc_clipped = sdl_render_scaled_texture(pane, &loc, texture);

    // return the possibly clipped location
    return loc_clipped;
}

rect_t sdl_render_scaled_texture(rect_t * pane, rect_t * loc, texture_t texture)
{
    SDL_Rect dstrect, srcrect;
    int32_t texture_width, texture_height;
    rect_t loc_clipped = {0,0,0,0};

    // verify texture arg
    if (texture == NULL) {
        return loc_clipped;
    }

    // if loc is completely outside of pane then return
    if (loc->x >= pane->w ||
        loc->y >= pane->h ||
        loc->x + loc->w <= 0 ||
        loc->y + loc->h <= 0)
    {
        return loc_clipped;
    }

    // if loc is completely inside the pane
    if (loc->x >= 0 && loc->x + loc->w <= pane->w &&
        loc->y >= 0 && loc->y + loc->h <= pane->h)
    {
        dstrect.x = loc->x + pane->x;
        dstrect.y = loc->y + pane->y;
        dstrect.w = loc->w;
        dstrect.h = loc->h;
        SDL_RenderCopy(sdl_renderer, texture, NULL, &dstrect);

        loc_clipped.x = dstrect.x - pane->x;
        loc_clipped.y = dstrect.y - pane->y;
        loc_clipped.w = dstrect.w;
        loc_clipped.h = dstrect.h;
        return loc_clipped;
    }

    // otherwise need to handle complicated cases to clip the 
    // texture at the pane border
    sdl_query_texture(texture, &texture_width, &texture_height);

    if ((loc->y < 0) &&
        (loc->y + loc->h > pane->h)) 
    {
        dstrect.y = pane->y;
        dstrect.h = pane->h;
        srcrect.y = texture_height * -loc->y / loc->h;
        srcrect.h = texture_height * pane->h / loc->h;
    } else if (loc->y < 0) {
        dstrect.y = pane->y;
        dstrect.h = loc->h + loc->y;
        srcrect.y = texture_height * -loc->y / loc->h;
        srcrect.h = texture_height - srcrect.y;
    } else if (loc->y + loc->h > pane->h) {
        dstrect.y = pane->y + loc->y;
        dstrect.h = pane->h - loc->y;
        srcrect.y = 0;
        srcrect.h = texture_height * dstrect.h / loc->h;
    } else {
        dstrect.y = pane->y + loc->y;
        dstrect.h = loc->h;
        srcrect.y = 0;
        srcrect.h = texture_height;
    }

    if ((loc->x < 0) &&
        (loc->x + loc->w > pane->w)) 
    {
        dstrect.x = pane->x;
        dstrect.w = pane->w;
        srcrect.x = texture_width * -loc->x / loc->w;
        srcrect.w = texture_width * pane->w / loc->w;
    } else if (loc->x < 0) {
        dstrect.x = pane->x;
        dstrect.w = loc->w + loc->x;
        srcrect.x = texture_width * -loc->x / loc->w;
        srcrect.w = texture_width - srcrect.x;
    } else if (loc->x + loc->w > pane->w) {
        dstrect.x = pane->x + loc->x;
        dstrect.w = pane->w - loc->x;
        srcrect.x = 0;
        srcrect.w = texture_width * dstrect.w / loc->w;
    } else {
        dstrect.x = pane->x + loc->x;
        dstrect.w = loc->w;
        srcrect.x = 0;
        srcrect.w = texture_width;
    }

    SDL_RenderCopy(sdl_renderer, texture, &srcrect, &dstrect);

    loc_clipped.x = dstrect.x - pane->x;
    loc_clipped.y = dstrect.y - pane->y;
    loc_clipped.w = dstrect.w;
    loc_clipped.h = dstrect.h;
    return loc_clipped;
}

void sdl_destroy_texture(texture_t texture)
{
    if (texture) {
        SDL_DestroyTexture((SDL_Texture *)texture);
    }
}

// -----------------  PRINT SCREEN -------------------------------------- 

void sdl_print_screen(char *file_name, bool flash_display, rect_t * rect_arg) 
{
    uint8_t * pixels = NULL;
    SDL_Rect  rect;
    int32_t   ret, len;

    // if caller has supplied region to print then 
    //   init rect to print with caller supplied position
    // else
    //   init rect to print with entire window
    // endif
    if (rect_arg) {
        rect.x = rect_arg->x;
        rect.y = rect_arg->y;
        rect.w = rect_arg->w;
        rect.h = rect_arg->h;
    } else {
        rect.x = 0;
        rect.y = 0;
        rect.w = sdl_win_width;
        rect.h = sdl_win_height;   
    }

    // allocate memory for pixels
    pixels = calloc(1, rect.w * rect.h * BYTES_PER_PIXEL);
    if (pixels == NULL) {
        ERROR("allocate pixels failed\n");
        return;
    }

    // copy display rect to pixels
    ret = SDL_RenderReadPixels(sdl_renderer, 
                               &rect, 
                               SDL_PIXELFORMAT_ABGR8888, 
                               pixels, 
                               rect.w * BYTES_PER_PIXEL);
    if (ret < 0) {
        ERROR("SDL_RenderReadPixels, %s\n", SDL_GetError());
        free(pixels);
        return;
    }

    // write pixels to file_name, 
    // filename must have .jpg or .png extension
    len = strlen(file_name);
    if (len > 4 && strcmp(file_name+len-4, ".jpg") == 0) {
        ret = write_jpeg_file(file_name, pixels, rect.w, rect.h);
        if (ret != 0) {
            ERROR("write_jpeg_file %s failed\n", file_name);
        }
    } else if (len > 4 && strcmp(file_name+len-4, ".png") == 0) {
        ret = write_png_file(file_name, pixels, rect.w, rect.h);
        if (ret != 0) {
            ERROR("write_png_file %s failed\n", file_name);
        }
    } else {
        ret = -1;
        ERROR("filename %s must have .jpg or .png extension\n", file_name);
    }
    if (ret != 0) {
        free(pixels);
        return;
    }

    // it worked, flash display if enabled;
    // the caller must redraw the screen if flash_display is enabled
    if (flash_display) {
        sdl_set_color(BLACK);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderPresent(sdl_renderer);
        usleep(250000);
    }

    // free pixels
    free(pixels);
}

static char * sdl_print_screen_filename(void)
{
    struct tm tm;
    time_t t;
    static char filename[PATH_MAX];

    t = time(NULL);
    localtime_r(&t, &tm);
    sprintf(filename, "screenshot_%2.2d%2.2d%2.2d_%2.2d%2.2d%2.2d.jpg",
            tm.tm_year-100, tm.tm_mon+1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);

    return filename;
}
