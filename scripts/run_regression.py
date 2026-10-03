import argparse
import json
import queue
import re
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITES = [
    ("safety", 10),
    ("sensor_queue", 5),
    ("pipeline", 3),
]


def run_qemu(command, log_path, timeout):
    lines = queue.Queue()
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    def reader():
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    worker = threading.Thread(target=reader, daemon=True)
    worker.start()
    output = []
    deadline = time.monotonic() + timeout
    reason = "QEMU exited before reporting success"

    try:
        with log_path.open("w", encoding="utf-8") as log:
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    reason = "Test timed out"
                    break

                try:
                    line = lines.get(timeout=min(remaining, 0.5))
                except queue.Empty:
                    continue

                if line is None:
                    break

                output.append(line)
                log.write(line)
                log.flush()
                print(line, end="")

                if "PROJECT EXECUTION SUCCESSFUL" in line:
                    reason = ""
                    break

                if "PROJECT EXECUTION FAILED" in line:
                    reason = "Zephyr reported test failure"
                    break
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        worker.join(timeout=2)
        process.stdout.close()

    return "".join(output), reason


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default=r"D:\qemu\qemu-system-arm.exe")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")

    qemu = Path(args.qemu)
    if not qemu.is_file():
        parser.error(f"QEMU not found: {qemu}")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    reports = ROOT / "build" / "regression_reports" / stamp
    reports.mkdir(parents=True, exist_ok=True)
    results = []

    for name, expected in SUITES:
        print(f"\n===== {name}: build and test =====", flush=True)
        result = {
            "suite": name,
            "expected": expected,
            "status": "FAIL",
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "reason": "",
        }

        try:
            build_dir = ROOT / "build" / f"regression_{name}"
            with (reports / f"{name}-build.log").open(
                "w", encoding="utf-8"
            ) as log:
                build = subprocess.run(
                    [
                        sys.executable, "-m", "west", "build",
                        "-b", "mps2/an385",
                        "-d", str(build_dir),
                        str(ROOT / "tests" / name),
                    ],
                    cwd=ROOT,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=300,
                )

            if build.returncode != 0:
                raise RuntimeError(
                    f"Build failed; see {name}-build.log"
                )

            elf = build_dir / "zephyr" / "zephyr.elf"
            if not elf.is_file():
                raise RuntimeError("Built ELF file not found")

            output, reason = run_qemu(
                [
                    str(qemu),
                    "-machine", "mps2-an385",
                    "-cpu", "cortex-m3",
                    "-nographic",
                    "-monitor", "none",
                    "-serial", "stdio",
                    "-kernel", str(elf),
                ],
                reports / f"{name}-run.log",
                args.timeout,
            )

            match = re.search(
                r"pass\s*=\s*(\d+),\s*fail\s*=\s*(\d+),"
                r"\s*skip\s*=\s*(\d+),\s*total\s*=\s*(\d+)",
                output,
            )

            if match:
                passed, failed, skipped, total = map(int, match.groups())
                result.update(
                    passed=passed, failed=failed,
                    skipped=skipped, total=total,
                )
                if not reason:
                    if (
                        passed == expected and total == expected
                        and failed == 0 and skipped == 0
                    ):
                        result["status"] = "PASS"
                    else:
                        reason = "Unexpected test counts"
            elif not reason:
                reason = "Test summary not found"

            result["reason"] = reason

        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            result["reason"] = str(error)

        results.append(result)
        print(f"\n{name}: {result['status']} {result['reason']}", flush=True)

    all_passed = all(row["status"] == "PASS" for row in results)
    summary = {
        "status": "PASS" if all_passed else "FAIL",
        "board": "mps2/an385",
        "expected_tests": sum(n for _, n in SUITES),
        "passed_tests": sum(row["passed"] for row in results),
        "results": results,
    }

    (reports / "summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )

    markdown = [
        "# Regression Results",
        "",
        f"Overall: {summary['status']}",
        "",
        "| Suite | Status | Passed | Failed | Skipped |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for row in results:
        markdown.append(
            f"| {row['suite']} | {row['status']} | "
            f"{row['passed']} | {row['failed']} | {row['skipped']} |"
        )
    markdown.extend(["", "## Failure details", ""])
    for row in results:
        if row["reason"]:
            markdown.append(f"- {row['suite']}: {row['reason']}")

    (reports / "summary.md").write_text(
        "\n".join(markdown) + "\n", encoding="utf-8"
    )

    print(f"\nOverall: {summary['status']}")
    print(f"Passed: {summary['passed_tests']}/18")
    print(f"Reports: {reports}")
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
