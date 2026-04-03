from __future__ import annotations

import importlib
import json
import subprocess
import sys
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
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QCheckBox,
    QDialog,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)


BOOK_DIR = Path(__file__).resolve().parent.parent / "stage1" / "book"
MAX_SOURCE_FILES = 4
DEFAULT_SOURCE_JSON_PATHS: list[Path | None] = [
    BOOK_DIR / "人教版小学单词表（3-6年级）.json",
    BOOK_DIR / "外研版小学单词表（1-6年级）.json",
    BOOK_DIR / "沪教牛津版小学单词表（3-6年级）.json",
    BOOK_DIR / "译林版小学单词表（3-6年级）.json",
]
DEFAULT_OUTPUT_JSON_PATH = BOOK_DIR / "record_stage1_merge_output.json"
SOURCE_SELECTION_COLORS = ["#0b6e4f", "#1d4ed8", "#7c3aed", "#b45309", "#be185d"]
LIST_PANEL_WIDTH = 220


def _source_key(index: int) -> str:
    return f"source_{index + 1}"


def _source_index(source_key: str) -> int:
    try:
        return max(0, int(source_key.rsplit("_", 1)[1]) - 1)
    except (IndexError, ValueError):
        return 0


def _source_label(source_key: str) -> str:
    return f"来源{_source_index(source_key) + 1}"


def _source_selection_color(source_key: str) -> str:
    return SOURCE_SELECTION_COLORS[_source_index(source_key) % len(SOURCE_SELECTION_COLORS)]


def _json_dumps(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2, sort_keys=False)


def _read_json_with_fallback(json_path: Path) -> Any:
    for encoding in ("utf-8-sig", "utf-8"):
        try:
            return json.loads(json_path.read_text(encoding=encoding))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
    return json.loads(json_path.read_text(encoding="utf-8-sig"))


def _word_from_record(record: dict[str, Any]) -> str:
    word_payload = record.get("word") or {}
    word = word_payload.get("word")
    if word is None:
        return ""
    return str(word).strip()


def _word_key(word: str) -> str:
    return word.strip().casefold()


def _normalize_for_compare(data: Any) -> Any:
    if isinstance(data, dict):
        return {key: _normalize_for_compare(value) for key, value in sorted(data.items())}
    if isinstance(data, list):
        normalized_items = [_normalize_for_compare(item) for item in data]
        try:
            return sorted(
                normalized_items,
                key=lambda item: json.dumps(item, ensure_ascii=False, sort_keys=True),
            )
        except TypeError:
            return normalized_items
    return data


def _record_group_signature(records: list[dict[str, Any]]) -> str:
    normalized = _normalize_for_compare(records)
    return json.dumps(normalized, ensure_ascii=False, sort_keys=True)


def _extract_records(payload: Any) -> list[dict[str, Any]]:
    if isinstance(payload, dict):
        records = payload.get("records")
        if not isinstance(records, list):
            raise ValueError("JSON 中缺少 records 数组")
        return [record for record in records if isinstance(record, dict)]
    if isinstance(payload, list):
        return [record for record in payload if isinstance(record, dict)]
    raise ValueError("JSON 顶层格式无效")


def _normalize_payload_for_preview(payload: Any) -> dict[str, Any]:
    if isinstance(payload, dict):
        normalized = deepcopy(payload)
        normalized.setdefault("metadata", {})
        normalized.setdefault("schema", {})
        normalized["records"] = _extract_records(normalized)
        return normalized
    if isinstance(payload, list):
        return {"metadata": {}, "schema": {}, "records": _extract_records(payload)}
    raise ValueError("JSON 顶层格式无效")


def _format_payload_for_preview(json_path: Path, payload: dict[str, Any]) -> str:
    metadata = payload.get("metadata")
    if not isinstance(metadata, dict):
        metadata = {}

    schema = payload.get("schema")
    if not isinstance(schema, dict):
        schema = {}

    records = payload.get("records")
    if not isinstance(records, list):
        records = []

    lines = [
        f"文件: {json_path}",
        f"顶层字段: {', '.join(payload.keys()) or '(空)'}",
        f"records 数量: {len(records)}",
        "",
        "metadata:",
        _json_dumps(metadata) if metadata else "{}",
        "",
        "schema 字段:",
        ", ".join(schema.keys()) if schema else "(空)",
        "",
        "records 全量内容:",
    ]

    if not records:
        lines.append("[]")
    else:
        for index, record in enumerate(records, start=1):
            lines.append(f"--- record {index} ---")
            lines.append(_json_dumps(record))
            lines.append("")

    return "\n".join(lines).rstrip()


def _flatten_payload_records(payload: dict[str, Any]) -> tuple[list[str], list[dict[str, str]]]:
    records = payload.get("records")
    if not isinstance(records, list):
        return [], []

    column_order: list[str] = []
    row_maps: list[dict[str, str]] = []

    for index, record in enumerate(records, start=1):
        flat_row: dict[str, str] = {"record_index": str(index)}
        _flatten_record_value(flat_row, "", record)
        for column_name in flat_row:
            if column_name not in column_order:
                column_order.append(column_name)
        row_maps.append(flat_row)

    return column_order, row_maps


def _flatten_record_value(target: dict[str, str], prefix: str, value: Any) -> None:
    if isinstance(value, dict):
        if not value:
            target[prefix or "value"] = "{}"
            return
        for key in sorted(value):
            next_prefix = f"{prefix}.{key}" if prefix else str(key)
            _flatten_record_value(target, next_prefix, value.get(key))
        return

    if isinstance(value, list):
        if not value:
            target[prefix or "value"] = "[]"
            return
        if all(isinstance(item, dict) for item in value):
            field_names: list[str] = []
            for item in value:
                for key in sorted(item):
                    if key not in field_names:
                        field_names.append(key)
            for field_name in field_names:
                column_name = f"{prefix}[].{field_name}" if prefix else field_name
                values = [_scalar_to_text(item.get(field_name)) for item in value]
                target[column_name] = " | ".join(values)
            return
        target[prefix or "value"] = " | ".join(_scalar_to_text(item) for item in value)
        return

    target[prefix or "value"] = _scalar_to_text(value)


def _scalar_to_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def _describe_top_level_diffs(
    left_records: list[dict[str, Any]],
    right_records: list[dict[str, Any]],
) -> str:
    if not left_records and not right_records:
        return "两个文件都没有该词条记录。"
    if not left_records:
        return "左侧文件缺少该词条。"
    if not right_records:
        return "右侧文件缺少该词条。"
    if _record_group_signature(left_records) == _record_group_signature(right_records):
        return "两个文件该词条记录完全一致。"

    lines: list[str] = []
    lines.append(f"左侧记录数: {len(left_records)}")
    lines.append(f"右侧记录数: {len(right_records)}")

    left_first = left_records[0]
    right_first = right_records[0]
    for key in ("word", "word_meaning", "word_form", "word_example", "hidden"):
        left_value = left_first.get(key) if key in left_first else None
        right_value = right_first.get(key) if key in right_first else None
        if _normalize_for_compare(left_value) == _normalize_for_compare(right_value):
            continue
        lines.append(f"- {key} 不同")

    left_extra = max(0, len(left_records) - 1)
    right_extra = max(0, len(right_records) - 1)
    if left_extra or right_extra:
        lines.append(f"- 多记录差异: 左侧额外 {left_extra} 条, 右侧额外 {right_extra} 条")

    return "\n".join(lines) if lines else "存在差异，但主要字段无法进一步归类。"


FIELD_DEFINITIONS = [
    ("word", "单词"),
    ("meaning_zh", "中文释义"),
    ("meaning_en", "英文释义"),
    ("example_1", "例句1"),
    ("example_2", "例句2"),
]
COMPARISON_FIELD_KEYS = {"word", "meaning_zh"}


def _collect_field_values(records: list[dict[str, Any]]) -> dict[str, str]:
    if not records:
        return {key: "" for key, _ in FIELD_DEFINITIONS}

    values: dict[str, list[str]] = {key: [] for key, _ in FIELD_DEFINITIONS}
    for record_index, record in enumerate(records, start=1):
        word_payload = record.get("word") or {}
        meaning_payload = record.get("word_meaning") or {}
        example_payloads = [item for item in record.get("word_example") or [] if isinstance(item, dict)]

        record_fields = {
            "word": str(word_payload.get("word") or "").strip(),
            "meaning_zh": str(meaning_payload.get("meaning_zh") or "").strip(),
            "meaning_en": str(meaning_payload.get("meaning_en") or "").strip(),
            "example_1": str((example_payloads[0].get("example_en") if len(example_payloads) > 0 else "") or "").strip(),
            "example_2": str((example_payloads[1].get("example_en") if len(example_payloads) > 1 else "") or "").strip(),
        }

        if len(records) == 1:
            for field_key, field_value in record_fields.items():
                if field_value:
                    values[field_key].append(field_value)
            continue

        for field_key, field_value in record_fields.items():
            if not field_value:
                continue
            values[field_key].append(f"记录{record_index}: {field_value}")

    return {field_key: "\n\n".join(field_values) for field_key, field_values in values.items()}


def _format_diff_value(value: str) -> str:
    return value if value else "(空)"


def _comparison_signature(records: list[dict[str, Any]]) -> str:
    field_values = _collect_field_values(records)
    comparable = {
        field_key: field_values.get(field_key, "")
        for field_key in sorted(COMPARISON_FIELD_KEYS)
    }
    return json.dumps(comparable, ensure_ascii=False, sort_keys=True)


def _describe_field_diffs(
    records_by_source: dict[str, list[dict[str, Any]]],
    active_source_keys: list[str],
) -> str:
    present_source_keys = [key for key in active_source_keys if records_by_source.get(key)]
    if not present_source_keys:
        return "所有来源都没有该词条。"

    present_labels = "、".join(_source_label(key) for key in present_source_keys)
    missing_source_keys = [key for key in active_source_keys if not records_by_source.get(key)]
    missing_labels = "、".join(_source_label(key) for key in missing_source_keys)
    field_values_by_source = {
        key: _collect_field_values(records_by_source.get(key, []))
        for key in active_source_keys
    }

    lines: list[str] = [f"包含该词条的来源: {present_labels}"]
    if missing_labels:
        lines.append(f"缺少该词条的来源: {missing_labels}")

    differing_fields: list[tuple[str, str, dict[str, str]]] = []
    for field_key, label in FIELD_DEFINITIONS:
        if field_key not in COMPARISON_FIELD_KEYS:
            continue
        values_by_source = {
            key: field_values_by_source[key].get(field_key, "")
            for key in present_source_keys
        }
        present_values = set(values_by_source.values())
        if len(present_values) > 1:
            differing_fields.append((field_key, label, values_by_source))

    if differing_fields:
        lines.append("固定字段差异:")
        for _field_key, label, values_by_source in differing_fields:
            lines.append(f"- {label} 不同")
            for source_key in present_source_keys:
                lines.append(
                    f"  {_source_label(source_key)}: {_format_diff_value(values_by_source.get(source_key, ''))}"
                )

    if missing_source_keys:
        if differing_fields:
            lines.append("")
        lines.append("现有记录在单词和中文释义上是一致的，仅存在来源覆盖差异。")
        return "\n".join(lines)

    if differing_fields:
        return "\n".join(lines)

    return "所有来源在单词和中文释义上都一致。其他字段未参与一致性判断。"


@dataclass(slots=True)
class SourceData:
    key: str
    label: str
    path: Path
    payload: dict[str, Any]
    records: list[dict[str, Any]]
    grouped_records: dict[str, list[dict[str, Any]]]
    ordered_words: list[str]
    duplicate_words: dict[str, int]


@dataclass(slots=True)
class WordComparison:
    word: str
    records_by_source: dict[str, list[dict[str, Any]]]
    status: str
    selected_sources: list[str]


class RecordCompareService:
    def load_source(self, source_key: str, json_path: Path) -> SourceData:
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        raw_payload = _read_json_with_fallback(json_path)
        payload = raw_payload if isinstance(raw_payload, dict) else {"metadata": {}, "records": raw_payload}
        payload.setdefault("metadata", {})
        records = _extract_records(payload)

        grouped_records: dict[str, list[dict[str, Any]]] = {}
        ordered_words: list[str] = []
        duplicate_words: dict[str, int] = {}

        for record in records:
            word = _word_from_record(record)
            if not word:
                continue
            key = _word_key(word)
            if key not in grouped_records:
                grouped_records[key] = []
                ordered_words.append(key)
                duplicate_words[key] = 0
            else:
                duplicate_words[key] += 1
            grouped_records[key].append(deepcopy(record))

        return SourceData(
            key=source_key,
            label=_source_label(source_key),
            path=json_path,
            payload=payload,
            records=records,
            grouped_records=grouped_records,
            ordered_words=ordered_words,
            duplicate_words=duplicate_words,
        )

    def build_comparisons(self, sources: list[SourceData]) -> list[WordComparison]:
        combined_order: list[str] = []
        seen_keys: set[str] = set()
        for source in sources:
            for key in source.ordered_words:
                if key in seen_keys:
                    continue
                seen_keys.add(key)
                combined_order.append(key)

        comparisons: list[WordComparison] = []
        for key in combined_order:
            word = ""
            records_by_source: dict[str, list[dict[str, Any]]] = {}
            present_signatures: list[str] = []

            for source in sources:
                source_records = deepcopy(source.grouped_records.get(key, []))
                if not source_records:
                    continue
                records_by_source[source.key] = source_records
                if not word:
                    word = _word_from_record(source_records[0])
                present_signatures.append(_comparison_signature(source_records))

            present_count = len(records_by_source)
            if present_count == len(sources) and len(set(present_signatures)) == 1:
                status = "same"
            elif present_count == 1:
                status = "source_only"
            else:
                status = "different"

            comparisons.append(
                WordComparison(
                    word=word,
                    records_by_source=records_by_source,
                    status=status,
                    selected_sources=[],
                )
            )
        return comparisons

    def merge_sources(
        self,
        sources: list[SourceData],
        output_path: Path,
    ) -> dict[str, Any]:
        if not sources:
            raise ValueError("请至少加载 1 个 JSON 文件再合并。")

        output_records: list[dict[str, Any]] = []
        skipped_word_count = 0
        skipped_meaning_count = 0
        next_word_id = 1
        next_meaning_id = 1
        merged_word_by_key: dict[str, dict[str, Any]] = {}
        output_indexes_by_word_key: dict[str, list[int]] = {}
        seen_meaning_signatures: set[str] = set()

        for source in sources:
            for record in source.records:
                if not isinstance(record, dict):
                    continue

                word = _word_from_record(record)
                if not word:
                    skipped_word_count += 1
                    continue

                meaning_payload = dict(record.get("word_meaning") or {})
                if not meaning_payload:
                    skipped_meaning_count += 1
                    continue

                word_key = _word_key(word)
                if word_key not in merged_word_by_key:
                    word_payload = dict(record.get("word") or {})
                    word_payload["id"] = next_word_id
                    word_payload["word"] = word
                    word_payload.setdefault("image", None)
                    merged_word_by_key[word_key] = word_payload
                    output_indexes_by_word_key[word_key] = []
                    next_word_id += 1
                else:
                    existing_word = merged_word_by_key[word_key]
                    incoming_word = dict(record.get("word") or {})
                    existing_phonetic = str(existing_word.get("phonetic") or "").strip()
                    incoming_phonetic = str(incoming_word.get("phonetic") or "").strip()
                    if not existing_phonetic and incoming_phonetic:
                        existing_word["phonetic"] = incoming_word.get("phonetic")
                        for output_index in output_indexes_by_word_key.get(word_key, []):
                            output_records[output_index]["word"]["phonetic"] = incoming_word.get("phonetic")

                merged_word = dict(merged_word_by_key[word_key])
                word_id = int(merged_word["id"])
                meaning_signature = self._meaning_signature(word_key, meaning_payload)
                if meaning_signature in seen_meaning_signatures:
                    skipped_meaning_count += 1
                    continue

                seen_meaning_signatures.add(meaning_signature)
                merged_meaning = dict(meaning_payload)
                merged_meaning["id"] = next_meaning_id
                merged_meaning["word_id"] = word_id
                next_meaning_id += 1

                output_records.append(
                    {
                        "word": merged_word,
                        "word_meaning": merged_meaning,
                        "word_form": [],
                        "word_example": [],
                    }
                )
                output_indexes_by_word_key.setdefault(word_key, []).append(len(output_records) - 1)

        metadata = self._build_output_metadata(
            sources,
            record_count=len(output_records),
            unique_word_count=len(merged_word_by_key),
            skipped_word_count=skipped_word_count,
            skipped_meaning_count=skipped_meaning_count,
        )
        payload = {
            "metadata": metadata,
            "schema": self._build_output_schema(sources),
            "records": output_records,
        }
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(_json_dumps(payload), encoding="utf-8")
        return payload

    @staticmethod
    def _meaning_signature(word_key: str, meaning_payload: dict[str, Any]) -> str:
        comparable = {
            key: value
            for key, value in meaning_payload.items()
            if key not in {"id", "word_id", "meaning_id"}
        }
        normalized = {
            "word_key": word_key,
            "word_meaning": _normalize_for_compare(comparable),
        }
        return json.dumps(normalized, ensure_ascii=False, sort_keys=True)

    @staticmethod
    def _build_output_metadata(
        sources: list[SourceData],
        *,
        record_count: int,
        unique_word_count: int,
        skipped_word_count: int,
        skipped_meaning_count: int,
    ) -> dict[str, Any]:
        first_metadata = deepcopy(sources[0].payload.get("metadata") or {})
        if not isinstance(first_metadata, dict):
            first_metadata = {}

        publishers = {
            str(source.payload.get("metadata", {}).get("publisher") or "").strip()
            for source in sources
            if isinstance(source.payload.get("metadata"), dict)
        }
        publishers.discard("")

        metadata = {
            "source_file": first_metadata.get("source_file") or first_metadata.get("source_database") or "",
            "source_workbook": first_metadata.get("source_workbook") or first_metadata.get("source_file") or "",
            "exported_at": datetime.now().isoformat(timespec="seconds"),
            "record_count": record_count,
            "root_table": first_metadata.get("root_table") or "word_meaning",
            "related_tables": first_metadata.get("related_tables") or ["word", "word_meaning", "word_form", "word_example"],
            "unique_word_count": unique_word_count,
            "source_files": [str(source.path) for source in sources],
            "source_count": len(sources),
            "skipped_word_count": skipped_word_count,
            "skipped_meaning_count": skipped_meaning_count,
            "note": "Merged by record_compare_ui.py",
        }
        if len(publishers) == 1:
            metadata["publisher"] = next(iter(publishers))
        elif publishers:
            metadata["publisher"] = "merged"
        return metadata

    @staticmethod
    def _build_output_schema(sources: list[SourceData]) -> dict[str, Any]:
        for source in sources:
            schema = source.payload.get("schema")
            if isinstance(schema, dict) and schema:
                normalized_schema = deepcopy(schema)
                word_meaning_schema = normalized_schema.get("word_meaning")
                if isinstance(word_meaning_schema, dict):
                    columns = word_meaning_schema.get("columns")
                    if isinstance(columns, list):
                        normalized_columns: list[dict[str, Any]] = []
                        for column in columns:
                            if not isinstance(column, dict):
                                continue
                            normalized_column = dict(column)
                            if str(normalized_column.get("name")) == "image":
                                normalized_column["name"] = "source"
                            normalized_columns.append(normalized_column)
                        word_meaning_schema["columns"] = normalized_columns

                    create_sql = word_meaning_schema.get("create_sql")
                    if isinstance(create_sql, str) and create_sql:
                        word_meaning_schema["create_sql"] = create_sql.replace("image TEXT", "source TEXT")
                return normalized_schema
        return {}


class JsonBrowserDialog(QDialog):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("JSON 全量浏览")
        self.resize(1400, 900)
        self._build_ui()

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)

        self.path_label = QLabel("当前文件: -")
        layout.addWidget(self.path_label)

        self.summary_label = QLabel("records 数量: 0，字段列数: 0")
        layout.addWidget(self.summary_label)

        self.tabs = QTabWidget()

        self.table = QTableWidget()
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.table.horizontalHeader().setStretchLastSection(False)
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        self.table.verticalHeader().setVisible(False)
        self.tabs.addTab(self.table, "表格预览")

        self.text_edit = QPlainTextEdit()
        self.text_edit.setReadOnly(True)
        self.tabs.addTab(self.text_edit, "文本预览")

        layout.addWidget(self.tabs)

    def load_json_file(self, json_path: Path) -> None:
        payload = _normalize_payload_for_preview(_read_json_with_fallback(json_path))
        columns, row_maps = _flatten_payload_records(payload)
        self._update_table(columns, row_maps)
        self.path_label.setText(f"当前文件: {json_path}")
        self.summary_label.setText(f"records 数量: {len(row_maps)}，字段列数: {len(columns)}")
        self.text_edit.setPlainText(_format_payload_for_preview(json_path, payload))

    def _update_table(self, columns: list[str], row_maps: list[dict[str, str]]) -> None:
        self.table.clear()
        self.table.setRowCount(len(row_maps))
        self.table.setColumnCount(len(columns))
        self.table.setHorizontalHeaderLabels(columns)

        for row_index, row_map in enumerate(row_maps):
            for column_index, column_name in enumerate(columns):
                value = row_map.get(column_name, "")
                item = QTableWidgetItem(value)
                item.setToolTip(value)
                self.table.setItem(row_index, column_index, item)

        if columns:
            self.table.resizeColumnsToContents()


class RecordCompareWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.service = RecordCompareService()
        self.source_json_paths: dict[str, Path | None] = {
            _source_key(index): DEFAULT_SOURCE_JSON_PATHS[index]
            for index in range(MAX_SOURCE_FILES)
        }
        self.output_json_path = DEFAULT_OUTPUT_JSON_PATH

        self.sources: list[SourceData] = []
        self.sources_by_key: dict[str, SourceData] = {}
        self.active_source_keys: list[str] = []
        self.comparisons: list[WordComparison] = []
        self.filtered_indexes: list[int] = []
        self.json_browser_dialog: JsonBrowserDialog | None = None
        self.source_path_edits: dict[str, QLineEdit] = {}
        self.source_pick_boxes: dict[str, QCheckBox] = {}
        self.source_field_edits: dict[str, dict[str, QPlainTextEdit]] = {}
        self.source_detail_labels: dict[str, QLabel] = {}

        self.setWindowTitle("record_stage1 多 JSON 比较与合并")
        self.resize(1500, 920)
        self._build_ui()
        self._sync_path_widgets()

    def _build_ui(self) -> None:
        central = QWidget(self)
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)

        path_group = QGroupBox("文件设置")
        path_layout = QFormLayout(path_group)

        for index in range(MAX_SOURCE_FILES):
            source_key = _source_key(index)
            path_edit = QLineEdit()
            choose_button = QPushButton(f"选择{_source_label(source_key)} JSON")
            choose_button.clicked.connect(
                lambda _checked=False, key=source_key: self.choose_source_json_path(key)
            )
            row = QHBoxLayout()
            row.addWidget(path_edit)
            row.addWidget(choose_button)
            self.source_path_edits[source_key] = path_edit
            path_layout.addRow(f"{_source_label(source_key)}文件", row)

        self.output_path_edit = QLineEdit()
        output_button = QPushButton("选择输出 JSON")
        output_button.clicked.connect(self.choose_output_json_path)
        output_row = QHBoxLayout()
        output_row.addWidget(self.output_path_edit)
        output_row.addWidget(output_button)
        path_layout.addRow("输出文件", output_row)

        action_row = QHBoxLayout()
        self.load_button = QPushButton("加载并比较")
        self.load_button.clicked.connect(self.load_and_compare)
        action_row.addWidget(self.load_button)
        self.merge_button = QPushButton("一键合并输出")
        self.merge_button.clicked.connect(self.merge_loaded_sources)
        action_row.addWidget(self.merge_button)
        path_layout.addRow("操作", action_row)

        browse_row = QHBoxLayout()
        for index in range(MAX_SOURCE_FILES):
            source_key = _source_key(index)
            browse_button = QPushButton(f"浏览{_source_label(source_key)}")
            browse_button.clicked.connect(
                lambda _checked=False, key=source_key: self.open_source_json_browser(key)
            )
            browse_row.addWidget(browse_button)
        browse_custom_button = QPushButton("浏览任意 JSON 文件")
        browse_custom_button.clicked.connect(self.open_custom_json_browser)
        browse_row.addWidget(browse_custom_button)
        path_layout.addRow("全量浏览", browse_row)

        root_layout.addWidget(path_group)

        content_splitter = QSplitter(Qt.Orientation.Horizontal)
        root_layout.addWidget(content_splitter, stretch=1)

        list_panel = QWidget()
        list_panel.setMaximumWidth(LIST_PANEL_WIDTH)
        list_layout = QVBoxLayout(list_panel)
        filter_row = QHBoxLayout()
        self.filter_combo = QComboBox()
        self.filter_combo.addItems([
            "全部词条",
            "仅完全相同",
            "仅存在差异",
            "仅单源词条",
        ])
        self.filter_combo.currentIndexChanged.connect(self.refresh_word_list)
        filter_row.addWidget(QLabel("过滤"))
        filter_row.addWidget(self.filter_combo)

        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("按单词搜索")
        self.search_edit.textChanged.connect(self.refresh_word_list)
        filter_row.addWidget(self.search_edit)
        list_layout.addLayout(filter_row)

        self.summary_label = QLabel("未加载文件")
        list_layout.addWidget(self.summary_label)

        self.word_list = QListWidget()
        self.word_list.setSelectionMode(QListWidget.SelectionMode.ExtendedSelection)
        self.word_list.itemSelectionChanged.connect(self.on_word_selection_changed)
        self.word_list.currentRowChanged.connect(self.on_word_current_changed)
        list_layout.addWidget(self.word_list, stretch=1)

        nav_row = QHBoxLayout()
        previous_button = QPushButton("上一条")
        previous_button.clicked.connect(self.select_previous_word)
        nav_row.addWidget(previous_button)
        next_button = QPushButton("下一条")
        next_button.clicked.connect(self.select_next_word)
        nav_row.addWidget(next_button)
        list_layout.addLayout(nav_row)

        content_splitter.addWidget(list_panel)

        detail_panel = QWidget()
        detail_layout = QVBoxLayout(detail_panel)

        self.current_word_label = QLabel("当前词条: -")
        detail_layout.addWidget(self.current_word_label)
        self.current_status_label = QLabel("状态: -")
        detail_layout.addWidget(self.current_status_label)

        pick_row = QHBoxLayout()
        self.pick_controls = QWidget()
        self.pick_controls.setLayout(pick_row)
        self.pick_controls.setVisible(False)
        for index in range(MAX_SOURCE_FILES):
            source_key = _source_key(index)
            pick_box = QCheckBox(f"采纳{_source_label(source_key)}")
            pick_box.toggled.connect(
                lambda checked, key=source_key: self.set_current_source_selected(key, checked)
            )
            pick_row.addWidget(pick_box)
            self.source_pick_boxes[source_key] = pick_box
        self.select_all_pick_button = QPushButton("采纳全部来源")
        self.select_all_pick_button.clicked.connect(self.pick_all_current_sources)
        pick_row.addWidget(self.select_all_pick_button)
        self.clear_pick_button = QPushButton("清除采纳")
        self.clear_pick_button.clicked.connect(self.clear_current_source_selection)
        pick_row.addWidget(self.clear_pick_button)
        detail_layout.addWidget(self.pick_controls)

        info_group = QGroupBox("字段差异")
        info_layout = QVBoxLayout(info_group)
        self.diff_edit = QPlainTextEdit()
        self.diff_edit.setReadOnly(True)
        info_layout.addWidget(self.diff_edit)
        detail_layout.addWidget(info_group, stretch=1)

        compare_group = QGroupBox("字段对照")
        compare_layout = QGridLayout(compare_group)
        for index in range(MAX_SOURCE_FILES):
            source_key = _source_key(index)
            source_group = QGroupBox(_source_label(source_key))
            source_layout = QVBoxLayout(source_group)
            source_detail_label = QLabel("未配置文件")
            source_detail_label.setWordWrap(True)
            source_layout.addWidget(source_detail_label)
            self.source_detail_labels[source_key] = source_detail_label

            field_layout = QFormLayout()
            self.source_field_edits[source_key] = {}
            for field_key, field_label in FIELD_DEFINITIONS:
                field_edit = QPlainTextEdit()
                field_edit.setReadOnly(True)
                field_edit.setMinimumHeight(72)
                field_layout.addRow(field_label, field_edit)
                self.source_field_edits[source_key][field_key] = field_edit

            source_layout.addLayout(field_layout)
            compare_layout.addWidget(source_group, 0, index)
            compare_layout.setColumnStretch(index, 1)
        detail_layout.addWidget(compare_group, stretch=4)

        content_splitter.addWidget(detail_panel)
        content_splitter.setStretchFactor(0, 1)
        content_splitter.setStretchFactor(1, 3)
        content_splitter.setSizes([LIST_PANEL_WIDTH, 1500 - LIST_PANEL_WIDTH])

        log_group = QGroupBox("运行日志")
        log_layout = QVBoxLayout(log_group)
        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        log_layout.addWidget(self.log_edit)
        root_layout.addWidget(log_group, stretch=0)

    def choose_source_json_path(self, source_key: str) -> None:
        current_path = self.source_json_paths.get(source_key)
        start_dir = str(current_path.parent) if current_path is not None else str(BOOK_DIR)
        selected, _ = QFileDialog.getOpenFileName(
            self,
            f"选择{_source_label(source_key)} JSON 文件",
            start_dir,
            "JSON Files (*.json);;All Files (*)",
        )
        if not selected:
            return
        self.source_json_paths[source_key] = Path(selected)
        self._sync_path_widgets()

    def choose_output_json_path(self) -> None:
        selected, _ = QFileDialog.getSaveFileName(
            self,
            "选择输出 JSON 文件",
            str(self.output_json_path),
            "JSON Files (*.json);;All Files (*)",
        )
        if not selected:
            return
        self.output_json_path = Path(selected)
        self._sync_path_widgets()

    def _sync_path_widgets(self) -> None:
        for source_key, path_edit in self.source_path_edits.items():
            path = self.source_json_paths.get(source_key)
            path_edit.setText(str(path) if path is not None else "")
            self._update_source_tab_header(source_key, [], loaded=False)
        self.output_path_edit.setText(str(self.output_json_path))

    def _update_paths_from_widgets(self) -> None:
        for source_key, path_edit in self.source_path_edits.items():
            text = path_edit.text().strip()
            self.source_json_paths[source_key] = Path(text) if text else None
        self.output_json_path = Path(self.output_path_edit.text().strip())

    def _configured_source_paths(self) -> list[tuple[str, Path]]:
        configured: list[tuple[str, Path]] = []
        for index in range(MAX_SOURCE_FILES):
            source_key = _source_key(index)
            path = self.source_json_paths.get(source_key)
            if path is None:
                continue
            configured.append((source_key, path))
        return configured

    def load_and_compare(self) -> None:
        self._update_paths_from_widgets()
        configured_source_paths = self._configured_source_paths()
        if len(configured_source_paths) < 2:
            QMessageBox.information(self, "提示", "请至少配置 2 个 JSON 文件再比较。")
            return
        try:
            self.sources = [
                self.service.load_source(source_key, json_path)
                for source_key, json_path in configured_source_paths
            ]
            self.sources_by_key = {source.key: source for source in self.sources}
            self.active_source_keys = [source.key for source in self.sources]
            self.comparisons = self.service.build_comparisons(self.sources)
        except Exception as exc:
            self._show_error("加载失败", exc)
            return

        self.refresh_word_list()
        duplicate_summary = self._build_duplicate_summary()
        loaded_summary = "、".join(
            f"{source.label} {len(source.records)} 条记录"
            for source in self.sources
        )
        self.log(
            f"已加载 {loaded_summary}。当前合并索引 {len(self.comparisons)} 个词条。{duplicate_summary}"
        )

    def refresh_word_list(self) -> None:
        selected_words = self._selected_word_values()
        current_word = self._current_word_value()
        self.word_list.clear()
        self.filtered_indexes = []

        keyword = self.search_edit.text().strip().casefold()
        filter_text = self.filter_combo.currentText()

        for index, comparison in enumerate(self.comparisons):
            if keyword and keyword not in comparison.word.casefold():
                continue
            if not self._match_filter(comparison, filter_text):
                continue
            self.filtered_indexes.append(index)
            item = QListWidgetItem(self._display_text_for_comparison(comparison))
            item.setData(Qt.ItemDataRole.UserRole, index)
            item.setForeground(self._color_for_comparison(comparison))
            self.word_list.addItem(item)

        self._refresh_summary_label()
        if not self.filtered_indexes:
            self.clear_detail_views()
            return

        rows_to_select: list[int] = []
        if selected_words:
            selected_word_set = set(selected_words)
            rows_to_select = [
                row
                for row, comparison_index in enumerate(self.filtered_indexes)
                if self.comparisons[comparison_index].word in selected_word_set
            ]

        if not rows_to_select:
            restored_row = 0
            if current_word:
                for row, comparison_index in enumerate(self.filtered_indexes):
                    if self.comparisons[comparison_index].word == current_word:
                        restored_row = row
                        break
            rows_to_select = [restored_row]

        self.word_list.blockSignals(True)
        for row in rows_to_select:
            item = self.word_list.item(row)
            if item is not None:
                item.setSelected(True)
        primary_row = rows_to_select[0]
        self.word_list.setCurrentRow(primary_row)
        self.word_list.blockSignals(False)
        self._update_detail_for_selection()

    def _refresh_summary_label(self) -> None:
        same_count = sum(1 for item in self.comparisons if item.status == "same")
        different_count = sum(1 for item in self.comparisons if item.status == "different")
        source_only_count = sum(1 for item in self.comparisons if item.status == "source_only")
        self.summary_label.setText(
            f"词条 {len(self.filtered_indexes)}/{len(self.comparisons)} | 完全相同 {same_count} | 存在差异 {different_count} | 单源词条 {source_only_count}"
        )

    def _build_duplicate_summary(self) -> str:
        if not self.sources:
            return ""
        duplicate_parts = [
            f"{source.label} {sum(count for count in source.duplicate_words.values() if count > 0)} 个"
            for source in self.sources
            if any(count > 0 for count in source.duplicate_words.values())
        ]
        if not duplicate_parts:
            return ""
        return "检测到重复词条: " + "，".join(duplicate_parts) + "。"

    def on_word_selection_changed(self) -> None:
        self._update_detail_for_selection()

    def on_word_current_changed(self, row: int) -> None:
        if row < 0 or row >= len(self.filtered_indexes):
            return
        if not self.word_list.selectedItems():
            item = self.word_list.item(row)
            if item is not None:
                item.setSelected(True)

    def _update_detail_for_selection(self) -> None:
        selected_indexes = self._selected_comparison_indexes()
        if not selected_indexes:
            self.clear_detail_views()
            return

        comparison = self.comparisons[selected_indexes[0]]
        selected_count = len(selected_indexes)
        selected_words_preview = "、".join(self.comparisons[index].word for index in selected_indexes[:3])
        if selected_count > 3:
            selected_words_preview += f" 等 {selected_count} 条"

        self.current_word_label.setText(
            f"当前词条: {comparison.word} | 已选记录: {selected_count}"
        )
        self.current_status_label.setText(
            f"状态: {self._status_label(comparison.status)} | 选中词条: {selected_words_preview}"
        )
        self.diff_edit.setPlainText(_describe_field_diffs(comparison.records_by_source, self.active_source_keys))
        self._fill_field_views(comparison)

    def _fill_field_views(self, comparison: WordComparison) -> None:
        field_values_by_source = {
            source_key: _collect_field_values(comparison.records_by_source.get(source_key, []))
            for source_key in self.source_field_edits
        }

        for source_key, field_edits in self.source_field_edits.items():
            source_records = comparison.records_by_source.get(source_key, [])
            self._update_source_tab_header(source_key, source_records, loaded=source_key in self.active_source_keys)
            for field_key, _field_label in FIELD_DEFINITIONS:
                field_edit = field_edits[field_key]
                field_text = field_values_by_source[source_key].get(field_key, "")
                field_edit.setPlainText(field_text)

                if not source_key in self.active_source_keys:
                    self._set_editor_background(field_edit, "#f5f5f5")
                    continue
                if not source_records:
                    self._set_editor_background(field_edit, "#f3f4f6")
                    continue

                if field_key not in COMPARISON_FIELD_KEYS:
                    self._set_editor_background(field_edit, "#fffdf0")
                    continue

                present_values = {
                    field_values_by_source[key].get(field_key, "")
                    for key in self.active_source_keys
                    if comparison.records_by_source.get(key)
                }
                if len(present_values) <= 1:
                    self._set_editor_background(field_edit, "#f3fff3")
                else:
                    self._set_editor_background(field_edit, "#fff0f0")

    @staticmethod
    def _set_editor_background(editor: QPlainTextEdit, color: str) -> None:
        editor.setStyleSheet(f"QPlainTextEdit {{ background-color: {color}; }}")

    def _update_source_tab_header(
        self,
        source_key: str,
        source_records: list[dict[str, Any]],
        *,
        loaded: bool,
    ) -> None:
        source_label = _source_label(source_key)
        source_path = self.source_json_paths.get(source_key)
        if not loaded:
            self.source_detail_labels[source_key].setText(f"{source_label}: 未加载")
            return
        path_text = str(source_path) if source_path is not None else "未配置文件"
        self.source_detail_labels[source_key].setText(
            f"{source_label}: {path_text} | 当前词条记录数: {len(source_records)}"
        )

    def set_current_source_selected(self, source: str, checked: bool) -> None:
        selected_indexes = self._selected_comparison_indexes()
        if not selected_indexes:
            return

        updated_words: list[str] = []
        skipped_words: list[str] = []
        for comparison_index in selected_indexes:
            comparison = self.comparisons[comparison_index]
            if checked and not comparison.records_by_source.get(source):
                skipped_words.append(comparison.word)
                continue

            if checked:
                if source not in comparison.selected_sources:
                    comparison.selected_sources.append(source)
            else:
                comparison.selected_sources = [key for key in comparison.selected_sources if key != source]
            comparison.selected_sources = [
                key for key in self.active_source_keys if key in comparison.selected_sources
            ]
            updated_words.append(comparison.word)

        self.refresh_word_list()
        self._restore_selected_words(updated_words or skipped_words)

        if checked and not updated_words:
            QMessageBox.information(self, "提示", f"{_source_label(source)}在所选词条中都不存在，无法采纳。")
            return

        log_message = f"已对 {len(updated_words)} 条词条设置 {_source_label(source)} 为{'采纳' if checked else '取消采纳'}。"
        if skipped_words:
            log_message += f" 跳过 {len(skipped_words)} 条无该来源的词条。"
        self.log(log_message)

    def pick_all_current_sources(self) -> None:
        selected_indexes = self._selected_comparison_indexes()
        if not selected_indexes:
            return
        updated_words: list[str] = []
        for comparison_index in selected_indexes:
            comparison = self.comparisons[comparison_index]
            comparison.selected_sources = [
                source_key for source_key in self.active_source_keys if comparison.records_by_source.get(source_key)
            ]
            updated_words.append(comparison.word)
        self.refresh_word_list()
        self._restore_selected_words(updated_words)
        self.log(f"已对 {len(updated_words)} 条词条采纳全部可用来源。")

    def clear_current_source_selection(self) -> None:
        selected_indexes = self._selected_comparison_indexes()
        if not selected_indexes:
            return
        cleared_words: list[str] = []
        for comparison_index in selected_indexes:
            comparison = self.comparisons[comparison_index]
            comparison.selected_sources = []
            cleared_words.append(comparison.word)
        self.refresh_word_list()
        self._restore_selected_words(cleared_words)
        self.log(f"已清除 {len(cleared_words)} 条词条的采纳来源。")

    def merge_loaded_sources(self) -> None:
        self._update_paths_from_widgets()
        configured_source_paths = self._configured_source_paths()
        if not configured_source_paths:
            QMessageBox.information(self, "提示", "请至少配置 1 个 JSON 文件再合并。")
            return

        if not self.output_path_edit.text().strip():
            QMessageBox.information(self, "提示", "请先设置输出 JSON 文件路径。")
            return

        try:
            self.sources = [
                self.service.load_source(source_key, json_path)
                for source_key, json_path in configured_source_paths
            ]
            self.sources_by_key = {source.key: source for source in self.sources}
            self.active_source_keys = [source.key for source in self.sources]
            payload = self.service.merge_sources(
                self.sources,
                self.output_json_path,
            )
        except Exception as exc:
            self._show_error("合并失败", exc)
            return

        metadata = payload.get("metadata") or {}
        self.log(
            f"已合并 {metadata.get('source_count', 0)} 个文件，输出 {metadata.get('record_count', 0)} 条 records、{metadata.get('unique_word_count', 0)} 个唯一单词到 {self.output_json_path}。跳过空单词 {metadata.get('skipped_word_count', 0)} 条，跳过重复词义 {metadata.get('skipped_meaning_count', 0)} 条。"
        )
        QMessageBox.information(self, "合并完成", f"已导出到:\n{self.output_json_path}")

    def select_previous_word(self) -> None:
        current_row = self.word_list.currentRow()
        if current_row > 0:
            self.word_list.setCurrentRow(current_row - 1)

    def select_next_word(self) -> None:
        current_row = self.word_list.currentRow()
        if 0 <= current_row < self.word_list.count() - 1:
            self.word_list.setCurrentRow(current_row + 1)

    def _match_filter(self, comparison: WordComparison, filter_text: str) -> bool:
        if filter_text == "全部词条":
            return True
        if filter_text == "仅完全相同":
            return comparison.status == "same"
        if filter_text == "仅存在差异":
            return comparison.status == "different"
        if filter_text == "仅单源词条":
            return comparison.status == "source_only"
        return True

    def _display_text_for_comparison(self, comparison: WordComparison) -> str:
        status_label = self._status_label(comparison.status)
        return f"[{status_label}] {comparison.word}"

    @staticmethod
    def _color_for_comparison(comparison: WordComparison) -> QColor:
        if comparison.status == "different":
            return QColor("#b45309")
        if comparison.status == "source_only":
            return QColor("#7c3aed")
        return QColor("#374151")

    @staticmethod
    def _status_label(status: str) -> str:
        return {
            "same": "完全相同",
            "different": "存在差异",
            "source_only": "仅单源存在",
        }.get(status, "未知")

    @staticmethod
    def _selected_sources_label(sources: list[str]) -> str:
        if not sources:
            return "未选"
        return "、".join(_source_label(source) for source in sources)

    def _update_pick_buttons(self, selected_indexes: list[int]) -> None:
        if not selected_indexes:
            for pick_box in self.source_pick_boxes.values():
                pick_box.blockSignals(True)
                pick_box.setEnabled(False)
                pick_box.setChecked(False)
                pick_box.blockSignals(False)
            self.select_all_pick_button.setEnabled(False)
            self.clear_pick_button.setEnabled(False)
            return

        selectable_count = 0
        for source_key, pick_box in self.source_pick_boxes.items():
            has_records_for_any = any(
                self.comparisons[index].records_by_source.get(source_key)
                for index in selected_indexes
            )
            all_selected = all(
                source_key in self.comparisons[index].selected_sources
                for index in selected_indexes
                if self.comparisons[index].records_by_source.get(source_key)
            ) and has_records_for_any
            pick_box.blockSignals(True)
            pick_box.setEnabled(has_records_for_any)
            pick_box.setChecked(all_selected)
            pick_box.blockSignals(False)
            if has_records_for_any:
                selectable_count += 1
        self.select_all_pick_button.setEnabled(selectable_count > 0)
        self.clear_pick_button.setEnabled(
            any(self.comparisons[index].selected_sources for index in selected_indexes)
        )

    def _restore_current_word(self, word: str) -> None:
        for row, comparison_index in enumerate(self.filtered_indexes):
            if self.comparisons[comparison_index].word == word:
                self.word_list.setCurrentRow(row)
                return

    def _current_word_value(self) -> str:
        current_item = self.word_list.currentItem()
        if current_item is None:
            return ""
        comparison_index = current_item.data(Qt.ItemDataRole.UserRole)
        if comparison_index is None:
            return ""
        return self.comparisons[int(comparison_index)].word

    def _selected_word_values(self) -> list[str]:
        return [self.comparisons[index].word for index in self._selected_comparison_indexes()]

    def _selected_comparison_indexes(self) -> list[int]:
        indexes: list[int] = []
        for item in self.word_list.selectedItems():
            comparison_index = item.data(Qt.ItemDataRole.UserRole)
            if comparison_index is None:
                continue
            indexes.append(int(comparison_index))
        return indexes

    def _restore_selected_words(self, words: list[str]) -> None:
        if not words:
            return
        word_set = set(words)
        self.word_list.blockSignals(True)
        self.word_list.clearSelection()
        primary_row = -1
        for row, comparison_index in enumerate(self.filtered_indexes):
            if self.comparisons[comparison_index].word not in word_set:
                continue
            item = self.word_list.item(row)
            if item is None:
                continue
            item.setSelected(True)
            if primary_row < 0:
                primary_row = row
        if primary_row >= 0:
            self.word_list.setCurrentRow(primary_row)
        self.word_list.blockSignals(False)
        self._update_detail_for_selection()

    def clear_detail_views(self) -> None:
        self.current_word_label.setText("当前词条: -")
        self.current_status_label.setText("状态: -")
        self.diff_edit.clear()
        for source_key, field_edits in self.source_field_edits.items():
            self._update_source_tab_header(source_key, [], loaded=source_key in self.active_source_keys)
            for field_key, _field_label in FIELD_DEFINITIONS:
                field_edits[field_key].clear()
                self._set_editor_background(field_edits[field_key], "white")
        for pick_box in self.source_pick_boxes.values():
            pick_box.blockSignals(True)
            pick_box.setChecked(False)
            pick_box.setEnabled(False)
            pick_box.blockSignals(False)
        self.select_all_pick_button.setEnabled(False)
        self.clear_pick_button.setEnabled(False)

    def open_source_json_browser(self, source_key: str) -> None:
        self._update_paths_from_widgets()
        json_path = self.source_json_paths.get(source_key)
        if json_path is None:
            QMessageBox.information(self, "提示", f"{_source_label(source_key)}尚未配置文件。")
            return
        try:
            dialog = self._ensure_json_browser_dialog()
            dialog.load_json_file(json_path)
        except Exception as exc:
            self._show_error(f"浏览{_source_label(source_key)} JSON 失败", exc)
            return

        self.log(f"已打开{_source_label(source_key)} JSON 全量浏览: {json_path}")
        dialog.show()
        dialog.raise_()
        dialog.activateWindow()

    def open_custom_json_browser(self) -> None:
        selected, _ = QFileDialog.getOpenFileName(
            self,
            "选择要浏览的 JSON 文件",
            str(BOOK_DIR),
            "JSON Files (*.json);;All Files (*)",
        )
        if not selected:
            return

        json_path = Path(selected)
        try:
            dialog = self._ensure_json_browser_dialog()
            dialog.load_json_file(json_path)
        except Exception as exc:
            self._show_error("浏览 JSON 失败", exc)
            return

        self.log(f"已打开 JSON 全量浏览: {json_path}")
        dialog.show()
        dialog.raise_()
        dialog.activateWindow()

    def _ensure_json_browser_dialog(self) -> JsonBrowserDialog:
        if self.json_browser_dialog is None:
            self.json_browser_dialog = JsonBrowserDialog(self)
        return self.json_browser_dialog

    def log(self, message: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_edit.appendPlainText(f"[{timestamp}] {message}")

    def _show_error(self, title: str, exc: Exception) -> None:
        self.log(f"{title}: {exc}")
        QMessageBox.critical(self, title, str(exc))


def main() -> int:
    app = QApplication(sys.argv)
    window = RecordCompareWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())