from __future__ import annotations

import argparse
import json
import re
import socket
import sys
import threading
import time
import uuid
import webbrowser
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, quote, unquote, urlparse

import qrcode
import qrcode.image.svg
from flask import Flask, Response, jsonify, request, send_file


INVENTORY_HEADER = "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at"
TRANSACTION_HEADER = "timestamp|item_id|action|delta|qty_after|note"
DEVICE_LOG_HEADER = "timestamp|mac_address|uptime_seconds|event|detail"
TIME_LOG_HEADER = "timestamp|event|detail"
CLOUD_CONFIG_HEADER = "provider|login_email|folder_name|folder_hint|mode|backup_mode|asset_mode|brand_name|brand_logo_ref|client_id|client_secret|updated_at"
GOOGLE_STATE_HEADER = "refresh_token|folder_id|last_sync_at|last_synced_manifest_hash|last_synced_snapshot_at|local_snapshot_at|auth_status|sync_status|last_error"
DEFAULT_CATEGORY = "part"
ALLOWED_IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp"}


def runtime_root() -> Path:
    if getattr(sys, "frozen", False):
        meipass = getattr(sys, "_MEIPASS", "")
        if meipass:
            return Path(meipass)
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[1]


def default_firmware_ino_path() -> Path:
    return runtime_root() / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino"


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


@dataclass
class WifiConfig:
    ssid: str = ""
    updated_at: str = ""


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


class DesktopStore:
    def __init__(self, data_dir: Path, firmware_ino: Path | None) -> None:
        self.data_dir = data_dir
        self.images_dir = self.data_dir / "images"
        self.inventory_file = self.data_dir / "inventory.csv"
        self.inventory_tmp_file = self.data_dir / "inventory.tmp"
        self.orders_file = self.data_dir / "orders.json"
        self.orders_tmp_file = self.data_dir / "orders.tmp"
        self.transaction_file = self.data_dir / "transactions.csv"
        self.device_log_file = self.data_dir / "device_log.csv"
        self.time_log_file = self.data_dir / "time_log.csv"
        self.cloud_config_file = self.data_dir / "cloud_backup.cfg"
        self.cloud_config_tmp_file = self.data_dir / "cloud_backup.tmp"
        self.google_state_file = self.data_dir / "google_drive_state.cfg"
        self.google_state_tmp_file = self.data_dir / "google_drive_state.tmp"
        self.lock = threading.RLock()
        self.start_monotonic = time.monotonic()
        self.mac_address = self._mac_address_string()
        self.device_id = f"PC-{socket.gethostname()}"
        self.items: list[ItemRecord] = []
        self.cloud_config = CloudConfig()
        self.google_state = GoogleState()
        self.wifi_config = WifiConfig()
        self.index_html, self.item_html = self._load_ui_assets(firmware_ino)
        self._ensure_data_files()
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

    def _ensure_data_files(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.images_dir.mkdir(parents=True, exist_ok=True)
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

    def _load_inventory(self) -> None:
        with self.lock:
            self.items = []
            if not self.inventory_file.exists():
                return
            for raw in self.inventory_file.read_text(encoding="utf-8", errors="replace").splitlines():
                line = raw.strip()
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

    def _save_orders_payload(self, payload: str) -> bool:
        trimmed = trim_copy(payload)
        if not trimmed:
            return False
        self.orders_tmp_file.write_text(trimmed, encoding="utf-8")
        self.orders_tmp_file.replace(self.orders_file)
        return True

    def _load_cloud_config(self) -> None:
        if not self.cloud_config_file.exists():
            self.cloud_config = CloudConfig()
            return
        lines = self.cloud_config_file.read_text(encoding="utf-8", errors="replace").splitlines()
        for raw in lines:
            line = raw.strip()
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
            line = raw.strip()
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
        payload = {
            "board": "PC_DESKTOP",
            "device_id": self.device_id,
            "storage_mode": "pc_fs",
            "hostname": socket.gethostname(),
            "base_url": base_url,
            "brand_name": self.cloud_config.brand_name,
            "brand_logo_ref": self.cloud_config.brand_logo_ref,
            "backup_mode": self.cloud_config.backup_mode,
            "asset_mode": self.cloud_config.asset_mode,
            "auth_status": self.google_state.auth_status,
            "sync_status": self.google_state.sync_status,
            "folder_id": self.google_state.folder_id,
            "last_sync_at": self.google_state.last_sync_at,
            "time_source": "system_clock",
            "wifi_connected": True,
            "wifi_ssid": "desktop",
            "wifi_ip": socket.gethostbyname(socket.gethostname()) if socket.gethostname() else "127.0.0.1",
            "wifi_rssi": 0,
            "wifi_mode": "desktop",
            "wifi_ap_active": False,
            "wifi_ap_ssid": "",
            "wifi_ap_clients": 0,
            "wifi_config_source": "desktop",
            "wifi_saved_ssid": self.wifi_config.ssid,
            "wifi_setup_required": False,
            "wifi_last_error": "",
        }
        payload.update(self.sd_status_fields())
        return payload

    def wifi_config_json(self) -> dict[str, Any]:
        payload = {
            "config_source": "desktop",
            "saved_ssid": self.wifi_config.ssid,
            "saved_updated_at": self.wifi_config.updated_at,
            "effective_ssid": self.wifi_config.ssid,
            "connected": True,
            "current_ssid": "desktop",
            "current_ip": socket.gethostbyname(socket.gethostname()) if socket.gethostname() else "127.0.0.1",
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
        return payload

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


def create_app(data_dir: Path, firmware_ino: Path | None) -> tuple[Flask, DesktopStore]:
    app = Flask(__name__)
    store = DesktopStore(data_dir=data_dir, firmware_ino=firmware_ino)

    def base_url() -> str:
        return request.host_url.rstrip("/")

    def arg(name: str, default: str = "") -> str:
        if request.is_json:
            payload = request.get_json(silent=True) or {}
            if name in payload:
                return str(payload.get(name, default))
        return str(request.values.get(name, default))

    def json_error(status: int, message: str):
        return jsonify({"error": message}), status

    @app.route("/", methods=["GET"])
    @app.route("/settings", methods=["GET"])
    @app.route("/orders", methods=["GET"])
    @app.route("/orders/view", methods=["GET"])
    @app.route("/orders/fulfill", methods=["GET"])
    def handle_index_page():
        return Response(store.index_html, mimetype="text/html; charset=utf-8")

    @app.route("/item", methods=["GET"])
    def handle_item_page():
        return Response(store.item_html, mimetype="text/html; charset=utf-8")

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
        store.wifi_config.ssid = sanitize_field(ssid)
        store.wifi_config.updated_at = current_timestamp()
        return jsonify(store.wifi_config_json())

    @app.route("/api/wifi/scan", methods=["GET"])
    def handle_wifi_scan():
        return jsonify({"networks": [], "current_ssid": "", "scan_attempts": 1})

    @app.route("/api/wifi/forget", methods=["POST"])
    def handle_wifi_forget():
        store.wifi_config = WifiConfig()
        return jsonify(store.wifi_config_json())

    @app.route("/api/cloud-config", methods=["GET"])
    def handle_get_cloud_config():
        return jsonify(store.cloud_config_json())

    @app.route("/api/cloud-config", methods=["POST"])
    def handle_save_cloud_config():
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
            return json_error(400, "Missing or invalid item id.")
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
            return json_error(400, "Part name is required.")
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
            return json_error(400, "BOM product is required when BOM quantity is set.")
        if category != "part" and (bom_product or bom_qty > 0):
            return json_error(400, "Only parts can be assigned to a BOM product or kit.")

        qty_raw = arg("qty", "0")
        qty_parsed = parse_int(qty_raw)
        if qty_parsed is None:
            return json_error(400, "Quantity must be an integer.")
        if qty_parsed < 0:
            return json_error(400, "Quantity cannot be negative.")

        with store.lock:
            if store.find_item_index(item_id) >= 0:
                return json_error(409, "Item id already exists.")
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

    @app.route("/api/items/remove", methods=["POST"])
    def handle_remove_item():
        item_id = trim_copy(arg("id"))
        if not item_id:
            return json_error(400, "Missing or invalid item id.")
        with store.lock:
            idx = store.find_item_index(item_id)
            if idx < 0:
                return json_error(404, "Item not found.")
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
            return json_error(400, "Missing or invalid item id.")
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
            return json_error(400, "Missing or invalid item id.")
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


def main() -> None:
    parser = argparse.ArgumentParser(description="Stingray Inventory Desktop App")
    parser.add_argument("--data-dir", type=Path, default=Path.home() / "StingrayInventoryDesktop" / "data")
    parser.add_argument(
        "--firmware-ino",
        type=Path,
        default=default_firmware_ino_path(),
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--open-browser", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    app, store = create_app(data_dir=args.data_dir, firmware_ino=args.firmware_ino)
    if args.self_test:
        run_self_test(app)
        print("Self-test passed.")
        return

    if args.open_browser:
        webbrowser.open(f"http://{args.host}:{args.port}/")

    print(f"Stingray Desktop running on http://{args.host}:{args.port}/")
    print(f"Data directory: {store.data_dir}")
    app.run(host=args.host, port=args.port, debug=False)


if __name__ == "__main__":
    main()
