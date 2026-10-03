import argparse
import json
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", help="Combined report directory name")
    args = parser.parse_args()

    parent = BUILD / "combined_reports"
    if args.report:
        report = (parent / args.report).resolve()
        if report.parent != parent.resolve():
            raise SystemExit("Report must be directly inside combined_reports")
    else:
        # Select the latest passing report, skipping intentional failure checks.
        report = None
        for candidate in sorted(parent.glob("*"), reverse=True):
            if not candidate.is_dir():
                continue
            try:
                data = json.loads(
                    (candidate / "summary.json").read_text(encoding="utf-8-sig")
                )
            except (OSError, ValueError):
                continue
            if isinstance(data, dict) and data.get("status") == "PASS":
                report = candidate
                break
        if report is None:
            raise SystemExit("No passing combined report found")

    summary = json.loads(
        (report / "summary.json").read_text(encoding="utf-8-sig")
    )
    if summary.get("status") != "PASS":
        raise SystemExit("Selected report is not PASS")

    checks = summary.get("checks", [])
    expected = {"regression", "runtime", "overload", "recovery"}
    if (
        len(checks) != 4
        or {check.get("check") for check in checks} != expected
        or any(check.get("status") != "PASS" for check in checks)
    ):
        raise SystemExit("Expected four passing checks")

    files = [report / "index.html", report / "summary.json"]
    for check in checks:
        source = Path(check["report_directory"]).resolve()
        expected_parent = (BUILD / f"{check['check']}_reports").resolve()
        if source.parent != expected_parent:
            raise SystemExit(f"Unexpected report directory: {source}")
        files.extend(
            path for path in sorted(source.iterdir())
            if path.is_file() and path.suffix in (".log", ".json", ".md")
        )

    for path in files:
        if not path.is_file():
            raise SystemExit(f"Missing file: {path}")

    destination = BUILD / "report_packages"
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / f"test-report-{report.name}.zip"

    with ZipFile(archive, "w", ZIP_DEFLATED) as bundle:
        for path in files:
            bundle.write(path, path.relative_to(BUILD))
        bundle.writestr(
            "OPEN_REPORT.txt",
            "Extract the entire ZIP first.\n"
            f"Open combined_reports/{report.name}/index.html in a browser.\n"
            "Keep all folders together so log links work.\n",
        )

    with ZipFile(archive) as bundle:
        if bundle.testzip() is not None:
            raise SystemExit("ZIP integrity check failed")

    print(f"Selected report: {report.name}")
    print(f"Package created: {archive}")
    print(f"Files included: {len(files)}")
    print("ZIP integrity: PASS")


if __name__ == "__main__":
    main()
