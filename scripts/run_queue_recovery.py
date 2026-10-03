import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from run_regression import run_qemu

ROOT = Path(__file__).resolve().parents[1]
QEMU = Path(r"D:\qemu\qemu-system-arm.exe")


def main():
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    reports = ROOT / "build" / "recovery_reports" / stamp
    reports.mkdir(parents=True, exist_ok=True)
    build_dir = ROOT / "build" / "runtime_recovery"
    errors = []
    drops_before = []
    late_drops = []
    settled_samples = []
    release_times = []

    try:
        if not QEMU.is_file():
            raise RuntimeError(f"QEMU not found: {QEMU}")

        print("Building overload recovery application...", flush=True)
        with (reports / "build.log").open("w", encoding="utf-8") as log:
            build = subprocess.run(
                [
                    sys.executable, "-m", "west", "build",
                    "-b", "mps2/an385",
                    "-d", str(build_dir), str(ROOT),
                    "--",
                    "-DSAFETY_PROCESS_DELAY_MS=800",
                    "-DSAFETY_DELAY_RELEASE_MS=10000",
                ],
                cwd=ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
            )
        if build.returncode != 0:
            raise RuntimeError("Build failed; see build.log")

        elf = build_dir / "zephyr" / "zephyr.elf"
        if not elf.is_file():
            raise RuntimeError("Built ELF file not found")

        print("Observing overload and recovery for 30 seconds...", flush=True)
        output, reason = run_qemu(
            [
                str(QEMU),
                "-machine", "mps2-an385", "-cpu", "cortex-m3",
                "-nographic", "-monitor", "none", "-serial", "stdio",
                "-kernel", str(elf),
            ],
            reports / "runtime.log",
            30,
        )
        if reason != "Test timed out":
            errors.append(f"Application ended early: {reason}")

        # Parse Zephyr timestamps, rather than host wall-clock timing.
        timestamp = re.compile(r"\[(\d+):(\d+):(\d+)\.(\d{3}),\d{3}\]")
        release_time = None
        for line in output.splitlines():
            match = timestamp.search(line)
            if not match:
                continue
            hours, minutes, seconds, fraction = match.groups()
            moment = (
                int(hours) * 3600 + int(minutes) * 60 + int(seconds)
                + int(fraction) / (10 ** len(fraction))
            )

            if "Overload injection released" in line:
                release_times.append(moment)
                release_time = moment

            drop = re.search(r"Queue full: dropped sample (\d+)", line)
            if drop:
                if release_time is None:
                    drops_before.append(int(drop.group(1)))
                elif moment >= release_time + 2:
                    late_drops.append(int(drop.group(1)))

            sample = re.search(r"sample=(\d+) temp=", line)
            if (
                sample and release_time is not None
                and moment >= release_time + 2
            ):
                settled_samples.append((moment, int(sample.group(1))))

        if len(drops_before) < 5:
            errors.append("Expected at least 5 drops before release")
        if len(release_times) != 1:
            errors.append("Expected exactly one delay-release marker")
        elif not 10 <= release_times[0] <= 12:
            errors.append("Delay release was outside 10-12 simulated seconds")
        if late_drops:
            errors.append("Queue drops continued after the settling period")
        if len(settled_samples) < 40:
            errors.append("Expected at least 40 samples after settling")
        if settled_samples:
            if settled_samples[-1][0] - settled_samples[0][0] < 10:
                errors.append("Recovered observation span was less than 10 seconds")
            for (_, previous), (_, current) in zip(
                settled_samples, settled_samples[1:]
            ):
                if current != previous + 1:
                    errors.append(
                        f"Recovered sequence discontinuity: {previous} -> {current}"
                    )
                    break

        for pattern in [
            r"Failed to receive sensor sample", r"<err>",
            r"ASSERTION FAIL", r"ZEPHYR FATAL",
            r"\*{5}.*FAULT", r"Stack overflow",
        ]:
            if re.search(pattern, output, re.IGNORECASE):
                errors.append(f"Runtime error matched: {pattern}")

    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        errors.append(str(error))

    status = "FAIL" if errors else "PASS"
    summary = {
        "status": status,
        "observation_wall_seconds": 30,
        "processing_delay_ms": 800,
        "release_target_uptime_ms": 10000,
        "release_times_seconds": release_times,
        "settling_seconds": 2,
        "drops_before_release": len(drops_before),
        "drops_after_settling": len(late_drops),
        "samples_after_settling": len(settled_samples),
        "errors": errors,
        "scope": "QEMU queue recovery inferred from drops and processed sequences",
    }
    (reports / "summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    lines = [
        "# Queue Recovery Test", "",
        f"Overall: {status}",
        f"Drops before release: {len(drops_before)}",
        f"Drops after settling: {len(late_drops)}",
        f"Samples after settling: {len(settled_samples)}",
        f"Release times (Zephyr seconds): {release_times}",
        "", "## Errors", "",
    ]
    lines.extend(
        [f"- {error}" for error in errors] if errors else ["None observed."]
    )
    lines.extend(["", f"Scope: {summary['scope']}"])
    (reports / "summary.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )
    print(f"\nQueue recovery test: {status}")
    print(f"Drops before release: {len(drops_before)}")
    print(f"Drops after settling: {len(late_drops)}")
    print(f"Samples after settling: {len(settled_samples)}")
    for error in errors:
        print(f"ERROR: {error}")
    print(f"Reports: {reports}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
