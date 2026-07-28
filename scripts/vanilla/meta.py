"""HTTP helpers, version manifest lookup, and asset index handling.

Shared by both the enchantment extraction pipeline and the language
resource downloader.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any
from urllib import request

USER_AGENT = "BestEnchSeq/1.0"

# Mojang endpoints
VERSION_MANIFEST_V1 = "https://launchermeta.mojang.com/mc/game/version_manifest.json"
VERSION_MANIFEST_V2 = "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json"

# resource directory (intermediate files: client jar, extraction, cache)
RES_DIR = Path("res")
INDEX_CACHE = RES_DIR / "asset_index.json"


# ── download helpers ──────────────────────────────────────────────────────

def fetch_json(url: str, *, retries: int = 3, delay: float = 3.0,
               ua: str = USER_AGENT) -> Any:
    """Fetch and decode a JSON response with retry logic."""
    last_err: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            req = request.Request(url, headers={"User-Agent": ua})
            with request.urlopen(req, timeout=30) as resp:
                return json.loads(resp.read())
        except Exception as exc:
            last_err = exc
            print(f"  [retry] GET {url[:80]}… failed ({attempt}/{retries}): {exc}")
            if attempt < retries:
                time.sleep(delay)
    raise RuntimeError(f"Failed to fetch {url} after {retries} retries") from last_err


def download_file(url: str, dest: Path, *, retries: int = 3,
                  ua: str = USER_AGENT) -> None:
    """Download a file to disk with retry logic."""
    last_err: Exception | None = None
    delay: float = 3.0
    for attempt in range(1, retries + 1):
        try:
            req = request.Request(url, headers={"User-Agent": ua})
            with request.urlopen(req, timeout=60) as resp:
                dest.write_bytes(resp.read())
            return
        except Exception as exc:
            last_err = exc
            print(f"  [retry] download {url[:80]}… failed ({attempt}/{retries}): {exc}")
            if attempt < retries:
                current = min(delay * 1.5, 10.0) if attempt > 1 else delay
                time.sleep(current)
                delay = current
    raise RuntimeError(f"Failed to download {url} after {retries} retries") from last_err


# ── version resolution ────────────────────────────────────────────────────

def resolve_version(version_id: str | None = None,
                    manifest_url: str = VERSION_MANIFEST_V1) -> str:
    """Return the latest release version id, or verify a pinned version.

    Parameters
    ----------
    version_id : str or None
        Desired version, or ``None`` for latest release.
    manifest_url : str
        Which manifest endpoint to query.

    Returns the version id string.
    """
    manifest = fetch_json(manifest_url)
    if version_id is None:
        return manifest["latest"]["release"]
    for v in manifest["versions"]:
        if v["id"] == version_id:
            return version_id
    available = ", ".join(v["id"] for v in manifest["versions"][:10])
    print(f"  [WARN] Version '{version_id}' not found in manifest. "
          f"Recent: {available}…")
    print(f"         Falling back to latest release.")
    return manifest["latest"]["release"]


# ── asset index ───────────────────────────────────────────────────────────

def get_asset_index(version_id: str,
                    manifest_url: str = VERSION_MANIFEST_V1) -> dict:
    """Download and parse the asset index for a given version.

    Returns the parsed JSON dict (contains ``objects``).
    """
    manifest = fetch_json(manifest_url)
    entry = next(v for v in manifest["versions"] if v["id"] == version_id)
    version_info = fetch_json(entry["url"])
    url = version_info["assetIndex"]["url"]
    print(f"  Fetching asset index for {version_id}…")
    index = fetch_json(url)
    print(f"  Asset index: {len(index.get('objects', {}))} objects")
    return index


def get_cached_asset_index(cache_path: Path = INDEX_CACHE) -> dict | None:
    """Return cached asset index if valid."""
    if cache_path.exists():
        try:
            data = json.loads(cache_path.read_bytes())
            if "objects" in data and isinstance(data["objects"], dict):
                print(f"  Using cached asset index ({cache_path.name})")
                return data
        except Exception:
            pass
    return None


def cache_asset_index(index: dict, cache_path: Path = INDEX_CACHE) -> None:
    """Persist asset index to disk for reuse."""
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(json.dumps(index, separators=(",", ":")), encoding="utf-8")
