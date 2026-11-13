#!/usr/bin/env python3
"""Dependency-free repository checks; report file paths, never secret values."""
import ast
import hashlib
import pathlib
import re
import subprocess
from urllib.parse import unquote

ROOT = pathlib.Path(__file__).resolve().parents[1]
names = subprocess.check_output(
    ["git", "-C", str(ROOT), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
).decode().split("\0")
paths = sorted({ROOT / name for name in names if name and (ROOT / name).is_file()})
errors = []
old_names = ["AirBridge" + ".S3", "AirBridge" + "_Pro", "AirBridge" + " Pro"]
private_key = re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----")
token = re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b")

for path in paths:
    relative = path.relative_to(ROOT)
    if any(part in {".git", "node_modules", "__pycache__"} for part in relative.parts):
        continue
    if path.suffix in {".apk", ".gz", ".zst"}:
        continue
    try:
        text = path.read_text()
    except UnicodeError:
        errors.append(f"Unexpected unreviewed binary: {relative}")
        continue
    if private_key.search(text) or token.search(text):
        errors.append(f"Potential private key/token in {relative}")
    if any(name in text or name in str(relative) for name in old_names):
        errors.append(f"Obsolete project branding in {relative}")
    if path.suffix == ".py":
        try:
            ast.parse(text, filename=str(relative))
        except SyntaxError:
            errors.append(f"Python syntax error: {relative}")
    if path.suffix == ".md":
        for target in re.findall(r"\]\(([^)\s]+)\)", text):
            if target.startswith(("https:", "http:", "mailto:", "#")):
                continue
            target = unquote(target.split("#", 1)[0])
            if target and not (path.parent / target).exists():
                errors.append(f"Broken local link in {relative}: {target}")

assets = ROOT / "openwrt/drivers/ma86xh"
for line in (assets / "SHA256SUMS").read_text().splitlines():
    if not line or line.startswith("#"):
        continue
    expected, name = line.split(None, 1)
    path = assets / name.strip()
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        errors.append(f"Recovery asset checksum mismatch: {name}")

# Do not allow the archived prototype fallback to silently reintroduce a real password.
config = (ROOT / "legacy/esp32/src/config.cpp").read_text()
for line in config.splitlines():
    if "cfg.password" in line and "getString" in line and "change-me-before-use" not in line:
        errors.append("Archived password fallback needs review (value suppressed)")

if errors:
    raise SystemExit("\n".join(errors))
print(f"Repository checks passed: {len(paths)} files, Python syntax, links, branding, key/token patterns, recovery hashes")
