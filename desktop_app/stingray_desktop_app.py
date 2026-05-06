from __future__ import annotations

import argparse
import base64
import ipaddress
import hashlib
import hmac
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import uuid
import secrets
import webbrowser
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, quote, unquote, urlparse
from urllib.request import Request, urlopen
import zipfile

import qrcode
import qrcode.image.svg
from flask import Flask, Response, has_request_context, jsonify, request, send_file, session


INVENTORY_HEADER = "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at"
TRANSACTION_HEADER = "timestamp|item_id|action|delta|qty_after|note"
DEVICE_LOG_HEADER = "timestamp|mac_address|uptime_seconds|event|detail"
TIME_LOG_HEADER = "timestamp|event|detail"
CLOUD_CONFIG_HEADER = "provider|login_email|folder_name|folder_hint|mode|backup_mode|asset_mode|brand_name|brand_logo_ref|client_id|client_secret|updated_at"
GOOGLE_STATE_HEADER = "refresh_token|folder_id|last_sync_at|last_synced_manifest_hash|last_synced_snapshot_at|local_snapshot_at|auth_status|sync_status|last_error"
APP_DISPLAY_NAME = "Inventory"
LEGACY_APP_DISPLAY_NAME = "Stingray Inventory Desktop"
PROGRAM_DATA_ROOT_NAME = "Inventory"
LEGACY_PROGRAM_DATA_ROOT_NAME = "StingrayInventoryDesktop"
DEFAULT_CATEGORY = "part"
ALLOWED_IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp"}
APP_CONFIG_FILE = "desktop_config.json"
FIREWALL_RULE_NAME = "Inventory LAN"
LEGACY_FIREWALL_RULE_NAME = "Stingray Inventory Desktop LAN"
SYSTEM_TASK_NAME = "Inventory (System Startup)"
LEGACY_SYSTEM_TASK_NAME = "Stingray Inventory Desktop (System Startup)"
IMPORT_COPY_FILES = [
    "inventory.csv",
    "orders.json",
    "transactions.csv",
    "device_log.csv",
    "time_log.csv",
    "cloud_backup.cfg",
    "google_drive_state.cfg",
]
SD_IMPORT_SEARCH_DEPTH = 4


def runtime_root() -> Path:
    if getattr(sys, "frozen", False):
        meipass = getattr(sys, "_MEIPASS", "")
        if meipass:
            return Path(meipass)
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[1]


def default_firmware_ino_path() -> Path:
    return runtime_root() / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino"


def default_program_data_root() -> Path:
    return Path(os.environ.get("PROGRAMDATA", str(Path.home()))) / PROGRAM_DATA_ROOT_NAME


def legacy_program_data_root() -> Path:
    return Path(os.environ.get("PROGRAMDATA", str(Path.home()))) / LEGACY_PROGRAM_DATA_ROOT_NAME


def resolve_default_desktop_data_dir() -> Path:
    new_dir = default_program_data_root() / "data"
    legacy_dir = legacy_program_data_root() / "data"
    if new_dir.exists():
        return new_dir

    legacy_markers = [
        legacy_dir / "inventory.csv",
        legacy_dir / "orders.json",
        legacy_dir / "transactions.csv",
        legacy_dir / APP_CONFIG_FILE,
        legacy_dir / "cloud_backup.cfg",
        legacy_dir / "google_drive_state.cfg",
    ]
    if any(marker.exists() for marker in legacy_markers):
        return legacy_dir

    return new_dir


@dataclass
class ItemRecord:
    id: str
    category: str
    part_name: str
    qr_code: str
    color: str
    material: str
    qty: int
    image_ref: str
    bom_product: str
    bom_qty: int
    updated_at: str


@dataclass
class DesktopConfig:
    bind_host: str = "0.0.0.0"
    port: int = 8787
    selected_lan_ip: str = ""
    configured_network_base_url: str = ""
    setup_complete: bool = False
    admin_pin_salt: str = ""
    admin_pin_hash: str = ""
    session_secret: str = ""
    updated_at: str = ""


@dataclass
class CloudConfig:
    provider: str = "google_drive"
    login_email: str = ""
    folder_name: str = ""
    folder_hint: str = ""
    mode: str = "select_or_create"
    backup_mode: str = "sd_only"
    asset_mode: str = "sd_only"
    brand_name: str = "Stingray Inventory"
    brand_logo_ref: str = ""
    client_id: str = ""
    client_secret: str = ""
    updated_at: str = ""


@dataclass
class GoogleState:
    refresh_token: str = ""
    folder_id: str = ""
    last_sync_at: str = ""
    last_synced_manifest_hash: str = ""
    last_synced_snapshot_at: str = ""
    local_snapshot_at: str = ""
    auth_status: str = "desktop_local"
    sync_status: str = "idle"
    last_error: str = ""


def trim_copy(value: str) -> str:
    return (value or "").strip()


def sanitize_field(value: str) -> str:
    out = trim_copy(value)
    out = out.replace("\n", " ").replace("\r", " ").replace("|", " ")
    return out


def lower_copy(value: str) -> str:
    return (value or "").lower()


def normalize_lookup_value(value: str) -> str:
    return lower_copy(trim_copy(value))


def normalize_category(value: str) -> str:
    normalized = lower_copy(trim_copy(value))
    if normalized == "parts":
        return "part"
    if normalized == "products":
        return "product"
    if normalized == "kits":
        return "kit"
    if normalized in {"part", "product", "kit"}:
        return normalized
    return DEFAULT_CATEGORY


def category_label(category: str) -> str:
    normalized = normalize_category(category)
    if normalized == "product":
        return "Product"
    if normalized == "kit":
        return "Kit"
    return "Part"


def split_pipe_line(line: str) -> list[str]:
    return line.split("|")


def parse_int(value: str) -> int | None:
    text = trim_copy(value)
    if not text:
        return None
    sign = 1
    if text[0] in "+-":
        if len(text) == 1:
            return None
        sign = -1 if text[0] == "-" else 1
        text = text[1:]
    if not text.isdigit():
        return None
    return sign * int(text)


def current_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def csv_escape(value: str) -> str:
    return '"' + str(value).replace('"', '""') + '"'


def safe_relative_image_path(path: str) -> bool:
    normalized = trim_copy(path)
    return normalized.startswith("/images/") and ".." not in normalized


def decode_storage_image_path(asset_ref: str) -> str:
    text = trim_copy(asset_ref)
    if not text:
        return ""
    if text.startswith("/images/"):
        return text
    if text.startswith("/api/files"):
        parsed = urlparse(text)
        values = parse_qs(parsed.query)
        raw_path = values.get("path", [""])[0]
        decoded = unquote(raw_path)
        if decoded.startswith("/images/"):
            return decoded
    return ""


def sanitize_filename_stem(value: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9_-]", "_", value)
    stem = re.sub(r"_+", "_", stem).strip("_")
    return stem or "image"


def is_loopback_host(host: str) -> bool:
    text = trim_copy(host).lower()
    if text in {"localhost", "127.0.0.1", "::1"}:
        return True
    try:
        return ipaddress.ip_address(text).is_loopback
    except ValueError:
        return False


def parse_bool(value: Any, default: bool = False) -> bool:
    if isinstance(value, bool):
        return value
    if value is None:
        return default
    if isinstance(value, (int, float)):
        return value != 0
    text = trim_copy(str(value)).lower()
    if not text:
        return default
    return text in {"1", "true", "yes", "on", "enabled"}


def hash_admin_pin(pin: str, salt_b64: str = "") -> tuple[str, str]:
    pin_text = trim_copy(pin)
    if len(pin_text) < 4:
        raise ValueError("Admin PIN must be at least 4 characters.")
    salt = base64.b64decode(salt_b64) if salt_b64 else secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac("sha256", pin_text.encode("utf-8"), salt, 120_000)
    return base64.b64encode(salt).decode("ascii"), base64.b64encode(digest).decode("ascii")


def verify_admin_pin(pin: str, salt_b64: str, expected_hash_b64: str) -> bool:
    if not salt_b64 or not expected_hash_b64:
        return False
    try:
        salt, digest = hash_admin_pin(pin, salt_b64)
    except ValueError:
        return False
    return hmac.compare_digest(digest, expected_hash_b64)


def probe_url(url: str, timeout: float = 2.0) -> tuple[bool, str]:
    target = trim_copy(url)
    if not target:
        return False, "Missing URL."
    try:
        request = Request(target, headers={"User-Agent": "InventoryDesktop/1.0"})
        with urlopen(request, timeout=timeout) as response:
            return 200 <= int(getattr(response, "status", response.getcode() or 0)) < 500, "Reachable"
    except Exception as exc:
        return False, str(exc)


def normalize_base_url(value: str) -> str:
    text = trim_copy(value).rstrip("/")
    if not text:
        return ""
    if not re.match(r"^https?://", text, re.I):
        text = "http://" + text
    parsed = urlparse(text)
    if not parsed.hostname:
        return ""
    return f"{parsed.scheme or 'http'}://{parsed.netloc}".rstrip("/")


def detect_lan_ips() -> list[str]:
    found: set[str] = set()

    def add_ip(value: str) -> None:
        try:
            ip = ipaddress.ip_address(value)
        except ValueError:
            return
        if ip.version == 4 and not ip.is_loopback and not ip.is_link_local and not ip.is_multicast:
            found.add(str(ip))

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            add_ip(info[4][0])
    except OSError:
        pass
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            add_ip(sock.getsockname()[0])
    except OSError:
        pass

    def sort_key(value: str) -> tuple[int, tuple[int, int, int, int]]:
        parts = tuple(int(part) for part in value.split("."))
        if value.startswith("192.168."):
            rank = 0
        elif value.startswith("10."):
            rank = 1
        elif parts[0] == 172 and 16 <= parts[1] <= 31:
            rank = 2
        else:
            rank = 3
        return rank, parts

    return sorted(found, key=sort_key)


def default_lan_ip() -> str:
    ips = detect_lan_ips()
    return ips[0] if ips else "127.0.0.1"


def firewall_rule_status(port: int) -> dict[str, Any]:
    if os.name != "nt":
        return {"supported": False, "installed": False, "detail": "Windows Firewall check is Windows-only."}
    try:
        for rule_name in (FIREWALL_RULE_NAME, LEGACY_FIREWALL_RULE_NAME):
            result = subprocess.run(
                ["netsh", "advfirewall", "firewall", "show", "rule", f"name={rule_name}"],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
            )
            text = (result.stdout or "") + (result.stderr or "")
            installed = result.returncode == 0 and rule_name in text
            if installed:
                return {
                    "supported": True,
                    "installed": True,
                    "name": rule_name,
                    "detail": "Installed" if rule_name == FIREWALL_RULE_NAME else f"Installed via legacy rule name: {rule_name}",
                }
        return {"supported": True, "installed": False, "name": FIREWALL_RULE_NAME, "detail": f"Missing TCP {port} rule"}
    except Exception as exc:
        return {"supported": True, "installed": False, "name": FIREWALL_RULE_NAME, "detail": f"Unable to check firewall: {exc}"}


def scheduled_task_status(task_name: str = SYSTEM_TASK_NAME) -> dict[str, Any]:
    if os.name != "nt":
        return {"supported": False, "exists": False, "enabled": False, "running": False, "detail": "Windows-only."}
    try:
        candidates = [task_name]
        if task_name == SYSTEM_TASK_NAME:
            candidates.append(LEGACY_SYSTEM_TASK_NAME)
        for candidate in candidates:
            result = subprocess.run(
                ["schtasks", "/Query", "/TN", candidate, "/FO", "LIST", "/V"],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
            )
            text = result.stdout or ""
            exists = result.returncode == 0 and candidate in text
            running = exists and re.search(r"Status:\s+Running", text, re.I) is not None
            disabled = re.search(r"Scheduled Task State:\s+Disabled", text, re.I) is not None
            enabled = exists and (running or not disabled)
            if exists:
                detail = "Installed" if candidate == SYSTEM_TASK_NAME else f"Installed via legacy task name: {candidate}"
                return {"supported": True, "exists": True, "enabled": enabled, "running": running, "name": candidate, "detail": detail}
        return {"supported": True, "exists": False, "enabled": False, "running": False, "name": task_name, "detail": "Not installed"}
    except Exception as exc:
        return {"supported": True, "exists": False, "enabled": False, "running": False, "name": task_name, "detail": f"Unable to check task: {exc}"}


def run_command(args: list[str]) -> tuple[bool, str]:
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=10, check=False)
        return result.returncode == 0, trim_copy((result.stdout or "") + "\n" + (result.stderr or ""))
    except Exception as exc:
        return False, str(exc)


def truthy(value: str) -> bool:
    return trim_copy(value).lower() in {"1", "true", "yes", "on", "enabled"}


def set_windows_autorun(enabled: bool) -> tuple[bool, str]:
    if os.name != "nt":
        return False, "Windows only"
    action = "/ENABLE" if enabled else "/DISABLE"
    task_status = scheduled_task_status()
    task_name = task_status.get("name") or SYSTEM_TASK_NAME
    ok, detail = run_command(["schtasks", "/Change", "/TN", task_name, action])
    if not ok and task_name == SYSTEM_TASK_NAME:
        legacy_ok, legacy_detail = run_command(["schtasks", "/Change", "/TN", LEGACY_SYSTEM_TASK_NAME, action])
        if legacy_ok:
            return legacy_ok, legacy_detail
        detail = legacy_detail
    if not ok and not enabled:
        lowered = detail.lower()
        if "cannot find" in lowered or "does not exist" in lowered:
            return True, "Startup task is not installed."
    return ok, detail


def parse_inventory_line(line: str) -> ItemRecord | None:
    fields = split_pipe_line(line)
    if len(fields) < 4:
        return None

    raw_id = trim_copy(fields[0])
    if not raw_id:
        return None

    item = ItemRecord(
        id=raw_id,
        category=DEFAULT_CATEGORY,
        part_name="",
        qr_code="",
        color="",
        material="",
        qty=0,
        image_ref="",
        bom_product="",
        bom_qty=0,
        updated_at="",
    )

    if len(fields) >= 11:
        qty = parse_int(fields[6])
        bom_qty = parse_int(fields[9])
        if qty is None or qty < 0 or bom_qty is None or bom_qty < 0:
            return None
        item.category = normalize_category(fields[1])
        item.part_name = fields[2]
        item.qr_code = fields[3]
        item.color = fields[4]
        item.material = fields[5]
        item.qty = qty
        item.image_ref = fields[7]
        item.bom_product = fields[8]
        item.bom_qty = bom_qty
        item.updated_at = fields[10]
        return item

    if len(fields) >= 10:
        qty = parse_int(fields[7])
        if qty is None or qty < 0:
            return None
        item.category = normalize_category(fields[1])
        item.part_name = fields[2]
        legacy_id = trim_copy(fields[3])
        item.id = legacy_id if legacy_id else raw_id
        item.qr_code = fields[4]
        item.color = fields[5]
        item.material = fields[6]
        item.qty = qty
        item.image_ref = fields[8]
        item.updated_at = fields[9]
        return item

    qty = parse_int(fields[2])
    if qty is None or qty < 0:
        return None
    item.part_name = fields[1]
    item.qty = qty
    item.updated_at = fields[3]
    return item


def normalize_local_folder_path(path: str) -> Path:
    text = trim_copy(path).strip('"').strip("'")
    if os.name == "nt" and re.fullmatch(r"[A-Za-z]:", text):
        text += "\\"
    return Path(text)


def is_inventory_header_line(line: str) -> bool:
    text = line.strip().lstrip("\ufeff").lower()
    if not text:
        return False
    fields = split_pipe_line(text)
    if not fields:
        return False
    header_tokens = {
        "part_number",
        "id",
        "category",
        "part_name",
        "name",
        "qr_code",
        "qr",
        "color",
        "material",
        "qty",
        "quantity",
        "image_ref",
        "bom_product",
        "bom_qty",
        "updated_at",
    }
    matches = sum(1 for field in fields if field in header_tokens)
    return fields[0] in {"part_number", "id"} or (matches >= 2 and any(field in {"qty", "quantity"} for field in fields))


def resolved_child(folder: Path, name: str) -> Path:
    direct = folder / name
    if direct.exists():
        return direct
    try:
        lookup = name.lower()
        for child in folder.iterdir():
            if child.name.lower() == lookup:
                return child
    except OSError:
        pass
    return direct


class DesktopStore:
    def __init__(self, data_dir: Path, firmware_ino: Path | None, bind_host: str = "0.0.0.0", port: int = 8787) -> None:
        self.data_dir = data_dir
        self.root_dir = self.data_dir.parent
        self.legacy_root_dir = self.root_dir.parent / LEGACY_PROGRAM_DATA_ROOT_NAME if self.root_dir.name != LEGACY_PROGRAM_DATA_ROOT_NAME else self.root_dir
        self.backups_dir = self.root_dir / "backups"
        self.logs_dir = self.root_dir / "logs"
        self.config_dir = self.root_dir / "config"
        self.import_uploads_dir = self.root_dir / "import_uploads"
        self.images_dir = self.data_dir / "images"
        self.inventory_file = self.data_dir / "inventory.csv"
        self.inventory_tmp_file = self.data_dir / "inventory.tmp"
        self.orders_file = self.data_dir / "orders.json"
        self.orders_tmp_file = self.data_dir / "orders.tmp"
        self.transaction_file = self.data_dir / "transactions.csv"
        self.device_log_file = self.logs_dir / "device_log.csv"
        self.time_log_file = self.logs_dir / "time_log.csv"
        self.cloud_config_file = self.config_dir / "cloud_backup.cfg"
        self.cloud_config_tmp_file = self.config_dir / "cloud_backup.tmp"
        self.google_state_file = self.config_dir / "google_drive_state.cfg"
        self.google_state_tmp_file = self.config_dir / "google_drive_state.tmp"
        self.app_config_file = self.config_dir / APP_CONFIG_FILE
        self.app_config_tmp_file = self.config_dir / "desktop_config.tmp"
        self.app_config_existed = self.app_config_file.exists()
        self.lock = threading.RLock()
        self.start_monotonic = time.monotonic()
        self.mac_address = self._mac_address_string()
        self.device_id = f"PC-{socket.gethostname()}"
        self.items: list[ItemRecord] = []
        self.cloud_config = CloudConfig()
        self.google_state = GoogleState()
        self.app_config = DesktopConfig(bind_host=bind_host, port=port)
        self.pending_import_uploads: dict[str, dict[str, Any]] = {}
        self.index_html, self.item_html = self._load_ui_assets(firmware_ino)
        self._ensure_data_files()
        self._load_app_config(bind_host, port)
        self._load_cloud_config()
        self._load_google_state()
        self._load_inventory()
        self.append_device_log("boot", "Desktop inventory service started.")

    def _mac_address_string(self) -> str:
        node = uuid.getnode()
        return ":".join(f"{(node >> shift) & 0xFF:02X}" for shift in (40, 32, 24, 16, 8, 0))

    def uptime_seconds(self) -> int:
        return int(time.monotonic() - self.start_monotonic)

    def _load_ui_assets(self, firmware_ino: Path | None) -> tuple[str, str]:
        if firmware_ino is None or not firmware_ino.exists():
            return self._fallback_index_html(), self._fallback_item_html()

        content = firmware_ino.read_text(encoding="utf-8", errors="replace")
        index_match = re.search(r'const char INDEX_HTML\[\] PROGMEM = R"HTML\((.*?)\)HTML";', content, re.S)
        item_match = re.search(r'const char ITEM_HTML\[\] PROGMEM = R"HTML\((.*?)\)HTML";', content, re.S)
        if not index_match or not item_match:
            return self._fallback_index_html(), self._fallback_item_html()
        return index_match.group(1), item_match.group(1)

    def _fallback_index_html(self) -> str:
        return """<!doctype html><html><head><meta charset='utf-8'><title>Stingray Desktop</title></head>
<body><h1>Stingray Desktop</h1><p>Firmware UI extraction failed. Use API endpoints directly.</p></body></html>"""

    def _fallback_item_html(self) -> str:
        return """<!doctype html><html><head><meta charset='utf-8'><title>Stingray Item</title></head>
<body><h1>Item Page</h1><p>Firmware item UI extraction failed.</p></body></html>"""

    def _ensure_header(self, path: Path, header: str) -> None:
        if path.exists():
            first_line = path.read_text(encoding="utf-8", errors="replace").splitlines()
            if first_line and first_line[0].strip() == header:
                return
        path.write_text(header + "\n", encoding="utf-8")

    def _migrate_file_if_present(self, legacy_path: Path, new_path: Path) -> None:
        if new_path.exists() or not legacy_path.exists():
            return
        new_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(legacy_path, new_path)

    def _migrate_legacy_tree(self) -> None:
        if self.legacy_root_dir == self.root_dir:
            return
        legacy_data_dir = self.legacy_root_dir / "data"
        if not legacy_data_dir.exists():
            return

        self._migrate_file_if_present(legacy_data_dir / "inventory.csv", self.inventory_file)
        self._migrate_file_if_present(legacy_data_dir / "orders.json", self.orders_file)
        self._migrate_file_if_present(legacy_data_dir / "transactions.csv", self.transaction_file)
        self._migrate_file_if_present(legacy_data_dir / "device_log.csv", self.device_log_file)
        self._migrate_file_if_present(legacy_data_dir / "time_log.csv", self.time_log_file)
        self._migrate_file_if_present(legacy_data_dir / "cloud_backup.cfg", self.cloud_config_file)
        self._migrate_file_if_present(legacy_data_dir / "google_drive_state.cfg", self.google_state_file)
        self._migrate_file_if_present(legacy_data_dir / APP_CONFIG_FILE, self.app_config_file)

        legacy_images_dir = legacy_data_dir / "images"
        if legacy_images_dir.exists():
            for src in legacy_images_dir.rglob("*"):
                if src.is_file():
                    dst = self.images_dir / src.relative_to(legacy_images_dir)
                    if not dst.exists():
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(src, dst)

    def _ensure_data_files(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.backups_dir.mkdir(parents=True, exist_ok=True)
        self.logs_dir.mkdir(parents=True, exist_ok=True)
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self.import_uploads_dir.mkdir(parents=True, exist_ok=True)
        self.images_dir.mkdir(parents=True, exist_ok=True)
        self._migrate_legacy_tree()
        if not self.inventory_file.exists():
            self.inventory_file.write_text(INVENTORY_HEADER + "\n", encoding="utf-8")
        if not self.orders_file.exists():
            self.orders_file.write_text('{"orders":[]}', encoding="utf-8")
        self._ensure_header(self.transaction_file, TRANSACTION_HEADER)
        self._ensure_header(self.device_log_file, DEVICE_LOG_HEADER)
        self._ensure_header(self.time_log_file, TIME_LOG_HEADER)
        if not self.cloud_config_file.exists():
            self._save_cloud_config()
        if not self.google_state_file.exists():
            self._save_google_state()

    def _load_app_config(self, bind_host: str, port: int) -> None:
        cfg = DesktopConfig(bind_host=bind_host or "0.0.0.0", port=port or 8787)
        if not self.app_config_existed:
            cfg.setup_complete = False
        if self.app_config_file.exists():
            try:
                data = json.loads(self.app_config_file.read_text(encoding="utf-8"))
                cfg.bind_host = trim_copy(str(data.get("bind_host", cfg.bind_host))) or cfg.bind_host
                cfg.port = int(data.get("port", cfg.port) or cfg.port)
                cfg.selected_lan_ip = trim_copy(str(data.get("selected_lan_ip", "")))
                cfg.configured_network_base_url = normalize_base_url(str(data.get("configured_network_base_url", "")))
                cfg.setup_complete = parse_bool(data.get("setup_complete", True if self.app_config_existed else False), default=True if self.app_config_existed else False)
                cfg.admin_pin_salt = trim_copy(str(data.get("admin_pin_salt", "")))
                cfg.admin_pin_hash = trim_copy(str(data.get("admin_pin_hash", "")))
                cfg.session_secret = trim_copy(str(data.get("session_secret", "")))
                cfg.updated_at = trim_copy(str(data.get("updated_at", "")))
            except (ValueError, json.JSONDecodeError):
                pass
        cfg.bind_host = bind_host or cfg.bind_host or "0.0.0.0"
        cfg.port = port or cfg.port or 8787
        if not cfg.selected_lan_ip:
            cfg.selected_lan_ip = default_lan_ip()
        if not cfg.configured_network_base_url:
            cfg.configured_network_base_url = f"http://{cfg.selected_lan_ip}:{cfg.port}"
        if not cfg.session_secret:
            cfg.session_secret = secrets.token_urlsafe(32)
        self.app_config = cfg
        self._save_app_config()

    def _save_app_config(self) -> None:
        self.app_config.updated_at = current_timestamp()
        self.app_config_tmp_file.write_text(json.dumps(asdict(self.app_config), indent=2) + "\n", encoding="utf-8")
        self.app_config_tmp_file.replace(self.app_config_file)

    def save_admin_pin(self, pin: str) -> None:
        salt, digest = hash_admin_pin(pin)
        with self.lock:
            self.app_config.admin_pin_salt = salt
            self.app_config.admin_pin_hash = digest
            self.app_config.setup_complete = True
            self._save_app_config()

    def clear_admin_unlock(self) -> None:
        if has_request_context():
            session.pop("desktop_admin_unlocked", None)
            session.pop("desktop_admin_unlocked_at", None)

    def set_admin_unlock(self, unlocked: bool) -> None:
        if not has_request_context():
            return
        if unlocked:
            session["desktop_admin_unlocked"] = True
            session["desktop_admin_unlocked_at"] = current_timestamp()
        else:
            self.clear_admin_unlock()

    def admin_pin_is_configured(self) -> bool:
        return bool(self.app_config.admin_pin_salt and self.app_config.admin_pin_hash)

    def admin_unlocked(self) -> bool:
        if not has_request_context():
            return False
        return bool(session.get("desktop_admin_unlocked"))

    def verify_admin_pin(self, pin: str) -> bool:
        return verify_admin_pin(pin, self.app_config.admin_pin_salt, self.app_config.admin_pin_hash)

    def admin_access_state(self) -> dict[str, Any]:
        return {
            "setup_required": not self.app_config.setup_complete,
            "setup_complete": bool(self.app_config.setup_complete),
            "admin_pin_configured": self.admin_pin_is_configured(),
            "admin_unlocked": self.admin_unlocked(),
        }

    def configured_base_url(self) -> str:
        base = normalize_base_url(self.app_config.configured_network_base_url)
        if base:
            return base
        return f"http://{self.app_config.selected_lan_ip or default_lan_ip()}:{self.app_config.port}"

    def local_pc_url(self) -> str:
        return f"http://127.0.0.1:{self.app_config.port}"

    def setup_page_url(self) -> str:
        return f"{self.configured_base_url()}/setup"

    def settings_page_url(self) -> str:
        return f"{self.configured_base_url()}/settings"

    def health_json(self, base_url: str) -> dict[str, Any]:
        lan_url = self.configured_base_url()
        local_url = self.local_pc_url()
        probe_local, local_detail = probe_url(f"{local_url}/api/status")
        probe_lan, lan_detail = probe_url(f"{lan_url}/api/status")
        firewall = firewall_rule_status(self.app_config.port)
        task = scheduled_task_status()
        setup = self.admin_access_state()
        overall_ok = bool(probe_local and probe_lan and firewall.get("installed") and setup["setup_complete"])
        overall = "green" if overall_ok else "red"
        return {
            "overall": overall,
            "overall_ok": overall_ok,
            "local_pc_url": local_url,
            "network_url": lan_url,
            "can_copy_lan_url": bool(lan_url),
            "checks": {
                "local_pc_url": {"ok": probe_local, "detail": local_detail},
                "lan_url": {"ok": probe_lan, "detail": lan_detail},
                "firewall_rule": firewall,
                "scheduled_task": task,
                "setup_complete": setup["setup_complete"],
                "admin_pin_configured": setup["admin_pin_configured"],
                "admin_unlocked": setup["admin_unlocked"],
            },
        }

    def _load_inventory(self) -> None:
        with self.lock:
            self.items = []
            if not self.inventory_file.exists():
                return
            for raw in self.inventory_file.read_text(encoding="utf-8", errors="replace").splitlines():
                line = raw.strip().lstrip("\ufeff")
                if not line or line.startswith("part_number|") or line.startswith("id|"):
                    continue
                parsed = parse_inventory_line(line)
                if parsed:
                    self.items.append(parsed)
            self.items.sort(key=lambda item: normalize_lookup_value(item.id))

    def _save_inventory(self) -> bool:
        with self.lock:
            lines = [INVENTORY_HEADER]
            for item in self.items:
                lines.append(
                    "|".join(
                        [
                            sanitize_field(item.id),
                            sanitize_field(normalize_category(item.category)),
                            sanitize_field(item.part_name),
                            sanitize_field(item.qr_code),
                            sanitize_field(item.color),
                            sanitize_field(item.material),
                            str(item.qty),
                            sanitize_field(item.image_ref),
                            sanitize_field(item.bom_product),
                            str(item.bom_qty),
                            sanitize_field(item.updated_at),
                        ]
                    )
                )
            self.inventory_tmp_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
            self.inventory_tmp_file.replace(self.inventory_file)
            return True

    def _load_orders_payload(self) -> str:
        if not self.orders_file.exists():
            return '{"orders":[]}'
        payload = self.orders_file.read_text(encoding="utf-8", errors="replace").strip()
        return payload if payload else '{"orders":[]}'

    def _read_orders_from_file(self, path: Path) -> list[dict[str, Any]]:
        if not path.exists() or not path.is_file():
            return []
        try:
            payload = json.loads(path.read_text(encoding="utf-8", errors="replace") or "{}")
        except json.JSONDecodeError:
            return []
        orders = payload.get("orders") if isinstance(payload, dict) else []
        return [order for order in orders if isinstance(order, dict)] if isinstance(orders, list) else []

    def _order_key(self, order: dict[str, Any]) -> str:
        return trim_copy(str(order.get("order_number", ""))).upper()

    def _save_orders_payload(self, payload: str) -> bool:
        trimmed = trim_copy(payload)
        if not trimmed:
            return False
        self.orders_tmp_file.write_text(trimmed, encoding="utf-8")
        self.orders_tmp_file.replace(self.orders_file)
        return True

    def _merge_orders_from_file(self, src: Path) -> tuple[int, int, int]:
        incoming = self._read_orders_from_file(src)
        current = self._read_orders_from_file(self.orders_file)
        by_number: dict[str, dict[str, Any]] = {}
        order_keys: list[str] = []
        for order in current:
            key = self._order_key(order)
            if not key:
                continue
            by_number[key] = order
            order_keys.append(key)

        added = 0
        updated = 0
        skipped = 0
        for order in incoming:
            key = self._order_key(order)
            if not key:
                skipped += 1
                continue
            if key in by_number:
                updated += 1
            else:
                added += 1
                order_keys.append(key)
            by_number[key] = order

        merged = [by_number[key] for key in order_keys if key in by_number]
        merged.sort(key=lambda order: str(order.get("updated_at") or order.get("created_at") or ""), reverse=True)
        self._save_orders_payload(json.dumps({"orders": merged}, indent=2))
        return added, updated, skipped

    def _count_orders(self, orders_file: Path) -> int:
        return len(self._read_orders_from_file(orders_file))

    def _append_data_rows_from_file(self, src: Path, dst: Path) -> int:
        if not src.exists() or not src.is_file():
            return 0
        existing: set[str] = set()
        if dst.exists() and dst.is_file():
            for raw in dst.read_text(encoding="utf-8", errors="replace").splitlines():
                line = raw.strip().lstrip("\ufeff")
                if line and not line.lower().startswith("timestamp|"):
                    existing.add(line)

        rows: list[str] = []
        for raw in src.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip().lstrip("\ufeff")
            if not line or line.lower().startswith("timestamp|") or line in existing:
                continue
            rows.append(line)
            existing.add(line)

        if rows:
            with dst.open("a", encoding="utf-8") as f:
                for line in rows:
                    f.write(line + "\n")
        return len(rows)

    def _load_cloud_config(self) -> None:
        if not self.cloud_config_file.exists():
            self.cloud_config = CloudConfig()
            return
        lines = self.cloud_config_file.read_text(encoding="utf-8", errors="replace").splitlines()
        for raw in lines:
            line = raw.strip().lstrip("\ufeff")
            if not line or line.startswith("provider|"):
                continue
            fields = split_pipe_line(line)
            if len(fields) < 6:
                continue
            cfg = CloudConfig()
            cfg.provider = trim_copy(fields[0]) or "google_drive"
            cfg.login_email = fields[1]
            cfg.folder_name = fields[2]
            cfg.folder_hint = fields[3]
            cfg.mode = trim_copy(fields[4]) or "select_or_create"
            if len(fields) >= 12:
                cfg.backup_mode = trim_copy(fields[5]) or "sd_only"
                cfg.asset_mode = trim_copy(fields[6]) or "sd_only"
                cfg.brand_name = fields[7] or "Stingray Inventory"
                cfg.brand_logo_ref = fields[8]
                cfg.client_id = fields[9]
                cfg.client_secret = fields[10]
                cfg.updated_at = fields[11]
            self.cloud_config = cfg
            return
        self.cloud_config = CloudConfig()

    def _save_cloud_config(self) -> None:
        cfg = self.cloud_config
        line = "|".join(
            [
                sanitize_field(cfg.provider),
                sanitize_field(cfg.login_email),
                sanitize_field(cfg.folder_name),
                sanitize_field(cfg.folder_hint),
                sanitize_field(cfg.mode),
                sanitize_field(cfg.backup_mode),
                sanitize_field(cfg.asset_mode),
                sanitize_field(cfg.brand_name),
                sanitize_field(cfg.brand_logo_ref),
                sanitize_field(cfg.client_id),
                sanitize_field(cfg.client_secret),
                sanitize_field(cfg.updated_at),
            ]
        )
        self.cloud_config_tmp_file.write_text(CLOUD_CONFIG_HEADER + "\n" + line + "\n", encoding="utf-8")
        self.cloud_config_tmp_file.replace(self.cloud_config_file)

    def _load_google_state(self) -> None:
        if not self.google_state_file.exists():
            self.google_state = GoogleState()
            return
        lines = self.google_state_file.read_text(encoding="utf-8", errors="replace").splitlines()
        for raw in lines:
            line = raw.strip().lstrip("\ufeff")
            if not line or line.startswith("refresh_token|"):
                continue
            fields = split_pipe_line(line)
            if len(fields) < 9:
                continue
            self.google_state = GoogleState(
                refresh_token=fields[0],
                folder_id=fields[1],
                last_sync_at=fields[2],
                last_synced_manifest_hash=fields[3],
                last_synced_snapshot_at=fields[4],
                local_snapshot_at=fields[5],
                auth_status=fields[6] or "desktop_local",
                sync_status=fields[7] or "idle",
                last_error=fields[8],
            )
            return
        self.google_state = GoogleState()

    def _save_google_state(self) -> None:
        state = self.google_state
        line = "|".join(
            [
                sanitize_field(state.refresh_token),
                sanitize_field(state.folder_id),
                sanitize_field(state.last_sync_at),
                sanitize_field(state.last_synced_manifest_hash),
                sanitize_field(state.last_synced_snapshot_at),
                sanitize_field(state.local_snapshot_at),
                sanitize_field(state.auth_status),
                sanitize_field(state.sync_status),
                sanitize_field(state.last_error),
            ]
        )
        self.google_state_tmp_file.write_text(GOOGLE_STATE_HEADER + "\n" + line + "\n", encoding="utf-8")
        self.google_state_tmp_file.replace(self.google_state_file)

    def append_device_log(self, event: str, detail: str) -> None:
        with self.lock:
            line = "|".join(
                [
                    sanitize_field(current_timestamp()),
                    sanitize_field(self.mac_address),
                    str(self.uptime_seconds()),
                    sanitize_field(event),
                    sanitize_field(detail),
                ]
            )
            with self.device_log_file.open("a", encoding="utf-8") as f:
                f.write(line + "\n")

    def append_time_log(self, event: str, detail: str) -> None:
        with self.lock:
            line = "|".join(
                [
                    sanitize_field(current_timestamp()),
                    sanitize_field(event),
                    sanitize_field(detail),
                ]
            )
            with self.time_log_file.open("a", encoding="utf-8") as f:
                f.write(line + "\n")

    def append_transaction(self, item_id: str, action: str, delta: int, qty_after: int, note: str) -> None:
        with self.lock:
            line = "|".join(
                [
                    sanitize_field(current_timestamp()),
                    sanitize_field(item_id),
                    sanitize_field(action),
                    str(delta),
                    str(qty_after),
                    sanitize_field(note),
                ]
            )
            with self.transaction_file.open("a", encoding="utf-8") as f:
                f.write(line + "\n")

    def find_item_index(self, item_id: str) -> int:
        target = normalize_lookup_value(item_id)
        for idx, item in enumerate(self.items):
            if normalize_lookup_value(item.id) == target:
                return idx
        return -1

    def item_url(self, item_id: str, base_url: str) -> str:
        return f"{base_url}/item?id={quote(item_id)}"

    def resolve_asset_file_path(self, asset_ref: str) -> Path | None:
        storage_path = decode_storage_image_path(asset_ref)
        if not storage_path or not safe_relative_image_path(storage_path):
            return None
        full_path = self.data_dir / storage_path.lstrip("/")
        if full_path.exists() and full_path.is_file():
            return full_path
        return None

    def item_can_have_bom(self, item: ItemRecord) -> bool:
        return normalize_category(item.category) in {"product", "kit"}

    def is_bom_component_of(self, component: ItemRecord, parent: ItemRecord) -> bool:
        if normalize_category(component.category) != "part":
            return False
        component_parent = normalize_lookup_value(component.bom_product)
        parent_id = normalize_lookup_value(parent.id)
        parent_name = normalize_lookup_value(parent.part_name)
        return bool(component_parent) and (component_parent == parent_id or component_parent == parent_name)

    def item_to_dict(self, item: ItemRecord, base_url: str) -> dict[str, Any]:
        return {
            "id": item.id,
            "category": normalize_category(item.category),
            "category_label": category_label(item.category),
            "part_name": item.part_name,
            "qr_code": item.qr_code,
            "color": item.color,
            "material": item.material,
            "qty": item.qty,
            "image_ref": item.image_ref,
            "bom_product": item.bom_product,
            "bom_qty": item.bom_qty,
            "has_bom": self.item_can_have_bom(item),
            "updated_at": item.updated_at,
            "stock_zero": item.qty == 0,
            "qr_link": self.item_url(item.id, base_url),
        }

    def item_payload(self, item: ItemRecord, base_url: str) -> dict[str, Any]:
        components = [self.item_to_dict(component, base_url) for component in self.items if self.is_bom_component_of(component, item)]
        return {"item": self.item_to_dict(item, base_url), "bom_components": components}

    def matches_category_filter(self, item: ItemRecord, raw_filter: str) -> bool:
        value = normalize_category(raw_filter)
        return raw_filter.strip().lower() in {"", "all"} or normalize_category(item.category) == value

    def matches_search_filter(self, item: ItemRecord, raw_search: str) -> bool:
        search = lower_copy(trim_copy(raw_search))
        if not search:
            return True
        haystack = " ".join(
            [
                item.id,
                item.category,
                item.part_name,
                item.qr_code,
                item.color,
                item.material,
                item.image_ref,
                item.bom_product,
                str(item.bom_qty),
            ]
        ).lower()
        return search in haystack

    def tail_log_lines(self, path: Path, limit: int = 40) -> list[str]:
        if not path.exists():
            return []
        lines = []
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip()
            if not line or line.startswith("timestamp|"):
                continue
            lines.append(line)
        return lines[-limit:]

    def backup_zip_path(self) -> Path:
        stamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
        return self.backups_dir / f"stingray-backup-{stamp}.zip"

    def create_backup_zip(self, reason: str) -> Path:
        self.backups_dir.mkdir(parents=True, exist_ok=True)
        target = self.backup_zip_path()
        temp_target = target.with_name(f"{target.stem}.tmp{target.suffix}")
        if temp_target.exists():
            temp_target.unlink()
        try:
            with zipfile.ZipFile(temp_target, "w", compression=zipfile.ZIP_DEFLATED, strict_timestamps=False) as zf:
                zf.writestr(
                    "manifest.json",
                    json.dumps({"created_at": current_timestamp(), "reason": reason, "item_count": len(self.items)}, indent=2) + "\n",
                )
                for name in IMPORT_COPY_FILES:
                    path = self.data_dir / name
                    if path.exists() and path.is_file():
                        zf.write(path, name)
                if self.images_dir.exists():
                    for path in self.images_dir.rglob("*"):
                        if path.is_file():
                            zf.write(path, path.relative_to(self.data_dir).as_posix())
            if target.exists():
                target.unlink()
            temp_target.replace(target)
        except Exception:
            temp_target.unlink(missing_ok=True)
            raise
        self.append_device_log("backup_created", f"{target.name} ({reason})")
        return target

    def restore_backup_zip(self, zip_path: Path) -> dict[str, Any]:
        backup = self.create_backup_zip("before_backup_restore")
        restored: list[str] = []
        allowed_names = set(IMPORT_COPY_FILES)
        with zipfile.ZipFile(zip_path, "r") as zf:
            for raw_name in zf.namelist():
                name = raw_name.replace("\\", "/").lstrip("/")
                if not name or name == "manifest.json" or ".." in name:
                    continue
                if not (name in allowed_names or name.startswith("images/")):
                    continue
                target = self.data_dir / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with zf.open(raw_name) as src, target.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
                restored.append(name)
        self._load_inventory()
        self._load_cloud_config()
        self._load_google_state()
        self._load_app_config(self.app_config.bind_host, self.app_config.port)
        self.append_device_log("backup_restored", f"Restored {len(restored)} files from {zip_path.name}")
        return {"ok": True, "backup_before_restore": backup.name, "files_restored": restored}

    def _sd_child(self, folder: Path, name: str) -> Path:
        return resolved_child(folder, name)

    def _sd_images_dir(self, folder: Path) -> Path:
        return resolved_child(folder, "images")

    def _sd_files_found(self, folder: Path) -> list[str]:
        found: list[str] = []
        for name in IMPORT_COPY_FILES:
            child = self._sd_child(folder, name)
            if child.exists() and child.is_file():
                found.append(name)
        return found

    def _count_inventory_rows(self, inventory_file: Path) -> int:
        if not inventory_file.exists() or not inventory_file.is_file():
            return 0
        count = 0
        for raw in inventory_file.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip().lstrip("\ufeff")
            if line and not is_inventory_header_line(line) and parse_inventory_line(line):
                count += 1
        return count

    def _count_transaction_rows(self, transaction_file: Path) -> int:
        if not transaction_file.exists() or not transaction_file.is_file():
            return 0
        rows = []
        for raw in transaction_file.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip().lstrip("\ufeff")
            if line and not line.lower().startswith("timestamp|"):
                rows.append(line)
        return len(rows)

    def _count_images(self, images_dir: Path) -> int:
        if not images_dir.exists() or not images_dir.is_dir():
            return 0
        return len([path for path in images_dir.rglob("*") if path.is_file()])

    def _sd_candidate_score(self, folder: Path) -> tuple[int, int, int, int]:
        inventory_count = self._count_inventory_rows(self._sd_child(folder, "inventory.csv"))
        files_found = self._sd_files_found(folder)
        images_found = self._count_images(self._sd_images_dir(folder))
        marker_count = len(files_found) + (1 if images_found else 0)
        return inventory_count, marker_count, images_found, -len(folder.parts)

    def resolve_sd_import_root(self, folder: Path) -> tuple[Path, list[str]]:
        messages: list[str] = []
        if not folder.exists() or not folder.is_dir():
            raise ValueError("SD folder not found.")

        direct_score = self._sd_candidate_score(folder)
        if direct_score[0] > 0 or direct_score[1] > 0:
            return folder, messages

        candidates: list[tuple[tuple[int, int, int, int], Path]] = []
        pending: list[tuple[Path, int]] = [(folder, 0)]
        try:
            while pending:
                current, depth = pending.pop()
                if depth >= SD_IMPORT_SEARCH_DEPTH:
                    continue
                for candidate in current.iterdir():
                    try:
                        if candidate.is_symlink() or not candidate.is_dir():
                            continue
                    except OSError:
                        continue
                    score = self._sd_candidate_score(candidate)
                    if score[0] > 0 or score[1] > 0:
                        candidates.append((score, candidate))
                    pending.append((candidate, depth + 1))
        except OSError as exc:
            messages.append(f"Stopped scanning some folders: {exc}")

        if not candidates:
            return folder, messages

        candidates.sort(key=lambda entry: entry[0], reverse=True)
        root = candidates[0][1]
        messages.append(f"Using detected SD data folder: {root}")
        return root, messages

    def inventory_import_suggestions(self) -> list[dict[str, Any]]:
        suggestions: list[dict[str, Any]] = []
        seen: set[str] = set()
        search_roots = [
            ("Desktop", Path.home() / "Desktop"),
            ("Downloads", Path.home() / "Downloads"),
            ("Documents", Path.home() / "Documents"),
        ]

        for location_label, root in search_roots:
            if not root.exists() or not root.is_dir():
                continue
            try:
                for child in root.iterdir():
                    if not child.is_dir():
                        continue
                    resolved = str(child.resolve())
                    if resolved in seen:
                        continue
                    score = self._sd_candidate_score(child)
                    if score[0] <= 0 and score[1] <= 0 and score[2] <= 0:
                        continue
                    seen.add(resolved)
                    files_found = self._sd_files_found(child)
                    suggestions.append(
                        {
                            "label": f"{location_label} \\ {child.name}",
                            "path": str(child),
                            "inventory_items_found": score[0],
                            "images_found": score[2],
                            "files_found": files_found,
                        }
                    )
            except OSError:
                continue

        suggestions.sort(key=lambda entry: (entry["inventory_items_found"], entry["images_found"], len(entry["files_found"])), reverse=True)
        return suggestions[:5]

    def _normalize_uploaded_relative_path(self, filename: str) -> Path | None:
        text = trim_copy(filename).replace("\\", "/")
        if not text:
            return None
        candidate = Path(text)
        if candidate.is_absolute() or candidate.drive:
            return None
        parts = [part for part in candidate.parts if part not in {"", ".", ".."}]
        if not parts:
            return None
        return Path(*parts)

    def _staged_import_path(self, token: str) -> Path:
        return self.import_uploads_dir / trim_copy(token)

    def discard_staged_import(self, token: str) -> None:
        stage_dir = self._staged_import_path(token)
        with self.lock:
            self.pending_import_uploads.pop(trim_copy(token), None)
        if stage_dir.exists():
            shutil.rmtree(stage_dir, ignore_errors=True)

    def stage_import_uploads(self, uploads: list[Any], source_label: str = "") -> dict[str, Any]:
        valid_uploads: list[tuple[Any, Path]] = []
        errors: list[str] = []
        for uploaded in uploads:
            raw_name = trim_copy(getattr(uploaded, "filename", ""))
            rel_path = self._normalize_uploaded_relative_path(raw_name)
            if rel_path is None:
                errors.append(f"Skipped invalid upload path: {raw_name[:80]}")
                continue
            valid_uploads.append((uploaded, rel_path))

        if not valid_uploads:
            raise ValueError("No valid files were uploaded from the selected folder.")

        token = uuid.uuid4().hex
        stage_dir = self._staged_import_path(token)
        if stage_dir.exists():
            shutil.rmtree(stage_dir, ignore_errors=True)
        stage_dir.mkdir(parents=True, exist_ok=True)

        for uploaded, rel_path in valid_uploads:
            target = stage_dir / rel_path
            target.parent.mkdir(parents=True, exist_ok=True)
            uploaded.save(target)

        first_parts = valid_uploads[0][1].parts
        label = trim_copy(source_label) or (first_parts[0] if first_parts else stage_dir.name)
        try:
            preview = self.preview_sd_import(stage_dir)
            payload = {
                "ok": True,
                "token": token,
                "source_label": label,
                "staged_path": str(stage_dir),
                "uploaded_files": len(valid_uploads),
                "errors": errors,
                "preview": preview,
            }
            with self.lock:
                self.pending_import_uploads[token] = {
                    "path": str(stage_dir),
                    "source_label": label,
                    "created_at": current_timestamp(),
                }
            return payload
        except Exception:
            shutil.rmtree(stage_dir, ignore_errors=True)
            raise

    def staged_import_preview(self, token: str) -> dict[str, Any]:
        token = trim_copy(token)
        if not token:
            raise ValueError("Missing staged import token.")
        with self.lock:
            entry = self.pending_import_uploads.get(token)
        if not entry:
            raise ValueError("Staged folder import not found.")
        stage_dir = Path(entry["path"])
        if not stage_dir.exists():
            self.discard_staged_import(token)
            raise ValueError("Staged folder import expired or was removed.")
        preview = self.preview_sd_import(stage_dir)
        preview["token"] = token
        preview["source_label"] = entry.get("source_label", "")
        preview["staged"] = True
        return preview

    def import_staged_upload(self, token: str, mode: str) -> dict[str, Any]:
        token = trim_copy(token)
        if not token:
            raise ValueError("Missing staged import token.")
        with self.lock:
            entry = self.pending_import_uploads.get(token)
        if not entry:
            raise ValueError("Staged folder import not found.")
        stage_dir = Path(entry["path"])
        if not stage_dir.exists():
            self.discard_staged_import(token)
            raise ValueError("Staged folder import expired or was removed.")
        try:
            result = self.import_sd_folder(stage_dir, mode)
        finally:
            self.discard_staged_import(token)
        result["token"] = token
        result["source_label"] = entry.get("source_label", "")
        return result

    def preview_sd_import(self, folder: Path) -> dict[str, Any]:
        requested_folder = folder
        folder, messages = self.resolve_sd_import_root(folder)
        inventory_count = 0
        transaction_count = 0
        order_count = 0
        device_log_count = 0
        time_log_count = 0
        image_count = 0
        files_found: list[str] = []
        inv = self._sd_child(folder, "inventory.csv")
        inventory_count = self._count_inventory_rows(inv)
        transaction_count = self._count_transaction_rows(self._sd_child(folder, "transactions.csv"))
        order_count = self._count_orders(self._sd_child(folder, "orders.json"))
        device_log_count = self._count_transaction_rows(self._sd_child(folder, "device_log.csv"))
        time_log_count = self._count_transaction_rows(self._sd_child(folder, "time_log.csv"))
        image_count = self._count_images(self._sd_images_dir(folder))
        files_found = self._sd_files_found(folder)
        return {
            "requested_path": str(requested_folder),
            "import_root": str(folder),
            "inventory_items_found": inventory_count,
            "orders_found": order_count,
            "transaction_rows_found": transaction_count,
            "device_log_rows_found": device_log_count,
            "time_log_rows_found": time_log_count,
            "images_found": image_count,
            "files_found": files_found,
            "current_inventory_items": len(self.items),
            "recommended_action": "Merge into current inventory" if self.items else "Replace current inventory",
            "messages": messages,
        }

    def import_sd_folder(self, folder: Path, mode: str) -> dict[str, Any]:
        requested_folder = folder
        folder, messages = self.resolve_sd_import_root(folder)
        if not self._sd_files_found(folder) and not self._count_images(self._sd_images_dir(folder)):
            raise ValueError("No Stingray SD data files were found. Select the SD card root or the folder containing inventory.csv.")
        inv = self._sd_child(folder, "inventory.csv")
        if mode in {"replace", "backup_replace"} and (not inv.exists() or not inv.is_file()):
            raise ValueError("Replace import requires inventory.csv. Select the SD card root or use merge mode for image-only imports.")
        backup = self.create_backup_zip("before_sd_import")
        incoming: list[ItemRecord] = []
        errors: list[str] = []
        saw_inventory_data = False
        if inv.exists() and inv.is_file():
            for raw in inv.read_text(encoding="utf-8", errors="replace").splitlines():
                line = raw.strip().lstrip("\ufeff")
                if not line or is_inventory_header_line(line):
                    continue
                saw_inventory_data = True
                parsed = parse_inventory_line(line)
                if parsed:
                    incoming.append(parsed)
                else:
                    errors.append(f"Skipped invalid inventory row: {line[:80]}")
        if mode in {"replace", "backup_replace"} and saw_inventory_data and not incoming:
            raise ValueError("No valid inventory rows were found in inventory.csv. Import was stopped so the current inventory is not erased.")

        imported = 0
        skipped = 0
        replaced = 0
        with self.lock:
            if mode in {"replace", "backup_replace"}:
                replaced = len(self.items)
                self.items = incoming
                imported = len(incoming)
            else:
                for item in incoming:
                    idx = self.find_item_index(item.id)
                    if idx >= 0:
                        skipped += 1
                    else:
                        self.items.append(item)
                        imported += 1
            self.items.sort(key=lambda row: normalize_lookup_value(row.id))
            self._save_inventory()
            for item in incoming:
                self.append_transaction(item.id, "import_item", 0, item.qty, f"SD import {mode}")

        copied_files = 0
        orders_added = 0
        orders_updated = 0
        orders_skipped = 0
        transactions_imported = 0
        device_log_rows_imported = 0
        time_log_rows_imported = 0
        if mode != "merge":
            orders_added = self._count_orders(self._sd_child(folder, "orders.json"))
            transactions_imported = self._count_transaction_rows(self._sd_child(folder, "transactions.csv"))
            device_log_rows_imported = self._count_transaction_rows(self._sd_child(folder, "device_log.csv"))
            time_log_rows_imported = self._count_transaction_rows(self._sd_child(folder, "time_log.csv"))
        for name in IMPORT_COPY_FILES:
            if name == "inventory.csv" and mode == "merge":
                continue
            src = self._sd_child(folder, name)
            if src.exists() and src.is_file():
                if mode == "merge" and name == "orders.json":
                    orders_added, orders_updated, orders_skipped = self._merge_orders_from_file(src)
                    copied_files += 1
                    continue
                if mode == "merge" and name == "transactions.csv":
                    transactions_imported = self._append_data_rows_from_file(src, self.transaction_file)
                    copied_files += 1
                    continue
                if mode == "merge" and name == "device_log.csv":
                    device_log_rows_imported = self._append_data_rows_from_file(src, self.device_log_file)
                    copied_files += 1
                    continue
                if mode == "merge" and name == "time_log.csv":
                    time_log_rows_imported = self._append_data_rows_from_file(src, self.time_log_file)
                    copied_files += 1
                    continue
                shutil.copy2(src, self.data_dir / name)
                copied_files += 1
        copied_images = 0
        src_images = self._sd_images_dir(folder)
        if src_images.exists() and src_images.is_dir():
            for src in src_images.rglob("*"):
                if src.is_file():
                    dst = self.images_dir / src.relative_to(src_images)
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(src, dst)
                    copied_images += 1
        self._load_inventory()
        self._load_cloud_config()
        self._load_google_state()
        return {
            "ok": True,
            "backup": backup.name,
            "requested_path": str(requested_folder),
            "import_root": str(folder),
            "items_imported": imported,
            "items_skipped": skipped,
            "items_replaced": replaced,
            "orders_added": orders_added,
            "orders_updated": orders_updated,
            "orders_skipped": orders_skipped,
            "images_copied": copied_images,
            "files_copied": copied_files,
            "transactions_imported": transactions_imported,
            "device_log_rows_imported": device_log_rows_imported,
            "time_log_rows_imported": time_log_rows_imported,
            "errors": errors,
            "messages": messages,
        }

    def sd_status_fields(self) -> dict[str, Any]:
        return {
            "sd_ready": True,
            "sd_card_present": True,
            "sd_card_size_bytes": 0,
            "sd_mount_state": "desktop_ready",
            "sd_last_error": "",
            "sd_can_format": False,
        }

    def status_json(self, base_url: str) -> dict[str, Any]:
        lan_ips = detect_lan_ips()
        configured_base_url = self.configured_base_url()
        host = urlparse(configured_base_url).hostname or ""
        firewall = firewall_rule_status(self.app_config.port)
        task = scheduled_task_status()
        setup = self.admin_access_state()
        payload = {
            "app_name": APP_DISPLAY_NAME,
            "legacy_app_name": LEGACY_APP_DISPLAY_NAME,
            "board": "PC_DESKTOP",
            "device_id": self.device_id,
            "storage_mode": "pc_fs",
            "hostname": socket.gethostname(),
            "base_url": configured_base_url,
            "configured_network_base_url": configured_base_url,
            "local_pc_url": self.local_pc_url(),
            "network_url": configured_base_url,
            "bind_address": self.app_config.bind_host,
            "port": self.app_config.port,
            "detected_lan_ips": lan_ips,
            "selected_lan_ip": self.app_config.selected_lan_ip,
            "server_listening_all_interfaces": self.app_config.bind_host in {"0.0.0.0", "::"},
            "firewall_rule": firewall,
            "firewall_rule_installed": firewall.get("installed", False),
            "firewall_rule_name": firewall.get("name", FIREWALL_RULE_NAME),
            "auto_run": task,
            "auto_run_enabled": task.get("enabled", False),
            "auto_run_task_name": task.get("name", SYSTEM_TASK_NAME),
            "run_mode": "Scheduled Task" if task.get("exists") else "Manual",
            "supervisor_active": task.get("running", False),
            "setup_complete": setup["setup_complete"],
            "setup_required": setup["setup_required"],
            "admin_pin_configured": setup["admin_pin_configured"],
            "admin_unlocked": setup["admin_unlocked"],
            "admin_shell_locked": setup["admin_pin_configured"] and not setup["admin_unlocked"],
            "qr_loopback_warning": is_loopback_host(host),
            "data_dir": str(self.data_dir),
            "backups_dir": str(self.backups_dir),
            "logs_dir": str(self.logs_dir),
            "config_dir": str(self.config_dir),
            "brand_name": self.cloud_config.brand_name,
            "brand_logo_ref": self.cloud_config.brand_logo_ref,
            "backup_mode": self.cloud_config.backup_mode,
            "asset_mode": self.cloud_config.asset_mode,
            "auth_status": self.google_state.auth_status,
            "sync_status": self.google_state.sync_status,
            "folder_id": self.google_state.folder_id,
            "last_sync_at": self.google_state.last_sync_at,
            "time_source": "system_clock",
            "network_transport": "LAN/Ethernet",
            "lan_connected": True,
            "lan_ip": self.app_config.selected_lan_ip or (lan_ips[0] if lan_ips else "127.0.0.1"),
            "lan_mode": "desktop",
            "lan_gateway": "",
            "lan_dns": [],
            "lan_speed": "",
            "lan_last_error": "",
        }
        payload.update(self.sd_status_fields())
        return payload

    def wifi_config_json(self) -> dict[str, Any]:
        lan_ip = self.app_config.selected_lan_ip or (detect_lan_ips()[0] if detect_lan_ips() else "127.0.0.1")
        return {
            "config_source": "desktop",
            "saved_ssid": lan_ip,
            "saved_updated_at": self.app_config.updated_at,
            "effective_ssid": lan_ip,
            "connected": True,
            "current_ssid": "desktop-lan",
            "current_ip": lan_ip,
            "current_rssi": 0,
            "wifi_status": "connected",
            "wifi_mode": "desktop",
            "ap_active": False,
            "ap_ssid": "",
            "ap_ip": "",
            "ap_clients": 0,
            "last_error": "",
            "setup_required": False,
        }

    def cloud_config_json(self) -> dict[str, Any]:
        payload = asdict(self.cloud_config)
        payload["auth_ready"] = bool(self.google_state.refresh_token)
        payload["auth_status"] = self.google_state.auth_status
        payload["sync_status"] = self.google_state.sync_status
        payload["last_error"] = self.google_state.last_error
        payload["verification_url"] = ""
        payload["user_code"] = ""
        payload["device_poll_interval"] = 5
        payload["folder_id"] = self.google_state.folder_id
        payload["last_sync_at"] = self.google_state.last_sync_at
        payload["last_synced_snapshot_at"] = self.google_state.last_synced_snapshot_at
        payload["local_snapshot_at"] = self.google_state.local_snapshot_at
        payload["drive_scope"] = "https://www.googleapis.com/auth/drive.file"
        payload.update(self.sd_status_fields())
        return payload


def create_app(data_dir: Path, firmware_ino: Path | None, bind_host: str = "0.0.0.0", port: int = 8787) -> tuple[Flask, DesktopStore]:
    app = Flask(__name__)
    store = DesktopStore(data_dir=data_dir, firmware_ino=firmware_ino, bind_host=bind_host, port=port)
    app.secret_key = store.app_config.session_secret or secrets.token_urlsafe(32)
    app.config["SESSION_COOKIE_NAME"] = "inventory_desktop_session"

    def base_url() -> str:
        return store.configured_base_url()

    def arg(name: str, default: str = "") -> str:
        if request.is_json:
            payload = request.get_json(silent=True) or {}
            if name in payload:
                return str(payload.get(name, default))
        return str(request.values.get(name, default))

    def json_error(status: int, message: str):
        return jsonify({"error": message}), status

    def require_admin_access() -> tuple[bool, Any | None]:
        if not store.admin_pin_is_configured():
            return True, None
        if store.admin_unlocked():
            return True, None
        return False, json_error(403, "Admin PIN required.")

    def read_pin_from_request(*names: str) -> str:
        for name in names:
            value = trim_copy(arg(name))
            if value:
                return value
        return ""

    def set_app_config_and_session_secret_if_needed() -> None:
        if not store.app_config.session_secret:
            store.app_config.session_secret = secrets.token_urlsafe(32)
            store._save_app_config()
        app.secret_key = store.app_config.session_secret

    def desktop_settings_html(setup_mode: bool = False) -> str:
        html = store.index_html
        html = html.replace("<h2>Wi-Fi Setup</h2>", "<h2>Desktop LAN Access</h2>")
        html = html.replace(
            '<p id="wifi-caption" class="caption">Scan nearby networks, save Wi-Fi credentials on the device, and connect without reflashing firmware.</p>',
            '<p id="wifi-caption" class="caption">Use this PC on the LAN or Ethernet. QR codes use the configured LAN URL.</p>',
        )
        html = html.replace("<span>Nearby Networks</span>", "<span>Detected LAN IPs</span>")
        html = html.replace('value="">Choose a scanned network', 'value="">Choose a LAN IP')
        html = html.replace('value="__manual__">Enter network manually / hidden SSID', 'value="__manual__">Enter LAN URL manually')
        html = html.replace('Scan Networks', 'Refresh LAN IPs')
        html = html.replace('Network Name (SSID)', 'Selected LAN IP')
        html = html.replace('Choose a network or type one manually', 'Choose a LAN IP or type one manually')
        html = html.replace('Leave blank for open networks', 'http://192.168.1.50:8787')
        html = html.replace('Save And Connect Wi-Fi', 'Save LAN URL')
        html = html.replace('Forget Saved Wi-Fi', 'Reset LAN URL')
        html = html.replace('Loading Wi-Fi status...', 'Loading LAN status...')
        html = html.replace('Wi-Fi: ', 'LAN/Ethernet: ')
        html = html.replace(
            "Saved Wi-Fi settings updated at ${safeConfig.saved_updated_at}. Scan nearby networks, or type a hidden SSID manually.",
            "Saved LAN settings updated at ${safeConfig.saved_updated_at}. Select a LAN IP or type one manually.",
        )
        html = html.replace(
            "Scan nearby networks, save Wi-Fi credentials on the device, and connect without reflashing firmware.",
            "Use the LAN or Ethernet connection on this PC. Select a LAN IP and keep the QR/base URL aligned with it.",
        )
        html = html.replace(
            "Choose a scanned network",
            "Choose a LAN IP",
        )
        html = html.replace(
            "No scanned networks yet",
            "No LAN IPs detected yet",
        )
        html = html.replace(
            "Enter network manually / hidden SSID",
            "Enter LAN URL manually",
        )
        html = html.replace(
            "Found 0 Wi-Fi network(s). Keep AP enabled, confirm a 2.4 GHz SSID is nearby, then scan again.",
            "Found 0 LAN IPs. Check the network connection, then refresh again.",
        )
        html = html.replace(
            "Found ${networks.length} Wi-Fi network(s).",
            "Found ${networks.length} LAN IP(s).",
        )
        html = html.replace(
            "Saved Wi-Fi settings cleared from this device.",
            "Saved LAN settings cleared from this device.",
        )
        html = html.replace(
            "Wi-Fi connected to ${data.current_ssid || data.saved_ssid || 'the selected network'}. Reconnect on ${data.current_ip || 'the new network address'} after AP shutdown.",
            "LAN connection using ${data.current_ssid || data.saved_ssid || 'the selected network'}. Reconnect on ${data.current_ip || 'the selected network address'} after the refresh.",
        )
        html = html.replace(
            "Wi-Fi settings saved for ${data.saved_ssid || 'the selected network'}, but the device stayed in AP mode. Check the password or signal and try again.",
            "LAN settings saved for ${data.saved_ssid || 'the selected network'}, but the connection did not update. Check the IP or URL and try again.",
        )
        wizard_visible = setup_mode or not store.app_config.setup_complete
        wizard_hidden = "" if wizard_visible else " hidden"
        admin_locked = not store.admin_unlocked()
        admin_shell_hidden = "" if not admin_locked else " hidden"
        panel = f'''
<style>
  #desktop-health-panel .desktop-health-grid {{
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 0.75rem;
    margin: 0.75rem 0 1rem;
  }}
  #desktop-health-panel .desktop-health-card {{
    border: 1px solid rgba(128, 128, 128, 0.35);
    border-radius: 14px;
    padding: 0.8rem 1rem;
    background: rgba(255, 255, 255, 0.03);
  }}
  #desktop-health-panel .desktop-health-card.ok {{
    border-color: rgba(50, 140, 60, 0.8);
    background: rgba(50, 140, 60, 0.12);
  }}
  #desktop-health-panel .desktop-health-card.bad {{
    border-color: rgba(180, 40, 40, 0.8);
    background: rgba(180, 40, 40, 0.12);
  }}
  #desktop-health-panel .desktop-health-state {{
    font-size: 1.05rem;
    font-weight: 700;
    margin: 0.25rem 0;
  }}
  #desktop-health-panel .desktop-health-detail {{
    font-size: 0.9rem;
    opacity: 0.85;
  }}
  .desktop-folder-tools {{
    display: flex;
    flex-wrap: wrap;
    gap: 0.6rem;
    margin: 0.75rem 0;
  }}
  .desktop-folder-tools button {{
    width: auto;
    min-width: 160px;
  }}
  .desktop-dropzone {{
    border: 2px dashed rgba(90, 120, 145, 0.55);
    border-radius: 16px;
    background: rgba(245, 248, 251, 0.75);
    color: var(--ink);
    min-height: 88px;
    padding: 0.95rem 1rem;
    display: flex;
    align-items: center;
    justify-content: center;
    text-align: center;
    margin-top: 0.35rem;
    user-select: none;
    cursor: copy;
  }}
  .desktop-dropzone.dragging {{
    border-color: rgba(90, 190, 120, 0.95);
    background: rgba(90, 190, 120, 0.16);
  }}
  .desktop-wizard {{
    border: 1px solid rgba(120, 120, 120, 0.28);
    border-radius: 18px;
    padding: 1rem;
    margin-bottom: 1rem;
    background: linear-gradient(135deg, rgba(32, 59, 30, 0.18), rgba(18, 27, 33, 0.04));
  }}
  .desktop-wizard[hidden],
  .desktop-admin-shell[hidden] {{
    display: none !important;
  }}
  .desktop-callout {{
    border-left: 4px solid rgba(80, 160, 90, 0.9);
    padding-left: 0.75rem;
    margin: 0.5rem 0 1rem;
  }}
</style>
      <section class="info-panel desktop-wizard" id="desktop-setup-wizard"{wizard_hidden}>
        <h2>First-Run Setup</h2>
        <p class="caption">Create a LAN URL and an admin PIN before you start using advanced desktop settings.</p>
        <div class="form-grid">
          <label>
            Local PC URL
            <input id="desktop-setup-local-url" type="text" readonly>
          </label>
          <label>
            LAN access URL
            <input id="desktop-setup-network-url" type="text" readonly>
          </label>
          <label>
            Selected host IP
            <select id="desktop-setup-lan-ip"></select>
          </label>
          <label>
            QR/base URL
            <input id="desktop-setup-base-url" type="text" placeholder="http://192.168.1.50:8787">
          </label>
          <label>
            Admin PIN
            <input id="desktop-setup-pin" type="password" placeholder="Create a 4+ character PIN">
          </label>
          <label>
            Confirm PIN
            <input id="desktop-setup-pin-confirm" type="password" placeholder="Repeat the PIN">
          </label>
        </div>
        <div class="cloud-actions">
          <button id="desktop-setup-save-btn" type="button">Finish Setup</button>
          <button id="desktop-setup-health-btn" type="button" class="secondary">Check LAN Reachability</button>
        </div>
        <div class="desktop-callout">After setup, the app will open the reachable LAN URL automatically.</div>
        <pre id="desktop-setup-status" class="cloud-status">Setup not complete.</pre>
      </section>

      <section class="info-panel" id="desktop-health-panel">
        <h2>Health Dashboard</h2>
        <p class="caption">Green means the desktop service is ready for phones, tablets, and reboot recovery.</p>
        <div class="desktop-health-grid">
          <div class="desktop-health-card" id="desktop-health-overall-card">
            <div class="small">Overall</div>
            <div class="desktop-health-state" id="desktop-health-overall-state">Loading...</div>
            <div class="desktop-health-detail" id="desktop-health-overall-detail">Waiting for status.</div>
          </div>
          <div class="desktop-health-card" id="desktop-health-lan-card">
            <div class="small">LAN URL</div>
            <div class="desktop-health-state" id="desktop-health-lan-state">Loading...</div>
            <div class="desktop-health-detail" id="desktop-health-lan-detail">Waiting for status.</div>
          </div>
          <div class="desktop-health-card" id="desktop-health-firewall-card">
            <div class="small">Firewall / Startup</div>
            <div class="desktop-health-state" id="desktop-health-firewall-state">Loading...</div>
            <div class="desktop-health-detail" id="desktop-health-firewall-detail">Waiting for status.</div>
          </div>
        </div>
        <div class="cloud-actions">
          <button id="desktop-copy-lan-btn" type="button">Copy LAN URL</button>
          <button id="desktop-health-test-btn" type="button" class="secondary">Test This PC</button>
        </div>
        <pre id="desktop-health-status" class="cloud-status">Loading desktop health...</pre>
      </section>

      <section class="info-panel" id="desktop-lan-panel">
        <h2>Desktop LAN Access</h2>
        <p class="caption">Use this URL from phones and tablets on the same LAN. Ethernet uplinks are fine. QR codes use this LAN URL.</p>
        <div class="form-grid">
          <label>
            Local PC URL
            <input id="desktop-local-url" type="text" readonly>
          </label>
          <label>
            LAN access URL
            <input id="desktop-network-url" type="text" readonly>
          </label>
          <label>
            Selected host IP
            <select id="desktop-lan-ip"></select>
          </label>
          <label>
            QR/base URL
            <input id="desktop-base-url" type="text" placeholder="http://192.168.1.50:8787">
          </label>
        </div>
        <div class="cloud-actions">
          <button id="desktop-save-network-btn" type="button">Save LAN URL</button>
          <button id="desktop-network-test-btn" type="button" class="secondary">Test LAN URL</button>
        </div>
        <pre id="desktop-network-status" class="cloud-status">Loading desktop network status...</pre>
      </section>

      <section class="info-panel" id="desktop-admin-access-panel">
        <h2>Admin Access</h2>
        <p class="caption">Unlock advanced settings with a PIN. If no PIN exists yet, create one here or in first-run setup.</p>
        <div class="form-grid">
          <label>
            Current PIN
            <input id="desktop-admin-pin" type="password" placeholder="Enter PIN">
          </label>
          <label>
            New PIN
            <input id="desktop-admin-new-pin" type="password" placeholder="Optional: change PIN">
          </label>
          <label>
            Confirm new PIN
            <input id="desktop-admin-new-pin-confirm" type="password" placeholder="Repeat new PIN">
          </label>
        </div>
        <div class="cloud-actions">
          <button id="desktop-admin-action-btn" type="button">Unlock Admin Settings</button>
          <button id="desktop-admin-lock-btn" type="button" class="secondary">Lock Admin Settings</button>
        </div>
        <pre id="desktop-admin-status" class="cloud-status">Admin settings are locked.</pre>
      </section>

      <section class="info-panel desktop-admin-shell" id="desktop-admin-shell"{admin_shell_hidden}>
        <h2>Desktop Auto Run</h2>
        <div class="form-grid">
          <label class="span-2">
            <input id="desktop-auto-toggle" type="checkbox">
            Auto run and crash restart
          </label>
        </div>
        <div class="cloud-actions">
          <button id="desktop-apply-auto-btn" type="button">Apply Auto Run</button>
          <button id="desktop-restart-btn" type="button" class="secondary">Restart App</button>
          <button id="desktop-stop-btn" type="button" class="secondary">Stop App</button>
          <button id="desktop-stop-disable-btn" type="button" class="secondary">Stop App And Disable Auto Run</button>
        </div>
        <pre id="desktop-run-status" class="cloud-status">Loading auto run status...</pre>

        <h2 id="desktop-import-panel">Import Inventory Folder</h2>
        <p class="caption">Point this at a folder like <code>C:\\Users\\TALLEY\\Desktop\\old inventory</code>. Any folder containing <code>inventory.csv</code> works.</p>
        <div class="form-grid">
          <label class="span-2">
            Inventory folder path
            <input id="desktop-sd-path" type="text" placeholder="C:\\Users\\TALLEY\\Desktop\\old inventory">
          </label>
          <label>
            Import mode
            <select id="desktop-sd-mode">
              <option value="merge">Merge into current inventory</option>
              <option value="backup_replace">Backup current data then replace</option>
              <option value="replace">Replace current inventory</option>
            </select>
          </label>
        </div>
        <div class="desktop-folder-tools">
          <input id="desktop-folder-picker" type="file" webkitdirectory directory multiple hidden>
          <button id="desktop-folder-picker-btn" type="button" class="secondary">Choose Folder...</button>
          <button id="desktop-folder-clear-btn" type="button" class="secondary" hidden>Clear Selected Folder</button>
        </div>
        <div id="desktop-folder-dropzone" class="desktop-dropzone" tabindex="0" role="button" aria-label="Drop an inventory folder here">
          Drop an inventory folder here, or choose one above.
        </div>
        <pre id="desktop-folder-summary" class="cloud-status">No folder selected.</pre>
        <div class="cloud-actions">
          <button id="desktop-backup-btn" type="button">Backup Current Data</button>
          <input id="desktop-backup-file" type="file" accept=".zip">
          <button id="desktop-import-backup-btn" type="button" class="secondary">Import Backup ZIP</button>
          <button id="desktop-use-old-inventory-btn" type="button" class="secondary">Use Desktop old inventory</button>
          <button id="desktop-preview-sd-btn" type="button" class="secondary">Preview Folder Import</button>
          <button id="desktop-import-sd-btn" type="button" class="secondary">Import Inventory Folder</button>
        </div>
        <pre id="desktop-import-status" class="cloud-status">Import ready.</pre>
      </section>
'''
        html = html.replace('      <h2>Storage And Branding</h2>', '      <div id="desktop-cloud-admin-shell" class="desktop-admin-shell" hidden>\n      <h2>Storage And Branding</h2>')
        html = html.replace('      <h2>Activity Log</h2>', '      </div>\n      <h2>Activity Log</h2>')
        marker = '      <div class="cloud-actions">\n        <button id="save-cloud-btn" type="button">Save Storage And Branding Settings</button>'
        if marker in html and "desktop-health-panel" not in html:
            html = html.replace(marker, panel + "\n" + marker)
        script = r'''
<script>
(function(){
  const $ = (id) => document.getElementById(id);
  const useOldInventoryBtn = $('desktop-use-old-inventory-btn');
  const folderPickerBtn = $('desktop-folder-picker-btn');
  const folderPickerInput = $('desktop-folder-picker');
  const folderDropzone = $('desktop-folder-dropzone');
  const folderClearBtn = $('desktop-folder-clear-btn');
  let importSuggestions = [];
  let stagedImportToken = '';
  let stagedImportLabel = '';
  let stagedImportPreview = null;
  async function json(url, options) {
    const response = await fetch(url, options || {});
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || data.message || `Request failed (${response.status})`);
    return data;
  }}
  async function post(url, body) {
    return json(url, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body || {})});
  }
  function setHidden(id, hidden) {
    const node = $(id);
    if (!node) return;
    node.hidden = hidden;
  }
  function setCardState(cardId, stateId, detailId, ok, stateText, detailText) {
    const card = $(cardId);
    const state = $(stateId);
    const detail = $(detailId);
    if (card) card.className = `desktop-health-card ${ok ? 'ok' : 'bad'}`;
    if (state) state.textContent = stateText || '';
    if (detail) detail.textContent = detailText || '';
  }
  function networkLines(s) {
    return [
      `Bind address: ${s.bind_address}`,
      `Port: ${s.port}`,
      `Detected LAN IPs: ${(s.detected_lan_ips || []).join(', ') || 'none'}`,
      `Selected LAN IP/base URL: ${s.selected_lan_ip || ''} / ${s.configured_network_base_url || ''}`,
      `Windows Firewall rule: ${s.firewall_rule_installed ? 'installed' : 'missing'} (${s.firewall_rule && s.firewall_rule.detail || ''})`,
      `Listening on 0.0.0.0: ${s.server_listening_all_interfaces ? 'yes' : 'no'}`,
      `Another device should open: ${s.network_url}`,
      s.qr_loopback_warning ? 'Warning: QR codes point to localhost. Phones will not be able to open them.' : '',
      '',
      'LAN / Ethernet checks: same subnet or routed access, firewall allows TCP 8787.'
    ].filter(Boolean).join('\n');
  }
  function applyHealth(h) {
    if ($('desktop-health-status')) {
      $('desktop-health-status').textContent = JSON.stringify(h || {}, null, 2);
    }
    const health = h || {};
    const overallOk = Boolean(health.overall_ok);
    const lanOk = Boolean((health.checks || {}).lan_url && (health.checks || {}).lan_url.ok);
    const firewallOk = Boolean((health.checks || {}).firewall_rule && (health.checks || {}).firewall_rule.installed);
    const taskOk = Boolean((health.checks || {}).scheduled_task && (health.checks || {}).scheduled_task.exists);
    setCardState('desktop-health-overall-card', 'desktop-health-overall-state', 'desktop-health-overall-detail', overallOk, overallOk ? 'Green' : 'Red', overallOk ? 'LAN and startup are ready.' : 'One or more checks need attention.');
    setCardState('desktop-health-lan-card', 'desktop-health-lan-state', 'desktop-health-lan-detail', lanOk, lanOk ? 'Green' : 'Red', lanOk ? 'LAN URL is reachable from this PC.' : 'LAN URL is not reachable yet.');
    setCardState('desktop-health-firewall-card', 'desktop-health-firewall-state', 'desktop-health-firewall-detail', firewallOk && taskOk, (firewallOk && taskOk) ? 'Green' : 'Red', [firewallOk ? 'Firewall ok' : 'Firewall missing', taskOk ? 'startup task ok' : 'startup task missing'].join(' / '));
  }
  function applyStatus(s) {
    if ($('desktop-local-url')) $('desktop-local-url').value = s.local_pc_url || '';
    if ($('desktop-network-url')) $('desktop-network-url').value = s.network_url || '';
    if ($('desktop-base-url')) $('desktop-base-url').value = s.configured_network_base_url || '';
    if ($('desktop-lan-ip')) {
      $('desktop-lan-ip').innerHTML = (s.detected_lan_ips || []).map((ip) => `<option value="${ip}" ${ip === s.selected_lan_ip ? 'selected' : ''}>${ip}</option>`).join('');
    }
    if ($('desktop-auto-toggle')) $('desktop-auto-toggle').checked = Boolean(s.auto_run_enabled);
    if ($('desktop-network-status')) $('desktop-network-status').textContent = networkLines(s);
    if ($('desktop-run-status')) {
      $('desktop-run-status').textContent = [
        `Auto run: ${s.auto_run_enabled ? 'Enabled' : 'Disabled'}`,
        `Run mode: ${s.run_mode}`,
        `Supervisor active: ${s.supervisor_active ? 'yes' : 'no'}`
      ].join('\n');
    }
    const setupRequired = Boolean(s.setup_required);
    const adminUnlocked = Boolean(s.admin_unlocked);
    const pinConfigured = Boolean(s.admin_pin_configured);
    setHidden('desktop-setup-wizard', !setupRequired && !window.location.pathname.endsWith('/setup'));
    if ($('desktop-admin-action-btn')) {
      $('desktop-admin-action-btn').textContent = pinConfigured ? 'Unlock Admin Settings' : 'Create Admin PIN';
    }
    if ($('desktop-admin-status')) {
      $('desktop-admin-status').textContent = pinConfigured
        ? (adminUnlocked ? 'Admin settings unlocked.' : 'Admin settings are locked.')
        : 'No admin PIN exists yet. Create one to lock advanced settings.';
    }
    setHidden('desktop-admin-shell', !adminUnlocked);
    if ($('desktop-lan-ip')) $('desktop-lan-ip').disabled = !adminUnlocked;
    if ($('desktop-base-url')) $('desktop-base-url').readOnly = !adminUnlocked;
    if ($('desktop-save-network-btn')) $('desktop-save-network-btn').hidden = !adminUnlocked;
    setHidden('desktop-cloud-admin-shell', !adminUnlocked);
  }
  async function refreshStatus() {
    const s = await json('/api/status');
    applyStatus(s);
    return s;
  }
  async function refreshHealth() {
    const s = await json('/api/desktop/health');
    applyHealth(s);
    return s;
  }
  async function loadDesktopStatus() {
    if (!$('desktop-health-panel')) return;
    await refreshStatus();
    await refreshHealth();
  }
  async function saveNetwork() {
    await post('/api/desktop/settings', {selected_lan_ip: $('desktop-lan-ip').value, configured_network_base_url: $('desktop-base-url').value});
    await refreshStatus();
  }
  async function copyLanUrl() {
    const s = await refreshStatus();
    const value = s.network_url || $('desktop-network-url').value || '';
    if (navigator.clipboard && value) {
      await navigator.clipboard.writeText(value);
    }
  }
  async function adminAction() {
    const pin = $('desktop-admin-pin') ? $('desktop-admin-pin').value.trim() : '';
    const newPin = $('desktop-admin-new-pin') ? $('desktop-admin-new-pin').value.trim() : '';
    const confirmPin = $('desktop-admin-new-pin-confirm') ? $('desktop-admin-new-pin-confirm').value.trim() : '';
    const action = newPin ? 'set_pin' : 'unlock';
    const payload = {action, pin, new_pin: newPin, confirm_pin: confirmPin};
    const result = await post('/api/desktop/admin', payload);
    if ($('desktop-admin-status')) $('desktop-admin-status').textContent = result.message || JSON.stringify(result, null, 2);
    if ($('desktop-admin-pin')) $('desktop-admin-pin').value = '';
    if ($('desktop-admin-new-pin')) $('desktop-admin-new-pin').value = '';
    if ($('desktop-admin-new-pin-confirm')) $('desktop-admin-new-pin-confirm').value = '';
    await refreshStatus();
  }
  async function lockAdmin() {
    const result = await post('/api/desktop/admin', {action: 'lock'});
    if ($('desktop-admin-status')) $('desktop-admin-status').textContent = result.message || 'Admin settings locked.';
    await refreshStatus();
  }
  async function finishSetup() {
    const result = await post('/api/desktop/setup', {
      selected_lan_ip: $('desktop-setup-lan-ip').value,
      configured_network_base_url: $('desktop-setup-base-url').value,
      admin_pin: $('desktop-setup-pin').value,
      admin_pin_confirm: $('desktop-setup-pin-confirm').value
    });
    if ($('desktop-setup-status')) $('desktop-setup-status').textContent = result.message || JSON.stringify(result, null, 2);
    $('desktop-setup-pin').value = '';
    $('desktop-setup-pin-confirm').value = '';
    await refreshStatus();
  }
  async function system(action) {
    try {
      const result = await post('/api/desktop/system', {action});
      $('desktop-run-status').textContent = result.message || JSON.stringify(result, null, 2);
      if (!action.includes('stop')) setTimeout(refreshStatus, 600);
    } catch (error) {
      $('desktop-run-status').textContent = error.message;
    }
  }
  async function applyAutoRun() {
    try {
      const result = await post('/api/desktop/system', {action: 'set_auto', enabled: $('desktop-auto-toggle').checked});
      $('desktop-run-status').textContent = result.message || JSON.stringify(result, null, 2);
      setTimeout(refreshStatus, 600);
    } catch (error) {
      $('desktop-run-status').textContent = error.message;
      setTimeout(refreshStatus, 600);
    }
  }
  async function previewSd() {
    if (stagedImportToken) {
      const preview = await previewStagedFolder();
      $('desktop-import-status').textContent = JSON.stringify(preview, null, 2);
      return;
    }
    $('desktop-import-status').textContent = JSON.stringify(await json(`/api/desktop/sd/preview?path=${encodeURIComponent($('desktop-sd-path').value)}`), null, 2);
  }
  async function importSd() {
    if (stagedImportToken) {
      const result = await post('/api/desktop/sd/import', {token: stagedImportToken, mode: $('desktop-sd-mode').value});
      clearStagedFolder();
      $('desktop-import-status').textContent = JSON.stringify(result, null, 2);
      await refreshStatus();
      return;
    }
    $('desktop-import-status').textContent = JSON.stringify(await post('/api/desktop/sd/import', {path: $('desktop-sd-path').value, mode: $('desktop-sd-mode').value}), null, 2);
  }
  async function backup() {
    const result = await post('/api/desktop/backup', {});
    $('desktop-import-status').textContent = JSON.stringify(result, null, 2);
    if (result.backup) window.location.href = `/api/desktop/backup/download?name=${encodeURIComponent(result.backup)}`;
  }
  async function importBackup() {
    const file = $('desktop-backup-file').files[0];
    if (!file) {
      $('desktop-import-status').textContent = 'Choose a backup ZIP first.';
      return;
    }
    const form = new FormData();
    form.append('backup', file);
    const response = await fetch('/api/desktop/backup/import', {method:'POST', body: form});
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || `Import failed (${response.status})`);
    $('desktop-import-status').textContent = JSON.stringify(data, null, 2);
  }
  function folderSummaryText() {
    if (!stagedImportToken) {
      return 'No folder selected.';
    }
    const preview = stagedImportPreview || {};
    const label = stagedImportLabel || preview.source_label || 'Selected folder';
    return [
      `Folder staged: ${label}`,
      `Token: ${stagedImportToken}`,
      `Items found: ${preview.inventory_items_found ?? 0}`,
      `Orders found: ${preview.orders_found ?? 0}`,
      `Images found: ${preview.images_found ?? 0}`,
      `Transactions found: ${preview.transaction_rows_found ?? 0}`,
      `Device log rows found: ${preview.device_log_rows_found ?? 0}`,
      `Time log rows found: ${preview.time_log_rows_found ?? 0}`,
      '',
      'Use Preview Folder Import to review the staged folder, then Import Inventory Folder to bring it in.'
    ].join('\n');
  }
  function refreshFolderSummary() {
    if ($('desktop-folder-summary')) {
      $('desktop-folder-summary').textContent = folderSummaryText();
    }
    if (folderClearBtn) {
      folderClearBtn.hidden = !stagedImportToken;
    }
  }
  function clearStagedFolder() {
    stagedImportToken = '';
    stagedImportLabel = '';
    stagedImportPreview = null;
    if (folderPickerInput) {
      folderPickerInput.value = '';
    }
    if (folderDropzone) {
      folderDropzone.classList.remove('dragging');
    }
    refreshFolderSummary();
  }
  function guessFolderLabel(entries) {
    const first = entries.length ? String(entries[0].relativePath || entries[0].file && entries[0].file.webkitRelativePath || entries[0].file && entries[0].file.name || '') : '';
    if (!first) return 'Selected folder';
    const parts = first.split(/[\\/]/).filter(Boolean);
    return parts.length > 1 ? parts[0] : 'Selected folder';
  }
  function collectPickerEntries() {
    if (!folderPickerInput || !folderPickerInput.files) return [];
    return Array.from(folderPickerInput.files).map((file) => ({
      file,
      relativePath: file.webkitRelativePath || file.name,
    }));
  }
  async function readDirectoryEntry(entry, prefix, results) {
    if (entry.isFile) {
      const file = await new Promise((resolve, reject) => entry.file(resolve, reject));
      results.push({file, relativePath: `${prefix}${file.name}`});
      return;
    }
    if (!entry.isDirectory) {
      return;
    }
    const reader = entry.createReader();
    while (true) {
      const children = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
      if (!children.length) {
        break;
      }
      for (const child of children) {
        await readDirectoryEntry(child, `${prefix}${entry.name}/`, results);
      }
    }
  }
  async function collectDroppedEntries(dataTransfer) {
    const results = [];
    const items = Array.from((dataTransfer && dataTransfer.items) || []);
    if (items.length) {
      for (const item of items) {
        const entry = item.webkitGetAsEntry ? item.webkitGetAsEntry() : null;
        if (entry) {
          await readDirectoryEntry(entry, '', results);
          continue;
        }
        const file = item.getAsFile ? item.getAsFile() : null;
        if (file) {
          results.push({file, relativePath: file.webkitRelativePath || file.name});
        }
      }
    }
    if (!results.length && dataTransfer && dataTransfer.files) {
      Array.from(dataTransfer.files).forEach((file) => {
        results.push({file, relativePath: file.webkitRelativePath || file.name});
      });
    }
    return results;
  }
  async function stageFolderEntries(entries, sourceLabel) {
    if (!entries.length) {
      throw new Error('No folder files were selected.');
    }
    const form = new FormData();
    form.append('source_label', sourceLabel || 'Selected folder');
    entries.forEach((entry) => {
      form.append('files', entry.file, entry.relativePath || entry.file.name);
    });
    const response = await fetch('/api/desktop/import/upload', {method:'POST', body: form});
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || `Folder upload failed (${response.status})`);
    stagedImportToken = data.token || '';
    stagedImportLabel = data.source_label || sourceLabel || 'Selected folder';
    stagedImportPreview = data.preview || null;
    if ($('desktop-import-status')) {
      $('desktop-import-status').textContent = `Folder staged: ${stagedImportLabel}. Preview and import controls now use the staged upload.`;
    }
    refreshFolderSummary();
    return data;
  }
  async function stageSelectedFolder(entries, label) {
    const data = await stageFolderEntries(entries, label);
    if ($('desktop-folder-summary')) {
      $('desktop-folder-summary').textContent = JSON.stringify(data.preview || data, null, 2);
    }
    return data;
  }
  async function previewStagedFolder() {
    if (!stagedImportToken) {
      return null;
    }
    const preview = await post('/api/desktop/import/staged/preview', {token: stagedImportToken});
    stagedImportPreview = preview;
    refreshFolderSummary();
    return preview;
  }
  async function loadImportSuggestions() {
    const response = await json('/api/desktop/import/suggestions');
    const suggestions = Array.isArray(response.suggestions) ? response.suggestions : [];
    importSuggestions = suggestions;
    if ($('desktop-sd-path')) {
      const current = $('desktop-sd-path').value.trim();
      if (!current) {
        $('desktop-sd-path').value = (suggestions[0] && suggestions[0].path) || response.default_path || '';
      }
    }
    if (useOldInventoryBtn) {
      useOldInventoryBtn.hidden = suggestions.length === 0;
      useOldInventoryBtn.textContent = suggestions.length ? `Use ${suggestions[0].label}` : 'Use Detected Desktop Folder';
    }
    if ($('desktop-import-status') && suggestions.length && $('desktop-import-status').textContent === 'Import ready.') {
      $('desktop-import-status').textContent = `Detected import folder: ${suggestions[0].label}`;
    }
    return suggestions;
  }
  async function handleFolderSelection() {
    const entries = collectPickerEntries();
    const label = guessFolderLabel(entries);
    await stageSelectedFolder(entries, label);
  }
  async function handleFolderDrop(event) {
    event.preventDefault();
    if (folderDropzone) {
      folderDropzone.classList.remove('dragging');
    }
    const entries = await collectDroppedEntries(event.dataTransfer);
    const label = guessFolderLabel(entries);
    await stageSelectedFolder(entries, label);
  }
  document.addEventListener('DOMContentLoaded', () => {
    if (!$('desktop-health-panel')) return;
    $('desktop-save-network-btn').addEventListener('click', saveNetwork);
    $('desktop-copy-lan-btn').addEventListener('click', () => copyLanUrl().catch((error) => $('desktop-health-status').textContent = error.message));
    $('desktop-health-test-btn').addEventListener('click', async () => $('desktop-health-status').textContent = JSON.stringify(await refreshHealth(), null, 2));
    $('desktop-network-test-btn').addEventListener('click', async () => $('desktop-network-status').textContent = JSON.stringify(await json('/api/desktop/network-test'), null, 2));
    $('desktop-admin-action-btn').addEventListener('click', () => adminAction().catch((error) => $('desktop-admin-status').textContent = error.message));
    $('desktop-admin-lock-btn').addEventListener('click', () => lockAdmin().catch((error) => $('desktop-admin-status').textContent = error.message));
    $('desktop-setup-save-btn').addEventListener('click', () => finishSetup().catch((error) => $('desktop-setup-status').textContent = error.message));
    $('desktop-setup-health-btn').addEventListener('click', async () => $('desktop-setup-status').textContent = JSON.stringify(await refreshHealth(), null, 2));
    $('desktop-apply-auto-btn').addEventListener('click', applyAutoRun);
    $('desktop-auto-toggle').addEventListener('change', applyAutoRun);
    $('desktop-restart-btn').addEventListener('click', () => system('restart'));
    $('desktop-stop-btn').addEventListener('click', () => system('stop'));
    $('desktop-stop-disable-btn').addEventListener('click', () => system('stop_disable_auto'));
    if (folderPickerBtn && folderPickerInput) {
      folderPickerBtn.addEventListener('click', () => folderPickerInput.click());
      folderPickerInput.addEventListener('change', () => handleFolderSelection().catch((error) => $('desktop-import-status').textContent = error.message));
    }
    if (folderClearBtn) {
      folderClearBtn.addEventListener('click', () => {
        clearStagedFolder();
        $('desktop-import-status').textContent = 'Folder selection cleared.';
      });
    }
    if (folderDropzone) {
      folderDropzone.addEventListener('dragover', (event) => {
        event.preventDefault();
        folderDropzone.classList.add('dragging');
      });
      folderDropzone.addEventListener('dragleave', () => {
        folderDropzone.classList.remove('dragging');
      });
      folderDropzone.addEventListener('drop', (event) => handleFolderDrop(event).catch((error) => $('desktop-import-status').textContent = error.message));
      folderDropzone.addEventListener('click', () => folderPickerInput && folderPickerInput.click());
      folderDropzone.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault();
          folderPickerInput && folderPickerInput.click();
        }
      });
    }
    if (useOldInventoryBtn) {
      useOldInventoryBtn.addEventListener('click', async () => {
        try {
          const suggestions = importSuggestions.length ? importSuggestions : await loadImportSuggestions();
          if (suggestions.length && $('desktop-sd-path')) {
            $('desktop-sd-path').value = suggestions[0].path || '';
            $('desktop-import-status').textContent = `Using ${suggestions[0].label}. Preview the folder before importing.`;
          } else {
            $('desktop-import-status').textContent = 'No likely inventory folder was detected on this PC.';
          }
        } catch (error) {
          $('desktop-import-status').textContent = error.message;
        }
      });
    }
    $('desktop-preview-sd-btn').addEventListener('click', previewSd);
    $('desktop-import-sd-btn').addEventListener('click', importSd);
    $('desktop-backup-btn').addEventListener('click', backup);
    $('desktop-import-backup-btn').addEventListener('click', () => importBackup().catch((e) => $('desktop-import-status').textContent = e.message));
    loadImportSuggestions().catch((error) => {
      if (useOldInventoryBtn) useOldInventoryBtn.hidden = true;
      if ($('desktop-import-status') && $('desktop-import-status').textContent === 'Import ready.') {
        $('desktop-import-status').textContent = error.message;
      }
    });
    refreshFolderSummary();
    refreshStatus();
  });
})();
</script>
'''
        html = html.replace("</body>", script + "\n</body>")
        return html

    def desktop_item_html() -> str:
        html = store.item_html
        panel = r'''
    <section class="info-panel" id="desktop-edit-panel">
      <h2>Edit Item</h2>
      <div class="meta-grid">
        <label>Part number<input id="desktop-edit-id" type="text"></label>
        <label>Name<input id="desktop-edit-name" type="text"></label>
        <label>Category<input id="desktop-edit-category" type="text"></label>
        <label>Quantity<input id="desktop-edit-qty" type="number" min="0"></label>
        <label>QR/UPC<input id="desktop-edit-qr" type="text"></label>
        <label>Color<input id="desktop-edit-color" type="text"></label>
        <label>Material<input id="desktop-edit-material" type="text"></label>
        <label>Parent product / kit<input id="desktop-edit-bom-product" type="text"></label>
        <label>Qty used in parent<input id="desktop-edit-bom-qty" type="number" min="0"></label>
        <label>Image<input id="desktop-edit-image" type="file" accept="image/*"></label>
      </div>
      <div class="actions">
        <button id="desktop-save-item-btn" type="button">Save Item</button>
        <button id="desktop-upload-image-btn" type="button" class="secondary">Upload/Replace Image</button>
        <button id="desktop-remove-image-btn" type="button" class="secondary">Remove Image</button>
      </div>
      <div id="desktop-edit-status" class="status"></div>
    </section>
'''
        marker = '    <section class="manual-panel">'
        if marker in html and "desktop-edit-panel" not in html:
            html = html.replace(marker, panel + "\n" + marker)
        script = r'''
<script>
(function(){
  const $ = (id) => document.getElementById(id);
  const params = new URLSearchParams(window.location.search);
  let currentId = params.get('id') || '';
  let currentItem = null;
  async function json(url, options) {
    const response = await fetch(url, options || {});
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || `Request failed (${response.status})`);
    return data;
  }
  function status(text, bad) {
    const el = $('desktop-edit-status');
    if (el) { el.textContent = text || ''; el.className = bad ? 'status error' : 'status'; }
  }
  async function loadEdit() {
    if (!$('desktop-edit-panel') || !currentId) return;
    const payload = await json(`/api/item?id=${encodeURIComponent(currentId)}`);
    currentItem = payload.item;
    $('desktop-edit-id').value = currentItem.id || '';
    $('desktop-edit-name').value = currentItem.part_name || '';
    $('desktop-edit-category').value = currentItem.category || '';
    $('desktop-edit-qty').value = currentItem.qty || 0;
    $('desktop-edit-qr').value = currentItem.qr_code || '';
    $('desktop-edit-color').value = currentItem.color || '';
    $('desktop-edit-material').value = currentItem.material || '';
    $('desktop-edit-bom-product').value = currentItem.bom_product || '';
    $('desktop-edit-bom-qty').value = currentItem.bom_qty || 0;
  }
  async function saveItem() {
    const body = {
      original_id: currentId,
      id: $('desktop-edit-id').value,
      part_name: $('desktop-edit-name').value,
      category: $('desktop-edit-category').value,
      qty: $('desktop-edit-qty').value,
      qr_code: $('desktop-edit-qr').value,
      color: $('desktop-edit-color').value,
      material: $('desktop-edit-material').value,
      bom_product: $('desktop-edit-bom-product').value,
      bom_qty: $('desktop-edit-bom-qty').value,
      image_ref: currentItem && currentItem.image_ref || ''
    };
    const saved = await json('/api/items/update', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
    currentId = saved.item.id;
    history.replaceState(null, '', `/item?id=${encodeURIComponent(currentId)}`);
    status('Item saved.');
    setTimeout(() => window.location.reload(), 500);
  }
  async function uploadImage() {
    const file = $('desktop-edit-image').files[0];
    if (!file) return status('Choose an image first.', true);
    const form = new FormData();
    form.append('id', currentId);
    form.append('image', file);
    await json('/api/items/image', {method:'POST', body: form});
    status('Image saved.');
    setTimeout(() => window.location.reload(), 500);
  }
  async function removeImage() {
    await json('/api/items/image', {method:'DELETE', headers:{'Content-Type':'application/json'}, body: JSON.stringify({id: currentId})});
    status('Image removed.');
    setTimeout(() => window.location.reload(), 500);
  }
  document.addEventListener('DOMContentLoaded', () => {
    if (!$('desktop-edit-panel')) return;
    $('desktop-save-item-btn').addEventListener('click', () => saveItem().catch((e) => status(e.message, true)));
    $('desktop-upload-image-btn').addEventListener('click', () => uploadImage().catch((e) => status(e.message, true)));
    $('desktop-remove-image-btn').addEventListener('click', () => removeImage().catch((e) => status(e.message, true)));
    loadEdit().catch((e) => status(e.message, true));
  });
})();
</script>
'''
        return html.replace("</body>", script + "\n</body>") if "loadEdit()" not in html else html

    def desktop_inventory_html() -> str:
        html = store.index_html
        if 'id="desktop-import-link"' not in html and 'href="/settings"' in html:
            replacements = [
                (
                    '<a id="settings-nav-link" class="nav-link" href="/settings">Settings</a>',
                    '<a id="settings-nav-link" class="nav-link" href="/settings">Settings</a> <button id="desktop-import-link" type="button" class="nav-link">Import</button>',
                ),
                (
                    '<a href="/settings">Settings</a>',
                    '<a href="/settings">Settings</a> <button id="desktop-import-link" type="button" class="nav-link">Import</button>',
                ),
            ]
            for old, new in replacements:
                if old in html:
                    html = html.replace(old, new, 1)
                    break
        script = r'''
<script>
(function(){
  async function exactOpen(value) {
    const q = String(value || '').trim();
    if (!q) return;
    const response = await fetch(`/api/items?q=${encodeURIComponent(q)}`);
    const data = await response.json().catch(() => ({items: []}));
    const items = Array.isArray(data.items) ? data.items : [];
    const hit = items.find((item) => [item.id, item.qr_code, item.qr_link].some((v) => String(v || '').toLowerCase() === q.toLowerCase()));
    if (hit) window.location.href = `/item?id=${encodeURIComponent(hit.id)}`;
  }
  document.addEventListener('DOMContentLoaded', () => {
    const importLink = document.getElementById('desktop-import-link');
    if (importLink) {
      importLink.addEventListener('click', () => {
        window.location.href = '/settings#desktop-import-panel';
      });
    }
    const search = document.getElementById('search-input');
    if (!search) return;
    search.focus();
    search.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') exactOpen(search.value);
    });
  });
})();
</script>
'''
        return html.replace("</body>", script + "\n</body>") if "exactOpen(value)" not in html else html

    def labels_html() -> str:
        return """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Stingray QR Labels</title><style>
body{font-family:Segoe UI,Arial,sans-serif;margin:16px;background:#f7f7f7;color:#182230}button,select,input{padding:8px;margin:4px}.toolbar{margin-bottom:12px}.labels{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:8px}.label{background:white;border:1px dashed #777;padding:8px;text-align:center;break-inside:avoid}.label img{width:116px;height:116px}.part{font-weight:700}.name{font-size:13px}@media print{.toolbar{display:none}body{background:white}.label{page-break-inside:avoid}}</style></head>
<body><div class="toolbar"><a href="/">Inventory</a> <a href="/settings">Settings</a><br><select id="category"><option value="all">All</option><option value="part">Parts</option><option value="product">Products</option><option value="kit">Kits</option></select><input id="search" placeholder="Search labels"><button id="print">Print</button></div><div id="labels" class="labels"></div>
<script>
function esc(v){return String(v||'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function load(){const q=document.getElementById('search').value||'';const cat=document.getElementById('category').value;const r=await fetch(`/api/items?category=${encodeURIComponent(cat)}&q=${encodeURIComponent(q)}`);const d=await r.json();document.getElementById('labels').innerHTML=(d.items||[]).map(i=>`<div class="label"><img src="/api/qr.svg?data=${encodeURIComponent(i.qr_link)}"><div class="part">${esc(i.id)}</div><div class="name">${esc(i.part_name)}</div></div>`).join('')}
document.getElementById('category').addEventListener('change',load);document.getElementById('search').addEventListener('input',load);document.getElementById('print').addEventListener('click',()=>window.print());load();
</script></body></html>"""

    @app.route("/", methods=["GET"])
    @app.route("/settings", methods=["GET"])
    @app.route("/setup", methods=["GET"])
    @app.route("/orders", methods=["GET"])
    @app.route("/orders/view", methods=["GET"])
    @app.route("/orders/fulfill", methods=["GET"])
    def handle_index_page():
        if request.path == "/setup":
            return Response(desktop_settings_html(setup_mode=True), mimetype="text/html; charset=utf-8")
        if request.path == "/settings":
            return Response(desktop_settings_html(setup_mode=not store.app_config.setup_complete), mimetype="text/html; charset=utf-8")
        if request.path == "/":
            return Response(desktop_inventory_html(), mimetype="text/html; charset=utf-8")
        return Response(store.index_html, mimetype="text/html; charset=utf-8")

    @app.route("/item", methods=["GET"])
    def handle_item_page():
        return Response(desktop_item_html(), mimetype="text/html; charset=utf-8")

    @app.route("/labels", methods=["GET"])
    def handle_labels_page():
        return Response(labels_html(), mimetype="text/html; charset=utf-8")

    @app.route("/qr.svg", methods=["GET"])
    @app.route("/api/qr.svg", methods=["GET"])
    def handle_qr_svg():
        value = trim_copy(arg("data"))
        if not value:
            return Response("Missing QR data.", status=400, mimetype="text/plain; charset=utf-8")
        if len(value) > 150:
            return Response("QR data is too long.", status=413, mimetype="text/plain; charset=utf-8")
        qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=10, border=4)
        qr.add_data(value)
        qr.make(fit=True)
        image = qr.make_image(image_factory=qrcode.image.svg.SvgPathImage)
        svg = image.to_string(encoding="unicode")
        response = Response(svg, mimetype="image/svg+xml; charset=utf-8")
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.route("/api/status", methods=["GET"])
    def handle_status():
        return jsonify(store.status_json(base_url()))

    @app.route("/api/desktop/health", methods=["GET"])
    def handle_desktop_health():
        return jsonify(store.health_json(base_url()))

    @app.route("/api/desktop/settings", methods=["POST"])
    def handle_desktop_settings():
        if store.app_config.setup_complete and not store.admin_unlocked():
            return json_error(403, "Admin PIN required.")
        selected_ip = sanitize_field(arg("selected_lan_ip", store.app_config.selected_lan_ip))
        configured_base_url = normalize_base_url(arg("configured_network_base_url", store.app_config.configured_network_base_url))
        with store.lock:
            if selected_ip:
                store.app_config.selected_lan_ip = selected_ip
            if configured_base_url:
                store.app_config.configured_network_base_url = configured_base_url
            else:
                store.app_config.configured_network_base_url = f"http://{store.app_config.selected_lan_ip}:{store.app_config.port}"
            store._save_app_config()
            store.append_device_log("desktop_settings_saved", f"LAN URL set to {store.configured_base_url()}")
        return jsonify(store.status_json(base_url()))

    @app.route("/api/desktop/network-test", methods=["GET"])
    def handle_desktop_network_test():
        return jsonify(store.health_json(base_url()))

    @app.route("/api/desktop/setup", methods=["POST"])
    def handle_desktop_setup():
        if store.app_config.setup_complete and not store.admin_unlocked():
            return json_error(403, "Admin PIN required.")
        selected_ip = sanitize_field(arg("selected_lan_ip", store.app_config.selected_lan_ip))
        configured_base_url = normalize_base_url(arg("configured_network_base_url", store.app_config.configured_network_base_url))
        admin_pin = trim_copy(arg("admin_pin"))
        admin_pin_confirm = trim_copy(arg("admin_pin_confirm"))
        if not store.admin_pin_is_configured() and not admin_pin and not admin_pin_confirm:
            return json_error(400, "Create an admin PIN to complete first-run setup.")
        with store.lock:
            if selected_ip:
                store.app_config.selected_lan_ip = selected_ip
            if configured_base_url:
                store.app_config.configured_network_base_url = configured_base_url
            else:
                store.app_config.configured_network_base_url = f"http://{store.app_config.selected_lan_ip}:{store.app_config.port}"
            if admin_pin or admin_pin_confirm:
                if admin_pin != admin_pin_confirm:
                    return json_error(400, "Admin PIN entries do not match.")
                try:
                    store.save_admin_pin(admin_pin)
                except ValueError as exc:
                    return json_error(400, str(exc))
                store.set_admin_unlock(True)
            store.app_config.setup_complete = True
            set_app_config_and_session_secret_if_needed()
            store._save_app_config()
            store.append_device_log("desktop_setup_saved", f"LAN URL set to {store.configured_base_url()}")
        return jsonify({"ok": True, "message": "Setup saved. The LAN URL is now ready.", "status": store.status_json(base_url())})

    @app.route("/api/desktop/admin", methods=["POST"])
    def handle_desktop_admin():
        action = trim_copy(arg("action"))
        current_pin = read_pin_from_request("pin", "current_pin")
        new_pin = trim_copy(arg("new_pin"))
        confirm_pin = trim_copy(arg("confirm_pin"))

        if action == "lock":
            store.set_admin_unlock(False)
            return jsonify({"ok": True, "message": "Admin settings locked."})

        if action == "unlock":
            if not store.admin_pin_is_configured():
                if new_pin:
                    if new_pin != confirm_pin:
                        return json_error(400, "New PIN entries do not match.")
                    try:
                        store.save_admin_pin(new_pin)
                    except ValueError as exc:
                        return json_error(400, str(exc))
                    store.set_admin_unlock(True)
                    return jsonify({"ok": True, "message": "Admin PIN created and admin settings unlocked."})
                return json_error(400, "No admin PIN exists yet. Create one first.")
            if store.verify_admin_pin(current_pin):
                store.set_admin_unlock(True)
                return jsonify({"ok": True, "message": "Admin settings unlocked."})
            return json_error(403, "Incorrect admin PIN.")

        if action == "set_pin":
            if new_pin != confirm_pin:
                return json_error(400, "New PIN entries do not match.")
            if store.admin_pin_is_configured() and not store.admin_unlocked():
                if not store.verify_admin_pin(current_pin):
                    return json_error(403, "Current admin PIN is required to change the PIN.")
            try:
                store.save_admin_pin(new_pin)
            except ValueError as exc:
                return json_error(400, str(exc))
            store.set_admin_unlock(True)
            return jsonify({"ok": True, "message": "Admin PIN updated and settings unlocked."})

        return json_error(400, "Unknown admin action.")

    @app.route("/api/desktop/backup", methods=["POST"])
    def handle_desktop_backup():
        ok, response = require_admin_access()
        if not ok:
            return response
        backup = store.create_backup_zip("manual")
        return jsonify({"ok": True, "backup": backup.name, "path": str(backup)})

    @app.route("/api/desktop/backup/download", methods=["GET"])
    def handle_desktop_backup_download():
        name = re.sub(r"[^A-Za-z0-9_.-]", "", Path(arg("name")).name)
        path = store.backups_dir / name
        if not name or not path.exists() or not path.is_file() or path.suffix.lower() != ".zip":
            return json_error(404, "Backup not found.")
        return send_file(path, as_attachment=True, download_name=path.name)

    @app.route("/api/desktop/backup/import", methods=["POST"])
    def handle_desktop_backup_import():
        ok, response = require_admin_access()
        if not ok:
            return response
        if not request.files:
            return json_error(400, "No backup ZIP uploaded.")
        uploaded = next(iter(request.files.values()))
        if not uploaded or not uploaded.filename.lower().endswith(".zip"):
            return json_error(400, "Backup must be a ZIP file.")
        store.backups_dir.mkdir(parents=True, exist_ok=True)
        temp_path = store.backups_dir / f"uploaded-{int(time.time())}-{sanitize_filename_stem(uploaded.filename)}.zip"
        uploaded.save(temp_path)
        try:
            return jsonify(store.restore_backup_zip(temp_path))
        except zipfile.BadZipFile:
            return json_error(400, "Invalid backup ZIP.")

    @app.route("/api/desktop/sd/preview", methods=["GET"])
    def handle_desktop_sd_preview():
        folder = normalize_local_folder_path(arg("path"))
        if not folder.exists() or not folder.is_dir():
            return json_error(404, "SD folder not found.")
        try:
            return jsonify(store.preview_sd_import(folder))
        except ValueError as exc:
            return json_error(400, str(exc))

    @app.route("/api/desktop/import/suggestions", methods=["GET"])
    def handle_desktop_import_suggestions():
        return jsonify({
            "suggestions": store.inventory_import_suggestions(),
            "default_path": str(Path.home() / "Desktop" / "old inventory"),
        })

    @app.route("/api/desktop/import/upload", methods=["POST"])
    def handle_desktop_import_upload():
        ok, response = require_admin_access()
        if not ok:
            return response
        files = request.files.getlist("files")
        if not files:
            return json_error(400, "No folder files were uploaded.")
        source_label = sanitize_field(arg("source_label"))
        try:
            return jsonify(store.stage_import_uploads(files, source_label))
        except ValueError as exc:
            return json_error(400, str(exc))

    @app.route("/api/desktop/import/staged/preview", methods=["POST"])
    def handle_desktop_import_staged_preview():
        ok, response = require_admin_access()
        if not ok:
            return response
        token = trim_copy(arg("token"))
        try:
            return jsonify(store.staged_import_preview(token))
        except ValueError as exc:
            return json_error(400, str(exc))

    @app.route("/api/desktop/sd/import", methods=["POST"])
    def handle_desktop_sd_import():
        ok, response = require_admin_access()
        if not ok:
            return response
        mode = trim_copy(arg("mode", "merge"))
        if mode not in {"merge", "replace", "backup_replace"}:
            return json_error(400, "Invalid import mode.")
        token = trim_copy(arg("token"))
        if token:
            try:
                return jsonify(store.import_staged_upload(token, mode))
            except ValueError as exc:
                return json_error(400, str(exc))
        try:
            return jsonify(store.import_sd_folder(normalize_local_folder_path(arg("path")), mode))
        except ValueError as exc:
            return json_error(400, str(exc))

    @app.route("/api/desktop/system", methods=["POST"])
    def handle_desktop_system():
        action = trim_copy(arg("action"))
        if action in {"enable_auto", "disable_auto", "set_auto", "stop_disable_auto"}:
            ok, response = require_admin_access()
            if not ok:
                return response
            enabled = truthy(arg("enabled")) if action == "set_auto" else action == "enable_auto"
            ok, detail = set_windows_autorun(enabled)
            if action == "stop_disable_auto":
                if not ok:
                    message = "Could not disable auto run, so the app was not stopped."
                    return jsonify({"ok": False, "error": message, "message": message, "detail": detail}), 500
                threading.Timer(0.5, lambda: os._exit(0)).start()
                return jsonify({"ok": True, "message": "Auto run disabled. App is stopping.", "detail": detail})
            message = ("Auto run enabled." if enabled else "Auto run disabled.") if ok else f"Could not {'enable' if enabled else 'disable'} auto run."
            status_code = 200 if ok else 500
            payload = {"ok": ok, "message": message, "detail": detail}
            if not ok:
                payload["error"] = message
            return jsonify(payload), status_code
        if action == "stop":
            threading.Timer(0.5, lambda: os._exit(0)).start()
            return jsonify({"ok": True, "message": "App is stopping. If auto run is still enabled, supervisor may restart it."})
        if action == "restart":
            threading.Timer(0.5, lambda: os._exit(3)).start()
            return jsonify({"ok": True, "message": "App restart requested."})
        return json_error(400, "Unknown action.")

    @app.route("/api/sd/mount", methods=["POST"])
    def handle_sd_mount():
        payload = {"ok": True, "message": "Desktop storage is already ready."}
        payload.update(store.sd_status_fields())
        return jsonify(payload)

    @app.route("/api/sd/format", methods=["POST"])
    def handle_sd_format():
        return json_error(409, "Desktop mode does not support remote format.")

    @app.route("/api/wifi/config", methods=["GET"])
    def handle_get_wifi_config():
        return jsonify(store.wifi_config_json())

    @app.route("/api/wifi/config", methods=["POST"])
    def handle_save_wifi_config():
        ssid = trim_copy(arg("ssid"))
        selected_ip = sanitize_field(ssid)
        with store.lock:
            if selected_ip:
                store.app_config.selected_lan_ip = selected_ip
                store.app_config.configured_network_base_url = f"http://{selected_ip}:{store.app_config.port}"
                store._save_app_config()
        return jsonify(store.wifi_config_json())

    @app.route("/api/wifi/scan", methods=["GET"])
    def handle_wifi_scan():
        return jsonify({"networks": [], "current_ssid": store.app_config.selected_lan_ip or "", "scan_attempts": 1})

    @app.route("/api/wifi/forget", methods=["POST"])
    def handle_wifi_forget():
        with store.lock:
            store.app_config.selected_lan_ip = default_lan_ip()
            store.app_config.configured_network_base_url = f"http://{store.app_config.selected_lan_ip}:{store.app_config.port}"
            store._save_app_config()
        return jsonify(store.wifi_config_json())

    @app.route("/api/cloud-config", methods=["GET"])
    def handle_get_cloud_config():
        ok, response = require_admin_access()
        if not ok:
            return response
        return jsonify(store.cloud_config_json())

    @app.route("/api/cloud-config", methods=["POST"])
    def handle_save_cloud_config():
        ok, response = require_admin_access()
        if not ok:
            return response
        with store.lock:
            store.cloud_config.provider = sanitize_field(arg("provider", store.cloud_config.provider or "google_drive"))
            store.cloud_config.login_email = sanitize_field(arg("login_email", store.cloud_config.login_email))
            store.cloud_config.folder_name = sanitize_field(arg("folder_name", store.cloud_config.folder_name))
            store.cloud_config.folder_hint = sanitize_field(arg("folder_hint", store.cloud_config.folder_hint))
            store.cloud_config.mode = sanitize_field(arg("mode", store.cloud_config.mode or "select_or_create"))
            store.cloud_config.backup_mode = sanitize_field(arg("backup_mode", store.cloud_config.backup_mode or "sd_only"))
            store.cloud_config.asset_mode = sanitize_field(arg("asset_mode", store.cloud_config.asset_mode or "sd_only"))
            store.cloud_config.brand_name = sanitize_field(arg("brand_name", store.cloud_config.brand_name or "Stingray Inventory"))
            store.cloud_config.brand_logo_ref = sanitize_field(arg("brand_logo_ref", store.cloud_config.brand_logo_ref))
            store.cloud_config.client_id = sanitize_field(arg("client_id", store.cloud_config.client_id))
            store.cloud_config.client_secret = sanitize_field(arg("client_secret", store.cloud_config.client_secret))
            store.cloud_config.updated_at = current_timestamp()
            store._save_cloud_config()
            store.append_device_log("config_saved", "Cloud and branding settings updated in desktop mode.")
        return jsonify(store.cloud_config_json())

    @app.route("/api/google-auth/start", methods=["POST"])
    @app.route("/api/google-auth/poll", methods=["POST"])
    @app.route("/api/google-drive/sync", methods=["POST"])
    @app.route("/api/google-drive/restore", methods=["POST"])
    def handle_google_unavailable():
        return json_error(400, "Google OAuth and Drive sync are not enabled in desktop mode yet.")

    @app.route("/api/google-auth/disconnect", methods=["POST"])
    def handle_google_disconnect():
        ok, response = require_admin_access()
        if not ok:
            return response
        with store.lock:
            store.google_state = GoogleState()
            store._save_google_state()
        return jsonify(store.cloud_config_json())

    @app.route("/api/logs/device", methods=["GET"])
    def handle_device_logs():
        return jsonify({"lines": store.tail_log_lines(store.device_log_file)})

    @app.route("/api/logs/time", methods=["GET"])
    def handle_time_logs():
        return jsonify({"lines": store.tail_log_lines(store.time_log_file)})

    @app.route("/api/orders", methods=["GET"])
    def handle_get_orders():
        return Response(store._load_orders_payload(), mimetype="application/json; charset=utf-8")

    @app.route("/api/orders", methods=["POST"])
    def handle_save_orders():
        payload = trim_copy(arg("payload"))
        if not payload:
            return json_error(400, "Missing orders payload.")
        if len(payload) > 256 * 1024:
            return json_error(413, "Orders payload is too large.")
        if not payload.startswith("{") or '"orders"' not in payload:
            return json_error(400, "Invalid orders payload.")
        with store.lock:
            if not store._save_orders_payload(payload):
                return json_error(500, "Failed to save orders.")
            store.append_device_log("orders_saved", "Order planner data saved in desktop mode.")
        return jsonify({"ok": True})

    @app.route("/api/orders/fulfill", methods=["POST"])
    def handle_fulfill_order():
        order_number = sanitize_field(arg("order_number"))
        if not order_number:
            return json_error(400, "Missing order number.")
        plan_raw = arg("plan")
        if not plan_raw:
            return json_error(400, "Missing fulfillment plan.")
        next_orders_payload = trim_copy(arg("orders_payload"))
        if not next_orders_payload:
            return json_error(400, "Missing updated orders payload.")
        if len(next_orders_payload) > 256 * 1024:
            return json_error(413, "Orders payload is too large.")

        requirements: dict[str, int] = {}
        for raw in plan_raw.replace("\r", "\n").split("\n"):
            line = trim_copy(raw)
            if not line:
                continue
            fields = split_pipe_line(line)
            if len(fields) < 2:
                return json_error(400, "Invalid fulfillment line format.")
            item_id = trim_copy(fields[0])
            needed = parse_int(fields[1])
            if not item_id or needed is None or needed <= 0:
                return json_error(400, "Fulfillment quantity must be a positive integer.")
            key = normalize_lookup_value(item_id)
            requirements[key] = requirements.get(key, 0) + needed

        if not requirements:
            return json_error(400, "No selected items to fulfill.")

        with store.lock:
            rollback = [ItemRecord(**asdict(item)) for item in store.items]
            deductions: list[tuple[int, int]] = []
            for lookup_id, needed in requirements.items():
                idx = next((i for i, item in enumerate(store.items) if normalize_lookup_value(item.id) == lookup_id), -1)
                if idx < 0:
                    return json_error(404, f"Item not found for fulfillment: {lookup_id}")
                if needed > store.items[idx].qty:
                    return json_error(
                        409,
                        f"Insufficient stock for {store.items[idx].id}. Needed {needed}, available {store.items[idx].qty}.",
                    )
                deductions.append((idx, needed))

            updated_at = current_timestamp()
            total_removed = 0
            detail_chunks = []
            for idx, needed in deductions:
                store.items[idx].qty -= needed
                store.items[idx].updated_at = updated_at
                total_removed += needed
                detail_chunks.append(f"{store.items[idx].id}:-{needed}")

            if not store._save_inventory():
                store.items = rollback
                return json_error(500, "Failed to save inventory while fulfilling the order.")

            if not store._save_orders_payload(next_orders_payload):
                store.items = rollback
                store._save_inventory()
                return json_error(500, "Failed to remove the fulfilled order.")

            detail = f"{order_number} " + ", ".join(detail_chunks)
            store.append_transaction(order_number, "fulfill_order", -total_removed, 0, detail[:220])
            store.append_device_log("order_fulfilled", detail[:220])

        return jsonify({"ok": True, "order_number": order_number, "line_count": len(deductions), "units_removed": total_removed})

    @app.route("/api/files", methods=["GET"])
    def handle_files():
        raw_path = trim_copy(arg("path"))
        if not raw_path:
            return json_error(400, "Missing file path.")
        path = unquote(raw_path)
        if not path.startswith("/"):
            path = "/" + path
        if not safe_relative_image_path(path):
            return json_error(403, "File path is not allowed.")
        full_path = store.data_dir / path.lstrip("/")
        if not full_path.exists() or not full_path.is_file():
            return json_error(404, "File not found.")
        response = send_file(full_path)
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.route("/api/images/upload", methods=["POST"])
    def handle_image_upload():
        if not request.files:
            return json_error(400, "No image was uploaded.")
        file = next(iter(request.files.values()))
        if not file or not file.filename:
            return json_error(400, "No image was uploaded.")
        suffix = Path(file.filename).suffix.lower()
        if suffix not in ALLOWED_IMAGE_EXTENSIONS:
            return json_error(400, "Unsupported image type. Use JPG, PNG, GIF, BMP, or WEBP.")
        stem = sanitize_filename_stem(Path(file.filename).stem)
        epoch_ms = int(time.time() * 1000)
        candidate = store.images_dir / f"{epoch_ms}_{stem}{suffix}"
        counter = 2
        while candidate.exists():
            candidate = store.images_dir / f"{epoch_ms}_{stem}_{counter}{suffix}"
            counter += 1
        file.save(candidate)
        storage_path = "/" + candidate.relative_to(store.data_dir).as_posix()
        image_ref = "/api/files?path=" + quote(storage_path, safe="")
        store.append_device_log("image_uploaded", f"Stored image asset at {storage_path}")
        return jsonify({"ok": True, "storage_path": storage_path, "image_ref": image_ref}), 201

    def save_item_image_upload(item_id: str) -> str:
        if not request.files:
            raise ValueError("No image was uploaded.")
        file = next(iter(request.files.values()))
        if not file or not file.filename:
            raise ValueError("No image was uploaded.")
        suffix = Path(file.filename).suffix.lower()
        if suffix not in ALLOWED_IMAGE_EXTENSIONS:
            raise ValueError("Unsupported image type. Use JPG, PNG, GIF, BMP, or WEBP.")
        stem = sanitize_filename_stem(f"{item_id}_{Path(file.filename).stem}")
        candidate = store.images_dir / f"{int(time.time() * 1000)}_{stem}{suffix}"
        counter = 2
        while candidate.exists():
            candidate = store.images_dir / f"{int(time.time() * 1000)}_{stem}_{counter}{suffix}"
            counter += 1
        file.save(candidate)
        storage_path = "/" + candidate.relative_to(store.data_dir).as_posix()
        return "/api/files?path=" + quote(storage_path, safe="")

    @app.route("/api/items/image", methods=["POST", "DELETE"])
    def handle_item_image():
        item_id = trim_copy(arg("id"))
        if not item_id:
            return json_error(400, "Missing part number.")
        with store.lock:
            idx = store.find_item_index(item_id)
            if idx < 0:
                return json_error(404, "Item not found.")
            if request.method == "DELETE":
                store.create_backup_zip("before_remove_item_image")
                store.items[idx].image_ref = ""
                store.items[idx].updated_at = current_timestamp()
                store._save_inventory()
                store.append_transaction(item_id, "edit_image", 0, store.items[idx].qty, "image removed")
                return jsonify(store.item_payload(store.items[idx], base_url()))
            try:
                image_ref = save_item_image_upload(item_id)
            except ValueError as exc:
                return json_error(400, str(exc))
            if store.items[idx].image_ref:
                store.create_backup_zip("before_replace_item_image")
            store.items[idx].image_ref = image_ref
            store.items[idx].updated_at = current_timestamp()
            store._save_inventory()
            store.append_transaction(item_id, "edit_image", 0, store.items[idx].qty, "image updated")
            return jsonify(store.item_payload(store.items[idx], base_url()))

    @app.route("/api/items", methods=["GET"])
    def handle_items_list():
        category_filter = arg("category", "all")
        search_filter = arg("q", "")
        with store.lock:
            payload = [
                store.item_to_dict(item, base_url())
                for item in store.items
                if store.matches_category_filter(item, category_filter) and store.matches_search_filter(item, search_filter)
            ]
        return jsonify({"items": payload})

    @app.route("/api/item", methods=["GET"])
    def handle_get_item():
        item_id = trim_copy(arg("id"))
        if not item_id:
            return json_error(400, "Missing or invalid part number.")
        with store.lock:
            idx = store.find_item_index(item_id)
            if idx < 0:
                return json_error(404, "Item not found.")
            return jsonify(store.item_payload(store.items[idx], base_url()))

    @app.route("/api/items/add", methods=["POST"])
    def handle_add_item():
        category = normalize_category(arg("category", DEFAULT_CATEGORY))
        part_name = sanitize_field(arg("part_name"))
        if not part_name:
            return json_error(400, "Name is required.")
        item_id = trim_copy(sanitize_field(arg("id")))
        if not item_id:
            return json_error(400, "Part number is required.")

        qr_code = sanitize_field(arg("qr_code"))
        color = sanitize_field(arg("color"))
        material = sanitize_field(arg("material"))
        image_ref = sanitize_field(arg("image_ref"))
        bom_product = sanitize_field(arg("bom_product"))

        bom_qty_raw = arg("bom_qty", "")
        bom_qty = 0
        if bom_qty_raw:
            parsed = parse_int(bom_qty_raw)
            if parsed is None:
                return json_error(400, "BOM quantity must be an integer.")
            bom_qty = parsed
        if bom_qty < 0:
            return json_error(400, "BOM quantity cannot be negative.")
        if bom_product and bom_qty == 0:
            bom_qty = 1
        if not bom_product and bom_qty > 0:
            return json_error(400, "Parent product or kit is required when quantity used in parent is set.")
        if category != "part" and (bom_product or bom_qty > 0):
            return json_error(400, "Only parts can be assigned to a parent product or kit.")

        qty_raw = arg("qty", "0")
        qty_parsed = parse_int(qty_raw)
        if qty_parsed is None:
            return json_error(400, "Quantity must be an integer.")
        if qty_parsed < 0:
            return json_error(400, "Quantity cannot be negative.")

        with store.lock:
            if store.find_item_index(item_id) >= 0:
                return json_error(409, "Part number already exists.")
            item = ItemRecord(
                id=item_id,
                category=category,
                part_name=part_name,
                qr_code=qr_code,
                color=color,
                material=material,
                qty=qty_parsed,
                image_ref=image_ref,
                bom_product=bom_product,
                bom_qty=bom_qty,
                updated_at=current_timestamp(),
            )
            store.items.append(item)
            store.items.sort(key=lambda row: normalize_lookup_value(row.id))
            if not store._save_inventory():
                store.items = [row for row in store.items if normalize_lookup_value(row.id) != normalize_lookup_value(item_id)]
                return json_error(500, "Failed to persist inventory to disk.")
            saved_idx = store.find_item_index(item_id)
            store.append_transaction(item.id, "create", qty_parsed, qty_parsed, f"{item.part_name} created")
            store.append_device_log("item_created", f"{item.id} saved with qty {qty_parsed}")
            return jsonify(store.item_payload(store.items[saved_idx], base_url())), 201

    @app.route("/api/items/update", methods=["POST"])
    def handle_update_item():
        original_id = trim_copy(arg("original_id", arg("id")))
        new_id = trim_copy(sanitize_field(arg("id")))
        if not original_id or not new_id:
            return json_error(400, "Part number is required.")
        qty = parse_int(arg("qty", "0"))
        bom_qty = parse_int(arg("bom_qty", "0"))
        if qty is None or qty < 0:
            return json_error(400, "Quantity cannot be negative.")
        if bom_qty is None or bom_qty < 0:
            return json_error(400, "BOM quantity cannot be negative.")
        category = normalize_category(arg("category", DEFAULT_CATEGORY))
        part_name = sanitize_field(arg("part_name"))
        if not part_name:
            return json_error(400, "Name is required.")
        bom_product = sanitize_field(arg("bom_product"))
        if bom_product and bom_qty == 0:
            bom_qty = 1
        if not bom_product and bom_qty > 0:
            return json_error(400, "Parent product or kit is required when quantity used in parent is set.")
        if category != "part" and (bom_product or bom_qty > 0):
            return json_error(400, "Only parts can be assigned to a parent product or kit.")
        with store.lock:
            idx = store.find_item_index(original_id)
            if idx < 0:
                return json_error(404, "Item not found.")
            duplicate_idx = store.find_item_index(new_id)
            if duplicate_idx >= 0 and duplicate_idx != idx:
                return json_error(409, "Part number already exists.")
            previous = ItemRecord(**asdict(store.items[idx]))
            item = store.items[idx]
            item.id = new_id
            item.category = category
            item.part_name = part_name
            item.qr_code = sanitize_field(arg("qr_code"))
            item.color = sanitize_field(arg("color"))
            item.material = sanitize_field(arg("material"))
            item.qty = qty
            item.image_ref = sanitize_field(arg("image_ref", item.image_ref))
            item.bom_product = bom_product
            item.bom_qty = bom_qty
            item.updated_at = current_timestamp()
            if normalize_lookup_value(original_id) != normalize_lookup_value(new_id):
                for row in store.items:
                    if normalize_lookup_value(row.bom_product) == normalize_lookup_value(original_id):
                        row.bom_product = new_id
            store.items.sort(key=lambda row: normalize_lookup_value(row.id))
            if not store._save_inventory():
                store.items[idx] = previous
                return json_error(500, "Failed to persist inventory to disk.")
            saved_idx = store.find_item_index(new_id)
            store.append_transaction(new_id, "edit_item", qty - previous.qty, qty, f"edited from {original_id}")
            store.append_device_log("item_updated", f"{original_id} updated to {new_id}")
            return jsonify(store.item_payload(store.items[saved_idx], base_url()))

    @app.route("/api/items/remove", methods=["POST"])
    def handle_remove_item():
        item_id = trim_copy(arg("id"))
        if not item_id:
            return json_error(400, "Missing or invalid part number.")
        with store.lock:
            idx = store.find_item_index(item_id)
            if idx < 0:
                return json_error(404, "Item not found.")
            store.create_backup_zip("before_delete_item")
            removed = store.items[idx]
            del store.items[idx]
            if not store._save_inventory():
                store.items.insert(idx, removed)
                return json_error(500, "Failed to persist inventory to disk.")
            store.append_transaction(item_id, "remove", -removed.qty, 0, "item removed")
            store.append_device_log("item_removed", f"{item_id} removed from inventory.")
        return jsonify({"ok": True})

    @app.route("/api/items/adjust", methods=["POST"])
    def handle_adjust_item():
        item_id = trim_copy(arg("id"))
        if not item_id:
            return json_error(400, "Missing or invalid part number.")
        delta = parse_int(arg("delta", ""))
        if delta is None:
            return json_error(400, "Missing or invalid delta.")
        if delta == 0:
            return json_error(400, "Delta cannot be zero.")
        with store.lock:
            idx = store.find_item_index(item_id)
            if idx < 0:
                return json_error(404, "Item not found.")
            new_qty = store.items[idx].qty + delta
            if new_qty < 0:
                return json_error(400, "Quantity cannot go below zero.")
            previous_qty = store.items[idx].qty
            previous_updated = store.items[idx].updated_at
            store.items[idx].qty = new_qty
            store.items[idx].updated_at = current_timestamp()
            if not store._save_inventory():
                store.items[idx].qty = previous_qty
                store.items[idx].updated_at = previous_updated
                return json_error(500, "Failed to persist inventory to disk.")
            store.append_transaction(item_id, "adjust", delta, new_qty, "quantity updated from item page")
            store.append_device_log("item_adjusted", f"{item_id} adjusted by {delta} to {new_qty}")
            return jsonify(store.item_payload(store.items[idx], base_url()))

    @app.route("/api/items/set", methods=["POST"])
    def handle_set_item_qty():
        item_id = trim_copy(arg("id"))
        if not item_id:
            return json_error(400, "Missing or invalid part number.")
        qty = parse_int(arg("qty", ""))
        if qty is None:
            return json_error(400, "Missing or invalid quantity.")
        if qty < 0:
            return json_error(400, "Quantity cannot be negative.")
        with store.lock:
            idx = store.find_item_index(item_id)
            if idx < 0:
                return json_error(404, "Item not found.")
            previous_qty = store.items[idx].qty
            previous_updated = store.items[idx].updated_at
            delta = qty - previous_qty
            store.items[idx].qty = qty
            store.items[idx].updated_at = current_timestamp()
            if not store._save_inventory():
                store.items[idx].qty = previous_qty
                store.items[idx].updated_at = previous_updated
                return json_error(500, "Failed to persist inventory to disk.")
            store.append_transaction(item_id, "set_qty", delta, qty, "quantity set directly")
            store.append_device_log("item_set_qty", f"{item_id} quantity set to {qty}")
            return jsonify(store.item_payload(store.items[idx], base_url()))

    @app.route("/api/export", methods=["GET"])
    def handle_export_csv():
        category_filter = arg("category", "all")
        with store.lock:
            rows = [
                item
                for item in store.items
                if store.matches_category_filter(item, category_filter)
            ]
        csv_lines = [
            "part_number,category,part_name,qr_code,color,material,qty,image_ref,bom_product,bom_qty,updated_at,qr_link"
        ]
        for item in rows:
            csv_lines.append(
                ",".join(
                    [
                        csv_escape(item.id),
                        csv_escape(normalize_category(item.category)),
                        csv_escape(item.part_name),
                        csv_escape(item.qr_code),
                        csv_escape(item.color),
                        csv_escape(item.material),
                        str(item.qty),
                        csv_escape(item.image_ref),
                        csv_escape(item.bom_product),
                        str(item.bom_qty),
                        csv_escape(item.updated_at),
                        csv_escape(store.item_url(item.id, base_url())),
                    ]
                )
            )
        file_name = "inventory_export.csv" if category_filter in {"", "all"} else f"inventory_export_{normalize_category(category_filter)}.csv"
        response = Response("\n".join(csv_lines) + "\n", mimetype="text/csv; charset=utf-8")
        response.headers["Content-Disposition"] = f"attachment; filename={file_name}"
        return response

    @app.route("/favicon.ico", methods=["GET"])
    def handle_favicon():
        with store.lock:
            logo_file = store.resolve_asset_file_path(store.cloud_config.brand_logo_ref)
        if logo_file:
            suffix = logo_file.suffix.lower()
            mimetype = {
                ".png": "image/png",
                ".jpg": "image/jpeg",
                ".jpeg": "image/jpeg",
                ".gif": "image/gif",
                ".bmp": "image/bmp",
                ".webp": "image/webp",
            }.get(suffix, "application/octet-stream")
            response = send_file(logo_file, mimetype=mimetype)
            response.headers["Cache-Control"] = "no-store"
            return response
        return Response("", status=204, mimetype="text/plain")

    return app, store


def run_self_test(app: Flask) -> None:
    with app.test_client() as client:
        assert client.get("/api/status").status_code == 200
        add_resp = client.post(
            "/api/items/add",
            data={
                "id": "DESKTOP-TEST-1001",
                "category": "part",
                "part_name": "Desktop Test Item",
                "qty": "5",
                "qr_code": "DESKTOP-QR-TEST",
                "color": "Black",
                "material": "ABS",
            },
        )
        assert add_resp.status_code in {201, 409}
        list_resp = client.get("/api/items")
        assert list_resp.status_code == 200
        qr_resp = client.get("/api/qr.svg", query_string={"data": "desktop-self-test"})
        assert qr_resp.status_code == 200
        assert "svg" in qr_resp.get_data(as_text=True)


def open_browser_when_ready(store: DesktopStore) -> None:
    target_url = store.setup_page_url() if not store.app_config.setup_complete else f"{store.local_pc_url()}/"
    fallback_url = f"{store.local_pc_url()}/setup" if not store.app_config.setup_complete else f"{store.local_pc_url()}/"
    lan_probe_url = f"{store.configured_base_url()}/api/status"
    local_probe_url = f"{store.local_pc_url()}/api/status"

    def worker() -> None:
        deadline = time.monotonic() + 45
        opened = False
        while time.monotonic() < deadline:
            lan_ok, _ = probe_url(lan_probe_url, timeout=1.5)
            local_ok, _ = probe_url(local_probe_url, timeout=1.5)
            if lan_ok:
                webbrowser.open(target_url)
                opened = True
                break
            if local_ok and time.monotonic() > deadline - 5:
                webbrowser.open(fallback_url)
                opened = True
                break
            time.sleep(1.0)
        if not opened:
            webbrowser.open(fallback_url)

    threading.Thread(target=worker, daemon=True).start()


def main() -> None:
    parser = argparse.ArgumentParser(description=f"{APP_DISPLAY_NAME} Desktop App")
    parser.add_argument("--data-dir", type=Path, default=resolve_default_desktop_data_dir())
    parser.add_argument(
        "--firmware-ino",
        type=Path,
        default=default_firmware_ino_path(),
    )
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--open-browser", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    app, store = create_app(data_dir=args.data_dir, firmware_ino=args.firmware_ino, bind_host=args.host, port=args.port)
    if args.self_test:
        run_self_test(app)
        print("Self-test passed.")
        return

    if args.open_browser:
        open_browser_when_ready(store)

    print(f"{APP_DISPLAY_NAME} desktop listening on http://{args.host}:{args.port}/")
    print(f"Local PC URL: {store.local_pc_url()}/")
    print(f"LAN URL: {store.configured_base_url()}/")
    if not store.app_config.setup_complete:
        print(f"Setup URL: {store.setup_page_url()}")
    print(f"Data directory: {store.data_dir}")
    app.run(host=args.host, port=args.port, debug=False)


if __name__ == "__main__":
    main()
