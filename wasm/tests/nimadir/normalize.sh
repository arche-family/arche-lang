#!/bin/sh
# A headless run as the two shells are compared: from its first frame on;
# the clock as N where an app shows it (the counter's init, how long hello
# has been open); and without the frames Nimadir
# asks for to write an app's storage to disk, 250 ms after a change, which
# this shell, keeping storage in memory, has no need of.
sed -n '/^=== headless run/,$p' | sed -e 's/host-time [0-9]*/host-time N/' -e 's/Open for [0-9]* s/Open for N s/' | grep -vE '^   \[next frame in (2[0-4][0-9]|250) ms\]$'
