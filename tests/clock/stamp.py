#!/usr/bin/env python3
#
# Each line of the guest's serial port, with when the host got it: "<host
# monotonic seconds> <line>" (tests/run.sh's clock test compares the
# guest's clock with it)

import sys
import time

for line in sys.stdin.buffer:
    sys.stdout.write("%.6f %s" % (time.monotonic(), line.decode(errors="replace")))
    sys.stdout.flush()
