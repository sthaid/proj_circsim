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

#ifndef __UTIL_SDL_PREDEFINED_PANES_H__
#define __UTIL_SDL_PREDEFINED_PANES_H__

typedef struct {
    char title_str[32];
    char x_units_str[32];
    char y_units_str[32];
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    int32_t max_points_alloced;
    int32_t max_points;
    struct pane_hndlr_display_graph_point_s {
        float x;
        float y;
    } points[0];
} pane_hndlr_display_graph_params_t;

int32_t pane_hndlr_display_text(pane_cx_t * pane_cx, int32_t request, void * init_params, sdl_event_t * event);
int32_t pane_hndlr_display_graph(pane_cx_t * pane_cx, int32_t request, void * init_params, sdl_event_t * event);

#endif
