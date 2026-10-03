import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from run_regression import run_qemu

ROOT = Path(__file__).resolve().parents[1]
QEMU = Path(r"D:\qemu\qemu-system-arm.exe")


def expected_values(sequence):
    phase = sequence % 40
    if phase < 20:
        return (
            2500 + (sequence % 5) * 10,
            200 + (sequence % 4) * 10,
            24000,
        )
    if phase < 28:
        return 7400, 1700, 21800
    if phase < 34:
        return 9200, 3200, 20500
    return 4200, 500, 23500


def main():
    if not QEMU.is_file():
        print(f"QEMU not found: {QEMU}")
        return 1

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    reports = ROOT / "build" / "runtime_reports" / stamp
    reports.mkdir(parents=True, exist_ok=True)
    build_dir = ROOT / "build" / "runtime_smoke"

    print("Building application with background threads enabled...", flush=True)
    try:
        with (reports / "build.log").open("w", encoding="utf-8") as log:
            build = subprocess.run(
                [
                    sys.executable, "-m", "west", "build",
                    "-b", "mps2/an385",
                    "-d", str(build_dir), str(ROOT),
                ],
                cwd=ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
            )

        if build.returncode != 0:
            print(f"Build failed. See: {reports / 'build.log'}")
            return 1

        print("Running application for 30 seconds...", flush=True)
        output, reason = run_qemu(
            [
                str(QEMU),
                "-machine", "mps2-an385",
                "-cpu", "cortex-m3",
                "-nographic",
                "-monitor", "none",
                "-serial", "stdio",
                "-kernel", str(build_dir / "zephyr" / "zephyr.elf"),
            ],
            reports / "runtime.log",
            30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"Runtime check failed: {error}")
        return 1

    # This application runs forever. The observation window ends deliberately.
    errors = []
    if reason != "Test timed out":
        errors.append(f"Application ended before observation window: {reason}")

    sample_pattern = re.compile(
        r"sample=(\d+) temp=(-?\d+) cC "
        r"vibration=(\d+) mg voltage=(\d+) mV"
    )
    transition_pattern = re.compile(
        r"Safety transition: (\w+) -> (\w+)"
    )
    expected_edges = {
        20: ("NORMAL", "WARNING"),
        28: ("WARNING", "FAULT"),
        34: ("FAULT", "RECOVERY"),
        38: ("RECOVERY", "NORMAL"),
    }

    samples = []
    observed_edges = []
    expected_transitions = []
    last_sequence = None

    for line in output.splitlines():
        match = sample_pattern.search(line)
        if match:
            sequence, temperature, vibration, voltage = map(int, match.groups())
            samples.append(sequence)
            last_sequence = sequence

            if (temperature, vibration, voltage) != expected_values(sequence):
                errors.append(f"Incorrect sensor values at sample {sequence}")

            edge = expected_edges.get(sequence % 40)
            if edge:
                expected_transitions.append((sequence, *edge))

        match = transition_pattern.search(line)
        if match:
            observed_edges.append((last_sequence, *match.groups()))

    if len(samples) < 80:
        errors.append(f"Too few samples: {len(samples)}; expected at least 80")

    if samples and samples[0] != 0:
        errors.append(f"First sample was {samples[0]}, expected 0")

    for previous, current in zip(samples, samples[1:]):
        if current != previous + 1:
            errors.append(f"Sequence discontinuity: {previous} -> {current}")

    if observed_edges != expected_transitions:
        errors.append("Observed transitions do not match expected sample positions")

    edge_counts = {}
    for edge in expected_edges.values():
        count = sum(
            1 for _, old, new in observed_edges if (old, new) == edge
        )
        edge_counts[f"{edge[0]} -> {edge[1]}"] = count
        if count < 2:
            errors.append(f"Fewer than two occurrences of {edge[0]} -> {edge[1]}")

    bad_patterns = [
        r"Queue full:",
        r"Failed to receive sensor sample",
        r"<err>",
        r"ASSERTION FAIL",
        r"ZEPHYR FATAL",
        r"\*{5}.*FAULT",
        r"Stack overflow",
    ]
    for pattern in bad_patterns:
        if re.search(pattern, output, re.IGNORECASE):
            errors.append(f"Runtime error matched: {pattern}")

    status = "FAIL" if errors else "PASS"
    summary = {
        "status": status,
        "observation_seconds": 30,
        "sample_count": len(samples),
        "first_sequence": samples[0] if samples else None,
        "last_sequence": samples[-1] if samples else None,
        "transition_counts": edge_counts,
        "errors": errors,
        "scope": "QEMU runtime smoke test with application threads enabled",
    }

    (reports / "summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )

    markdown = [
        "# Runtime Smoke Test",
        "",
        f"Overall: {status}",
        f"Samples received: {len(samples)}",
        "Observation window: 30 seconds",
        "",
        "| Transition | Count |",
        "| --- | ---: |",
    ]
    for edge, count in edge_counts.items():
        markdown.append(f"| {edge} | {count} |")
    markdown.extend(["", "## Errors", ""])
    markdown.extend(f"- {error}" for error in errors)
    if not errors:
        markdown.append("None observed.")
    markdown.extend([
        "",
        "Scope: QEMU smoke test. Does not establish hardware timing,",
        "long-term stability, or behavior under forced queue overload.",
    ])

    (reports / "summary.md").write_text(
        "\n".join(markdown) + "\n", encoding="utf-8"
    )

    print(f"\nRuntime smoke test: {status}")
    print(f"Samples received: {len(samples)}")
    for edge, count in edge_counts.items():
        print(f"{edge}: {count}")
    for error in errors:
        print(f"ERROR: {error}")
    print(f"Reports: {reports}")

    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
