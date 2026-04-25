from __future__ import annotations

import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parent))

from stingray_desktop_app import create_app, parse_inventory_line


class DesktopAppTests(unittest.TestCase):
    def make_client(self):
        tmp = tempfile.TemporaryDirectory()
        firmware_ino = Path(__file__).resolve().parents[1] / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino"
        app, store = create_app(Path(tmp.name) / "data", firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        self.addCleanup(tmp.cleanup)
        return app.test_client(), store

    def test_inventory_parser_current_and_legacy(self):
        current = parse_inventory_line("PN1|part|Widget|QR|red|pla|3|/images/a.png||0|2026")
        legacy = parse_inventory_line("OLD1|Old Name|7|2024-01-01T00:00:00Z")
        self.assertEqual(current.id, "PN1")
        self.assertEqual(current.qty, 3)
        self.assertEqual(legacy.id, "OLD1")
        self.assertEqual(legacy.part_name, "Old Name")

    def test_qr_link_uses_configured_lan_base_url(self):
        client, _ = self.make_client()
        client.post("/api/desktop/settings", json={"selected_lan_ip": "192.168.1.50", "configured_network_base_url": "http://192.168.1.50:8787"})
        client.post("/api/items/add", json={"id": "LAN1", "part_name": "LAN Item", "qty": "1"})
        item = client.get("/api/item?id=LAN1").get_json()["item"]
        self.assertEqual(item["qr_link"], "http://192.168.1.50:8787/item?id=LAN1")
        self.assertNotIn("127.0.0.1", item["qr_link"])
        export = client.get("/api/export").get_data(as_text=True)
        self.assertIn("http://192.168.1.50:8787/item?id=LAN1", export)

    def test_item_update_validates_duplicates_and_saves_fields(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "A", "part_name": "A", "qty": "1"})
        client.post("/api/items/add", json={"id": "B", "part_name": "B", "qty": "1"})
        duplicate = client.post("/api/items/update", json={"original_id": "A", "id": "B", "part_name": "A2", "qty": "1"})
        self.assertEqual(duplicate.status_code, 409)
        ok = client.post(
            "/api/items/update",
            json={"original_id": "A", "id": "A2", "part_name": "Edited", "category": "part", "qty": "4", "bom_qty": "0", "color": "blue"},
        )
        self.assertEqual(ok.status_code, 200)
        self.assertEqual(ok.get_json()["item"]["id"], "A2")
        self.assertEqual(ok.get_json()["item"]["color"], "blue")

    def test_item_image_add_replace_remove(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "IMG", "part_name": "Image Item", "qty": "1"})
        upload = client.post(
            "/api/items/image",
            data={"id": "IMG", "image": (io.BytesIO(b"fake"), "part.png")},
            content_type="multipart/form-data",
        )
        self.assertEqual(upload.status_code, 200)
        self.assertIn("/api/files?path=", upload.get_json()["item"]["image_ref"])
        remove = client.delete("/api/items/image", json={"id": "IMG"})
        self.assertEqual(remove.status_code, 200)
        self.assertEqual(remove.get_json()["item"]["image_ref"], "")

    def test_delete_item_creates_backup(self):
        client, store = self.make_client()
        client.post("/api/items/add", json={"id": "DEL", "part_name": "Delete Me", "qty": "1"})
        before = set(store.backups_dir.glob("stingray-backup-*.zip"))
        self.assertEqual(client.post("/api/items/remove", json={"id": "DEL"}).status_code, 200)
        after = set(store.backups_dir.glob("stingray-backup-*.zip"))
        self.assertGreater(len(after - before), 0)

    def test_sd_import_backup_and_merge_no_duplicate(self):
        client, store = self.make_client()
        client.post("/api/items/add", json={"id": "KEEP", "part_name": "Current", "qty": "2"})
        sd = store.data_dir.parent / "sd"
        (sd / "images").mkdir(parents=True)
        (sd / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "KEEP|part|SD|QR|||9|||0|2026\nNEW|part|New|QR|||3|||0|2026\n",
            encoding="utf-8",
        )
        (sd / "transactions.csv").write_text("timestamp|item_id|action|delta|qty_after|note\n2026|NEW|x|0|3|ok\n", encoding="utf-8")
        (sd / "images" / "x.png").write_bytes(b"x")
        preview = client.get("/api/desktop/sd/preview", query_string={"path": str(sd)}).get_json()
        self.assertEqual(preview["inventory_items_found"], 2)
        result = client.post("/api/desktop/sd/import", json={"path": str(sd), "mode": "merge"}).get_json()
        self.assertEqual(result["items_imported"], 1)
        self.assertEqual(result["items_skipped"], 1)
        self.assertTrue((store.backups_dir / result["backup"]).exists())
        self.assertTrue((store.images_dir / "x.png").exists())

    def test_sd_import_auto_detects_nested_esp32_data_folder(self):
        client, store = self.make_client()
        sd_root = store.data_dir.parent / "old_sd_card"
        nested = sd_root / "backup_copy" / "esp32_sd"
        (nested / "images").mkdir(parents=True)
        (nested / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "NEST|part|Nested Import|QRN|red|pla|6|/images/nested.png||0|2026\n",
            encoding="utf-8",
        )
        (nested / "images" / "nested.png").write_bytes(b"nested")

        preview = client.get("/api/desktop/sd/preview", query_string={"path": str(sd_root)}).get_json()
        self.assertEqual(preview["inventory_items_found"], 1)
        self.assertEqual(Path(preview["import_root"]), nested)
        self.assertIn("Using detected SD data folder", "\n".join(preview["messages"]))

        result = client.post("/api/desktop/sd/import", json={"path": str(sd_root), "mode": "merge"}).get_json()
        self.assertEqual(result["items_imported"], 1)
        self.assertEqual(Path(result["import_root"]), nested)
        self.assertTrue((store.images_dir / "nested.png").exists())
        imported = client.get("/api/item?id=NEST").get_json()["item"]
        self.assertEqual(imported["qty"], 6)

    def test_sd_merge_imports_orders_and_appends_logs(self):
        client, store = self.make_client()
        store._save_orders_payload(
            '{"orders":[{"order_number":"ORD-LOCAL","created_at":"2026","updated_at":"2026","lines":[]}]}'
        )
        store.transaction_file.write_text(
            "timestamp|item_id|action|delta|qty_after|note\n2026|KEEP|adjust|1|1|already here\n",
            encoding="utf-8",
        )
        store.device_log_file.write_text(
            "timestamp|mac_address|uptime_seconds|event|detail\n2026|PC|1|boot|already here\n",
            encoding="utf-8",
        )
        store.time_log_file.write_text(
            "timestamp|event|detail\n2026|boot|already here\n",
            encoding="utf-8",
        )
        sd = store.data_dir.parent / "sd_orders"
        sd.mkdir(parents=True)
        (sd / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "ORDERITEM|part|Order Item|QR|||2|||0|2026\n",
            encoding="utf-8",
        )
        (sd / "orders.json").write_text(
            '{"orders":[{"order_number":"ORD-SD","created_at":"2026","updated_at":"2026","lines":[{"lineId":"a","itemId":"ORDERITEM","needed":1}]}]}',
            encoding="utf-8",
        )
        (sd / "transactions.csv").write_text(
            "timestamp|item_id|action|delta|qty_after|note\n2026|ORDERITEM|create|2|2|from sd\n2026|KEEP|adjust|1|1|already here\n",
            encoding="utf-8",
        )
        (sd / "device_log.csv").write_text(
            "timestamp|mac_address|uptime_seconds|event|detail\n2026|ESP|2|import|from sd\n2026|PC|1|boot|already here\n",
            encoding="utf-8",
        )
        (sd / "time_log.csv").write_text(
            "timestamp|event|detail\n2026|import|from sd\n2026|boot|already here\n",
            encoding="utf-8",
        )

        preview = client.get("/api/desktop/sd/preview", query_string={"path": str(sd)}).get_json()
        self.assertEqual(preview["orders_found"], 1)
        self.assertEqual(preview["device_log_rows_found"], 2)
        result = client.post("/api/desktop/sd/import", json={"path": str(sd), "mode": "merge"}).get_json()
        self.assertEqual(result["items_imported"], 1)
        self.assertEqual(result["orders_added"], 1)
        self.assertEqual(result["transactions_imported"], 1)
        self.assertEqual(result["device_log_rows_imported"], 1)
        self.assertEqual(result["time_log_rows_imported"], 1)

        orders = client.get("/api/orders").get_json()["orders"]
        self.assertEqual({order["order_number"] for order in orders}, {"ORD-LOCAL", "ORD-SD"})
        self.assertIn("ORDERITEM|create|2|2|from sd", store.transaction_file.read_text(encoding="utf-8"))
        self.assertIn("ESP|2|import|from sd", store.device_log_file.read_text(encoding="utf-8"))
        self.assertIn("import|from sd", store.time_log_file.read_text(encoding="utf-8"))

    def test_replace_import_preserves_backup_and_settings_status(self):
        client, store = self.make_client()
        sd = store.data_dir.parent / "sd2"
        sd.mkdir(parents=True)
        (sd / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "R|part|Replace|QR|||1|||0|2026\n",
            encoding="utf-8",
        )
        result = client.post("/api/desktop/sd/import", json={"path": str(sd), "mode": "backup_replace"}).get_json()
        self.assertTrue((store.backups_dir / result["backup"]).exists())
        self.assertEqual(result["items_imported"], 1)
        status = client.get("/api/status").get_json()
        self.assertIn("network_url", status)
        self.assertIn("auto_run_enabled", status)

    def test_backup_zip_export_import_and_labels_page(self):
        client, store = self.make_client()
        client.post("/api/desktop/settings", json={"selected_lan_ip": "192.168.1.55", "configured_network_base_url": "http://192.168.1.55:8787"})
        client.post("/api/items/add", json={"id": "LBL", "part_name": "Label Item", "qty": "1"})
        backup = client.post("/api/desktop/backup").get_json()
        self.assertTrue((store.backups_dir / backup["backup"]).exists())
        download = client.get("/api/desktop/backup/download", query_string={"name": backup["backup"]})
        self.assertEqual(download.status_code, 200)
        backup_bytes = download.get_data()
        download.close()
        restore = client.post(
            "/api/desktop/backup/import",
            data={"backup": (io.BytesIO(backup_bytes), "restore.zip")},
            content_type="multipart/form-data",
        )
        self.assertEqual(restore.status_code, 200)
        labels = client.get("/labels").get_data(as_text=True)
        self.assertIn("/api/qr.svg?data=", labels)
        self.assertIn("Stingray QR Labels", labels)

    def test_backup_zip_handles_esp32_fat_timestamps_before_1980(self):
        client, store = self.make_client()
        client.post("/api/items/add", json={"id": "OLDTIME", "part_name": "Old Timestamp", "qty": "1"})
        old_timestamp = 315507600
        for path in (store.inventory_file, store.transaction_file):
            os.utime(path, (old_timestamp, old_timestamp))

        response = client.post("/api/desktop/backup")
        self.assertEqual(response.status_code, 200)
        backup_path = store.backups_dir / response.get_json()["backup"]
        with ZipFile(backup_path) as zf:
            names = zf.namelist()
            inventory_info = zf.getinfo("inventory.csv")

        self.assertIn("inventory.csv", names)
        self.assertIn("transactions.csv", names)
        self.assertGreaterEqual(inventory_info.date_time[0], 1980)

    def test_backup_zip_is_portable_and_does_not_restore_desktop_network_settings(self):
        client, store = self.make_client()
        client.post("/api/desktop/settings", json={"selected_lan_ip": "192.168.1.55", "configured_network_base_url": "http://192.168.1.55:8787"})
        client.post("/api/items/add", json={"id": "PORT", "part_name": "Portable", "qty": "1"})

        backup = client.post("/api/desktop/backup").get_json()
        backup_path = store.backups_dir / backup["backup"]
        with ZipFile(backup_path) as zf:
            names = set(zf.namelist())

        self.assertNotIn("desktop_config.json", names)
        self.assertIn("inventory.csv", names)
        self.assertIn("orders.json", names)

        store.app_config.selected_lan_ip = "10.0.0.5"
        store.app_config.configured_network_base_url = "http://10.0.0.5:8787"
        store._save_app_config()
        restore = client.post(
            "/api/desktop/backup/import",
            data={"backup": (io.BytesIO(backup_path.read_bytes()), backup_path.name)},
            content_type="multipart/form-data",
        ).get_json()

        status = client.get("/api/status").get_json()
        self.assertTrue(restore["ok"])
        self.assertEqual(status["selected_lan_ip"], "10.0.0.5")
        self.assertEqual(status["network_url"], "http://10.0.0.5:8787")

    def test_existing_ui_gets_desktop_settings_and_item_edit_controls(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "UI1", "part_name": "UI Item", "qty": "1"})
        settings_html = client.get("/settings").get_data(as_text=True)
        inventory_html = client.get("/").get_data(as_text=True)
        item_html = client.get("/item?id=UI1").get_data(as_text=True)
        self.assertIn("desktop-lan-ip", settings_html)
        self.assertIn("settings-nav-link", settings_html)
        self.assertIn("exactOpen(value)", inventory_html)
        self.assertIn("desktop-edit-panel", item_html)
        self.assertIn("manual-panel", item_html)

    def test_settings_autorun_toggle_and_system_api(self):
        client, _ = self.make_client()
        settings_html = client.get("/settings").get_data(as_text=True)
        self.assertIn('id="desktop-auto-toggle"', settings_html)

        with patch("stingray_desktop_app.set_windows_autorun", return_value=(True, "ok")) as mocked:
            response = client.post("/api/desktop/system", json={"action": "set_auto", "enabled": False})
        self.assertEqual(response.status_code, 200)
        mocked.assert_called_once_with(False)
        self.assertIn("disabled", response.get_json()["message"].lower())

        with patch("stingray_desktop_app.set_windows_autorun", return_value=(False, "denied")) as mocked:
            response = client.post("/api/desktop/system", json={"action": "set_auto", "enabled": True})
        self.assertEqual(response.status_code, 500)
        mocked.assert_called_once_with(True)
        self.assertIn("could not enable", response.get_json()["message"].lower())


if __name__ == "__main__":
    unittest.main()
