// Puts an eye on the panel: owns the display start-up, the strip buffers and
// the second core. Shared by every firmware that draws an eye.
#ifndef EYE_SCREEN_H
#define EYE_SCREEN_H

#include <stdint.h>

#include "eye.h"

#define SCREEN_W 368
#define SCREEN_H 448
#define EYE_SIZE 368
#define EYE_Y ((SCREEN_H - EYE_SIZE) / 2)   // 40: the eye sits in the middle of the panel
#define PAIR_SIZE (SCREEN_W / 2)            // 184: each eye of a pair
#define PAIR_Y ((SCREEN_H - PAIR_SIZE) / 2) // 132
#define TEXT_H 24                           // height of one text line
#define STATUS_Y (EYE_Y + EYE_SIZE + 6)     // text line under the eye

// Start the display, clear it, start touch, and start core 1.
void eye_screen_init(uint8_t brightness);

// Panel brightness, 0 to 100.
void eye_screen_brightness(uint8_t brightness);

// Draw one frame of the eye and send it. Both cores draw. The renderer must
// have been set up at EYE_SIZE.
void eye_screen_draw(eye_renderer_t *renderer, const eye_pose_t *pose);

// The same for a pair of eyes side by side, both in the same pose, with the
// right-hand eye's lids mirrored. The renderer must have been set up at
// PAIR_SIZE.
void eye_screen_draw_pair(eye_renderer_t *renderer, const eye_pose_t *pose);

// One line of text at pixel row y, at most 26 characters. The panel's rounded
// corners hide the first character or two of the top and bottom rows.
void eye_screen_text(uint32_t y, uint16_t color, const char *fmt, ...);

// Blank rows y0 .. y1-1.
void eye_screen_clear(uint32_t y0, uint32_t y1);

#endif
