#!/usr/bin/env python3
"""Run the isolated native UI recipe on a Linux desktop, including AppImages."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--pixel-scale", type=int, choices=(1, 2, 3), default=1)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
executable = args.executable.resolve(strict=True)
output = args.output.resolve()
if output.exists() and any(output.iterdir()):
    parser.error("Choose an empty output folder; this runner uses disposable Macs.")
output.mkdir(parents=True, exist_ok=True)
for name in ("profile", "machines"):
    (output / name).mkdir()
recipe = json.loads((root / "native/tests/ui-ux-flow.json").read_text())
recipe["output"] = str(output)
recipe["steps"][0].update(scale=args.pixel_scale, pixelScale=args.pixel_scale)
path = output / "recipe.json"
path.write_text(json.dumps(recipe, indent=2) + "\n")
env = dict(os.environ, CLASSICMAC_QUALIFY="1",
           CLASSICMAC_TEST_HOME=str(output / "profile"),
           CLASSICMAC_TEST_DOCUMENTS=str(output / "machines"),
           QT_SCALE_FACTOR=str(args.pixel_scale), QT_ENABLE_HIGHDPI_SCALING="0",
           QT_LOGGING_TO_CONSOLE="1")
for name in ("QT_FONT_DPI", "QT_SCREEN_SCALE_FACTORS", "CLASSICMAC_RUNTIME"):
    env.pop(name, None)
with (output / "stdout.log").open("w") as stdout, (output / "stderr.log").open("w") as stderr:
    result = subprocess.run([str(executable), "--qualify", str(path)], env=env,
                            cwd=executable.parent, stdout=stdout, stderr=stderr,
                            timeout=180)
report_path = output / "result.json"
if not report_path.exists():
    sys.exit(f"No qualification report (exit {result.returncode}); inspect {output}")
report = json.loads(report_path.read_text())
if result.returncode or not report["passed"]:
    sys.exit(f"UI qualification failed: {report.get('error')}; inspect {output}")
print(f"Passed {len(report['steps'])} UI steps at {args.pixel_scale}×: {output}")
