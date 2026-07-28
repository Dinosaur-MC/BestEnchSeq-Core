"""Minecraft client jar download and extraction.

Downloads the official client jar for the latest (or pinned) Minecraft
release, then extracts it to a working directory for further analysis.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import time
from pathlib import Path
from zipfile import ZipFile

from . import meta

RES_DIR = meta.RES_DIR
EXTRACT = RES_DIR / "vanilla"
JAR_PATH = RES_DIR / "vanilla.jar"
VERSION_PATH = RES_DIR / "version.json"


# ── download ──────────────────────────────────────────────────────────────

def download_jar() -> str:
    """Download the client jar for the latest Minecraft release.

    Returns the release version string.
    """
    m = meta.fetch_json(meta.VERSION_MANIFEST_V1)
    release = m["latest"]["release"]
    entry = next(v for v in m["versions"] if v["id"] == release)

    # version detail
    meta.download_file(entry["url"], VERSION_PATH)

    # client jar
    vd = json.loads(VERSION_PATH.read_bytes())
    meta.download_file(vd["downloads"]["client"]["url"], JAR_PATH)

    sz = JAR_PATH.stat().st_size
    print(f"  Client jar saved ({sz / 1024 / 1024:.1f} MB)")
    return release


def get_release_from_cache() -> str:
    """Read release version from cached version.json or manifest."""
    if VERSION_PATH.exists():
        return json.loads(VERSION_PATH.read_bytes()).get("id", "unknown")
    try:
        m = meta.fetch_json(meta.VERSION_MANIFEST_V1)
        return m["latest"]["release"]
    except Exception:
        return "unknown"


# ── extract ───────────────────────────────────────────────────────────────

def extract_jar() -> None:
    """Extract client jar into ``EXTRACT`` directory."""
    if EXTRACT.exists():
        shutil.rmtree(EXTRACT)
    with ZipFile(JAR_PATH) as zf:
        zf.extractall(EXTRACT)


def check_javap() -> None:
    """Verify javap is available and can read the extracted class files."""
    javap_path = shutil.which("javap")
    if not javap_path:
        print("  [INFO] javap not found - will use builtin fallback data")
        return

    # version string
    try:
        r = subprocess.run([javap_path, "-version"],
                           capture_output=True, timeout=10,
                           encoding="utf-8", errors="replace")
        javap_ver = r.stdout.strip() or r.stderr.strip()
    except Exception:
        javap_ver = "unknown"

    # probe against a known class
    test_class = EXTRACT / "net" / "minecraft" / "world" / "item" / "ToolMaterial.class"
    if test_class.exists():
        try:
            r = subprocess.run([javap_path, "-c", "-p", str(test_class)],
                               capture_output=True, timeout=30)
            if r.returncode != 0 or not r.stdout:
                err_text = ""
                try:
                    err_text = r.stderr.decode("utf-8", errors="replace").strip()
                except Exception:
                    err_text = "(cannot decode stderr)"
                print(f"  [WARN] javap incompatible - {err_text or 'unknown error'}")
                print(f"  [WARN] Will use builtin fallback data instead.")
                print(f"  [WARN] javap version: {javap_ver[:60]}")
                return
        except Exception:
            print(f"  [WARN] javap test failed - will use builtin fallback data")
            return
    print(f"  javap OK ({javap_ver[:60].strip()})")


# ── convenience ───────────────────────────────────────────────────────────

def ensure_jar(download_fn=download_jar) -> bool:
    """Download the jar if not already present.  Returns True when ready."""
    if JAR_PATH.exists():
        print("Vanilla jar exists, skipping download")
        return True

    print("Downloading…")
    for attempt in range(5):
        try:
            download_fn()
            return True
        except Exception as e:
            print(f"  Attempt {attempt + 1} failed: {e}")
            if attempt < 4:
                time.sleep(5)
    print("FATAL: download failed")
    return False
