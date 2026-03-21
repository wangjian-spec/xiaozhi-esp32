from __future__ import annotations

import importlib
import json
import sqlite3
import subprocess
import sys
from collections import defaultdict
from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


def _ensure_dependency(module_name: str, package_name: str | None = None) -> object:
    package = package_name or module_name
    try:
        return importlib.import_module(module_name)
    except ModuleNotFoundError:
        print(f"缺少依赖 {package}，正在尝试自动安装...", flush=True)
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", package],
            stdout=sys.stdout,
            stderr=sys.stderr,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"安装依赖 {package} 失败，请手动执行: {sys.executable} -m pip install {package}"
            ) from None
        return importlib.import_module(module_name)


_ensure_dependency("PySide6.QtCore", "PySide6")

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QGroupBox,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


DEFAULT_DB_PATH = Path(__file__).resolve().parent / "words.db"
DEFAULT_JSON_PATH = Path(__file__).resolve().parent / "words_export.json"
RELATED_TABLES = ("word", "word_meaning", "word_form", "word_example")
LIST_TABLES = {"word_form", "word_example"}
INSERT_ORDER = ("word", "word_meaning", "word_form", "word_example")
DROP_ORDER = ("word_example", "word_form", "word_meaning", "word")
SQLITE_VARIABLE_BATCH_SIZE = 900


@dataclass(slots=True)
class SchemaTable:
    name: str
    columns: list[dict[str, Any]]
    create_sql: str
    indexes: list[str]
    foreign_keys: list[dict[str, Any]]

    @property
    def column_names(self) -> list[str]:
        return [str(column["name"]) for column in self.columns]


class WordsJsonService:
    def export_database(self, db_path: Path, json_path: Path) -> dict[str, Any]:
        if not db_path.exists():
            raise FileNotFoundError(f"数据库不存在: {db_path}")

        schema = self.read_schema(db_path)
        records = self._load_related_records(db_path)
        payload = {
            "metadata": {
                "source_database": str(db_path),
                "exported_at": datetime.now().isoformat(timespec="seconds"),
                "record_count": len(records),
                "root_table": "word_meaning",
                "related_tables": list(RELATED_TABLES),
            },
            "schema": {
                table_name: {
                    "columns": table.columns,
                    "create_sql": table.create_sql,
                    "indexes": table.indexes,
                    "foreign_keys": table.foreign_keys,
                }
                for table_name, table in schema.items()
            },
            "records": records,
        }
        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        return payload

    def load_json(self, json_path: Path) -> dict[str, Any]:
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        payload = json.loads(json_path.read_text(encoding="utf-8"))
        if isinstance(payload, list):
            payload = {"metadata": {}, "schema": {}, "records": payload}

        records = payload.get("records")
        if not isinstance(records, list):
            raise ValueError("JSON 中缺少 records 数组")

        payload.setdefault("metadata", {})
        payload.setdefault("schema", {})
        payload["records"] = records
        return payload

    def load_json_files(self, json_paths: list[Path]) -> dict[str, Any]:
        if not json_paths:
            raise ValueError("请至少选择一个 JSON 文件")

        payloads = [self.load_json(json_path) for json_path in json_paths]
        return self.merge_payloads(payloads, json_paths)

    def merge_payloads(
        self,
        payloads: list[dict[str, Any]],
        source_paths: list[Path] | None = None,
    ) -> dict[str, Any]:
        if not payloads:
            raise ValueError("没有可合并的 JSON 内容")

        merged_records: list[dict[str, Any]] = []
        merged_schema: dict[str, Any] = {}
        schema_signature: str | None = None
        merged_metadata = deepcopy(payloads[0].get("metadata") or {})
        if not isinstance(merged_metadata, dict):
            merged_metadata = {}

        for index, payload in enumerate(payloads, start=1):
            payload_records = payload.get("records")
            if not isinstance(payload_records, list):
                raise ValueError(f"第 {index} 个 JSON 缺少 records 数组")

            payload_schema = payload.get("schema")
            if isinstance(payload_schema, dict) and payload_schema:
                current_signature = json.dumps(payload_schema, ensure_ascii=False, sort_keys=True)
                if schema_signature is None:
                    schema_signature = current_signature
                    merged_schema = deepcopy(payload_schema)
                elif current_signature != schema_signature:
                    source_label = str(source_paths[index - 1]) if source_paths else f"第 {index} 个 JSON"
                    raise ValueError(f"JSON schema 不一致，无法合并导入: {source_label}")

            merged_records.extend(deepcopy(payload_records))

        merged_metadata["merged_at"] = datetime.now().isoformat(timespec="seconds")
        merged_metadata["record_count"] = len(merged_records)
        merged_metadata["source_count"] = len(payloads)
        if source_paths:
            merged_metadata["source_files"] = [str(path) for path in source_paths]

        return {
            "metadata": merged_metadata,
            "schema": merged_schema,
            "records": merged_records,
        }

    def save_json(self, json_path: Path, payload: dict[str, Any]) -> None:
        payload.setdefault("metadata", {})
        payload["metadata"]["saved_at"] = datetime.now().isoformat(timespec="seconds")
        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def import_json_to_database(self, payload: dict[str, Any], output_db_path: Path) -> dict[str, int]:
        records = payload.get("records")
        if not isinstance(records, list):
            raise ValueError("JSON 中缺少 records 数组")

        schema = self._schema_from_payload(payload, output_db_path)
        output_db_path.parent.mkdir(parents=True, exist_ok=True)

        with sqlite3.connect(output_db_path) as conn:
            conn.execute("PRAGMA foreign_keys = ON")
            self._ensure_schema(conn, schema)
            normalized_rows = self._normalize_rows_for_import(records, self._read_max_ids(conn))

            self._upsert_rows(conn, schema["word"], normalized_rows["word"])
            self._upsert_rows(conn, schema["word_meaning"], normalized_rows["word_meaning"])

            affected_word_ids = [row["id"] for row in normalized_rows["word"] if row.get("id") is not None]
            affected_meaning_ids = [
                row["id"] for row in normalized_rows["word_meaning"] if row.get("id") is not None
            ]
            self._delete_rows_for_parent_ids(conn, "word_form", "word_id", affected_word_ids)
            self._delete_rows_for_parent_ids(conn, "word_example", "meaning_id", affected_meaning_ids)
            self._insert_rows(conn, schema["word_form"], normalized_rows["word_form"])
            self._insert_rows(conn, schema["word_example"], normalized_rows["word_example"])

            row_counts: dict[str, int] = {}
            for table_name in INSERT_ORDER:
                row_counts[table_name] = len(normalized_rows[table_name])

            conn.commit()

        return row_counts

    def read_schema(self, db_path: Path) -> dict[str, SchemaTable]:
        with sqlite3.connect(db_path) as conn:
            conn.row_factory = sqlite3.Row
            schema: dict[str, SchemaTable] = {}
            for table_name in RELATED_TABLES:
                create_row = conn.execute(
                    "SELECT sql FROM sqlite_master WHERE type='table' AND name = ?",
                    (table_name,),
                ).fetchone()
                if create_row is None or not create_row["sql"]:
                    raise ValueError(f"数据库中缺少表定义: {table_name}")

                columns = [
                    dict(column)
                    for column in conn.execute(f'PRAGMA table_info("{table_name}")').fetchall()
                ]
                foreign_keys = [
                    dict(foreign_key)
                    for foreign_key in conn.execute(f'PRAGMA foreign_key_list("{table_name}")').fetchall()
                ]
                indexes = [
                    row["sql"]
                    for row in conn.execute(
                        """
                        SELECT sql
                        FROM sqlite_master
                        WHERE type = 'index' AND tbl_name = ? AND sql IS NOT NULL
                        ORDER BY name
                        """,
                        (table_name,),
                    ).fetchall()
                ]
                schema[table_name] = SchemaTable(
                    name=table_name,
                    columns=columns,
                    create_sql=str(create_row["sql"]),
                    indexes=[str(index_sql) for index_sql in indexes],
                    foreign_keys=foreign_keys,
                )
            return schema

    def _load_related_records(self, db_path: Path) -> list[dict[str, Any]]:
        with sqlite3.connect(db_path) as conn:
            conn.row_factory = sqlite3.Row

            words = {
                int(row["id"]): dict(row)
                for row in conn.execute("SELECT * FROM word ORDER BY id").fetchall()
            }

            forms_by_word: dict[int, list[dict[str, Any]]] = defaultdict(list)
            for row in conn.execute("SELECT * FROM word_form ORDER BY word_id, id").fetchall():
                word_id = row["word_id"]
                if word_id is None:
                    continue
                forms_by_word[int(word_id)].append(dict(row))

            examples_by_meaning: dict[int, list[dict[str, Any]]] = defaultdict(list)
            for row in conn.execute("SELECT * FROM word_example ORDER BY meaning_id, id").fetchall():
                meaning_id = row["meaning_id"]
                if meaning_id is None:
                    continue
                examples_by_meaning[int(meaning_id)].append(dict(row))

            records: list[dict[str, Any]] = []
            for meaning_row in conn.execute("SELECT * FROM word_meaning ORDER BY id").fetchall():
                meaning = dict(meaning_row)
                word_id = meaning.get("word_id")
                word = words.get(int(word_id)) if word_id is not None else None
                if word is None:
                    continue
                meaning_id = meaning.get("id")
                records.append(
                    {
                        "word": dict(word),
                        "word_meaning": meaning,
                        "word_form": [dict(item) for item in forms_by_word.get(int(word_id), [])],
                        "word_example": [
                            dict(item) for item in examples_by_meaning.get(int(meaning_id), [])
                        ] if meaning_id is not None else [],
                    }
                )
            return records

    def _schema_from_payload(self, payload: dict[str, Any], output_db_path: Path) -> dict[str, SchemaTable]:
        schema_payload = payload.get("schema")
        if isinstance(schema_payload, dict) and all(table in schema_payload for table in RELATED_TABLES):
            result: dict[str, SchemaTable] = {}
            for table_name in RELATED_TABLES:
                table_data = schema_payload.get(table_name) or {}
                columns = table_data.get("columns")
                create_sql = table_data.get("create_sql")
                indexes = table_data.get("indexes") or []
                foreign_keys = table_data.get("foreign_keys") or []
                if not isinstance(columns, list) or not create_sql:
                    raise ValueError(f"JSON schema 中的表定义不完整: {table_name}")
                result[table_name] = SchemaTable(
                    name=table_name,
                    columns=[dict(column) for column in columns],
                    create_sql=str(create_sql),
                    indexes=[str(index_sql) for index_sql in indexes if index_sql],
                    foreign_keys=[dict(foreign_key) for foreign_key in foreign_keys],
                )
            return result

        if output_db_path.exists():
            return self.read_schema(output_db_path)
        if DEFAULT_DB_PATH.exists():
            return self.read_schema(DEFAULT_DB_PATH)
        raise ValueError("JSON 中缺少 schema，且无法从目标数据库或默认 words.db 恢复表结构")

    def _normalize_rows_for_import(
        self,
        records: list[Any],
        existing_max_ids: dict[str, int] | None = None,
    ) -> dict[str, list[dict[str, Any]]]:
        max_ids = dict(existing_max_ids or {table_name: 0 for table_name in RELATED_TABLES})
        for record in records:
            if not isinstance(record, dict):
                continue
            for table_name in ("word", "word_meaning"):
                row = record.get(table_name)
                if isinstance(row, dict):
                    row_id = self._as_int(row.get("id"))
                    if row_id is not None:
                        max_ids[table_name] = max(max_ids[table_name], row_id)
            for table_name in LIST_TABLES:
                for row in record.get(table_name, []):
                    if not isinstance(row, dict):
                        continue
                    row_id = self._as_int(row.get("id"))
                    if row_id is not None:
                        max_ids[table_name] = max(max_ids[table_name], row_id)
        next_ids = {table_name: max_value + 1 for table_name, max_value in max_ids.items()}
        collected: dict[str, dict[Any, dict[str, Any]]] = {table_name: {} for table_name in RELATED_TABLES}
        seen_form_keys: dict[int, set[tuple[Any, ...]]] = defaultdict(set)
        seen_example_keys: dict[int, set[tuple[Any, ...]]] = defaultdict(set)

        for index, record in enumerate(records, start=1):
            if not isinstance(record, dict):
                raise ValueError(f"第 {index} 条 records 不是对象")

            word = dict(record.get("word") or {})
            meaning = dict(record.get("word_meaning") or {})
            if not word:
                raise ValueError(f"第 {index} 条 records 缺少 word")
            if not meaning:
                raise ValueError(f"第 {index} 条 records 缺少 word_meaning")

            word_id = self._ensure_row_id(word, "word", next_ids)
            meaning_id = self._ensure_row_id(meaning, "word_meaning", next_ids)
            meaning["word_id"] = word_id

            collected["word"][word_id] = word
            collected["word_meaning"][meaning_id] = meaning

            for form_row in record.get("word_form", []):
                if not isinstance(form_row, dict):
                    continue
                form = dict(form_row)
                form_id = self._ensure_row_id(form, "word_form", next_ids)
                form["word_id"] = word_id
                form_key = self._child_signature(form, schema_key=("word_form", word_id))
                if form_key in seen_form_keys[word_id]:
                    continue
                seen_form_keys[word_id].add(form_key)
                collected["word_form"][form_id] = form

            for example_row in record.get("word_example", []):
                if not isinstance(example_row, dict):
                    continue
                example = dict(example_row)
                example_id = self._ensure_row_id(example, "word_example", next_ids)
                example["meaning_id"] = meaning_id
                example_key = self._child_signature(example, schema_key=("word_example", meaning_id))
                if example_key in seen_example_keys[meaning_id]:
                    continue
                seen_example_keys[meaning_id].add(example_key)
                collected["word_example"][example_id] = example

        return {
            table_name: list(collected[table_name].values())
            for table_name in INSERT_ORDER
        }

    def _ensure_row_id(
        self,
        row: dict[str, Any],
        table_name: str,
        next_ids: dict[str, int],
    ) -> int:
        row_id = self._as_int(row.get("id"))
        if row_id is None:
            row_id = next_ids[table_name]
            next_ids[table_name] += 1
            row["id"] = row_id
            return row_id
        next_ids[table_name] = max(next_ids[table_name], row_id + 1)
        row["id"] = row_id
        return row_id

    def _insert_rows(
        self,
        conn: sqlite3.Connection,
        schema_table: SchemaTable,
        rows: list[dict[str, Any]],
    ) -> None:
        if not rows:
            return

        column_names = schema_table.column_names
        placeholders = ", ".join("?" for _ in column_names)
        columns_sql = ", ".join(f'"{column_name}"' for column_name in column_names)
        sql = f'INSERT INTO "{schema_table.name}" ({columns_sql}) VALUES ({placeholders})'
        values = [
            tuple(row.get(column_name) for column_name in column_names)
            for row in rows
        ]
        conn.executemany(sql, values)

    def _upsert_rows(
        self,
        conn: sqlite3.Connection,
        schema_table: SchemaTable,
        rows: list[dict[str, Any]],
    ) -> None:
        if not rows:
            return

        column_names = schema_table.column_names
        placeholders = ", ".join("?" for _ in column_names)
        columns_sql = ", ".join(f'"{column_name}"' for column_name in column_names)
        update_columns = [column_name for column_name in column_names if column_name != "id"]
        if update_columns:
            update_sql = ", ".join(
                f'"{column_name}" = excluded."{column_name}"' for column_name in update_columns
            )
            sql = (
                f'INSERT INTO "{schema_table.name}" ({columns_sql}) '
                f'VALUES ({placeholders}) '
                f'ON CONFLICT("id") DO UPDATE SET {update_sql}'
            )
        else:
            sql = (
                f'INSERT INTO "{schema_table.name}" ({columns_sql}) '
                f'VALUES ({placeholders}) '
                'ON CONFLICT("id") DO NOTHING'
            )

        values = [tuple(row.get(column_name) for column_name in column_names) for row in rows]
        conn.executemany(sql, values)

    def _ensure_schema(
        self,
        conn: sqlite3.Connection,
        schema: dict[str, SchemaTable],
    ) -> None:
        existing_tables = {
            str(row[0])
            for row in conn.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            ).fetchall()
        }
        existing_indexes = {
            str(row[0])
            for row in conn.execute(
                "SELECT name FROM sqlite_master WHERE type='index'"
            ).fetchall()
        }

        for table_name in INSERT_ORDER:
            table_schema = schema[table_name]
            if table_name not in existing_tables and table_schema.create_sql:
                conn.execute(table_schema.create_sql)

        for table_name in INSERT_ORDER:
            for index_sql in schema[table_name].indexes:
                if not index_sql:
                    continue
                index_name = self._extract_index_name(index_sql)
                if index_name and index_name in existing_indexes:
                    continue
                conn.execute(index_sql)

    def _read_max_ids(self, conn: sqlite3.Connection) -> dict[str, int]:
        max_ids: dict[str, int] = {}
        for table_name in RELATED_TABLES:
            row = conn.execute(
                f'SELECT COALESCE(MAX(id), 0) FROM "{table_name}"'
            ).fetchone()
            max_ids[table_name] = int(row[0]) if row and row[0] is not None else 0
        return max_ids

    def _delete_rows_for_parent_ids(
        self,
        conn: sqlite3.Connection,
        table_name: str,
        parent_column: str,
        parent_ids: list[Any],
    ) -> None:
        unique_ids = [parent_id for parent_id in dict.fromkeys(parent_ids) if parent_id is not None]
        if not unique_ids:
            return
        for start in range(0, len(unique_ids), SQLITE_VARIABLE_BATCH_SIZE):
            batch_ids = unique_ids[start : start + SQLITE_VARIABLE_BATCH_SIZE]
            placeholders = ", ".join("?" for _ in batch_ids)
            conn.execute(
                f'DELETE FROM "{table_name}" WHERE "{parent_column}" IN ({placeholders})',
                batch_ids,
            )

    def _child_signature(self, row: dict[str, Any], schema_key: tuple[str, int]) -> tuple[Any, ...]:
        row_id = self._as_int(row.get("id"))
        if row_id is not None:
            return (schema_key[0], "id", row_id)
        return (
            schema_key[0],
            schema_key[1],
            tuple((key, row.get(key)) for key in sorted(row) if key != "id"),
        )

    def _extract_index_name(self, index_sql: str) -> str | None:
        tokens = index_sql.replace("\n", " ").split()
        if len(tokens) < 4:
            return None
        if tokens[0].upper() != "CREATE":
            return None
        if tokens[1].upper() == "UNIQUE":
            if len(tokens) < 5 or tokens[2].upper() != "INDEX":
                return None
            return tokens[3].strip('"')
        if tokens[1].upper() != "INDEX":
            return None
        return tokens[2].strip('"')

    @staticmethod
    def _as_int(value: Any) -> int | None:
        if value is None or value == "":
            return None
        if isinstance(value, bool):
            return int(value)
        if isinstance(value, int):
            return value
        if isinstance(value, float):
            return int(value)
        try:
            return int(str(value).strip())
        except ValueError:
            return None


class WordsJsonManagerWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.service = WordsJsonService()
        self.export_json_path = DEFAULT_JSON_PATH
        self.current_import_json_paths: list[Path] = [DEFAULT_JSON_PATH]
        self.export_db_path = DEFAULT_DB_PATH
        self.import_db_path = DEFAULT_DB_PATH

        self.setWindowTitle("words.db JSON 管理器")
        self.resize(980, 560)
        self._build_ui()
        self._sync_path_widgets()

    def _build_ui(self) -> None:
        central = QWidget(self)
        self.setCentralWidget(central)

        layout = QVBoxLayout(central)

        export_group = QGroupBox("数据库导出 JSON")
        export_layout = QFormLayout(export_group)
        self.export_db_path_edit = QLineEdit()
        export_layout.addRow(QLabel("数据库路径"), self.export_db_path_edit)
        browse_export_db_button = QPushButton("选择数据库")
        browse_export_db_button.clicked.connect(self.choose_export_db_path)
        export_layout.addRow(QLabel(""), browse_export_db_button)

        self.export_json_path_edit = QLineEdit()
        self.export_json_path_edit.setPlaceholderText("导出的 JSON 文件路径")
        export_layout.addRow(QLabel("JSON 输出路径"), self.export_json_path_edit)
        browse_export_json_button = QPushButton("选择输出路径")
        browse_export_json_button.clicked.connect(self.choose_export_json_path)
        export_layout.addRow(QLabel(""), browse_export_json_button)

        export_button = QPushButton("导出数据库到 JSON")
        export_button.clicked.connect(self.export_db_to_json)
        export_layout.addRow(QLabel(""), export_button)
        layout.addWidget(export_group)

        import_group = QGroupBox("JSON 导入数据库")
        import_layout = QFormLayout(import_group)
        self.import_json_list = QListWidget()
        self.import_json_list.setSelectionMode(QListWidget.SelectionMode.ExtendedSelection)
        self.import_json_list.setAlternatingRowColors(True)
        self.import_json_list.setMinimumHeight(140)
        import_layout.addRow(QLabel("JSON 文件列表"), self.import_json_list)

        import_buttons_layout = QHBoxLayout()
        browse_import_json_button = QPushButton("添加导入文件")
        browse_import_json_button.clicked.connect(self.choose_import_json_paths)
        import_buttons_layout.addWidget(browse_import_json_button)
        remove_import_json_button = QPushButton("移除选中")
        remove_import_json_button.clicked.connect(self.remove_selected_import_json_paths)
        import_buttons_layout.addWidget(remove_import_json_button)
        clear_import_json_button = QPushButton("清空列表")
        clear_import_json_button.clicked.connect(self.clear_import_json_paths)
        import_buttons_layout.addWidget(clear_import_json_button)
        import_layout.addRow(QLabel(""), import_buttons_layout)

        self.import_db_path_edit = QLineEdit()
        import_layout.addRow(QLabel("目标数据库路径"), self.import_db_path_edit)
        browse_import_db_button = QPushButton("选择目标数据库")
        browse_import_db_button.clicked.connect(self.choose_import_db_path)
        import_layout.addRow(QLabel(""), browse_import_db_button)

        import_button = QPushButton("导入 JSON 到数据库")
        import_button.clicked.connect(self.import_json_to_db)
        import_layout.addRow(QLabel(""), import_button)
        layout.addWidget(import_group)

        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        self.log_edit.setPlaceholderText("运行日志")
        layout.addWidget(self.log_edit)

    def choose_export_db_path(self) -> None:
        selected, _ = QFileDialog.getOpenFileName(
            self,
            "选择数据库文件",
            str(self.export_db_path.parent),
            "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*)",
        )
        if not selected:
            return
        self.export_db_path = Path(selected)
        self._sync_path_widgets()

    def choose_import_db_path(self) -> None:
        selected, _ = QFileDialog.getOpenFileName(
            self,
            "选择目标数据库文件",
            str(self.import_db_path.parent),
            "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*)",
        )
        if not selected:
            return
        self.import_db_path = Path(selected)
        self._sync_path_widgets()

    def choose_export_json_path(self) -> None:
        selected, _ = QFileDialog.getSaveFileName(
            self,
            "选择 JSON 文件",
            str(self.export_json_path),
            "JSON Files (*.json);;All Files (*)",
        )
        if not selected:
            return
        self.export_json_path = Path(selected)
        self._sync_path_widgets()

    def choose_import_json_paths(self) -> list[Path]:
        selected, _ = QFileDialog.getOpenFileNames(
            self,
            "选择一个或多个 JSON 文件",
            str(self.current_import_json_paths[0].parent if self.current_import_json_paths else self.export_json_path.parent),
            "JSON Files (*.json);;All Files (*)",
        )
        selected_paths = [Path(path) for path in selected]
        if not selected_paths:
            return []

        existing_paths = [str(path) for path in self.current_import_json_paths]
        appended_paths = [path for path in selected_paths if str(path) not in existing_paths]
        if not appended_paths:
            self.log("本次选择的 JSON 文件已全部存在于导入列表中")
            return list(self.current_import_json_paths)

        self.current_import_json_paths.extend(appended_paths)
        self._sync_path_widgets()
        self._log_json_paths("已添加导入 JSON 文件", appended_paths)
        self.log(f"当前导入列表共 {len(self.current_import_json_paths)} 个文件")
        return list(self.current_import_json_paths)

    def remove_selected_import_json_paths(self) -> None:
        selected_items = self.import_json_list.selectedItems()
        if not selected_items:
            QMessageBox.information(self, "提示", "请先在列表中选中要移除的 JSON 文件")
            return

        selected_paths = {item.text() for item in selected_items}
        removed_paths = [path for path in self.current_import_json_paths if str(path) in selected_paths]
        self.current_import_json_paths = [
            path for path in self.current_import_json_paths if str(path) not in selected_paths
        ]
        self._sync_path_widgets()
        self._log_json_paths("已移除导入 JSON 文件", removed_paths)
        self.log(f"当前导入列表共 {len(self.current_import_json_paths)} 个文件")

    def clear_import_json_paths(self) -> None:
        if not self.current_import_json_paths:
            return
        reply = QMessageBox.question(
            self,
            "确认清空",
            "确定清空当前导入 JSON 文件列表吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        self.current_import_json_paths = []
        self._sync_path_widgets()
        self.log("已清空导入 JSON 文件列表")

    def export_db_to_json(self) -> None:
        self._update_paths_from_widgets()
        try:
            payload = self.service.export_database(self.export_db_path, self.export_json_path)
        except Exception as exc:
            self._show_error("导出失败", exc)
            return

        metadata = payload.get("metadata", {})
        self.log(
            f"已从数据库 {self.export_db_path} 导出 {metadata.get('record_count', 0)} 条 word_meaning 记录到 {self.export_json_path}"
        )

    def import_json_to_db(self) -> None:
        self._update_paths_from_widgets()

        selected_json_paths = list(self.current_import_json_paths)
        if not selected_json_paths:
            selected_json_paths = self.choose_import_json_paths()
            if not selected_json_paths:
                return

        try:
            if len(selected_json_paths) > 1:
                payload_to_import = self.service.load_json_files(selected_json_paths)
            else:
                payload_to_import = self.service.load_json(selected_json_paths[0])
        except Exception as exc:
            self._show_error("导入失败", exc)
            return

        record_count = len(payload_to_import.get("records", []))
        import_source = (
            f"{len(selected_json_paths)} 个 JSON 文件"
            if len(selected_json_paths) > 1
            else str(selected_json_paths[0])
        )

        reply = QMessageBox.question(
            self,
            "确认导入",
            f"将按 {import_source} 更新数据库中对应词条并同步相关子表到:\n"
            f"{self.import_db_path}\n"
            f"待导入记录数: {record_count}\n继续吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        try:
            row_counts = self.service.import_json_to_database(payload_to_import, self.import_db_path)
        except Exception as exc:
            self._show_error("导入失败", exc)
            return

        self._log_json_paths("本次导入的 JSON 文件", selected_json_paths)
        summary = "，".join(f"{table_name}: {count}" for table_name, count in row_counts.items())
        self.log(f"数据库导入完成: {summary}")
        QMessageBox.information(self, "导入完成", f"数据库已按 JSON 更新完成。\n{summary}")

    def _sync_path_widgets(self) -> None:
        self.export_db_path_edit.setText(str(self.export_db_path))
        self.export_json_path_edit.setText(str(self.export_json_path))
        self.import_db_path_edit.setText(str(self.import_db_path))
        self.import_json_list.clear()
        for path in self.current_import_json_paths:
            self.import_json_list.addItem(QListWidgetItem(str(path)))

    def _update_paths_from_widgets(self) -> None:
        export_db_text = self.export_db_path_edit.text().strip()
        export_json_text = self.export_json_path_edit.text().strip()
        import_db_text = self.import_db_path_edit.text().strip()
        if export_db_text:
            self.export_db_path = Path(export_db_text)
        if export_json_text:
            self.export_json_path = Path(export_json_text)
        if import_db_text:
            self.import_db_path = Path(import_db_text)

    def _log_json_paths(self, prefix: str, json_paths: list[Path]) -> None:
        if not json_paths:
            return
        self.log(prefix)
        for path in json_paths:
            self.log(f"  - {path}")

    def log(self, message: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_edit.appendPlainText(f"[{timestamp}] {message}")

    def _show_error(self, title: str, error: Exception) -> None:
        message = str(error)
        self.log(f"{title}: {message}")
        QMessageBox.critical(self, title, message)


def main() -> int:
    app = QApplication(sys.argv)
    window = WordsJsonManagerWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())