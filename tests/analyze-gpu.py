#!/usr/bin/env python3
"""Summarize every completed benchmark run; no filtering of slow samples."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics as stats


def values(line):
    """Parse profiling key/value fields, preserving nonnumeric values."""
    result = {}
    for key, value in re.findall(r"([\w-]+)=([^\s]+)", line):
        try:
            result[key] = float(value) if "." in value else int(value)
        except ValueError:
            result[key] = value
    return result


def distribution(items):
    """Summarize all samples with median, p95 and maximum values."""
    items = sorted(items)
    return {"n": len(items), "median": stats.median(items),
            "p95": items[math.ceil(.95 * len(items)) - 1], "max": items[-1]} if items else {}


def parse(run):
    """Collect phase timings, resource counters and reuse evidence from one run."""
    phases = {}
    current = None
    conversions = {}
    audit = dict(draws=0, readbacks=0)
    deferred = {}
    for line in Path(run["log"]).read_text().splitlines():
        v = values(line)
        if "BENCH_PHASE" in line:
            current = phases.setdefault(v["name"], dict(events={}, navigation=[], selection=[], zoom=[], zoom_stream=[], drag=[], discarded=0, shader_programs=0, image_programs=0, binary_hits=0, binary_misses=0, binary_writes=0, deferrals={}))
            deferred.clear()
        if "RSTTO_GL_AUDIT" in line:
            audit["draws"] += v["draws"]
            audit["readbacks"] += v["readbacks"]
        if current is None:
            continue
        events = current["events"]
        if "RSTTO_PROFILE" in line:
            event, ms = v["event"], v["duration_us"] / 1000
            events.setdefault(event, []).append(ms)
            if event == "schedule-job":
                job = (v["object"], v["id"])
                if v["state"] == "admitted":
                    if job in deferred:
                        events.setdefault("admission-wait", []).append((v["start_us"] - deferred.pop(job)) / 1000)
                else:
                    deferred.setdefault(job, v["start_us"])
                    current["deferrals"][v["state"]] = current["deferrals"].get(v["state"], 0) + 1
            if event == "shader-cache-load":
                current["binary_hits" if v["hit"] else "binary_misses"] += 1
            if event == "shader-cache-save":
                current["binary_writes"] += v["saved"]
            if event == "gpu-program":
                current["shader_programs"] += 1
            if event == "gpu-submit":
                current["image_programs"] += v.get("new_programs", 0)
            if event == "pixel-conversion":
                conversions[v["object"]] = ms
            if event == "worker" and v.get("phase") == 4:
                conversion = conversions.pop(v["object"], 0)
                events.setdefault("decode-excluding-conversion", []).append(max(0, ms - conversion))
                if v.get("demand"):
                    events.setdefault("foreground-decode", []).append(ms)
            if event in ("gpu-prepare", "render-prepare") and v.get("demand"):
                events.setdefault("foreground-prepare", []).append(ms)
            if event == "gpu-render":
                for key in ("upload_gpu_ms", "scale_gpu_ms"):
                    events.setdefault(key, []).append(v[key])
            if event == "gpu-submit":
                key = "gpu-submit-first-use" if v.get("new_programs", 0) else "gpu-submit-warm"
                events.setdefault(key, []).append(ms)
            if event == "request-to-ready":
                current["peak_accounted"] = max(current.get("peak_accounted", 0), v["resident"] + v["reserved"])
            if event == "work-discarded":
                current["discarded"] += 1
        elif "startup first_image_ms=" in line:
            current["startup"] = v
        elif "drag-presented input=" in line:
            current["drag"].append(v)
        elif "drag-summary inputs=" in line:
            current["drag_summary"] = v
        elif "zoom-presented input=" in line:
            current["zoom_stream"].append(v)
        elif "zoom-stream-summary inputs=" in line:
            current["zoom_stream_summary"] = v
        elif "navigation sample=" in line:
            current["navigation"].append(v["elapsed_ms"])
        elif "selection-presented" in line:
            current["selection"].append(v)
        elif "zoom sample=" in line:
            current["zoom"].append(v["elapsed_ms"])
        elif "scroll-summary" in line:
            current["scroll"] = v
        elif "gpu-source-summary" in line:
            current["gpu_source"] = v
        elif "gpu-summary" in line:
            current["gpu"] = v
    for phase, data in phases.items():
        data["events"] = {k: distribution(v) for k, v in data["events"].items()}
        data["drag_gaps"] = distribution([v["gap_ms"] for v in data["drag"] if v["held"]])
        data["drag_revision_rollback"] = any(a["revision"] > b["revision"] for a, b in zip(data["drag"], data["drag"][1:]))
        data["navigation"] = distribution(data["navigation"])
        data["zoom"] = distribution(data["zoom"])
        data["zoom_stream_latest"] = distribution([v["latency_ms"] for v in data["zoom_stream"] if v["latest"]])
        data["zoom_stream_stale"] = sum(not v["latest"] for v in data["zoom_stream"])
        data["zoom_stream"] = distribution([v["latency_ms"] for v in data["zoom_stream"]])
        data["selection_latest"] = distribution([v["latency_ms"] for v in data["selection"] if v["latest"]])
        data["selection"] = distribution([v["latency_ms"] for v in data["selection"]])
    return dict(name=run["name"], repeat=run["repeat"], exit_code=run["exit_code"],
                phases=phases, memory=run["memory_peaks"], audit=audit)


def main():
    """Aggregate retained runs and write comparison data and a report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    runs = [parse(r) for r in json.loads((args.directory / "results.json").read_text())]
    groups = {}
    for run in runs:
        if run["exit_code"]:
            raise SystemExit("A comparison run failed; do not aggregate it as a success")
        groups.setdefault(run["name"], []).append(run)
    summary = {}
    for name, group in groups.items():

        def med(path):
            """Compute the median available value at a nested path across this group."""
            items = []
            for run in group:
                item = run
                for key in path:
                    item = item.get(key, {})
                if isinstance(item, (int, float)):
                    items.append(item)
            return stats.median(items) if items else None

        row = {"runs": len(group)}
        for phase, sample in (("cold", "navigation"), ("warm", "navigation"), ("scroll", "selection"), ("zoom", "zoom")):
            for stat in ("median", "p95"):
                row[f"{phase}_{stat}_ms"] = med(("phases", phase, sample, stat))
        for phase in ("cold", "warm", "zoom", "zoomstream"):
            row[phase + "_first_image_ms"] = med(("phases", phase, "startup", "first_image_ms"))
        for metric in ("frames", "moving_frames", "fps", "final_ms", "settled", "decodes"):
            row["drag_" + metric] = med(("phases", "drag", "drag_summary", metric))
        row["drag_gap_p95_ms"] = med(("phases", "drag", "drag_gaps", "p95"))
        for phase in ("drag", "zoom", "zoomstream"):
            row[phase + "_uploads"] = med(("phases", phase, "gpu", "uploads"))
            row[phase + "_source_hits"] = med(("phases", phase, "gpu_source", "hits"))
            row[phase + "_gpu_peak_bytes"] = med(("phases", phase, "gpu", "peak_bytes"))
            for event in ("gpu-prepare", "gpu-upload", "upload_gpu_ms", "scale_gpu_ms", "ready-to-presentation"):
                row[phase + "_" + event] = med(("phases", phase, "events", event, "median"))
        row["zoom_stream_p95_ms"] = med(("phases", "zoomstream", "zoom_stream_latest", "p95"))
        row["zoom_stream_frames"] = med(("phases", "zoomstream", "zoom_stream_summary", "frames"))
        row["zoom_stream_max_gap_ms"] = med(("phases", "zoomstream", "zoom_stream_summary", "max_gap_ms"))
        row["zoom_stream_stale"] = med(("phases", "zoomstream", "zoom_stream_stale"))
        row["zoom_image_programs"] = med(("phases", "zoom", "image_programs"))
        for metric in ("frames", "max_gap_ms", "final_target_ms", "peak_accounted_mib"):
            row[metric] = med(("phases", "scroll", "scroll", metric))
        for metric in ("peak_active", "peak_bytes", "uploads", "submissions", "discarded"):
            row["gpu_" + metric] = med(("phases", "scroll", "gpu", metric))
        row["discarded_work"] = med(("phases", "scroll", "discarded"))
        for event in ("admission-wait", "worker-queue", "glycin-metadata", "gpu-warmup", "decode-excluding-conversion", "pixel-conversion", "render-prepare", "gpu-prepare", "gpu-upload", "gpu-submit-first-use", "gpu-submit-warm", "upload_gpu_ms", "scale_gpu_ms", "gpu-paint", "cairo-paint"):
            row[event] = med(("phases", "scroll", "events", event, "median"))
        row["admission_wait_p95_ms"] = med(("phases", "scroll", "events", "admission-wait", "p95"))
        for metric in ("parent_rss", "helper_rss", "drm-memory-vram", "drm-memory-gtt"):
            row[metric] = med(("memory", "scroll", metric))
        row["audit_draws"] = sum(r["audit"]["draws"] for r in group)
        row["audit_readbacks"] = sum(r["audit"]["readbacks"] for r in group)
        summary[name] = row
    eligible = {k: v for k, v in summary.items() if k.startswith("gpu") and v["scroll_p95_ms"] is not None}
    best = min((v["scroll_p95_ms"] for v in eligible.values()), default=0)
    chosen = min((int(k[3:]) for k, v in eligible.items() if v["scroll_p95_ms"] <= 1.05 * best), default=None)
    output = dict(percentile="nearest rank; table uses median of per-run statistics", runs=runs,
                  summary=summary, chosen_workers=chosen, best_scroll_p95_ms=best)
    (args.directory / "analysis.json").write_text(json.dumps(output, indent=2) + "\n")

    def number(value):
        """Format a report value while preserving unavailable measurements."""
        return f"{value:.1f}" if value is not None else "—"

    lines = ["| Mode | Runs | Cold p95 ms | Warm p95 ms | Scroll p95 ms | Frames / 48 | Max gap ms | Zoom p95 ms |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for name in ("old", "zoomfixed", "new", "baseline", "cpu", "gpu1", "gpu2", "gpu3", "gpu4", "gpu5"):
        if name not in summary:
            continue
        r = summary[name]
        lines.append("| " + " | ".join([name, str(r["runs"])] + [number(r[k]) for k in
            ("cold_p95_ms", "warm_p95_ms", "scroll_p95_ms", "frames", "max_gap_ms", "zoom_p95_ms")]) + " |")
    if chosen is not None:
        lines += ["", f"Lowest worker count within 5% of best median scroll p95: {chosen}."]
    (args.directory / "table.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
