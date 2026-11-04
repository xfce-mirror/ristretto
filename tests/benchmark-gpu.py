#!/usr/bin/env python3
"""Interleaved CPU/GPU comparison. Standard library only; never changes settings.

Both binaries must use this branch's test-image-viewer benchmark harness. The
CPU baseline application sources remain exactly at 0744b09d; --render-fix instead
uses an exact saved experimental snapshot. Every completed run, including
failures, is retained. Sampling is outside the measured application.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import signal
import subprocess
import sys
import time


def children(pid):
    """Find process descendants through every task, tolerating process exit."""
    try:
        direct = set()
        for task in Path(f"/proc/{pid}/task").iterdir():
            try:
                direct.update(int(p) for p in (task / "children").read_text().split())
            except OSError:
                pass
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return []
    return list(direct) + [c for p in direct for c in children(p)]


def process_info(pid):
    """Read the process name and RSS in bytes without failing on exited tasks."""
    try:
        status = Path(f"/proc/{pid}/status").read_text()
        name = re.search(r"^Name:\s*(.*)$", status, re.M).group(1)
        rss = re.search(r"^VmRSS:\s*(\d+)", status, re.M)
        return name, int(rss.group(1)) * 1024 if rss else 0
    except (OSError, AttributeError):
        return "", 0


def drm_memory(pid):
    """Sum DRM memory counters once per client across the process file handles."""
    clients = {}
    try:
        for fd in Path(f"/proc/{pid}/fdinfo").iterdir():
            try:
                text = fd.read_text()
            except OSError:
                continue
            client = re.search(r"^drm-client-id:\s*(\d+)", text, re.M)
            if not client:
                continue
            values = {}
            for key, value, unit in re.findall(r"^(drm-(?:memory|resident|shared|active)-[\w-]+):\s*(\d+)\s*(\w*)", text, re.M):
                values[key] = int(value) * {"KiB": 1024, "MiB": 1048576, "GiB": 1073741824}.get(unit, 1)
            clients[client.group(1)] = values
    except OSError:
        pass
    return {key: sum(c.get(key, 0) for c in clients.values()) for c in clients.values() for key in c}


def run(args, name, binary, workers, repeat):
    """Run one isolated workload and retain its log, outcome and memory peaks."""
    tag = f"r{repeat + 1}-{name}"
    path = args.output / f"{tag}.log"
    env = os.environ.copy()
    env.update(GIO_USE_VFS="local", NO_AT_BRIDGE="1", RISTRETTO_PROFILE="1", RSTTO_BENCH_LARGE="1",
               RSTTO_BENCH_FIXTURES=str(args.fixtures), RISTRETTO_RENDERER="placebo" if workers else "cpu",
               RISTRETTO_GL_WORKERS=str(workers or 2))
    state = args.output / (tag + "-state")
    state.mkdir()
    env.update(XDG_CONFIG_HOME=str(state / "config"), XDG_DATA_HOME=str(state / "data"),
               XDG_CACHE_HOME=str(state / "cache"), MESA_SHADER_CACHE_DIR=str(state / "mesa"))
    if args.drag:
        env["RSTTO_BENCH_DRAG"] = "1"
    if args.render_fix:
        env["RSTTO_BENCH_ZOOM_STREAM"] = "1"
    env.pop("RSTTO_BENCH_COLD", None)
    env.pop("GDK_GL", None)
    env.pop("GDK_SCALE", None)
    if workers and args.audit:
        env["LD_PRELOAD"] = str(args.audit)
    else:
        env.pop("LD_PRELOAD", None)
    command = ["dbus-run-session", "--config-file=" + str(Path(__file__).with_name("benchmark-dbus.conf")),
               "--", sys.executable, str(Path(__file__).resolve()), "--session", str(args.thumbnailer), str(binary)]
    peaks, phase = {}, "setup"
    start = time.monotonic()
    timed_out = False
    with path.open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
        with path.open() as reader:
            while process.poll() is None:
                for match in re.finditer(r"BENCH_PHASE name=(\w+)", reader.read()):
                    phase = match.group(1)
                sample = peaks.setdefault(phase, {})
                for pid in children(process.pid):
                    comm, rss = process_info(pid)
                    if not comm.startswith("test-image-v"):
                        continue
                    sample["parent_rss"] = max(sample.get("parent_rss", 0), rss)
                    helper = sum(r for c in children(pid) for n, r in [process_info(c)] if "glycin" in n)
                    sample["helper_rss"] = max(sample.get("helper_rss", 0), helper)
                    for key, value in drm_memory(pid).items():
                        sample[key] = max(sample.get(key, 0), value)
                if time.monotonic() - start > 180:
                    os.killpg(process.pid, signal.SIGTERM)
                    timed_out = True
                    break
                time.sleep(.05)
        code = process.wait(timeout=15)
    contents = path.read_text()
    complete = (all(f"BENCH_PHASE name={p}" in contents for p in ("cold", "warm", "scroll", "zoom"))
                and len(re.findall(r"navigation sample=", contents)) == 16
                and len(re.findall(r"zoom sample=", contents)) == 12
                and "scroll-summary inputs=48" in contents
                and (not args.drag or "drag-summary inputs=480" in contents)
                and (not args.render_fix or "zoom-stream-summary inputs=48" in contents))
    if not complete and code == 0:
        code = 99
    result = dict(name=name, workers=workers, repeat=repeat + 1, exit_code=code, timed_out=timed_out,
                  complete=complete,
                  elapsed_s=time.monotonic() - start, memory_peaks=peaks, log=str(path), command=command)
    (args.output / f"{tag}.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("name", "repeat", "exit_code", "elapsed_s")}), flush=True)
    return result


def main():
    """Build and execute the interleaved comparison schedule from command options."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--zoom-baseline", type=Path, help="Saved zoom-fixed binary for three-way recovery comparison")
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--audit", type=Path)
    parser.add_argument("--thumbnailer", type=Path)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--render-fix", action="store_true", help="Compare old/new GPU worker=1, including sustained zoom")
    parser.add_argument("--drag", action="store_true", help="Include 60 Hz held-button dragging")
    parser.add_argument("--only", help="Comma-separated configuration names for diagnostic runs")
    args = parser.parse_args()
    if args.thumbnailer is None:
        args.thumbnailer = args.experiment.parent.parent / "tests/benchmark-thumbnailer"
    for key in ("baseline", "zoom_baseline", "experiment", "output", "fixtures", "audit", "thumbnailer"):
        value = getattr(args, key)
        if value is not None:
            setattr(args, key, value.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    args.fixtures.mkdir(parents=True, exist_ok=True)
    configurations = [("baseline", args.baseline, 0), ("cpu", args.experiment, 0)]
    configurations += [(f"gpu{n}", args.experiment, n) for n in range(1, 6)]
    if args.render_fix:
        configurations = [("old", args.baseline, 1), ("new", args.experiment, 1)]
        if args.zoom_baseline:
            configurations.insert(1, ("zoomfixed", args.zoom_baseline, 1))
    if args.only:
        configurations = [c for c in configurations if c[0] in args.only.split(",")]
    schedule = []
    rng = random.Random(20261103)
    for repeat in range(args.repetitions):
        order = configurations.copy()
        rng.shuffle(order)
        schedule.extend((repeat, *config) for config in order)
    manifest = dict(schedule=[(r + 1, n, w) for r, n, _, w in schedule],
                    binaries={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.baseline, args.zoom_baseline, args.experiment) if p is not None},
                    sampling_ms=50, fixtures=str(args.fixtures), thumbnails="disabled in isolated D-Bus session")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    results = []
    for repeat, name, binary, workers in schedule:
        result = run(args, name, binary, workers, repeat)
        results.append(result)
        (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        if result["exit_code"] != 0:
            raise SystemExit(f"Failed run retained at {result['log']}; stopping comparison")
    (args.output / "fixtures.sha256").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in sorted(args.fixtures.glob("*.jpg"))))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--session":
        stub = subprocess.Popen([sys.argv[2]], stdout=subprocess.PIPE, text=True,
                                env=dict(os.environ, RISTRETTO_BENCH_PRIVATE_BUS="1"))
        try:
            if stub.stdout.readline().strip() != "READY":
                raise SystemExit("Benchmark thumbnail service failed to start")
            scenarios = ["cold", "warm", "scroll", "zoom"]
            if os.environ.get("RSTTO_BENCH_ZOOM_STREAM"):
                scenarios.append("zoomstream")
            if os.environ.get("RSTTO_BENCH_DRAG"):
                scenarios.append("drag")
            for scenario in scenarios:
                code = subprocess.call([sys.argv[3], f"--benchmark-scenario={scenario}"])
                if code:
                    raise SystemExit(code)
        finally:
            stub.terminate()
            stub.wait(timeout=5)
    else:
        main()
