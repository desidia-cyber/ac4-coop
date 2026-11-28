"""tools/coop_test_suite.py -- Automated, crash-resumable multi-mission AC4 co-op test runner.

Drives a single persistent Host + Joiner pair through any list of AC4 campaign missions using the
in-process C++ autotest harness (AC4_COOP_AUTOTEST=1 in coop_session.cpp) — zero PNG classification,
zero hub UI clicking, zero cmd.txt polling. If a mission crashes or hangs, the runner logs the
failure, restarts the pair, and automatically resumes from the next mission.

Usage:
  python tools/coop_test_suite.py --preset pilot
  python tools/coop_test_suite.py --preset all
  python tools/coop_test_suite.py --preset chara_deaths
  python tools/coop_test_suite.py --missions m000,m080:hard:killp2,m350
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AC4_EXE = os.path.join(ROOT, "run", "ac4.exe")
OVERLAY = os.path.join(ROOT, "payload", "overlay_coop")
DISC = os.path.join(ROOT, "disc")
DATA_HOST = os.path.join(ROOT, "run", "data")
DATA_JOIN = os.path.join(ROOT, "run", "data2")
FLASH_HOST = os.path.join(DATA_HOST, "dev_flash")
FLASH_JOIN = os.path.join(DATA_JOIN, "dev_flash")
RIG_SCRIPT = os.path.join(ROOT, "run", "rig", "coop.txt")

ALL_OVERLAY_MISSIONS = [
    0, 10, 20, 30, 40, 51, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170,
    180, 190, 200, 210, 220, 230, 240, 250, 260, 270, 280, 290, 300, 310, 320,
    330, 350, 360, 380, 490, 650, 710,
]

CHARA_DEATH_MISSIONS = [0, 10, 80, 90, 170, 200, 210, 220, 280, 310, 380, 650]


def parse_job_spec(spec, default_hold):
    """Parse 'm080', '80:hard:killp2:6', 'm010:0:smoke' -> dict."""
    parts = spec.strip().split(":")
    m_str = parts[0].lstrip("mM")
    mission = int(m_str)
    hard = 0
    scenario = "smoke"
    hold = default_hold
    if len(parts) >= 2 and parts[1]:
        hard = 1 if parts[1].lower() in ("1", "hard", "h", "true") else 0
    if len(parts) >= 3 and parts[2]:
        scenario = parts[2].lower()
    if len(parts) >= 4 and parts[3]:
        hold = float(parts[3])
    return {"mission": mission, "hard": hard, "scenario": scenario, "hold": hold}


def build_jobs(args):
    jobs = []
    if args.missions:
        for tok in args.missions.split(","):
            if tok.strip():
                jobs.append(parse_job_spec(tok, args.hold))
        return jobs
    if args.preset == "pilot":
        for m in (0, 80, 350):
            jobs.append({"mission": m, "hard": 0, "scenario": "smoke", "hold": args.hold})
    elif args.preset == "all":
        for m in ALL_OVERLAY_MISSIONS:
            jobs.append({"mission": m, "hard": 0, "scenario": "smoke", "hold": args.hold})
    elif args.preset == "chara_deaths":
        for m in CHARA_DEATH_MISSIONS:
            jobs.append({"mission": m, "hard": 0, "scenario": "killp1", "hold": args.hold})
            jobs.append({"mission": m, "hard": 0, "scenario": "killp2", "hold": args.hold})
    return jobs


def kill_pair(procs):
    for p in procs:
        if p is None:
            continue
        try:
            if p.poll() is None:
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(p.pid)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
        except Exception:
            try:
                p.kill()
            except Exception:
                pass
    time.sleep(0.5)


def write_queue(path, jobs):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        for j in jobs:
            f.write(f"{j['mission']} {j['hard']} {j['scenario']} {j['hold']:.1f}\n")


def read_jsonl(path):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return rows


def launch_pair(queue_path, host_report, join_report, port):
    base_env = os.environ.copy()
    base_env["AC4_COOP_AUTOTEST"] = "1"
    base_env["AC4_COOP_DUMP"] = "0"
    for k in ("AC4_COOP_TEST_MISSION", "AC4_COOP_TEST_HARD", "AC4_COOP_TEST_AUTOKILL", "AC4_COOP_TEST_KILLP1", "AC4_COOP_TEST_KILLP2"):
        base_env.pop(k, None)

    host_env = base_env.copy()
    host_env["AC4_COOP_TEST_QUEUE"] = queue_path
    host_env["AC4_COOP_TEST_REPORT"] = host_report

    join_env = base_env.copy()
    join_env["AC4_COOP_TEST_REPORT"] = join_report

    common = [
        AC4_EXE,
        "--disc", DISC,
        "--test-overlay", OVERLAY,
        "--hidden",
        "--isolate-input",
        "--rig", RIG_SCRIPT,
        "--fps", "60",
    ]
    host_cmd = common + ["--data", DATA_HOST, "--flash", FLASH_HOST, "--coop-host", str(port)]
    join_cmd = common + ["--data", DATA_JOIN, "--flash", FLASH_JOIN, "--coop-join", f"127.0.0.1:{port}"]

    p_host = subprocess.Popen(host_cmd, cwd=ROOT, env=host_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    p_join = subprocess.Popen(join_cmd, cwd=ROOT, env=join_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return p_host, p_join


def evaluate(job, host_rec, join_rec):
    if host_rec is None:
        return "FAIL", "no host record"
    st = host_rec.get("status", "")
    if st == "not_in_table":
        return "SKIP", "not in MissionParamTable"
    if st != "completed":
        return st.upper(), host_rec.get("reason", st)

    issues = []
    if not host_rec.get("barrier_ok"):
        issues.append("host_barrier_fail")
    if not host_rec.get("p1_ok") or not host_rec.get("p2_ok"):
        issues.append("host_missing_body")
    if host_rec.get("lua_errors", 0) > 0:
        issues.append(f"host_lua_err({host_rec['lua_errors']}:{host_rec.get('first_lua_err', '')[:40]})")
    if not host_rec.get("result_synced"):
        issues.append("host_no_result")

    if join_rec is None:
        issues.append("join_no_record")
    else:
        if not join_rec.get("barrier_ok"):
            issues.append("join_barrier_fail")
        if not join_rec.get("p1_ok") or not join_rec.get("p2_ok"):
            issues.append("join_missing_body")
        if join_rec.get("lua_errors", 0) > 0:
            issues.append(f"join_lua_err({join_rec['lua_errors']}:{join_rec.get('first_lua_err', '')[:40]})")
        if not join_rec.get("result_synced"):
            issues.append("join_no_result")

    # P2 movement check (soft warning if mission locks controls/cutscene at start, hard check otherwise)
    p2_disp = host_rec.get("p2_disp", 0.0)
    if issues:
        return "FAIL", "; ".join(issues)
    note = f"p2_disp={p2_disp:.1f}m enemies={host_rec.get('max_enemies', 0)} ev={host_rec.get('host_events', 0)}"
    if join_rec:
        note += f"/{join_rec.get('join_replayed', 0)}"
    note += f" t={host_rec.get('total_s', 0):.1f}s"
    return "PASS", note


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", choices=["pilot", "all", "chara_deaths"], default="pilot")
    ap.add_argument("--missions", default="", help="Comma-separated specs e.g. m000,m080:hard:killp2")
    ap.add_argument("--hold", type=float, default=6.5, help="Seconds in gameplay before triggering scenario")
    ap.add_argument("--timeout", type=float, default=150.0, help="Per-mission timeout seconds")
    ap.add_argument("--boot-grace", type=float, default=90.0, help="Extra grace seconds for initial boot to hub")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    jobs = build_jobs(args)
    if not jobs:
        print("No jobs to run.")
        return 1

    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.out or os.path.join(ROOT, "run", "autotest", ts)
    os.makedirs(out_dir, exist_ok=True)

    print(f"=== AC4 Co-op Autotest Suite: {len(jobs)} job(s) -> {out_dir} ===", flush=True)

    results = []
    idx = 0
    attempt_num = 0
    while idx < len(jobs):
        attempt_num += 1
        remaining = jobs[idx:]
        q_path = os.path.join(out_dir, f"queue_{attempt_num}.txt")
        h_rep = os.path.join(out_dir, f"host_{attempt_num}.jsonl")
        j_rep = os.path.join(out_dir, f"join_{attempt_num}.jsonl")
        write_queue(q_path, remaining)

        session_port = args.port + (attempt_num - 1)
        print(f"\n[session {attempt_num}] Launching Host + Joiner on udp/{session_port} for {len(remaining)} remaining job(s) (starting at m{remaining[0]['mission']:03d})...", flush=True)
        p_host, p_join = launch_pair(q_path, h_rep, j_rep, session_port)

        seen_host = 0
        last_progress = time.time()
        first_mission = True
        need_restart = False

        while idx < len(jobs):
            time.sleep(1.0)
            h_rows = read_jsonl(h_rep)
            j_rows = read_jsonl(j_rep)
            h_missions = [r for r in h_rows if r.get("event") == "mission"]
            j_missions = [r for r in j_rows if r.get("event") == "mission"]

            if len(h_missions) > seen_host:
                # Wait briefly for join to flush its matching epoch record too
                while seen_host < len(h_missions):
                    h_rec = h_missions[seen_host]
                    seen_host += 1
                    job = jobs[idx]
                    idx += 1
                    first_mission = False
                    last_progress = time.time()

                    j_rec = None
                    if h_rec.get("status") == "completed":
                        ep = h_rec.get("epoch")
                        wait_end = time.time() + 12.0
                        while time.time() < wait_end:
                            j_rows = read_jsonl(j_rep)
                            j_missions = [r for r in j_rows if r.get("event") == "mission"]
                            for jr in j_missions:
                                if jr.get("epoch") == ep:
                                    j_rec = jr
                                    break
                            if j_rec is not None:
                                break
                            time.sleep(0.5)

                    verdict, note = evaluate(job, h_rec, j_rec)
                    rec = {"job": job, "verdict": verdict, "note": note, "host": h_rec, "join": j_rec}
                    results.append(rec)
                    diff = "HARD" if job["hard"] else "NORM"
                    print(f"  [{len(results):2d}/{len(jobs)}] m{job['mission']:03d} {diff:4s} {job['scenario']:8s} -> {verdict:4s} | {note}", flush=True)
                continue

            # Check process liveness
            hc = p_host.poll()
            jc = p_join.poll()
            if hc is not None or jc is not None:
                job = jobs[idx]
                reason = f"process_exit(host={hc}, join={jc})"
                print(f"  [{len(results)+1:2d}/{len(jobs)}] m{job['mission']:03d} -> CRASH | {reason}", flush=True)
                results.append({"job": job, "verdict": "CRASH", "note": reason, "host": None, "join": None})
                idx += 1
                need_restart = True
                break

            # Check hang timeout
            limit = args.timeout + (args.boot_grace if first_mission else 0.0)
            if time.time() - last_progress > limit:
                job = jobs[idx]
                reason = f"timeout({limit:.0f}s)"
                print(f"  [{len(results)+1:2d}/{len(jobs)}] m{job['mission']:03d} -> TIMEOUT | {reason}", flush=True)
                results.append({"job": job, "verdict": "TIMEOUT", "note": reason, "host": None, "join": None})
                idx += 1
                need_restart = True
                break

        kill_pair((p_host, p_join))
        if not need_restart:
            break

    # Save JSON + Markdown summary
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    counts = {}
    for r in results:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1

    md_path = os.path.join(out_dir, "summary.md")
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(f"# AC4 Co-op Autotest Summary ({ts})\n\n")
        f.write(f"Total: **{len(results)}** — " + ", ".join(f"**{k}**: {v}" for k, v in sorted(counts.items())) + "\n\n")
        f.write("| Mission | Diff | Scenario | Verdict | Details |\n|---|---|---|---|---|\n")
        for r in results:
            j = r["job"]
            diff = "HARD" if j["hard"] else "NORM"
            f.write(f"| `m{j['mission']:03d}` | {diff} | `{j['scenario']}` | **{r['verdict']}** | {r['note']} |\n")

    print("\n=== SUMMARY ===", flush=True)
    for k, v in sorted(counts.items()):
        print(f"  {k}: {v}")
    print(f"Report: {md_path}", flush=True)
    return 0 if counts.get("FAIL", 0) == 0 and counts.get("CRASH", 0) == 0 and counts.get("TIMEOUT", 0) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
