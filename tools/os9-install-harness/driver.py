"""Closed-loop pointer driver for the OS 9 install harness."""

import os
import time
from qmp import QMP
from cursor import Cursor


class Driver:
    def __init__(self, qmp_sock, template_json, live_shot, w=640, h=480):
        self.q = QMP(qmp_sock)
        self.q.connect()
        self.cur = Cursor(template_json)
        self.live_shot = live_shot
        os.makedirs(os.path.dirname(live_shot), exist_ok=True)
        self.w, self.h = w, h
        self.last = None

    def rel(self, dx, dy):
        self.q.execute("input-send-event", events=[
            {"type": "rel", "data": {"axis": "x", "value": int(dx)}},
            {"type": "rel", "data": {"axis": "y", "value": int(dy)}}])

    def home(self):
        for _ in range(60):
            self.rel(-40, -40)
        # Nudge away from the corner so the cursor sprite is fully on-screen
        # and therefore matchable.
        for _ in range(40):
            self.rel(1, 1)
            time.sleep(0.003)
        self.last = (25, 25)
        time.sleep(0.2)

    def locate(self):
        self.q.execute("screendump", filename=self.live_shot)
        near = self.last
        p = (self.cur.locate(self.live_shot, near=near, radius=60)
             if near else None)
        if p is None:
            p = self.cur.locate(self.live_shot)
        if p:
            self.last = p
        return p

    def move_to(self, tx, ty, tries=45, tol=2):
        if self.last is None:
            self.home()
        for _ in range(tries):
            p = self.locate()
            if p is None:
                self.home()
                continue
            dx, dy = tx - p[0], ty - p[1]
            if abs(dx) <= tol and abs(dy) <= tol:
                return p
            # Slow, unaccelerated 1px pulses on each axis independently:
            # acceleration only kicks in on fast consecutive deltas, so pace
            # them and step each axis by its own sign.
            n = min(max(abs(dx), abs(dy)), 25)
            sx = 1 if dx > 0 else (-1 if dx < 0 else 0)
            sy = 1 if dy > 0 else (-1 if dy < 0 else 0)
            rx, ry = abs(dx), abs(dy)
            for i in range(n):
                self.rel(sx if i < rx else 0, sy if i < ry else 0)
                time.sleep(0.002)
        p = self.locate()
        if p is not None and abs(tx - p[0]) <= tol and abs(ty - p[1]) <= tol:
            return p
        raise RuntimeError(
            f"cursor failed to reach ({tx}, {ty}) after {tries} attempts; "
            f"last position was {p}")

    def click(self, tx, ty, double=False):
        self.move_to(tx, ty)
        time.sleep(0.1)
        for _ in range(2 if double else 1):
            self.q.execute("input-send-event", events=[
                {"type": "btn", "data": {"button": "left", "down": True}}])
            time.sleep(0.04)
            self.q.execute("input-send-event", events=[
                {"type": "btn", "data": {"button": "left", "down": False}}])
            time.sleep(0.06)

    def shot(self, path):
        return self.q.screendump(path)
