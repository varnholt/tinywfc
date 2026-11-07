"""Regenerate C++ reference samples, or verify them with --check. Requires a C++23 compiler."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
rust = Path(__file__).resolve().parents[1]
repo = rust.parent
directory = rust / "target/reference"
directory.mkdir(parents=True, exist_ok=True)
binary = Path(directory) / ("reference.exe" if os.name == "nt" else "reference")
compiler = shlex.split(os.environ.get("CXX", "clang++" if os.name == "nt" else "c++"))
subprocess.run(compiler + ["-std=c++23", "-O2", "-ffp-contract=off", "-I" + str(repo / "include"), str(rust / "tests/reference.cpp"), "-o", str(binary)], check=True)
data = subprocess.check_output([str(binary)]).replace(b"\r\n", b"\n")
output = rust / "tests/data/cpp_reference.txt"
if args.check:
    if output.read_bytes() != data:
        raise SystemExit("C++ reference samples changed; regenerate and review the parity tests")
else:
    output.parent.mkdir(exist_ok=True)
    output.write_bytes(data)
print("Verified" if args.check else "Wrote", output)
