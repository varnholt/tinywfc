"""Build the standalone browser demo. Requires Rust 1.95 and wasm-bindgen-cli 0.2.130."""
from pathlib import Path
import json
import shutil
import subprocess
import tomllib

root = Path(__file__).resolve().parents[1]
package = tomllib.loads((root / "Cargo.toml").read_text(encoding="utf-8"))["package"]["name"]
metadata = json.loads(subprocess.check_output(["cargo", "+1.95", "metadata", "--no-deps", "--format-version=1", "--manifest-path", str(root / "Cargo.toml")]))
subprocess.run(["cargo", "+1.95", "build", "--locked", "--release", "--target", "wasm32-unknown-unknown", "--manifest-path", str(root / "Cargo.toml")], check=True)
wasm = Path(metadata["target_directory"]) / "wasm32-unknown-unknown/release" / (package + ".wasm")
output = root / "dist"
output.mkdir(exist_ok=True)
subprocess.run(["wasm-bindgen", "--target", "web", "--out-dir", str(output / "pkg"), "--out-name", "app", str(wasm)], check=True)
shutil.copyfile(root / "index.html", output / "index.html")
print("Browser demo:", output)
print("Serve with: python -m http.server 8080 --directory", output)
