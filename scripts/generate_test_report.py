import argparse
import html
import json
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKS = {
    "regression": "回归测试",
    "runtime": "正常运行冒烟",
    "overload": "持续队列过载",
    "recovery": "过载恢复",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--since", help="Only include reports from this batch")
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    destination = ROOT / "build" / "combined_reports" / stamp
    destination.mkdir(parents=True, exist_ok=True)
    records = []

    for key, label in CHECKS.items():
        parent = ROOT / "build" / f"{key}_reports"
        candidates = sorted(
            (
                path for path in parent.glob("*")
                if path.is_dir()
                and (not args.since or path.name >= args.since)
            ),
            key=lambda path: path.name,
            reverse=True,
        )
        record = {
            "check": key,
            "label": label,
            "status": "MISSING",
            "report_directory": None,
            "summary": {},
            "errors": [],
        }
        if not candidates:
            record["errors"].append("No report found for this selection")
        else:
            source = candidates[0]
            record["report_directory"] = str(source)
            try:
                data = json.loads(
                    (source / "summary.json").read_text(encoding="utf-8-sig")
                )
                if not isinstance(data, dict):
                    raise ValueError("Summary must be a JSON object")
                record["summary"] = data
                record["status"] = (
                    data["status"] if data.get("status") in ("PASS", "FAIL")
                    else "INVALID"
                )
                if record["status"] == "INVALID":
                    record["errors"].append("Invalid summary status")
            except (OSError, ValueError) as error:
                record["status"] = "INVALID"
                record["errors"].append(str(error))
        records.append(record)

    passed = all(record["status"] == "PASS" for record in records)
    result = {
        "status": "PASS" if passed else "FAIL",
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "selection": (
            "Reports created since " + args.since
            if args.since else "Latest report per check; may be from different runs"
        ),
        "checks": records,
    }
    (destination / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    cards = []
    escape = html.escape
    for record in records:
        data = record["summary"]
        metrics = {
            key: value for key, value in data.items()
            if key not in ("status", "errors", "results", "scope")
        }
        failures = list(record["errors"]) + list(data.get("errors", []))
        for suite in data.get("results", []):
            if suite.get("reason"):
                failures.append(f"{suite.get('suite')}: {suite['reason']}")

        links = []
        if record["report_directory"]:
            source = Path(record["report_directory"])
            for path in sorted(source.iterdir()):
                if path.is_file() and path.suffix in (".json", ".md", ".log"):
                    relative = "../../" + source.parent.name + "/" + source.name + "/" + path.name
                    links.append(
                        f'<a href="{escape(relative, quote=True)}">'
                        f'{escape(path.name)}</a>'
                    )

        rows = "".join(
            f"<tr><td>{escape(key)}</td><td>"
            f"{escape(json.dumps(value, ensure_ascii=False))}</td></tr>"
            for key, value in metrics.items()
        )
        details = escape(json.dumps(
            data.get("results", []), ensure_ascii=False, indent=2
        ))
        cards.append(
            f'<section><h2>{escape(record["label"])} '
            f'<span class="{record["status"].lower()}">'
            f'{record["status"]}</span></h2>'
            f'<table>{rows}</table>'
            + (f"<details><summary>测试组明细</summary><pre>{details}</pre></details>"
               if data.get("results") else "")
            + "<p>" + escape("; ".join(failures) or "无记录错误") + "</p>"
            + '<nav>' + " · ".join(links) + "</nav></section>"
        )

    page = """<!doctype html>
<html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zephyr 自动化测试报告</title>
<style>
body{font-family:Segoe UI,Microsoft YaHei,sans-serif;background:#f1f5f9;
color:#182537;max-width:1000px;margin:36px auto;padding:0 20px}
section{background:white;padding:24px;margin:20px 0;border-radius:12px}
h1{font-size:28px}h2{font-size:21px}
span{float:right;font-size:16px}.pass{color:#15803d}
.fail,.missing,.invalid{color:#b91c1c}
table{border-collapse:collapse;width:100%}
td{padding:9px;border-bottom:1px solid #e2e8f0;overflow-wrap:anywhere}
td:first-child{width:45%;color:#475569}
a{color:#2563eb}nav{line-height:2}pre{white-space:pre-wrap}
</style><h1>Zephyr 自动化测试报告</h1>"""
    page += (
        f'<p>总体结果：<strong>{result["status"]}</strong></p>'
        f'<p>生成时间：{escape(result["generated_at"])}</p>'
        f'<p>报告选择：{escape(result["selection"])}</p>'
        + "".join(cards)
        + "<p>验证环境为 QEMU；不代表真实硬件时序或长期稳定性。</p></html>"
    )
    (destination / "index.html").write_text(page, encoding="utf-8")
    print(f"Combined report: {result['status']}")
    print(f"HTML: {destination / 'index.html'}")
    print(f"JSON: {destination / 'summary.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
