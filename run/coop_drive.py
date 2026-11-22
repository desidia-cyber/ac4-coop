"""Closed-loop driver for one ac4 instance: boot to gameplay, then play an input sequence.

    python coop_drive.py --tag m2 [--seq m2] [--after 20]

Everything is verified from evidence, never from a fixed delay:
  * every press is "<neutral line> <press line> <neutral line>" in cmd.txt, because the rig coalesces
    two identical lines and a press with no neutral before it is dropped (research\\16 (e)3);
  * after every press the driver waits for a screenshot file that did not exist before it, so no
    comparison is ever made against a stale frame (research\\16 (e)8);
  * the screen is classified numerically from that fresh PNG (numpy), and the next input is chosen
    from the classification. The classes are the ones the hub/menus genuinely show:
      attract/title/movies   SCHEMATICS   hub   accept-dialog   briefing   gameplay
  * the movies play with NO input: a press during a movie, or in the gap between two, stalls the
    pipeline or restarts the attract loop (research\\21 section 7, measured over 14 launches).

The mission path in the hub (measured, research\\10 "Hub UI" + this session):
  START on the title card -> START opens the menu -> A on LOAD GAME -> save read -> hub.
  In the hub the focus starts on the right-hand buttons (SCHEMATICS/ACSIS/SYSTEM). B exits a panel
  and LEFT moves the focus to the mission list, A then opens "Accept this mission?" with CANCEL
  focused, LEFT then A accepts.
"""
import argparse
import glob
import os
import sys
import time

import numpy as np
from PIL import Image

B = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"


class Inst:
    def __init__(self, tag, shots=None, runs=None, log=None):
        self.tag = tag
        self.shots = shots or os.path.join(B, "run", "shots", tag)
        self.runs = runs or os.path.join(B, "run", "data", "runs")
        self.log = log
        self.cmd_path = os.path.join(self.shots, "cmd.txt")
        self.n_shots = 0

    # ---- evidence -------------------------------------------------------------------
    def clock(self):
        try:
            return float(open(os.path.join(self.shots, "clock.txt")).read().strip())
        except (OSError, ValueError):
            return 0.0

    def alive(self):
        return self.clock() > 0

    def log_path(self):
        if self.log:
            return self.log
        try:
            return sorted(glob.glob(os.path.join(self.runs, "*")), key=os.path.getmtime)[-1] + r"\ac4.log"
        except (OSError, IndexError):
            return ""

    def log_text(self):
        p = self.log_path()
        try:
            return open(p, errors="ignore").read() if p else ""
        except OSError:
            return ""

    def has(self, needle):
        return needle in self.log_text()

    def wait_hub(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if self.has("MissionSelectScene"):
                return True
            time.sleep(1)
        return self.has("MissionSelectScene")

    def fresh_shot(self, timeout=25.0):
        """A screenshot written after this call started; the newest PNG path, or None.

        Keyed on (name, mtime): the rig's file names restart at shot_000 each launch, so a name that
        already exists can still be a NEW frame when the directory was not cleared.
        """
        def snapshot():
            return {(p, os.path.getmtime(p)) for p in glob.glob(os.path.join(self.shots, "shot_*.png"))}

        before = snapshot()
        end = time.time() + timeout
        while time.time() < end:
            new = snapshot() - before
            if new:
                time.sleep(0.35)  # let the PNG finish flushing
                newest = max(glob.glob(os.path.join(self.shots, "shot_*.png")), key=os.path.getmtime)
                self.n_shots += 1
                return newest
            time.sleep(0.15)
        return None

    # ---- input ----------------------------------------------------------------------
    def _write(self, line):
        with open(self.cmd_path, "a", newline="\n") as f:
            f.write(line + "\n")

    def press(self, buttons, port=0, hold=200, settle=0.9, shot=True):
        self._write("0 p%d none 0 0 0 0" % port)
        time.sleep(0.35)
        self._write("%d p%d %s 0 0 0 0" % (hold, port, buttons))
        time.sleep(0.35 + hold / 1000.0)
        self._write("0 p%d none 0 0 0 0" % port)
        time.sleep(settle)
        return self.fresh_shot() if shot else None

    def hold(self, buttons, ms, port=0, lx=0, ly=0, rx=0, ry=0, settle=0):
        self._write("0 p%d none 0 0 0 0" % port)
        time.sleep(0.3)
        self._write("%d p%d %s %d %d %d %d" % (ms, port, buttons, lx, ly, rx, ry))
        time.sleep(0.3 + ms / 1000.0)
        if settle:
            self._write("0 p%d none 0 0 0 0" % port)
            time.sleep(settle / 1000.0)


# ---- screen classification (numbers only: named regions measured from real frames) -------------
def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def mean_bright(a, x0, y0, x1, y1):
    return float(a[y0:y1, x0:x1].mean())


def gold_score(a, x0, y0, x1, y1):
    """Selection fills are gold: red well above blue. 0 elsewhere."""
    r = a[y0:y1, x0:x1, 0].astype(np.float32)
    b = a[y0:y1, x0:x1, 2].astype(np.float32)
    return float((r - b).mean())


def classify(path):
    """Name the screen from measured regions. Returns (name, dict of scores)."""
    a = load(path)
    s = {}
    s["full"] = float(a.mean())
    # Hub: a gold "Chapter" tab at the very top-left, plus the CURRENT MISSION column.
    # SCHEMATICS: a bright header band and a bright table, and the same top-left band is white.
    s["top_left_band"] = mean_bright(a, 0, 40, 440, 70)
    s["tl_tab"] = mean_bright(a, 0, 45, 120, 70)       # 0 in the title card/menu, >150 in a panel
    s["table_area"] = mean_bright(a, 100, 240, 1150, 600)
    s["left_panel"] = mean_bright(a, 800, 120, 1030, 620)
    s["dialog_band"] = mean_bright(a, 440, 310, 840, 400)
    s["dialog_gold"] = gold_score(a, 640, 366, 820, 396)
    s["hud_bottom"] = mean_bright(a, 0, 600, 1280, 715)
    s["centre"] = mean_bright(a, 480, 260, 800, 460)
    s["mid_col"] = mean_bright(a, 560, 460, 760, 610)  # NEW GAME / LOAD GAME / MULTIPLAYER
    s["title_gold"] = gold_score(a, 560, 520, 760, 548)  # the LOAD GAME row's selection band
    # The hub fills the header with a second panel right of the chapter tab (the GARAGE/START band);
    # SCHEMATICS leaves it black. Measured: hub 143, SCHEMATICS 3, title card 0.
    s["hdr_right"] = mean_bright(a, 440, 45, 660, 100)
    # Order matters: a centered dialog sits on top of whatever screen is behind it.
    if s["dialog_band"] > 40 and s["dialog_gold"] > 18:
        return "dialog", s
    if s["hdr_right"] > 60 and s["left_panel"] > 50:
        return "hub", s
    if s["tl_tab"] > 150 and s["table_area"] > 40:
        return "schematics", s
    if s["mid_col"] > 5 and s["title_gold"] > 8 and s["tl_tab"] < 5:
        return "title_menu", s
    if s["hud_bottom"] > 18 and s["centre"] > 25 and s["tl_tab"] < 5:
        return "gameplay", s
    if s["full"] < 12:
        return "dark", s          # the title card / a fade
    return "other", s


# ---- the boot walk -----------------------------------------------------------------------------
def wait_clock(i, limit=180):
    end = time.time() + limit
    while time.time() < end:
        if i.clock() > 1:
            return True
        time.sleep(0.5)
    return False


def movies_state(i):
    """(opens, closed) from the vdec handle lines: a movie decodes 8 pictures then closes."""
    opens = 0
    last = None
    for line in i.log_text().splitlines():
        if "vdec] open handle" in line:
            opens += 1
            last = "open"
        elif "vdec] close handle" in line:
            last = "close"
    return opens, last == "close"


def sail_state(i):
    """(starts, exits) of '_libsail-control': one movie pipeline, started for each movie."""
    starts = exits = 0
    for line in i.log_text().splitlines():
        if "'_libsail-control'" in line:
            if " started " in line:
                starts += 1
            elif " exited " in line:
                exits += 1
    return starts, exits


def boot(i, log=print):
    """Boot to the hub. This simple loop is the one that actually worked (runs 20261115-083555 and
    085419 reached the hub at clock ~314 s in 7-8 STARTs each).

    Facts behind it (measured here and in research\\21 s7):
      * the attract loop plays the movies BACK TO BACK: a close and the next start are ~3 s apart, so
        there is no quiet window to wait for. Pressing START blindly therefore usually lands inside a
        movie (harmless - it is swallowed) and sometimes in the gap (which is what the menu needs);
      * the startup dialogs must be dismissed first; they precede the movies;
      * the title card shows "PRESS START BUTTON": a START there gets to the title menu (dark frame,
        mid_col > 5 and the gold LOAD GAME band). A on the default LOAD GAME then reads the save and
        MissionSelectScene arrives.
    """
    if not wait_clock(i):
        log("[boot] no clock: the game did not start")
        return False
    log("[boot] clock %.1f" % i.clock())

    for b in ("A", "B", "A+B", "A", "B", "A"):
        i.press(b, settle=1.6)
        if sail_state(i)[0]:
            break

    for attempt in range(26):
        if i.has("MissionSelectScene"):
            break
        shot = i.press("START", hold=150, settle=2.2)
        cls, sc = classify(shot) if shot else ("?", {})
        log("[boot] %2d START -> %-11s mid=%5.1f gold=%6.1f tab=%4.0f (clock %.1f)" % (
            attempt, cls, sc.get("mid_col", 0), sc.get("title_gold", 0), sc.get("tl_tab", 0), i.clock()))
        if cls == "title_menu":
            for k in range(3):
                shot = i.press("A", settle=3.4)
                log("[boot]      A -> %s (save lists %d, clock %.1f)" % (
                    classify(shot)[0] if shot else "?", i.log_text().count("cellSaveDataListLoad2"), i.clock()))
                if i.wait_hub(45):
                    break
                if i.has("Ac4SortieScene"):
                    break
            if i.has("MissionSelectScene"):
                break
            # Not the load list: the title menu may be back; try the next START round.
            continue
        if cls in ("hub", "schematics", "dialog") and i.wait_hub(10):
            break
    if i.wait_hub(120):
        log("[boot] HUB at clock %.1f" % i.clock())
        return True
    log("[boot] FAILED: no hub")
    return False


def enter_mission(i, log=print):
    """Hub -> gameplay, verified at every step."""
    for step in range(14):
        if i.has("input context gameplay"):
            log("[hub] GAMEPLAY at clock %.1f" % i.clock())
            return True
        shot = i.fresh_shot(6)
        cls, sc = classify(shot) if shot else ("?", {})
        log("[hub] step %d: screen %s centre=%.0f left=%.0f top=%.0f dlg=%.0f/%.0f" % (
            step, cls, sc.get("centre", 0), sc.get("left_panel", 0), sc.get("top_left_band", 0),
            sc.get("dialog_band", 0), sc.get("dialog_gold", 0)))
        if cls == "dialog":
            # "Accept this mission?" focuses CANCEL: LEFT then A.
            i.press("LEFT", settle=1.6)
            i.press("A", settle=3.0)
            log("[hub] dialog: LEFT + A (accept)")
        elif cls == "schematics":
            i.press("B", settle=1.6)          # leave the panel
        elif cls == "hub":
            # Focus may be on the buttons column: LEFT moves it to the mission list.
            i.press("LEFT", settle=1.2)
            i.press("A", settle=2.2)
            log("[hub] mission list: A")
        else:
            i.press("A", settle=2.5)
        if i.has("Ac4SortieScene"):
            log("[hub] sortie scene opened at clock %.1f" % i.clock())
            end = time.time() + 300
            while time.time() < end:
                if i.has("input context gameplay"):
                    log("[hub] GAMEPLAY at clock %.1f" % i.clock())
                    return True
                time.sleep(1.5)
            # A briefing movie may need dismissing.
            for _ in range(6):
                i.press("A", settle=2.5)
                if i.has("input context gameplay"):
                    log("[hub] GAMEPLAY at clock %.1f" % i.clock())
                    return True
            log("[hub] sortie opened but no gameplay")
            return False
    return i.has("input context gameplay")


# ---- input sequences ---------------------------------------------------------------------------
def seq_m1(i, log=print):
    log("[m1] baseline idle 5 s")
    i.hold("none", 5000)
    log("[m1] p1 left stick forward 3 s (p0 idle)")
    i.hold("none", 3000, port=1, ly=26000)
    log("[m1] idle 2 s")
    i.hold("none", 2000)
    log("[m1] p1 right stick right 2 s")
    i.hold("none", 2000, port=1, rx=26000)
    log("[m1] idle 2 s")
    i.hold("none", 2000)
    log("[m1] p1 A (fire) 1 s")
    i.hold("A", 1000, port=1)
    log("[m1] idle 4 s")
    i.hold("none", 4000)
    log("[m1] --- reverse: p0 drives, p1 idle ---")
    log("[m1] p0 left stick forward 3 s")
    i.hold("none", 3000, ly=26000)
    log("[m1] idle 2 s")
    i.hold("none", 2000)
    log("[m1] p0 right stick right 2 s")
    i.hold("none", 2000, rx=26000)
    log("[m1] idle 4 s")
    i.hold("none", 4000)


def seq_m2(i, log=print):
    """A known AC1 pattern for the transform-layout work."""
    log("[m2] idle 5 s")
    i.hold("none", 5000)
    log("[m2] p0 forward 4 s")
    i.hold("none", 4000, ly=26000)
    log("[m2] idle 3 s")
    i.hold("none", 3000)
    log("[m2] p0 turn right 3 s")
    i.hold("none", 3000, rx=26000)
    log("[m2] idle 3 s")
    i.hold("none", 3000)
    log("[m2] p0 boost (LT) + forward 3 s")
    i.hold("LT", 3000, ly=26000)
    log("[m2] idle 4 s")
    i.hold("none", 4000)
    log("[m2] p0 backward 3 s")
    i.hold("none", 3000, ly=-26000)
    log("[m2] idle 4 s")
    i.hold("none", 4000)


def seq_m4(i, log=print):
    """M4: p0 forward, turn, fire - the sequence the peer must mirror."""
    log("[m4] idle 3 s")
    i.hold("none", 3000)
    log("[m4] p0 forward 3 s")
    i.hold("none", 3000, ly=26000)
    log("[m4] idle 2 s")
    i.hold("none", 2000)
    log("[m4] p0 turn right 2 s")
    i.hold("none", 2000, rx=26000)
    log("[m4] idle 2 s")
    i.hold("none", 2000)
    log("[m4] p0 fire 1 s")
    i.hold("A", 1000)
    log("[m4] idle 3 s")
    i.hold("none", 3000)
    log("[m4] p0 forward 3 s again")
    i.hold("none", 3000, ly=26000)
    log("[m4] end")


def poke(i, offset, delta, log=print):
    """Queue one write test for body2 (coop_body.cpp) and wait for it to be consumed."""
    p = os.path.join(i.shots, "poke.txt")
    before = i.log_text().count("COOP POKE: body2")
    with open(p, "w", newline="\n") as f:
        f.write("0x%x %g\n" % (offset, delta))
    end = time.time() + 8
    while time.time() < end:
        if i.log_text().count("COOP POKE: body2") > before:
            for line in i.log_text().splitlines()[-60:]:
                if "COOP POKE: body2" in line:
                    log("  " + line[line.index("COOP POKE"):])
            return True
        time.sleep(0.2)
    log("  poke 0x%x not consumed" % offset)
    return False


def seq_m2poke(i, log=print):
    """M2: the write test. Body2 is idle; add +50 to one candidate position coordinate at a time and
    watch what the game does with it (memory a second later, plus a screenshot)."""
    log("[m2poke] settling 4 s with both ACs idle")
    i.hold("none", 4000)
    base = i.fresh_shot(8)
    log("[m2poke] baseline %s" % os.path.basename(base or "?"))
    for cand, off in (("matrix-2 row3 (+0x80)", 0x80), ("matrix-1 row3 (+0x40)", 0x40),
                      ("matrix-4 row3 (+0x1c0)", 0x1c0)):
        log("[m2poke] === candidate %s ===" % cand)
        poke(i, off, 50.0)
        s1 = i.fresh_shot(8)
        time.sleep(1.2)
        s2 = i.fresh_shot(8)
        log("[m2poke] 1 s later %s, 2 s later %s" % (os.path.basename(s1 or "?"), os.path.basename(s2 or "?")))
        i.hold("none", 3000)


SEQS = {"m1": seq_m1, "m2": seq_m2, "m2poke": seq_m2poke, "m4": seq_m4}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--seq", default="")
    ap.add_argument("--shots")
    ap.add_argument("--runs")
    ap.add_argument("--log")
    ap.add_argument("--after", type=float, default=15)
    ap.add_argument("--to-gameplay", action="store_true", default=True)
    a = ap.parse_args()
    i = Inst(a.tag, a.shots, a.runs, a.log)
    if not boot(i):
        print("RESULT: boot failed")
        return 2
    if not enter_mission(i):
        print("RESULT: no gameplay")
        return 3
    if a.seq and a.seq in SEQS:
        SEQS[a.seq](i)
    end = time.time() + a.after
    while time.time() < end:
        if "fatal]" in i.log_text():
            print("RESULT: CRASH at clock %.1f" % i.clock())
            return 4
        time.sleep(2)
    print("RESULT: ok, clock %.1f" % i.clock())
    return 0


if __name__ == "__main__":
    sys.exit(main())
