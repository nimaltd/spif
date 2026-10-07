#!/usr/bin/env python3
"""
Build and run the host tests, then print the result.

Usage:
    python test/run_tests.py            build and run
    python test/run_tests.py --clean    throw the build folder away first

Runs on a PC. No board is involved: the tests replace the HAL calls with stubs
they control, so a 200 ms delay is tested instantly instead of waited out.

Needs cmake and any C compiler. On Windows, "scoop install gcc" gets you one.

This file is not specific to one library. It reads the <NAME>_BUILD_TESTS option
out of the project's CMakeLists.txt, so it can be dropped into any of the
NimaLTD repositories without editing it.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def find_test_option():
    """Read the project's test option from CMakeLists.txt, so this file stays generic."""
    cmakelists = ROOT / "CMakeLists.txt"

    if not cmakelists.exists():
        return None

    text = cmakelists.read_text(encoding="utf-8")
    match = re.search(r"option\s*\(\s*(\w+_BUILD_TESTS)", text)

    return match.group(1) if match else None


def find_generator():
    """Prefer Ninja. The default generator on Windows is nmake, which is rarely installed."""
    return ["-G", "Ninja"] if shutil.which("ninja") else []


def run(step, command):
    """Run one step, streaming its output. Returns the exit code."""
    # Flushed, or Python's buffered prints land after the subprocess output.
    print(f"\n>>> {step}", flush=True)
    print(f"    {' '.join(str(c) for c in command)}\n", flush=True)
    return subprocess.run(command, cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description="Build and run the host tests.")
    parser.add_argument("--clean", action="store_true", help="delete the build folder first")
    args = parser.parse_args()

    if shutil.which("cmake") is None:
        print("cmake was not found on PATH. Install it and try again.")
        return 2

    option = find_test_option()
    if option is None:
        print("No <NAME>_BUILD_TESTS option found in CMakeLists.txt, so there is nothing to run.")
        return 2

    if args.clean and BUILD.exists():
        print(f"removing {BUILD}")
        shutil.rmtree(BUILD)

    configure = ["cmake", "-S", str(ROOT), "-B", str(BUILD), f"-D{option}=ON"]
    configure += find_generator()

    if run("Configure", configure) != 0:
        print("\nConfigure failed. The usual cause is no C compiler on PATH.")
        print("On Windows: scoop install gcc     On Linux: sudo apt install gcc")
        return 2

    if run("Build", ["cmake", "--build", str(BUILD)]) != 0:
        print("\nBuild failed. The warnings above are errors, because the tests use -Werror.")
        return 2

    code = run("Test", ["ctest", "--test-dir", str(BUILD), "--output-on-failure"])

    print()
    if code == 0:
        print("PASS: every check succeeded.")
    else:
        print("FAIL: see the failing lines above. Each one names the assertion that broke.")

    return code


if __name__ == "__main__":
    sys.exit(main())
