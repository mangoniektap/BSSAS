import os
from pathlib import Path


def resolve_database_root() -> Path:
    configured_root = os.environ.get("BSSAS_DATABASE_ROOT", "").strip()
    if configured_root:
        return Path(configured_root)

    if Path(r"D:/").exists():
        return Path(r"D:/BSSASdatabase")

    local_app_data = os.environ.get("LOCALAPPDATA", "").strip()
    if local_app_data:
        return Path(local_app_data) / "BSSASdatabase"

    return Path.home() / "AppData" / "Local" / "BSSASdatabase"


def resolve_temporary_dir() -> Path:
    configured_temp_dir = os.environ.get("BSSAS_TEMP_DIR", "").strip()
    if configured_temp_dir:
        temporary_dir = Path(configured_temp_dir)
    else:
        temporary_dir = resolve_database_root() / "TemporaryFile"

    temporary_dir.mkdir(parents=True, exist_ok=True)
    return temporary_dir
