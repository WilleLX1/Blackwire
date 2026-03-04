from __future__ import annotations

import os
import subprocess
from functools import lru_cache
from pathlib import Path


def _safe_env(name: str, fallback: str = "unknown") -> str:
    value = os.getenv(name, "").strip()
    return value if value else fallback


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _resolve_git_commit() -> str:
    explicit = os.getenv("BLACKWIRE_GIT_COMMIT", "").strip()
    if explicit:
        return explicit
    try:
        output = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=_repo_root(),
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=2,
        )
    except Exception:
        return "unknown"
    commit = output.strip()
    return commit if commit else "unknown"


@lru_cache(maxsize=1)
def resolve_server_version_info() -> dict[str, str]:
    return {
        "server_version": _safe_env("BLACKWIRE_SERVER_VERSION", "0.3.0"),
        "api_version": "v2",
        "git_commit": _resolve_git_commit(),
        "build_timestamp": _safe_env("BLACKWIRE_BUILD_TIMESTAMP", "unknown"),
    }
