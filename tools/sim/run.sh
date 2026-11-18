#!/bin/sh
# Render test frames of the eye into tools/sim/out/ as PPM images.
set -e
cd "$(dirname "$0")"
mkdir -p out
cc -O2 -I../../firmware/eye -o out/sim sim.c ../../firmware/eye/eye.c ../../firmware/eye/behave.c ../../firmware/eye/motion.c ../../firmware/eye/styles/*.c -lm
out/sim out "${1:-368}"
# Other modes: out/sim out 368 anim 300   (idle behaviour frames)
#              out/sim out 368 styles     (one frame of every style)
