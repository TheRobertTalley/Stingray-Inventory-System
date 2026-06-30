from __future__ import annotations

import io
import os
import sys
import tempfile
import unittest
import time
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile
from werkzeug.datastructures import MultiDict

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

    def test_sync_qr_updates_the_stored_qr_code_to_current_lan_url(self):
        client, _ = self.make_client()
        client.post("/api/desktop/settings", json={"selected_lan_ip": "192.168.1.50", "configured_network_base_url": "http://192.168.1.50:8787"})
        client.post("/api/items/add", json={"id": "SYNC1", "part_name": "Sync Item", "qty": "1", "qr_code": "old-qr"})
        response = client.post("/api/items/sync-qr", json={"id": "SYNC1"})
        self.assertEqual(response.status_code, 200)
        item = response.get_json()["item"]
        self.assertEqual(item["qr_code"], "http://192.168.1.50:8787/item?id=SYNC1")
        self.assertEqual(item["qr_link"], "http://192.168.1.50:8787/item?id=SYNC1")

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

    def test_kit_creation_links_parts_and_subkits_with_recursive_stock_updates(self):
        client, _ = self.make_client()
        self.assertEqual(client.post("/api/items/add", json={"id": "BASE", "part_name": "Base Part", "qty": "50"}).status_code, 201)
        self.assertEqual(client.post("/api/items/add", json={"id": "SUBPART", "part_name": "Sub Part", "qty": "50"}).status_code, 201)
        self.assertEqual(
            client.post(
                "/api/items/add",
                json={"id": "SUBKIT", "category": "kit", "part_name": "Sub Kit", "qty": "0", "components": "SUBPART|2"},
            ).status_code,
            201,
        )
        kit = client.post(
            "/api/items/add",
            json={"id": "KIT", "category": "kit", "part_name": "Main Kit", "qty": "2", "components": "BASE|3\nSUBKIT|1"},
        )
        self.assertEqual(kit.status_code, 201)

        kit_payload = client.get("/api/item?id=KIT").get_json()
        self.assertEqual({component["id"] for component in kit_payload["bom_components"]}, {"BASE", "SUBKIT"})
        self.assertEqual(client.get("/api/item?id=BASE").get_json()["item"]["qty"], 44)
        self.assertEqual(client.get("/api/item?id=SUBKIT").get_json()["item"]["qty"], 0)
        self.assertEqual(client.get("/api/item?id=SUBPART").get_json()["item"]["qty"], 46)

        adjusted = client.post("/api/items/adjust", json={"id": "KIT", "delta": "-1"})
        self.assertEqual(adjusted.status_code, 200)
        self.assertEqual(client.get("/api/item?id=KIT").get_json()["item"]["qty"], 1)
        self.assertEqual(client.get("/api/item?id=BASE").get_json()["item"]["qty"], 47)
        self.assertEqual(client.get("/api/item?id=SUBKIT").get_json()["item"]["qty"], 0)
        self.assertEqual(client.get("/api/item?id=SUBPART").get_json()["item"]["qty"], 48)

    def test_product_creation_links_parts_and_subkits_with_recursive_stock_updates(self):
        client, _ = self.make_client()
        self.assertEqual(client.post("/api/items/add", json={"id": "BASE", "part_name": "Base Part", "qty": "50"}).status_code, 201)
        self.assertEqual(client.post("/api/items/add", json={"id": "SUBPART", "part_name": "Sub Part", "qty": "50"}).status_code, 201)
        self.assertEqual(
            client.post(
                "/api/items/add",
                json={"id": "SUBKIT", "category": "kit", "part_name": "Sub Kit", "qty": "0", "components": "SUBPART|2"},
            ).status_code,
            201,
        )
        product = client.post(
            "/api/items/add",
            json={"id": "PROD", "category": "product", "part_name": "Main Product", "qty": "2", "components": "BASE|3\nSUBKIT|1"},
        )
        self.assertEqual(product.status_code, 201)

        product_payload = client.get("/api/item?id=PROD").get_json()
        self.assertEqual({component["id"] for component in product_payload["bom_components"]}, {"BASE", "SUBKIT"})
        self.assertEqual(client.get("/api/item?id=BASE").get_json()["item"]["qty"], 44)
        self.assertEqual(client.get("/api/item?id=SUBKIT").get_json()["item"]["qty"], 0)
        self.assertEqual(client.get("/api/item?id=SUBPART").get_json()["item"]["qty"], 46)

    def test_shared_part_stays_linked_to_multiple_kits_after_reload(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        firmware_ino = Path(__file__).resolve().parents[1] / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino"
        data_dir = Path(tmp.name) / "ProgramData" / "Inventory" / "data"

        app1, _ = create_app(data_dir, firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        client1 = app1.test_client()
        self.assertEqual(client1.post("/api/items/add", json={"id": "SHARED", "part_name": "Shared Part", "qty": "100"}).status_code, 201)
        self.assertEqual(client1.post("/api/items/add", json={"id": "ONLY1", "part_name": "Only One", "qty": "100"}).status_code, 201)
        self.assertEqual(client1.post("/api/items/add", json={"id": "ONLY2", "part_name": "Only Two", "qty": "100"}).status_code, 201)
        self.assertEqual(
            client1.post(
                "/api/items/add",
                json={"id": "KIT1", "category": "kit", "part_name": "Kit One", "qty": "1", "components": "SHARED|2\nONLY1|1"},
            ).status_code,
            201,
        )
        self.assertEqual(
            client1.post(
                "/api/items/add",
                json={"id": "KIT2", "category": "kit", "part_name": "Kit Two", "qty": "1", "components": "SHARED|3\nONLY2|1"},
            ).status_code,
            201,
        )

        kit1_payload = client1.get("/api/item?id=KIT1").get_json()
        kit2_payload = client1.get("/api/item?id=KIT2").get_json()
        self.assertEqual({component["id"] for component in kit1_payload["bom_components"]}, {"SHARED", "ONLY1"})
        self.assertEqual({component["id"] for component in kit2_payload["bom_components"]}, {"SHARED", "ONLY2"})
        self.assertEqual(client1.get("/api/item?id=SHARED").get_json()["item"]["qty"], 95)

        app2, _ = create_app(data_dir, firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        client2 = app2.test_client()
        kit1_payload = client2.get("/api/item?id=KIT1").get_json()
        kit2_payload = client2.get("/api/item?id=KIT2").get_json()
        self.assertEqual({component["id"] for component in kit1_payload["bom_components"]}, {"SHARED", "ONLY1"})
        self.assertEqual({component["id"] for component in kit2_payload["bom_components"]}, {"SHARED", "ONLY2"})
        self.assertEqual(client2.get("/api/item?id=SHARED").get_json()["item"]["qty"], 95)

        adjusted = client2.post("/api/items/adjust", json={"id": "KIT1", "delta": "-1"})
        self.assertEqual(adjusted.status_code, 200)
        self.assertEqual(client2.get("/api/item?id=SHARED").get_json()["item"]["qty"], 97)
        self.assertEqual({component["id"] for component in client2.get("/api/item?id=KIT2").get_json()["bom_components"]}, {"SHARED", "ONLY2"})

    def test_updating_one_shared_kit_keeps_shared_part_in_other_kit(self):
        client, _ = self.make_client()
        self.assertEqual(client.post("/api/items/add", json={"id": "SHARED", "part_name": "Shared Part", "qty": "100"}).status_code, 201)
        self.assertEqual(client.post("/api/items/add", json={"id": "ONLY1", "part_name": "Only One", "qty": "100"}).status_code, 201)
        self.assertEqual(client.post("/api/items/add", json={"id": "ONLY2", "part_name": "Only Two", "qty": "100"}).status_code, 201)
        self.assertEqual(
            client.post(
                "/api/items/add",
                json={"id": "KIT1", "category": "kit", "part_name": "Kit One", "qty": "1", "components": "SHARED|2\nONLY1|1"},
            ).status_code,
            201,
        )
        self.assertEqual(
            client.post(
                "/api/items/add",
                json={"id": "KIT2", "category": "kit", "part_name": "Kit Two", "qty": "1", "components": "SHARED|3\nONLY2|1"},
            ).status_code,
            201,
        )

        updated = client.post(
            "/api/items/update",
            json={
                "original_id": "KIT1",
                "id": "KIT1",
                "category": "kit",
                "part_name": "Kit One",
                "qty": "1",
                "components": "SHARED|4\nONLY1|1",
                "bom_qty": "0",
            },
        )
        self.assertEqual(updated.status_code, 200)
        self.assertEqual({component["id"] for component in client.get("/api/item?id=KIT1").get_json()["bom_components"]}, {"SHARED", "ONLY1"})
        self.assertEqual({component["id"] for component in client.get("/api/item?id=KIT2").get_json()["bom_components"]}, {"SHARED", "ONLY2"})
        self.assertEqual(client.get("/api/item?id=SHARED").get_json()["item"]["qty"], 93)

    def test_item_update_replaces_product_bom_components_and_recalculates_linked_stock(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "BASE1", "part_name": "Base 1", "qty": "20"})
        client.post("/api/items/add", json={"id": "BASE2", "part_name": "Base 2", "qty": "30"})
        client.post("/api/items/add", json={"id": "NESTPART", "part_name": "Nested Part", "qty": "40"})
        client.post("/api/items/add", json={"id": "SUBKIT", "category": "kit", "part_name": "Sub Kit", "qty": "0", "components": "NESTPART|2"})
        created = client.post(
            "/api/items/add",
            json={"id": "PROD", "category": "product", "part_name": "Main Product", "qty": "2", "components": "BASE1|3\nSUBKIT|1"},
        )
        self.assertEqual(created.status_code, 201)

        updated = client.post(
            "/api/items/update",
            json={
                "original_id": "PROD",
                "id": "PROD2",
                "category": "product",
                "part_name": "Main Product",
                "qty": "3",
                "components": "BASE2|4\nSUBKIT|2",
                "bom_qty": "0",
            },
        )
        self.assertEqual(updated.status_code, 200)

        product_payload = client.get("/api/item?id=PROD2").get_json()
        self.assertEqual({component["id"] for component in product_payload["bom_components"]}, {"BASE2", "SUBKIT"})
        self.assertEqual(client.get("/api/item?id=PROD2").get_json()["item"]["qty"], 3)

        base1 = client.get("/api/item?id=BASE1").get_json()["item"]
        base2 = client.get("/api/item?id=BASE2").get_json()["item"]
        subkit = client.get("/api/item?id=SUBKIT").get_json()["item"]
        nested = client.get("/api/item?id=NESTPART").get_json()["item"]
        self.assertEqual(base1["qty"], 20)
        self.assertEqual(base1["bom_product"], "")
        self.assertEqual(base1["bom_qty"], 0)
        self.assertEqual(base2["qty"], 18)
        self.assertEqual(base2["bom_product"], "PROD2")
        self.assertEqual(base2["bom_qty"], 4)
        self.assertEqual(subkit["qty"], 0)
        self.assertEqual(subkit["bom_product"], "PROD2")
        self.assertEqual(subkit["bom_qty"], 2)
        self.assertEqual(nested["qty"], 28)

    def test_order_fulfillment_deducts_linked_kit_components(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "PART", "part_name": "Part", "qty": "20"})
        client.post("/api/items/add", json={"id": "KIT", "category": "kit", "part_name": "Kit", "qty": "2", "components": "PART|2"})

        fulfilled = client.post(
            "/api/orders/fulfill",
            json={"order_number": "ORD-KIT", "plan": "KIT|1", "orders_payload": '{"orders":[]}'},
        )
        self.assertEqual(fulfilled.status_code, 200)
        self.assertEqual(fulfilled.get_json()["units_removed"], 1)
        self.assertEqual(client.get("/api/item?id=KIT").get_json()["item"]["qty"], 1)
        self.assertEqual(client.get("/api/item?id=PART").get_json()["item"]["qty"], 14)

    def test_order_fulfillment_deducts_nested_product_components(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "PARTA", "part_name": "Part A", "qty": "80"})
        client.post("/api/items/add", json={"id": "PARTB", "part_name": "Part B", "qty": "60"})
        client.post("/api/items/add", json={"id": "PARTC", "part_name": "Part C", "qty": "100"})
        client.post("/api/items/add", json={"id": "SUBKIT", "category": "kit", "part_name": "Sub Kit", "qty": "0", "components": "PARTC|2"})
        client.post(
            "/api/items/add",
            json={"id": "KITALPHA", "category": "kit", "part_name": "Kit Alpha", "qty": "3", "components": "PARTA|4\nSUBKIT|1"},
        )
        client.post(
            "/api/items/add",
            json={"id": "PROD", "category": "product", "part_name": "Product", "qty": "2", "components": "KITALPHA|1\nPARTB|5"},
        )

        fulfilled = client.post(
            "/api/orders/fulfill",
            json={"order_number": "ORD-PROD", "plan": "PROD|1", "orders_payload": '{"orders":[]}'},
        )
        self.assertEqual(fulfilled.status_code, 200)
        self.assertEqual(fulfilled.get_json()["units_removed"], 1)
        self.assertEqual(client.get("/api/item?id=PROD").get_json()["item"]["qty"], 1)
        self.assertEqual(client.get("/api/item?id=KITALPHA").get_json()["item"]["qty"], 3)
        self.assertEqual(client.get("/api/item?id=PARTA").get_json()["item"]["qty"], 56)
        self.assertEqual(client.get("/api/item?id=PARTB").get_json()["item"]["qty"], 45)
        self.assertEqual(client.get("/api/item?id=PARTC").get_json()["item"]["qty"], 88)

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

    def test_inventory_folder_import_handles_desktop_legacy_folder(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        home = Path(tmp.name)
        legacy_root = home / "Desktop" / "old inventory"
        legacy_images = legacy_root / "images"
        legacy_ui = legacy_root / "ui"
        legacy_images.mkdir(parents=True, exist_ok=True)
        legacy_ui.mkdir(parents=True, exist_ok=True)
        (legacy_root / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "DESK1|part|Desktop Item|QR|||7|/images/desktop.png||0|2026\n",
            encoding="utf-8",
        )
        (legacy_root / "orders.json").write_text('{"orders":[{"order_number":"DESK-ORD","created_at":"2026","updated_at":"2026","lines":[]}]}', encoding="utf-8")
        (legacy_root / "transactions.csv").write_text("timestamp|item_id|action|delta|qty_after|note\n2026|DESK1|import|7|7|desktop\n", encoding="utf-8")
        (legacy_root / "device_log.csv").write_text("timestamp|mac_address|uptime_seconds|event|detail\n2026|PC|1|import|desktop\n", encoding="utf-8")
        (legacy_root / "time_log.csv").write_text("timestamp|event|detail\n2026|import|desktop\n", encoding="utf-8")
        (legacy_images / "desktop.png").write_bytes(b"desktop")
        (legacy_ui / "index.html").write_text("<html>legacy ui</html>", encoding="utf-8")

        with patch("stingray_desktop_app.Path.home", return_value=home):
            client, store = self.make_client()
            preview = client.get("/api/desktop/sd/preview", query_string={"path": str(legacy_root)}).get_json()
            self.assertEqual(Path(preview["import_root"]), legacy_root)
            self.assertEqual(preview["inventory_items_found"], 1)
            self.assertIn("inventory.csv", preview["files_found"])

            result = client.post("/api/desktop/sd/import", json={"path": str(legacy_root), "mode": "merge"}).get_json()
            self.assertEqual(result["items_imported"], 1)
            self.assertEqual(result["orders_added"], 1)
            self.assertTrue((store.images_dir / "desktop.png").exists())
            imported = client.get("/api/item?id=DESK1").get_json()["item"]
            self.assertEqual(imported["qty"], 7)

    def test_import_suggestions_detect_desktop_old_inventory_folder(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        home = Path(tmp.name)
        legacy_root = home / "Desktop" / "old inventory"
        (legacy_root / "images").mkdir(parents=True, exist_ok=True)
        (legacy_root / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "SUG1|part|Suggested Item|QR|||1|||0|2026\n",
            encoding="utf-8",
        )

        with patch("stingray_desktop_app.Path.home", return_value=home):
            client, _ = self.make_client()
            suggestions = client.get("/api/desktop/import/suggestions").get_json()

        self.assertGreaterEqual(len(suggestions["suggestions"]), 1)
        self.assertEqual(Path(suggestions["suggestions"][0]["path"]), legacy_root)
        self.assertIn("old inventory", suggestions["suggestions"][0]["label"].lower())

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
        health = client.get("/api/desktop/health").get_json()
        self.assertEqual(status["app_name"], "Inventory")
        self.assertIn("network_url", status)
        self.assertIn("auto_run_enabled", status)
        self.assertIn("firewall_rule_name", status)
        self.assertIn("auto_run_task_name", status)
        self.assertNotIn("health", status)
        self.assertIn("overall_ok", health)
        self.assertIn("checks", health)

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

    def test_legacy_programdata_tree_migrates_into_inventory_layout(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        programdata = Path(tmp.name) / "ProgramData"
        legacy_data = programdata / "StingrayInventoryDesktop" / "data"
        new_data = programdata / "Inventory" / "data"
        legacy_data.mkdir(parents=True)
        (legacy_data / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "LEG1|part|Legacy Item|QR|||4|||0|2026\n",
            encoding="utf-8",
        )
        (legacy_data / "desktop_config.json").write_text(
            '{\n  "bind_host": "0.0.0.0",\n  "port": 8787,\n  "selected_lan_ip": "192.168.1.77",\n  "configured_network_base_url": "http://192.168.1.77:8787",\n  "updated_at": "2026-01-01T00:00:00Z"\n}\n',
            encoding="utf-8",
        )
        (legacy_data / "cloud_backup.cfg").write_text(
            "provider|login_email|folder_name|folder_hint|mode|backup_mode|asset_mode|brand_name|brand_logo_ref|client_id|client_secret|updated_at\n"
            "google_drive||||select_or_create|sd_only|sd_only|Legacy Brand||||2026\n",
            encoding="utf-8",
        )

        app, store = create_app(new_data, firmware_ino=Path(__file__).resolve().parents[1] / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino", bind_host="0.0.0.0", port=8787)
        client = app.test_client()

        item = client.get("/api/item?id=LEG1").get_json()["item"]
        status = client.get("/api/status").get_json()

        self.assertEqual(item["qty"], 4)
        self.assertEqual(status["selected_lan_ip"], "192.168.1.77")
        self.assertEqual(status["data_dir"], str(new_data))
        self.assertEqual(status["logs_dir"], str(store.logs_dir))
        self.assertEqual(status["config_dir"], str(store.config_dir))
        self.assertTrue((store.inventory_file).exists())
        self.assertTrue((store.app_config_file).exists())
        self.assertTrue((store.cloud_config_file).exists())
        self.assertTrue((store.logs_dir / "device_log.csv").exists())

    def test_inventory_changes_autosave_and_recover_after_restart(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        firmware_ino = Path(__file__).resolve().parents[1] / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino"
        data_dir = Path(tmp.name) / "ProgramData" / "Inventory" / "data"

        app1, store1 = create_app(data_dir, firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        client1 = app1.test_client()
        add = client1.post("/api/items/add", json={"id": "SAVE1", "part_name": "Saved Item", "qty": "3"})
        self.assertEqual(add.status_code, 201)
        self.assertTrue(store1.inventory_file.exists())
        self.assertTrue(store1.state_journal_file.exists())
        self.assertTrue(store1.state_snapshot_file.exists())
        self.assertIn("SAVE1", store1.inventory_file.read_text(encoding="utf-8"))

        store1.inventory_file.unlink()
        store1.state_snapshot_file.unlink()
        if store1.orders_file.exists():
            store1.orders_file.unlink()

        app2, store2 = create_app(data_dir, firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        client2 = app2.test_client()
        item = client2.get("/api/item?id=SAVE1").get_json()["item"]
        status = client2.get("/api/status").get_json()

        self.assertEqual(item["qty"], 3)
        self.assertTrue(store2.inventory_file.exists())
        self.assertTrue(status["state_snapshot_available"])
        self.assertTrue(status["state_journal_available"])
        self.assertTrue(status["last_saved_at"])
        self.assertIn(status["last_saved_reason"], {"inventory", "journal_recovery"})

    def test_bom_inventory_and_linked_counts_persist_after_restart(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        firmware_ino = Path(__file__).resolve().parents[1] / "firmware" / "StingrayInventoryESP32" / "StingrayInventoryESP32.ino"
        data_dir = Path(tmp.name) / "ProgramData" / "Inventory" / "data"

        app1, _ = create_app(data_dir, firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        client1 = app1.test_client()
        self.assertEqual(client1.post("/api/items/add", json={"id": "PARTA", "part_name": "Part A", "qty": "10"}).status_code, 201)
        self.assertEqual(client1.post("/api/items/add", json={"id": "PARTB", "part_name": "Part B", "qty": "20"}).status_code, 201)
        self.assertEqual(
            client1.post(
                "/api/items/add",
                json={"id": "KIT1", "category": "kit", "part_name": "Kit One", "qty": "1", "components": "PARTA|2"},
            ).status_code,
            201,
        )
        self.assertEqual(
            client1.post(
                "/api/items/add",
                json={"id": "PROD1", "category": "product", "part_name": "Product One", "qty": "3", "components": "PARTB|4\nKIT1|1"},
            ).status_code,
            201,
        )

        app2, _ = create_app(data_dir, firmware_ino=firmware_ino, bind_host="0.0.0.0", port=8787)
        client2 = app2.test_client()

        product_payload = client2.get("/api/item?id=PROD1").get_json()
        self.assertEqual({component["id"] for component in product_payload["bom_components"]}, {"PARTB", "KIT1"})
        self.assertEqual(client2.get("/api/item?id=PROD1").get_json()["item"]["qty"], 3)
        self.assertEqual(client2.get("/api/item?id=KIT1").get_json()["item"]["qty"], 1)
        self.assertEqual(client2.get("/api/item?id=PARTA").get_json()["item"]["qty"], 2)
        self.assertEqual(client2.get("/api/item?id=PARTB").get_json()["item"]["qty"], 8)

        adjusted = client2.post("/api/items/adjust", json={"id": "PROD1", "delta": "-1"})
        self.assertEqual(adjusted.status_code, 200)
        self.assertEqual(client2.get("/api/item?id=PROD1").get_json()["item"]["qty"], 2)
        self.assertEqual(client2.get("/api/item?id=KIT1").get_json()["item"]["qty"], 1)
        self.assertEqual(client2.get("/api/item?id=PARTA").get_json()["item"]["qty"], 4)
        self.assertEqual(client2.get("/api/item?id=PARTB").get_json()["item"]["qty"], 12)

    def test_existing_ui_gets_desktop_settings_and_item_edit_controls(self):
        client, _ = self.make_client()
        client.post("/api/items/add", json={"id": "UI1", "part_name": "UI Item", "qty": "1"})
        settings_html = client.get("/settings").get_data(as_text=True)
        inventory_html = client.get("/").get_data(as_text=True)
        import_html = client.get("/import-folder").get_data(as_text=True)
        compat_import_html = client.get("/settings?view=import").get_data(as_text=True)
        item_html = client.get("/item?id=UI1").get_data(as_text=True)
        self.assertIn("desktop-lan-ip", settings_html)
        self.assertIn("Ethernet uplinks are fine", settings_html)
        self.assertNotIn("Nearby Networks", settings_html)
        self.assertNotIn("scan networks", settings_html.lower())
        self.assertIn("desktop-folder-picker-btn", settings_html)
        self.assertIn("desktop-folder-dropzone", settings_html)
        self.assertIn("webkitdirectory", settings_html)
        self.assertIn("Choose Folder...", settings_html)
        self.assertIn("settings-nav-link", settings_html)
        self.assertIn('</section>\n\n      <section class="info-panel" id="desktop-import-panel">', settings_html)
        self.assertLess(settings_html.index('desktop-import-panel'), settings_html.index('desktop-lan-panel'))
        self.assertIn('Import Inventory Folder', import_html)
        self.assertIn('Import Inventory Folder', compat_import_html)
        self.assertIn('Import workflow', import_html)
        self.assertIn('Select a folder', import_html)
        self.assertNotIn('Preview Folder Import', import_html)
        self.assertIn('status-panel is-empty', import_html)
        self.assertIn('desktop-folder-picker-btn', import_html)
        self.assertIn('desktop-import-sd-btn', import_html)
        self.assertIn('desktop-import-panel', import_html)
        self.assertNotIn('Desktop LAN Access', import_html)
        self.assertNotIn('desktop-lan-panel', import_html)
        self.assertIn("Inventory Actions", inventory_html)
        self.assertIn('id="desktop-import-link"', inventory_html)
        self.assertIn("Import Inventory Folder", inventory_html)
        self.assertIn("/import-folder", inventory_html)
        self.assertIn("Add Kit", inventory_html)
        self.assertIn("Add Product", inventory_html)
        self.assertIn("Paste existing part or kit number", inventory_html)
        self.assertIn("Product / kit components", inventory_html)
        self.assertIn("data-draft-component-select", inventory_html)
        self.assertIn("Add Component", inventory_html)
        self.assertIn("exactOpen(value)", inventory_html)
        self.assertIn("desktop-edit-panel", item_html)
        self.assertIn('<select id="desktop-edit-bom-product">', item_html)
        self.assertIn("desktop-edit-components-panel", item_html)
        self.assertIn('id="desktop-edit-component-select"', item_html)
        self.assertIn("desktop-sync-qr-btn", item_html)
        self.assertIn("Sync QR to Current URL", item_html)
        self.assertIn("manual-panel", item_html)
        self.assertIn("desktop-save-badge", item_html)
        self.assertIn("desktop-main-save-badge", item_html)
        self.assertIn("[hidden]", item_html)
        self.assertIn("#desktop-edit-panel", item_html)
        self.assertIn("order: -1", item_html)

    def test_launcher_opens_inventory_screen_by_default(self):
        client, store = self.make_client()
        store.app_config.setup_complete = True
        with patch("stingray_desktop_app.probe_url", return_value=(True, "ok")), patch("stingray_desktop_app.webbrowser.open") as open_mock:
            from stingray_desktop_app import open_browser_when_ready

            open_browser_when_ready(store)
            time.sleep(0.2)

        open_mock.assert_called()
        self.assertTrue(any(call.args and call.args[0].endswith("/") for call in open_mock.call_args_list))

    def test_folder_picker_upload_stages_and_imports_inventory_folder(self):
        client, store = self.make_client()
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name) / "Desktop" / "old inventory"
        images = root / "images"
        ui = root / "ui"
        images.mkdir(parents=True)
        ui.mkdir(parents=True)
        (root / "inventory.csv").write_text(
            "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at\n"
            "PICK1|part|Picker Item|QR|||8|/images/picker.png||0|2026\n",
            encoding="utf-8",
        )
        (root / "orders.json").write_text('{"orders":[{"order_number":"PICK-1","created_at":"2026","updated_at":"2026","lines":[]}]}', encoding="utf-8")
        (root / "transactions.csv").write_text("timestamp|item_id|action|delta|qty_after|note\n2026|PICK1|import|8|8|picker\n", encoding="utf-8")
        (images / "picker.png").write_bytes(b"picker")
        (ui / "index.html").write_text("<html>picker</html>", encoding="utf-8")

        uploads = []
        for path in root.rglob("*"):
            if path.is_file():
                uploads.append(("files", (io.BytesIO(path.read_bytes()), path.relative_to(Path(tmp.name)).as_posix())))

        upload = client.post(
            "/api/desktop/import/upload",
            data=MultiDict([("source_label", "Desktop old inventory"), *uploads]),
            content_type="multipart/form-data",
        )
        self.assertEqual(upload.status_code, 200)
        staged = upload.get_json()
        self.assertIn("token", staged)
        self.assertEqual(staged["preview"]["inventory_items_found"], 1)
        self.assertEqual(staged["preview"]["orders_found"], 1)
        self.assertEqual(staged["preview"]["images_found"], 1)
        self.assertNotIn("Preview Folder Import", client.get("/import-folder").get_data(as_text=True))

        imported = client.post("/api/desktop/sd/import", json={"token": staged["token"], "mode": "backup_replace"})
        self.assertEqual(imported.status_code, 200)
        result = imported.get_json()
        self.assertEqual(result["items_imported"], 1)
        self.assertEqual(result["orders_added"], 1)
        self.assertTrue((store.images_dir / "picker.png").exists())
        item = client.get("/api/item?id=PICK1").get_json()["item"]
        self.assertEqual(item["qty"], 8)

    def test_desktop_status_reports_lan_transport_not_wifi(self):
        client, _ = self.make_client()
        status = client.get("/api/status").get_json()
        self.assertEqual(status["network_transport"], "LAN/Ethernet")
        self.assertIn("lan_connected", status)
        self.assertIn("lan_ip", status)
        self.assertNotIn("wifi_connected", status)
        self.assertNotIn("wifi_ssid", status)

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

    def test_first_run_setup_wizard_and_admin_pin_flow(self):
        client, _ = self.make_client()

        setup_html = client.get("/setup").get_data(as_text=True)
        self.assertIn("desktop-setup-wizard", setup_html)
        self.assertIn("First-Run Setup", setup_html)

        setup = client.post(
            "/api/desktop/setup",
            json={
                "selected_lan_ip": "192.168.1.77",
                "configured_network_base_url": "http://192.168.1.77:8787",
                "admin_pin": "2468",
                "admin_pin_confirm": "2468",
            },
        )
        self.assertEqual(setup.status_code, 200)
        payload = setup.get_json()
        self.assertTrue(payload["status"]["setup_complete"])
        self.assertTrue(payload["status"]["admin_pin_configured"])
        self.assertTrue(payload["status"]["admin_unlocked"])

        locked = client.post("/api/desktop/admin", json={"action": "lock"})
        self.assertEqual(locked.status_code, 200)

        locked_settings = client.post(
            "/api/desktop/settings",
            json={
                "selected_lan_ip": "192.168.1.88",
                "configured_network_base_url": "http://192.168.1.88:8787",
            },
        )
        self.assertEqual(locked_settings.status_code, 403)

        denied = client.post("/api/desktop/admin", json={"action": "unlock", "pin": "9999"})
        self.assertEqual(denied.status_code, 403)

        unlocked = client.post("/api/desktop/admin", json={"action": "unlock", "pin": "2468"})
        self.assertEqual(unlocked.status_code, 200)
        self.assertIn("unlocked", unlocked.get_json()["message"].lower())

        unlocked_settings = client.post(
            "/api/desktop/settings",
            json={
                "selected_lan_ip": "192.168.1.88",
                "configured_network_base_url": "http://192.168.1.88:8787",
            },
        )
        self.assertEqual(unlocked_settings.status_code, 200)

        health = client.get("/api/desktop/health")
        self.assertEqual(health.status_code, 200)
        health_payload = health.get_json()
        self.assertIn("checks", health_payload)
        self.assertIn("lan_url", health_payload["checks"])
        self.assertIn("firewall_rule", health_payload["checks"])


if __name__ == "__main__":
    unittest.main()
