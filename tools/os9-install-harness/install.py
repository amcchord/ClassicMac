#!/usr/bin/env python3
"""Drive the Mac OS 9.2.x installer from its CD's top-level window.

The destination must already have been initialized by Drive Setup.  The
installer has the same 640x480 layout on the 9.2.1 and 9.2.2 discs used by the
storage regression harness.
"""

import argparse
import os
import time

from driver import Driver


def step(driver, label, x, y, delay, double=False):
    print(label, flush=True)
    driver.click(x, y, double=double)
    time.sleep(delay)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qmp", required=True)
    parser.add_argument("--template", default=os.path.join(
        os.path.dirname(__file__), "cursor.json"))
    parser.add_argument("--shots", required=True)
    args = parser.parse_args()

    os.makedirs(args.shots, exist_ok=True)
    driver = Driver(args.qmp, args.template,
                    os.path.join(args.shots, "_live.ppm"))

    step(driver, "opening installer", 191, 150, 7, double=True)
    driver.shot(os.path.join(args.shots, "01-welcome.ppm"))
    step(driver, "continuing from welcome", 515, 384, 3)
    step(driver, "selecting destination", 515, 384, 5)
    step(driver, "continuing from important information", 515, 384, 3)
    step(driver, "continuing from license", 515, 384, 2)
    step(driver, "accepting license", 451, 306, 5)
    driver.shot(os.path.join(args.shots, "02-ready.ppm"))

    step(driver, "opening install options", 234, 384, 2)
    step(driver, "disabling Apple HD driver update", 137, 166, 1)
    driver.shot(os.path.join(args.shots, "02-options-disabled.ppm"))
    step(driver, "closing install options", 472, 330, 1)
    step(driver, "starting install", 515, 384, 8)
    driver.shot(os.path.join(args.shots, "03-started.ppm"))
    print("installer started", flush=True)


if __name__ == "__main__":
    main()
