// Makes the eye look alive on its own: it glances around, blinks, and its
// pupil and eyelids drift. Produces an eye_pose_t each frame. Portable C.
#ifndef EYE_BEHAVE_H
#define EYE_BEHAVE_H

#include "eye.h"
#include "motion.h"

typedef struct {
    uint32_t rng;
    // Gaze: hold still, then jump to a new spot.
    float gaze_x, gaze_y;
    float from_x, from_y, to_x, to_y;
    float move_t, move_dur;      // seconds into the current jump, and its length
    float hold_left;             // seconds left looking at the current spot
    bool moving;
    // Blink: 0 open, 1 closing, 2 opening.
    int blink_state, blinks_queued;
    float blink_t, blink_wait;
    // Pupil and eyelids ease toward targets.
    float pupil, pupil_target, pupil_wait;
    float lid_upper, lid_lower;
    float dizzy_angle;
    float spin;
} eye_behave_t;

void eye_behave_init(eye_behave_t *b, uint32_t seed);

// Advance by dt seconds and fill in the pose to draw. react carries the
// motion reactions to blend in; pass NULL for the idle behaviour alone.
void eye_behave_update(eye_behave_t *b, const eye_renderer_t *r, const eye_reaction_t *react,
                       float dt, eye_pose_t *pose);

#endif
