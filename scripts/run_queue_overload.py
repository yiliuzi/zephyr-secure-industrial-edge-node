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
    reports = ROOT / "build" / "overload_reports" / stamp
    reports.mkdir(parents=True, exist_ok=True)
    build_dir = ROOT / "build" / "runtime_overload"
    errors = []
    output = ""
    reason = ""
    drops = []
    samples = []
    continued = False

    try:
        if not QEMU.is_file():
            raise RuntimeError(f"QEMU not found: {QEMU}")

        print("Building with safety processing delay = 800 ms...", flush=True)
        with (reports / "build.log").open("w", encoding="utf-8") as log:
            build = subprocess.run(
                [
                    sys.executable, "-m", "west", "build",
                    "-b", "mps2/an385",
                    "-d", str(build_dir),
                    str(ROOT),
                    "--", "-DSAFETY_PROCESS_DELAY_MS=800",
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

        print("Observing forced queue overload for 30 seconds...", flush=True)
        output, reason = run_qemu(
            [
                str(QEMU),
                "-machine", "mps2-an385",
                "-cpu", "cortex-m3",
                "-nographic",
                "-monitor", "none",
                "-serial", "stdio",
                "-kernel", str(elf),
            ],
            reports / "runtime.log",
            30,
        )

        if reason != "Test timed out":
            errors.append(f"Application ended early: {reason}")

        seen_drop = False
        for line in output.splitlines():
            drop = re.search(r"Queue full: dropped sample (\d+)", line)
            if drop:
                drops.append(int(drop.group(1)))
                seen_drop = True

            sample = re.search(
                r"sample=(\d+) temp=(-?\d+) cC "
                r"vibration=(\d+) mg voltage=(\d+) mV",
                line,
            )
            if sample:
                samples.append(int(sample.group(1)))
                if seen_drop:
                    continued = True

        if len(drops) < 5:
            errors.append("Expected at least 5 queue-full drop events")
        if len(samples) < 10:
            errors.append("Expected at least 10 processed samples")
        if not continued:
            errors.append("No processed sample observed after first drop")

        if any(b <= a for a, b in zip(samples, samples[1:])):
            errors.append("Processed sample sequence is not strictly increasing")

        if set(samples).intersection(drops):
            errors.append("A rejected sample was also reported as processed")

        for pattern in [
            r"Failed to receive sensor sample",
            r"<err>",
            r"ASSERTION FAIL",
            r"ZEPHYR FATAL",
            r"\*{5}.*FAULT",
            r"Stack overflow",
        ]:
            if re.search(pattern, output, re.IGNORECASE):
                errors.append(f"Runtime error matched: {pattern}")

    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        errors.append(str(error))

    status = "FAIL" if errors else "PASS"
    summary = {
        "status": status,
        "observation_seconds": 30,
        "processing_delay_ms": 800,
        "sensor_period_ms": 200,
        "queue_capacity": 8,
        "processed_samples": len(samples),
        "queue_full_events": len(drops),
        "processing_continued_after_drop": continued,
        "errors": errors,
        "scope": "QEMU sustained queue overload; no recovery or hardware timing claim",
    }
    (reports / "summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    markdown = [
        "# Queue Overload Test",
        "",
        f"Overall: {status}",
        "Observation window: 30 seconds",
        "Processing delay: 800 ms; sensor period: 200 ms; queue capacity: 8",
        f"Processed samples: {len(samples)}",
        f"Queue-full events: {len(drops)}",
        f"Processing continued after first drop: {continued}",
        "",
        "## Errors",
        "",
    ]
    markdown.extend(
        [f"- {error}" for error in errors] if errors else ["None observed."]
    )
    markdown.extend(["", f"Scope: {summary['scope']}"])
    (reports / "summary.md").write_text(
        "\n".join(markdown) + "\n", encoding="utf-8"
    )

    print(f"\nQueue overload test: {status}")
    print(f"Processed samples: {len(samples)}")
    print(f"Queue-full events: {len(drops)}")
    for error in errors:
        print(f"ERROR: {error}")
    print(f"Reports: {reports}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
