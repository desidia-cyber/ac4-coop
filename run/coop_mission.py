"""coop_mission.py -- RELIABLE boot -> hub -> gameplay for one ac4 rig instance.

This exists because getting into gameplay cost this session far too many launches and tokens. The two
things that actually work are encoded here as a verified state machine, one screenshot-checked action
per step, and it is the script to use next time instead of improvising.

    python coop_mission.py --tag host
    python coop_mission.py --tag join --shots <dir> --runs <dir>

WHY THE OLD ATTEMPTS FAILED (all measured, do not re-derive):
 1. The attract loop plays opening.pam + title.pam BACK TO BACK: a vdec close and the next
    `_libsail-control started` are only ~3 s apart, so "wait until the movies go quiet" NEVER fires.
    Waiting for quiet is a dead end. Correspondingly, pressing START blindly is fine (a press inside a
    movie is swallowed) - the simple START-spam loop below reached the hub twice (clock 314, 1497).
 2. The hub's focus starts on the RIGHT-HAND BUTTONS (SCHEMATICS/ACSIS/SYSTEM), not on the mission
    list. LEFT + A from there reopens SCHEMATICS, which is the loop that wasted the most time:
    hub -> SCHEMATICS -> B -> hub -> SCHEMATICS ...
    The only correct order is:  B (leave any panel) -> LEFT (focus the mission list) -> A (dialog)
    -> LEFT (CANCEL -> OK) -> A (accept). Every one of those five steps must be VERIFIED, because
    the dialog focuses CANCEL and a stray A closes it with CANCEL.
 3. `classify()` must run on a screenshot taken AFTER the input, or it compares a frame to itself.
 4. Two driver processes were launched with Start-Process without quoting: '-' + a path with spaces
    split the argument. Any launcher here quotes every path.

The screen states are recognised numerically (see classify below), never assumed:
    title_menu  the dark title menu, LOAD GAME row gold-highlighted
    hub         the mission panel: gold Chapter tab + the CURRENT MISSION column
    schematics  the AC table (light header, bright rows)
    dialog      a centred OK/CANCEL box
    gameplay    a 3-D view with the HUD strip along the bottom
"""
import argparse
import glob
import os
import sys
import time

import numpy as np
from PIL import Image

B = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"


# ---------------------------------------------------------------- screen classification
def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def mean_bright(a, x0, y0, x1, y1):
    return float(a[y0:y1, x0:x1].mean())


def gold_score(a, x0, y0, x1, y1):
    r = a[y0:y1, x0:x1, 0].astype(np.float32)
    b = a[y0:y1, x0:x1, 2].astype(np.float32)
    return float((r - b).mean())


def classify(path):
    """Name the screen from measured regions. Verified 5/5 on real frames (research\\24 s4)."""
    a = load(path)
    s = {}
    s["full"] = float(a.mean())
    s["tl_tab"] = mean_bright(a, 0, 45, 120, 70)          # bright in a panel, 0 on the title card
    s["hdr_right"] = mean_bright(a, 440, 45, 660, 100)    # hub fills it, SCHEMATICS leaves it black
    s["left_panel"] = mean_bright(a, 800, 120, 1030, 620)
    s["table_area"] = mean_bright(a, 100, 240, 1150, 600)
    s["dialog_band"] = mean_bright(a, 440, 310, 840, 400)
    s["dialog_gold"] = gold_score(a, 640, 366, 820, 396)
    s["dialog_ok"] = gold_score(a, 450, 366, 620, 396)
    s["hud_bottom"] = mean_bright(a, 0, 600, 1280, 715)
    s["centre"] = mean_bright(a, 480, 260, 800, 460)
    s["mid_col"] = mean_bright(a, 560, 460, 760, 610)
    s["title_gold"] = gold_score(a, 560, 520, 760, 548)
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
        return "dark", s
    return "other", s


def hub_focus(a):
    """Which hub column has the focus: 'buttons', 'mission' or '?'.

    MEASURED, and the reason hub navigation kept failing: the hub's focus starts on the RIGHT-HAND
    BUTTONS. The highlighted cell is gold and the other column's cells are not, so the two gold scores
    separate cleanly (real frames from run 20261115-102501):
        focus on buttons  SCHEMATICS row gold > +13, the mission row gold < -3
        focus on mission  SCHEMATICS row gold < -4, the mission row gold > +19
    Pressing LEFT from 'buttons' gives 'mission'; pressing A while still on 'buttons' reopens
    SCHEMATICS, which is the loop that burned this session's launches.
    """
    sc = gold_score(a, 1050, 175, 1190, 600)
    # Check every right-hand button band AND every mission list row (cursor may be on row 2, 3, ...).
    btn_max = max(gold_score(a, 1050, y, 1190, y + 36) for y in range(180, 560, 38))
    mi_max = max(gold_score(a, 700, y, 990, y + 34) for y in range(205, 580, 35))
    if btn_max > 8 and mi_max < 8:
        return "buttons"
    if mi_max > 10 and btn_max < 8:
        return "mission"
    return "?"


# ---------------------------------------------------------------- the instance
class Inst:
    def __init__(self, tag, shots=None, runs=None):
        self.tag = tag
        self.shots = shots or os.path.join(B, "run", "shots", tag)
        self.runs = runs or os.path.join(B, "run", "data", "runs")
        self.cmd_path = os.path.join(self.shots, "cmd.txt")

    def clock(self):
        try:
            return float(open(os.path.join(self.shots, "clock.txt")).read().strip())
        except (OSError, ValueError):
            return 0.0

    def log_text(self):
        try:
            p = os.path.join(sorted(glob.glob(os.path.join(self.runs, "2*")))[-1], "ac4.log")  # newest by NAME: a dir mtime can move (s28h)
            return open(p, errors="ignore").read()
        except (OSError, IndexError):
            return ""

    def has(self, needle):
        return needle in self.log_text()

    def last_gameplay_time(self):
        """Log time of the LAST 'input context gameplay'. -1 when it never appeared.

        The log is the whole run, so a bare substring test says "gameplay" long after the mission
        ended and the game returned to the hub. Every gameplay test here compares this against the
        most recent 'input context menu' / 'screen': gameplay is current only when its line is the
        newest context transition.
        """
        t_game, t_other = -1.0, -1.0
        for line in self.log_text().splitlines():
            if "input context" not in line:
                continue
            try:
                t = float(line[1:line.index("]")])
            except ValueError:
                continue
            if "input context gameplay" in line:
                t_game = max(t_game, t)
            elif "input context menu" in line or "input context screen" in line:
                t_other = max(t_other, t)
        return t_game if t_game > t_other else -1.0

    def in_gameplay(self):
        return self.last_gameplay_time() > 0

    def scene_times(self):
        """(hub_alive, sortie_alive): which scene owns the screen, from the thread lifetimes.

        MissionSelectScene is started ONCE and kept for the whole session (it is not restarted when a
        mission ends), so its `started` line says nothing about now. What is decisive is whether it
        has EXITED, and whether an Ac4SortieScene is currently running:
          hub     MissionSelectScene started, not exited, and no sortie running
          sortie  an Ac4SortieScene started and has not exited
        Both are counts, so a second sortie's start/exit pair balances out.
        """
        hub_net = 0
        sortie_net = 0
        for line in self.log_text().splitlines():
            if "'MissionSelectScene' started" in line:
                hub_net += 1
            elif "'MissionSelectScene' exited" in line:
                hub_net -= 1
            elif "'Ac4SortieScene' started" in line:
                sortie_net += 1
            elif "'Ac4SortieScene' exited" in line:
                sortie_net -= 1
        return hub_net > 0, sortie_net > 0

    def at_hub_scene(self):
        hub, sortie = self.scene_times()
        return hub and not sortie

    def sortie_running(self):
        hub, sortie = self.scene_times()
        return sortie

    def shot(self, timeout=20.0):
        """A PNG written after this call started (names restart at shot_000, so key on name+mtime)."""
        before = {(p, os.path.getmtime(p)) for p in glob.glob(os.path.join(self.shots, "shot_*.png"))}
        end = time.time() + timeout
        while time.time() < end:
            new = {(p, os.path.getmtime(p)) for p in glob.glob(os.path.join(self.shots, "shot_*.png"))} - before
            if new:
                time.sleep(0.35)
                return max(glob.glob(os.path.join(self.shots, "shot_*.png")), key=os.path.getmtime)
            time.sleep(0.15)
        return None

    def press(self, buttons, port=0, hold=200, settle=0.9):
        # A neutral line BEFORE the press: the rig coalesces two identical lines into one.
        self._w("0 p%d none 0 0 0 0" % port)
        time.sleep(0.35)
        self._w("%d p%d %s 0 0 0 0" % (hold, port, buttons))
        time.sleep(0.35 + hold / 1000.0)
        self._w("0 p%d none 0 0 0 0" % port)
        time.sleep(settle)
        return self.shot()

    def _w(self, line):
        with open(self.cmd_path, "a", newline="\n") as f:
            f.write(line + "\n")

    def state(self, log=print, label=""):
        s = self.shot()
        if not s:
            log("  %s no screenshot" % label)
            return "?", {}
        cls, sc = classify(s)
        focus = hub_focus(load(s)) if cls == "hub" else "-"
        log("  %-24s %-11s focus=%-7s mid=%5.1f gold=%6.1f tab=%4.0f hdr=%4.0f tbl=%4.0f dlg=%4.0f/%4.0f ok=%4.0f"
            % (label, cls, focus, sc["mid_col"], sc["title_gold"], sc["tl_tab"], sc["hdr_right"],
               sc["table_area"], sc["dialog_band"], sc["dialog_gold"], sc.get("dialog_ok", 0.0)))
        sc["focus"] = focus
        return cls, sc


def dialog_focus(a):
    """'OK' / 'CANCEL' / '?' from the gold fill on the dialog's two buttons.

    Mapped from a real "Accept this mission?" frame (research\\e9_shots\\m4_*): the box spans
    x 437..843, y 308..408, and its two buttons are OK at x 456..632 and CANCEL at x 647..822,
    y 363..401. The FOCUSED button is filled gold (R well above B, measured ~+60 to +89 on CANCEL in
    the frames where it opened) while the other is flat (~0). The focus starts on CANCEL every time
    the dialog opens, which is why a plain A cancels the mission.
    """
    ok = gold_score(a, 456, 363, 632, 401)
    canc = gold_score(a, 647, 363, 822, 401)
    if canc > ok + 8:
        return "CANCEL", ok, canc
    if ok > canc + 8:
        return "OK", ok, canc
    return "?", ok, canc


# ---------------------------------------------------------------- the verified walk
def to_gameplay(i, log=print, max_actions=70, hub_only=False):
    """Drive this instance to gameplay, verifying the screen before every action. Returns True/False.

    The three phases are the ones that actually work (measured, same session):
      1. dismiss the startup dialogs, then START until the title menu is on screen;
      2. A on the default LOAD GAME, which reads the save and lands in the hub;
      3. hub -> mission with the five verified steps B/LEFT/A/LEFT/A, re-checking the screen each time.
    Nothing here decides from a log substring alone (the log accumulates across missions); the screen
    is classified numerically before every action and the log is only used for recency-checked scenes.
    """
    # A fresh instance writes its first clock.txt a few seconds after start (s28c's drivers quit here
    # because they were launched 3 s after the game): wait for it instead of failing.
    end = time.time() + 90
    while not (i.clock() > 0) and time.time() < end:
        time.sleep(1)
    if not (i.clock() > 0):
        log("no clock.txt: the game is not running")
        return False
    if i.in_gameplay():
        log("already in gameplay")
        return True

    # Phase 1: startup dialogs, then START until the title menu. A press inside a movie is swallowed,
    # and the attract loop never goes quiet, so blind STARTs ARE the right tool here.
    log("phase 1: dialogs + START until the title menu")
    for b in ("A", "B", "A+B", "A", "B", "A"):
        i.press(b, settle=1.6)
    for n in range(30):
        cls, _ = i.state(log, "START %d" % n)
        if cls == "title_menu":
            log("phase 1: title menu up after %d START(s), clock %.1f" % (n + 1, i.clock()))
            break
        if cls in ("hub", "schematics") or (cls == "dialog" and i.at_hub_scene()):
            log("phase 1: already past the title (%s)" % cls)
            break
        i.press("START", hold=150, settle=2.2)
    else:
        log("phase 1: FAILED to reach the title menu (clock %.1f)" % i.clock())
        return False

    # Phase 2: LOAD GAME (the default) reads the save; the hub follows. Once the HUB is on screen,
    # STOP pressing: an A there opens the mission dialog, which is phase 3's job with its own rules.
    if not i.at_hub_scene():
        for k in range(5):
            cls, _ = i.state(log, "A (LOAD GAME) %d" % k)
            if cls in ("hub", "schematics", "gameplay"):
                break
            if hub_only and k > 0:
                # A hub fading in reads "other"; an A there can open AND accept the mission (s28b),
                # which the host-driven join must never do. Wait for the hub scene instead.
                time.sleep(3)
                if i.at_hub_scene():
                    break
            i.press("A", settle=3.4)
            if i.at_hub_scene():
                break
            time.sleep(4)
    if not i.at_hub_scene() and not i.in_gameplay():
        log("phase 2: FAILED, no hub (clock %.1f)" % i.clock())
        return False
    log("phase 2: HUB at clock %.1f" % i.clock())

    if hub_only:
        # The join of a host-driven session: no input at the hub; the port starts the host's mission
        # by itself (coop_session autostart). Wait for the sortie, then the briefing logic below.
        log("phase 3: hub-only, waiting for the automatic mission start")
        end = time.time() + 900
        while time.time() < end and not i.sortie_running():
            time.sleep(1)
        if not i.sortie_running():
            log("phase 3: no automatic start (clock %.1f)" % i.clock())
            return False
        log("phase 3: AUTOSTART seen at clock %.1f" % i.clock())
    # Phase 3: hub -> mission. THE FIVE VERIFIED STEPS. Focus starts on the buttons column.
    log("phase 3: hub -> mission (B, LEFT, A, LEFT, A; each step verified)")
    for step in range(max_actions):
        if i.in_gameplay():
            log("phase 3: GAMEPLAY at clock %.1f" % i.clock())
            return True
        cls, sc = i.state(log, "step %d" % step)
        if cls == "schematics":
            # The trap: LEFT+A from here reopens this panel. B leaves it.
            i.press("B", settle=1.3)
        elif cls == "dialog":
            # "Accept this mission?" opens with CANCEL focused. The proven sequence (runs 083555 and
            # 085419 reached gameplay with it) is LEFT once, then A. The focus indicator is NOT
            # reliable enough to branch on - the unfocused OK button reads 0 gold as often as a
            # faded one does - so the condition tested is the outcome instead: did the dialog close?
            # A 200 ms LEFT is swallowed here often enough to make a join take 17 attempts (r29);
            # 500 ms is the measured tap that moves the focus. If the dialog is a SCHEMATICS
            # "Load this schematic?" box (bright top-left tab), the answer is B, not LEFT+A.
            if sc.get("tl_tab", 0) > 150:
                i.press("B", hold=500, settle=1.4)
                continue
            for attempt in range(3):
                # If OK already carries the gold focus, do not move it to CANCEL.
                if sc.get("dialog_ok", 0.0) > sc.get("dialog_gold", 0.0) + 4.0:
                    i.press("A", hold=300, settle=3.0)
                else:
                    i.press("LEFT", hold=700, settle=1.8)
                    i.press("A", hold=300, settle=3.0)
                cls2, _ = i.state(log, "      after LEFT+A (%d)" % attempt)
                if cls2 != "dialog":
                    break
        elif cls == "hub":
            # Focus state decides the next press, and it is read from the screen, not assumed:
            #   buttons -> LEFT (moves to the mission list);  mission -> A (opens the accept dialog)
            if sc.get("focus") == "buttons" or sc.get("focus") == "?":
                i.press("LEFT", hold=500, settle=1.4)
            else:
                i.press("A", hold=250, settle=2.2)
        elif cls == "gameplay":
            return True
        else:
            i.press("A", settle=2.4)  # a fade or a briefing: A advances it
        if i.sortie_running() and not i.in_gameplay():
            # The sortie's pre-gameplay part is the briefing MOVIE (brf_XXXX.pam, ~55 s here). Nothing
            # is pressed during a movie, so: wait, checking every second, for up to 70 s, then a few
            # A taps for a "BATTLE START" banner. Gameplay follows ~55 s after the sortie starts.
            log("phase 3: sortie scene running (briefing), waiting without input")
            end = time.time() + 70
            while time.time() < end:
                if i.in_gameplay():
                    log("phase 3: GAMEPLAY at clock %.1f" % i.clock())
                    return True
                time.sleep(1)
            for _ in range(4):
                if i.in_gameplay():
                    log("phase 3: GAMEPLAY at clock %.1f" % i.clock())
                    return True
                i.press("A", settle=2.6)
    log("phase 3: FAILED after %d actions (clock %.1f)" % (max_actions, i.clock()))
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--shots")
    ap.add_argument("--runs")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--hub-only", action="store_true")
    a = ap.parse_args()
    i = Inst(a.tag, a.shots, a.runs)
    t0 = time.time()
    ok = to_gameplay(i, hub_only=a.hub_only)
    print("RESULT: %s after %.0f s (clock %.1f)" % ("GAMEPLAY" if ok else "FAILED", time.time() - t0, i.clock()))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

