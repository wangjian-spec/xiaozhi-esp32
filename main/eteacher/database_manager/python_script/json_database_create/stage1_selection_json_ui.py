from __future__ import annotations

import importlib
import json
import random
import sqlite3
import subprocess
import sys
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


_ensure_dependency("PySide6.QtWidgets", "PySide6")

from PySide6.QtWidgets import (
	QApplication,
	QComboBox,
	QFileDialog,
	QFormLayout,
	QGridLayout,
	QHBoxLayout,
	QLabel,
	QLineEdit,
	QMainWindow,
	QMessageBox,
	QPlainTextEdit,
	QPushButton,
	QCheckBox,
	QVBoxLayout,
	QWidget,
)


BASE_DIR = Path(__file__).resolve().parent
PYTHON_SCRIPT_DIR = BASE_DIR.parent
DEFAULT_SOURCE_JSON = PYTHON_SCRIPT_DIR / "stage1" / "book" / "record_stage1_gpt5.4_generated_1_4096_merged.json"
DEFAULT_DB_PATH = PYTHON_SCRIPT_DIR / "stage1" / "words.db"
DEFAULT_OUTPUT_JSON = DEFAULT_SOURCE_JSON.with_name(
	DEFAULT_SOURCE_JSON.stem + "_selection_template.json"
)

FIELD_ORDER = ("word", "word_meaning", "example_en", "example_zh", "selection_en", "selection_zh")
FIELD_LABELS = {
	"word": "word",
	"word_meaning": "word_meaning",
	"example_en": "example_en",
	"example_zh": "example_zh",
	"selection_en": "selection_en",
	"selection_zh": "selection_zh",
}
DEFAULT_EXPORT_FIELDS = ("word", "word_meaning", "example_en", "selection_en")
DEFAULT_CLEAR_FIELDS = ("selection_en",)
DEFAULT_IMPORT_TARGET_COLUMNS = (
	"example_en",
	"example_zh",
	"difficulty",
	"image",
	"example_tag",
	"selection_zh",
	"selection_en",
)
DEFAULT_IMPORT_MAPPINGS = {
	"example_en": "example_en",
	"selection_en": "selection_en",
}
INTERNAL_IMPORT_FIELDS = ("__word_example_id", "__meaning_id", "__example_index")


def _to_text(value: object) -> str:
	if value is None:
		return ""
	if isinstance(value, str):
		return value.strip()
	return str(value).strip()


def _parse_int(value: object) -> int | None:
	text = _to_text(value)
	if not text:
		return None
	try:
		return int(text)
	except ValueError:
		try:
			return int(float(text))
		except ValueError:
			return None


@dataclass(slots=True)
class ExportStats:
	record_count: int = 0
	example_count: int = 0


@dataclass(slots=True)
class ImportStats:
	processed: int = 0
	updated: int = 0
	skipped: int = 0
	not_found: int = 0


@dataclass(slots=True)
class FileUpdateStats:
	processed: int = 0
	updated: int = 0


class Stage1SelectionJsonService:
	def export_template(
		self,
		source_json_path: Path,
		output_json_path: Path,
		export_fields: list[str],
	) -> tuple[dict[str, Any], ExportStats]:
		if not source_json_path.exists():
			raise FileNotFoundError(f"源 JSON 不存在: {source_json_path}")
		selected_fields = self._normalize_fields(export_fields)

		payload = json.loads(source_json_path.read_text(encoding="utf-8"))
		records = payload.get("records") if isinstance(payload, dict) else None
		if not isinstance(records, list):
			raise ValueError("源 JSON 缺少 records 数组")

		items: list[dict[str, Any]] = []
		stats = ExportStats(record_count=len(records), example_count=0)

		for record_index, record in enumerate(records, start=1):
			if not isinstance(record, dict):
				continue

			word_payload = record.get("word")
			meaning_payload = record.get("word_meaning")
			examples = record.get("word_example")
			if not isinstance(examples, list):
				continue

			word = word_payload.get("word") if isinstance(word_payload, dict) else ""
			word_meaning = meaning_payload.get("meaning_zh") if isinstance(meaning_payload, dict) else ""

			for example_index, example in enumerate(examples, start=1):
				if not isinstance(example, dict):
					continue

				item = {
					"__word_example_id": _parse_int(example.get("id")),
					"__meaning_id": _parse_int(example.get("meaning_id")),
					"__example_index": example_index,
					"word": _to_text(word),
					"word_meaning": _to_text(word_meaning),
					"example_en": _to_text(example.get("example_en")),
					"example_zh": _to_text(example.get("example_zh")),
					"selection_en": _to_text(example.get("selection_en")),
					"selection_zh": _to_text(example.get("selection_zh")),
				}
				exported_item = {field: item.get(field, "") for field in selected_fields}
				for field in INTERNAL_IMPORT_FIELDS:
					exported_item[field] = item[field]
				items.append(exported_item)
				stats.example_count += 1

		result = {
			"record_count": stats.record_count,
			"example_count": stats.example_count,
			"fields": selected_fields,
		}
		json_lines = "\n".join(json.dumps(item, ensure_ascii=False, separators=(",", ": ")) for item in items)
		if json_lines:
			json_lines += "\n"
		output_json_path.write_text(json_lines, encoding="utf-8")
		return result, stats

	def import_selection_json(
		self,
		selection_json_path: Path,
		db_path: Path,
		field_mappings: list[tuple[str, str]],
	) -> ImportStats:
		if not selection_json_path.exists():
			raise FileNotFoundError(f"待导入 JSON 不存在: {selection_json_path}")
		if not db_path.exists():
			raise FileNotFoundError(f"words.db 不存在: {db_path}")
		if not field_mappings:
			raise ValueError("至少选择一个导入映射")

		items = self._load_items(selection_json_path)
		stats = ImportStats(processed=len(items), updated=0, skipped=0, not_found=0)

		with sqlite3.connect(db_path) as conn:
			conn.row_factory = sqlite3.Row
			self._ensure_selection_columns(conn)
			available_columns = set(self.get_word_example_import_columns(db_path))
			invalid_targets = [target for _, target in field_mappings if target not in available_columns]
			if invalid_targets:
				raise ValueError(f"目标列不存在: {', '.join(sorted(set(invalid_targets)))}")
			existing_ids = {int(row[0]) for row in conn.execute("SELECT id FROM word_example").fetchall()}
			meaning_row_ids: dict[int, list[int]] = {}

			for line_number, item in enumerate(items, start=1):
				assignments: dict[str, str | None] = {}
				has_any_source_field = False
				for source_field, target_column in field_mappings:
					if source_field not in item:
						continue
					has_any_source_field = True
					assignments[target_column] = _to_text(item.get(source_field)) or None
				if not has_any_source_field:
					stats.skipped += 1
					continue

				word_example_id = self._resolve_word_example_id(
					conn,
					item,
					line_number,
					existing_ids,
					meaning_row_ids,
				)
				if word_example_id is None:
					stats.not_found += 1
					continue

				columns_sql = ", ".join(f"{column} = ?" for column in assignments)
				params = [assignments[column] for column in assignments]
				params.append(word_example_id)

				rowcount = conn.execute(
					f"UPDATE word_example SET {columns_sql} WHERE id = ?",
					params,
				).rowcount

				if rowcount == 0:
					stats.not_found += 1
				else:
					stats.updated += rowcount

			conn.commit()

		return stats

	def get_available_import_source_fields(self, selection_json_path: Path) -> list[str]:
		fields = list(FIELD_ORDER)
		if not selection_json_path.exists():
			return fields
		for item in self._load_items(selection_json_path):
			for field in item:
				if field in INTERNAL_IMPORT_FIELDS or field in fields:
					continue
				fields.append(field)
		return fields

	def get_word_example_import_columns(self, db_path: Path) -> list[str]:
		columns = list(DEFAULT_IMPORT_TARGET_COLUMNS)
		if not db_path.exists():
			return columns
		with sqlite3.connect(db_path) as conn:
			actual_columns = [str(row[1]) for row in conn.execute("PRAGMA table_info(word_example)").fetchall()]
		filtered = [column for column in actual_columns if column not in {"id", "meaning_id"}]
		for column in ("selection_zh", "selection_en"):
			if column not in filtered:
				filtered.append(column)
		return filtered or columns

	def _load_items(self, selection_json_path: Path) -> list[dict[str, Any]]:
		text = selection_json_path.read_text(encoding="utf-8")
		items: list[dict[str, Any]] = []
		for raw_line in text.splitlines():
			line = raw_line.strip()
			if not line:
				continue
			payload = json.loads(line)
			if not isinstance(payload, dict):
				raise ValueError("JSONL 格式无效，每一行必须是 JSON 对象")
			items.append(payload)

		return items

	def clear_fields(self, selection_json_path: Path, fields: list[str]) -> FileUpdateStats:
		if not selection_json_path.exists():
			raise FileNotFoundError(f"待处理 JSON 不存在: {selection_json_path}")
		selected_fields = self._normalize_fields(fields)
		items = self._load_items(selection_json_path)
		updated = 0
		for item in items:
			changed = False
			for field in selected_fields:
				if _to_text(item.get(field)):
					changed = True
				item[field] = ""
			if changed:
				updated += 1
		self._write_items(selection_json_path, items)
		return FileUpdateStats(processed=len(items), updated=updated)

	def populate_random_selection_words(self, selection_json_path: Path, sample_size: int = 5) -> FileUpdateStats:
		if not selection_json_path.exists():
			raise FileNotFoundError(f"待处理 JSON 不存在: {selection_json_path}")
		items = self._load_items(selection_json_path)
		word_pool = []
		seen_words: set[str] = set()
		for item in items:
			word = _to_text(item.get("word"))
			if not word:
				continue
			word_key = word.casefold()
			if word_key in seen_words:
				continue
			seen_words.add(word_key)
			word_pool.append(word)

		if not word_pool:
			raise ValueError("当前 JSON 中没有可用于随机填充的 word 字段内容")

		actual_size = min(sample_size, len(word_pool))
		updated = 0
		for item in items:
			choices = random.sample(word_pool, actual_size)
			value = " ".join(choices)
			if _to_text(item.get("selection_en")) != value:
				updated += 1
			item["selection_en"] = value

		self._write_items(selection_json_path, items)
		return FileUpdateStats(processed=len(items), updated=updated)

	def _write_items(self, selection_json_path: Path, items: list[dict[str, Any]]) -> None:
		json_lines = "\n".join(json.dumps(item, ensure_ascii=False, separators=(",", ": ")) for item in items)
		if json_lines:
			json_lines += "\n"
		selection_json_path.write_text(json_lines, encoding="utf-8")

	def _resolve_word_example_id(
		self,
		conn: sqlite3.Connection,
		item: dict[str, Any],
		line_number: int,
		existing_ids: set[int],
		meaning_row_ids: dict[int, list[int]],
	) -> int | None:
		word_example_id = _parse_int(item.get("__word_example_id"))
		if word_example_id is None:
			word_example_id = _parse_int(item.get("word_example_id"))
		if word_example_id is None:
			word_example_id = _parse_int(item.get("id"))
		if word_example_id is not None and word_example_id in existing_ids:
			return word_example_id

		meaning_id = _parse_int(item.get("__meaning_id"))
		if meaning_id is None:
			meaning_id = _parse_int(item.get("meaning_id"))
		example_index = _parse_int(item.get("__example_index"))
		if example_index is None:
			example_index = _parse_int(item.get("example_index"))
		if meaning_id is not None and example_index is not None and example_index > 0:
			row_ids = meaning_row_ids.get(meaning_id)
			if row_ids is None:
				row_ids = [
					int(row[0])
					for row in conn.execute(
						"SELECT id FROM word_example WHERE meaning_id = ? ORDER BY id",
						(meaning_id,),
					).fetchall()
				]
				meaning_row_ids[meaning_id] = row_ids
			if example_index <= len(row_ids):
				return row_ids[example_index - 1]

		return line_number if line_number in existing_ids else None

	def _normalize_fields(self, fields: list[str]) -> list[str]:
		selected_fields = [field for field in FIELD_ORDER if field in fields]
		if not selected_fields:
			raise ValueError("至少选择一个字段")
		return selected_fields

	def _ensure_selection_columns(self, conn: sqlite3.Connection) -> None:
		columns = {str(row[1]) for row in conn.execute("PRAGMA table_info(word_example)").fetchall()}
		if "selection_zh" not in columns:
			conn.execute("ALTER TABLE word_example ADD COLUMN selection_zh TEXT")
		if "selection_en" not in columns:
			conn.execute("ALTER TABLE word_example ADD COLUMN selection_en TEXT")


class MainWindow(QMainWindow):
	def __init__(self) -> None:
		super().__init__()
		self.service = Stage1SelectionJsonService()
		self.import_mapping_combos: dict[str, QComboBox] = {}
		self.setWindowTitle("Stage1 Selection JSON 导出/导入")
		self.resize(920, 620)
		self._build_ui()

	def _build_ui(self) -> None:
		central_widget = QWidget(self)
		self.setCentralWidget(central_widget)

		layout = QVBoxLayout(central_widget)
		layout.setContentsMargins(12, 12, 12, 12)
		layout.setSpacing(10)

		form_layout = QFormLayout()
		form_layout.setSpacing(10)

		self.source_json_edit = self._create_path_row(
			form_layout,
			"源 JSON",
			str(DEFAULT_SOURCE_JSON),
			self._browse_source_json,
		)
		self.output_json_edit = self._create_path_row(
			form_layout,
			"导出 JSON",
			str(DEFAULT_OUTPUT_JSON),
			self._browse_output_json,
		)
		self.db_path_edit = self._create_path_row(
			form_layout,
			"words.db",
			str(DEFAULT_DB_PATH),
			self._browse_db,
		)

		layout.addLayout(form_layout)

		field_layout = QGridLayout()
		field_layout.setHorizontalSpacing(16)
		field_layout.setVerticalSpacing(8)

		field_layout.addWidget(QLabel("导出字段"), 0, 0)
		self.export_field_checks = self._create_field_checkboxes(DEFAULT_EXPORT_FIELDS)
		field_layout.addLayout(self._build_checkbox_row(self.export_field_checks), 0, 1)

		field_layout.addWidget(QLabel("清空字段"), 1, 0)
		self.clear_field_checks = self._create_field_checkboxes(DEFAULT_CLEAR_FIELDS)
		field_layout.addLayout(self._build_checkbox_row(self.clear_field_checks), 1, 1)

		field_layout.addWidget(QLabel("导入映射"), 2, 0)
		self.import_mapping_widget = QWidget(self)
		self.import_mapping_layout = QFormLayout(self.import_mapping_widget)
		self.import_mapping_layout.setContentsMargins(0, 0, 0, 0)
		self.import_mapping_layout.setSpacing(8)
		field_layout.addWidget(self.import_mapping_widget, 2, 1)

		layout.addLayout(field_layout)

		button_row = QHBoxLayout()
		self.export_button = QPushButton("1. 导出 selection 模板 JSON")
		self.export_button.clicked.connect(self.export_template)
		button_row.addWidget(self.export_button)

		self.import_button = QPushButton("2. 按映射导入到 words.db")
		self.import_button.clicked.connect(self.import_selection_json)
		button_row.addWidget(self.import_button)

		layout.addLayout(button_row)

		edit_button_row = QHBoxLayout()
		self.clear_fields_button = QPushButton("3. 清空选中字段")
		self.clear_fields_button.clicked.connect(self.clear_selected_fields)
		edit_button_row.addWidget(self.clear_fields_button)

		self.random_fill_button = QPushButton("4. 随机写入 5 个 word 到 selection_en")
		self.random_fill_button.clicked.connect(self.populate_random_selection_en)
		edit_button_row.addWidget(self.random_fill_button)

		layout.addLayout(edit_button_row)

		self.summary_label = QLabel(
			"导出文件为 JSONL，可勾选导出字段；导入时可为 word_example 的每个目标列选择任意源字段。文件内会附带隐藏定位字段，优先按 word_example_id 或 meaning_id+example_index 精确写回，旧文件仍兼容按行号回写。"
		)
		self.summary_label.setWordWrap(True)
		layout.addWidget(self.summary_label)

		self.log_edit = QPlainTextEdit(self)
		self.log_edit.setReadOnly(True)
		layout.addWidget(self.log_edit, 1)

		self._refresh_output_path()
		self._refresh_import_controls()
		self.log("准备就绪。先导出 JSONL 模板，再按映射把任意源字段写入 word_example 目标列。")

	def _create_path_row(
		self,
		form_layout: QFormLayout,
		label_text: str,
		default_path: str,
		browse_handler: Any,
	) -> QLineEdit:
		container = QWidget(self)
		row_layout = QHBoxLayout(container)
		row_layout.setContentsMargins(0, 0, 0, 0)
		row_layout.setSpacing(8)

		line_edit = QLineEdit(default_path, self)
		row_layout.addWidget(line_edit, 1)

		browse_button = QPushButton("浏览", self)
		browse_button.clicked.connect(browse_handler)
		row_layout.addWidget(browse_button)

		form_layout.addRow(label_text, container)
		return line_edit

	def _browse_source_json(self) -> None:
		current = Path(self.source_json_edit.text().strip() or str(DEFAULT_SOURCE_JSON))
		path, _ = QFileDialog.getOpenFileName(
			self,
			"选择源 JSON",
			str(current.parent if current.parent.exists() else PYTHON_SCRIPT_DIR),
			"JSON Files (*.json)",
		)
		if path:
			self.source_json_edit.setText(path)
			self._refresh_output_path()

	def _browse_output_json(self) -> None:
		current = Path(self.output_json_edit.text().strip() or str(DEFAULT_OUTPUT_JSON))
		path, _ = QFileDialog.getSaveFileName(
			self,
			"选择导出 JSON",
			str(current),
			"JSON Files (*.json)",
		)
		if path:
			self.output_json_edit.setText(path)
			self._refresh_import_controls()

	def _browse_db(self) -> None:
		current = Path(self.db_path_edit.text().strip() or str(DEFAULT_DB_PATH))
		path, _ = QFileDialog.getOpenFileName(
			self,
			"选择 words.db",
			str(current.parent if current.parent.exists() else PYTHON_SCRIPT_DIR),
			"SQLite DB (*.db *.sqlite *.sqlite3);;All Files (*)",
		)
		if path:
			self.db_path_edit.setText(path)
			self._refresh_import_controls()

	def export_template(self) -> None:
		source_json_path = Path(self.source_json_edit.text().strip())
		export_fields = self._selected_fields(self.export_field_checks)
		output_json_path = self._build_output_path(export_fields)
		self.output_json_edit.setText(str(output_json_path))
		try:
			result, stats = self.service.export_template(source_json_path, output_json_path, export_fields)
		except Exception as exc:
			self._show_error("导出失败", str(exc))
			return

		message = (
			f"已导出模板 JSONL: {output_json_path}\n"
			f"fields: {', '.join(result['fields'])}\n"
			f"records: {stats.record_count}\n"
			f"examples: {stats.example_count}\n"
			"meta: __word_example_id, __meaning_id, __example_index"
		)
		self._refresh_import_controls()
		self.log(message)
		QMessageBox.information(self, "导出完成", message)

	def import_selection_json(self) -> None:
		selection_json_path = Path(self.output_json_edit.text().strip())
		db_path = Path(self.db_path_edit.text().strip())
		self._refresh_import_controls()
		field_mappings = self._selected_import_mappings()
		try:
			stats = self.service.import_selection_json(selection_json_path, db_path, field_mappings)
		except Exception as exc:
			self._show_error("导入失败", str(exc))
			return

		message = (
			f"导入完成: {selection_json_path}\n"
			f"mappings: {', '.join(f'{source}->{target}' for source, target in field_mappings)}\n"
			f"processed: {stats.processed}\n"
			f"updated: {stats.updated}\n"
			f"skipped(no mapped source): {stats.skipped}\n"
			f"not_found: {stats.not_found}"
		)
		self.log(message)
		QMessageBox.information(self, "导入完成", message)

	def clear_selected_fields(self) -> None:
		selection_json_path = Path(self.output_json_edit.text().strip())
		fields = self._selected_fields(self.clear_field_checks)
		try:
			stats = self.service.clear_fields(selection_json_path, fields)
		except Exception as exc:
			self._show_error("清空失败", str(exc))
			return

		message = (
			f"已清空字段: {', '.join(fields)}\n"
			f"file: {selection_json_path}\n"
			f"processed: {stats.processed}\n"
			f"updated: {stats.updated}"
		)
		self.log(message)
		QMessageBox.information(self, "清空完成", message)

	def populate_random_selection_en(self) -> None:
		selection_json_path = Path(self.output_json_edit.text().strip())
		try:
			stats = self.service.populate_random_selection_words(selection_json_path, sample_size=5)
		except Exception as exc:
			self._show_error("随机填充失败", str(exc))
			return

		message = (
			f"已为 selection_en 随机写入 5 个 word\n"
			f"file: {selection_json_path}\n"
			f"processed: {stats.processed}\n"
			f"updated: {stats.updated}"
		)
		self.log(message)
		QMessageBox.information(self, "随机填充完成", message)

	def _create_field_checkboxes(self, checked_fields: tuple[str, ...]) -> dict[str, QCheckBox]:
		checkboxes: dict[str, QCheckBox] = {}
		for field in FIELD_ORDER:
			checkbox = QCheckBox(FIELD_LABELS[field], self)
			checkbox.setChecked(field in checked_fields)
			checkboxes[field] = checkbox
		return checkboxes

	def _build_checkbox_row(self, checkboxes: dict[str, QCheckBox]) -> QHBoxLayout:
		layout = QHBoxLayout()
		layout.setContentsMargins(0, 0, 0, 0)
		layout.setSpacing(12)
		for field in FIELD_ORDER:
			checkbox = checkboxes[field]
			layout.addWidget(checkbox)
			if checkboxes is self.export_field_checks:
				checkbox.toggled.connect(self._refresh_output_path)
		layout.addStretch(1)
		return layout

	def _selected_fields(self, checkboxes: dict[str, QCheckBox]) -> list[str]:
		selected_fields = [field for field in FIELD_ORDER if checkboxes[field].isChecked()]
		if not selected_fields:
			raise ValueError("至少选择一个字段")
		return selected_fields

	def _selected_import_mappings(self) -> list[tuple[str, str]]:
		mappings = []
		for target_column, combo in self.import_mapping_combos.items():
			source_field = str(combo.currentData() or "").strip()
			if source_field:
				mappings.append((source_field, target_column))
		if not mappings:
			raise ValueError("至少选择一个导入映射")
		return mappings

	def _refresh_output_path(self) -> None:
		try:
			output_path = self._build_output_path(self._selected_fields(self.export_field_checks))
		except Exception:
			return
		self.output_json_edit.setText(str(output_path))
		self._refresh_import_controls()

	def _build_output_path(self, export_fields: list[str]) -> Path:
		current_output = Path(self.output_json_edit.text().strip() or str(DEFAULT_OUTPUT_JSON))
		source_path = Path(self.source_json_edit.text().strip() or str(DEFAULT_SOURCE_JSON))
		directory = current_output.parent if current_output.parent.exists() else source_path.parent
		field_suffix = "_".join(export_fields)
		return directory / f"{field_suffix}.json"

	def _refresh_import_controls(self) -> None:
		selection_json_path = Path(self.output_json_edit.text().strip() or str(DEFAULT_OUTPUT_JSON))
		db_path = Path(self.db_path_edit.text().strip() or str(DEFAULT_DB_PATH))
		source_fields = self.service.get_available_import_source_fields(selection_json_path)
		target_columns = self.service.get_word_example_import_columns(db_path)
		previous_selection = {
			target_column: str(combo.currentData() or "")
			for target_column, combo in self.import_mapping_combos.items()
		}
		self.import_mapping_combos = {}
		self._clear_layout(self.import_mapping_layout)
		for target_column in target_columns:
			combo = QComboBox(self)
			combo.addItem("(不导入)", "")
			for source_field in source_fields:
				combo.addItem(source_field, source_field)
			selected_source = previous_selection.get(target_column, DEFAULT_IMPORT_MAPPINGS.get(target_column, ""))
			self._set_combo_value(combo, selected_source)
			self.import_mapping_layout.addRow(target_column, combo)
			self.import_mapping_combos[target_column] = combo

	def _clear_layout(self, layout: QFormLayout | QHBoxLayout | QVBoxLayout | QGridLayout) -> None:
		while layout.count():
			item = layout.takeAt(0)
			child_widget = item.widget()
			child_layout = item.layout()
			if child_widget is not None:
				child_widget.deleteLater()
			elif child_layout is not None:
				self._clear_layout(child_layout)

	def _set_combo_value(self, combo: QComboBox, value: str) -> None:
		for index in range(combo.count()):
			if str(combo.itemData(index) or "") == value:
				combo.setCurrentIndex(index)
				return
		combo.setCurrentIndex(0)

	def log(self, message: str) -> None:
		timestamp = datetime.now().strftime("%H:%M:%S")
		self.log_edit.appendPlainText(f"[{timestamp}] {message}")

	def _show_error(self, title: str, message: str) -> None:
		self.log(f"{title}: {message}")
		QMessageBox.critical(self, title, message)


def main() -> int:
	app = QApplication(sys.argv)
	window = MainWindow()
	window.show()
	return app.exec()


if __name__ == "__main__":
	raise SystemExit(main())