from __future__ import annotations

from collections import Counter
from contextlib import closing
import importlib
import json
import random
import re
import sqlite3
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


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

_ensure_dependency("xlrd")
_ensure_dependency("openpyxl")
_ensure_dependency("PySide6.QtWidgets", "PySide6")

import xlrd
from openpyxl import load_workbook

from PySide6.QtWidgets import (
	QAbstractItemView,
	QApplication,
	QComboBox,
	QFileDialog,
	QFrame,
	QGridLayout,
	QGroupBox,
	QHBoxLayout,
	QLabel,
	QLineEdit,
	QListWidget,
	QMainWindow,
	QMessageBox,
	QPlainTextEdit,
	QPushButton,
	QSplitter,
	QVBoxLayout,
	QWidget,
)


EXACT_WORD_HEADERS = (
	"word",
	"phonetic",
	"word_type",
	"image",
	"stage",
	"pos",
	"meaning_en",
	"meaning_zh",
	"source",
	"word_tag",
	"form_type",
	"form",
	"example_en",
	"example_zh",
	"difficulty",
	"example_tag",
	"selection_zh",
	"selection_en",
	"image_path",
)

WHITESPACE_PATTERN = re.compile(r"\s+")

STAGE_NAME_MAP = {
	"1": 1,
	"primary": 1,
	"xiaoxue": 1,
	"小学": 1,
	"2": 2,
	"junior": 2,
	"middleschool": 2,
	"juniorhigh": 2,
	"初中": 2,
	"七上": 2,
	"七下": 2,
	"八上": 2,
	"八下": 2,
	"九上": 2,
	"九下": 2,
	"3": 3,
	"senior": 3,
	"highschool": 3,
	"高中": 3,
	"4": 4,
	"cet4": 4,
	"四级": 4,
	"5": 5,
	"cet6": 5,
	"六级": 5,
	"6": 6,
	"postgraduate": 6,
	"考研": 6,
	"7": 7,
	"tem4": 7,
	"专四": 7,
	"8": 8,
	"tem8": 8,
	"专八": 8,
	"9": 9,
	"toefl": 9,
	"托福": 9,
	"10": 10,
	"ielts": 10,
	"雅思": 10,
	"11": 11,
	"gre": 11,
}


def _normalize_header_name(value: object) -> str:
	text = _to_text(value).strip().lower()
	return re.sub(r"[^a-z0-9\u4e00-\u9fff]+", "", text)


def _to_text(value: object) -> str:
	if value is None:
		return ""
	if isinstance(value, str):
		return value.strip()
	if isinstance(value, float) and value.is_integer():
		return str(int(value))
	return str(value).strip()


def _is_blank(value: object) -> bool:
	return _to_text(value) == ""


def _normalize_spaces(value: object) -> str:
	if value is None:
		return ""
	return WHITESPACE_PATTERN.sub(" ", str(value).strip())


def _parse_int(value: object, default: int | None = None) -> int | None:
	text = _to_text(value)
	if not text:
		return default
	try:
		return int(float(text))
	except ValueError:
		return default


def _detect_stage_from_name(path: Path) -> int | None:
	match = re.search(r"stage[_-]?(\d+)", path.stem, flags=re.IGNORECASE)
	if match:
		return int(match.group(1))
	return None


def _parse_stage(value: object, fallback: int | None = None) -> int | None:
	if value is None:
		return fallback
	if isinstance(value, (int, float)):
		return int(value)

	text = _to_text(value)
	if not text:
		return fallback

	number = _parse_int(text)
	if number is not None:
		return number

	normalized = _normalize_header_name(text)
	if normalized in STAGE_NAME_MAP:
		return STAGE_NAME_MAP[normalized]
	return fallback


@dataclass
class ImportSummary:
	action: str
	processed: int = 0
	added: int = 0
	updated: int = 0
	skipped: int = 0
	failed: int = 0
	details: list[str] = field(default_factory=list)

	def add_error(self, message: str) -> None:
		self.failed += 1
		self.add_detail(message)

	def add_detail(self, message: str) -> None:
		self.details.append(message)

	def merge(self, other: "ImportSummary") -> None:
		self.processed += other.processed
		self.added += other.added
		self.updated += other.updated
		self.skipped += other.skipped
		self.failed += other.failed
		self.details.extend(other.details)

	def to_message(self, max_details: int | None = None) -> str:
		base = (
			f"{self.action}完成：处理 {self.processed} 条，"
			f"新增 {self.added} 条，更新 {self.updated} 条，"
			f"跳过 {self.skipped} 条，失败 {self.failed} 条。"
		)
		if not self.details:
			return base

		details = self.details if max_details is None else self.details[:max_details]
		message = base + "\n" + "\n".join(details)
		if max_details is not None and len(self.details) > len(details):
			message += f"\n... 另有 {len(self.details) - len(details)} 条明细未展示，请查看日志。"
		return message


@dataclass
class ExcelReadResult:
	records: list[dict[str, object]] = field(default_factory=list)
	header_row_number: int | None = None
	unmatched_header_row_number: int | None = None
	unmatched_headers: list[str] = field(default_factory=list)
	validation_issues: list[str] = field(default_factory=list)
	error_message: str | None = None


class DatabaseService:
	def __init__(self, base_dir: Path) -> None:
		self.base_dir = base_dir
		self.words_db_path = self.base_dir / "words.db"
		self.question_db_path = self.base_dir / "question.db"
		self.user_db_path = self.base_dir / "user.db"
		self._database_paths = {
			"words.db": self.words_db_path,
			"question.db": self.question_db_path,
			"user.db": self.user_db_path,
		}

	def ensure_all_databases(self) -> None:
		self._ensure_words_schema()
		self._ensure_question_schema()
		self._ensure_user_schema()

	def get_counts(self) -> dict[str, int]:
		self.ensure_all_databases()
		return {
			"word": self._fetch_scalar(self.words_db_path, "SELECT COUNT(*) FROM word"),
			"word_meaning": self._fetch_scalar(self.words_db_path, "SELECT COUNT(*) FROM word_meaning"),
			"word_form": self._fetch_scalar(self.words_db_path, "SELECT COUNT(*) FROM word_form"),
			"word_example": self._fetch_scalar(self.words_db_path, "SELECT COUNT(*) FROM word_example"),
			"question_bank": self._fetch_scalar(self.question_db_path, "SELECT COUNT(*) FROM question_bank"),
		}

	def clear_words_database(self) -> None:
		self.clear_database("words.db")

	def clear_question_database(self) -> None:
		self.clear_database("question.db")

	def clear_user_database(self) -> None:
		self.clear_database("user.db")

	def clear_all_databases(self) -> None:
		self.clear_words_database()
		self.clear_question_database()
		self.clear_user_database()

	def get_clear_targets(self) -> dict[str, dict[str, list[str]]]:
		self.ensure_all_databases()
		targets: dict[str, dict[str, list[str]]] = {}
		for database_name, db_path in self._database_paths.items():
			tables: dict[str, list[str]] = {}
			for table_name in self._list_tables(db_path):
				columns = [
					column_info["name"]
					for column_info in self._get_table_columns(db_path, table_name)
					if self._is_column_clearable(column_info)
				]
				tables[table_name] = columns
			targets[database_name] = tables
		return targets

	def clear_database(self, database_name: str) -> dict[str, int]:
		db_path = self._get_database_path(database_name)
		self._ensure_database_schema(database_name)
		cleared_rows: dict[str, int] = {}
		with sqlite3.connect(db_path) as conn:
			conn.execute("PRAGMA foreign_keys = OFF")
			for table_name in self._get_clear_sequence(database_name):
				row_count = self._count_rows(conn, table_name)
				conn.execute(f"DELETE FROM {self._quote_identifier(table_name)}")
				cleared_rows[table_name] = row_count
			conn.commit()
		return cleared_rows

	def clear_table(self, database_name: str, table_name: str) -> dict[str, int]:
		db_path = self._get_database_path(database_name)
		self._ensure_database_schema(database_name)
		self._validate_table_name(db_path, table_name)
		cleared_rows: dict[str, int] = {}
		with sqlite3.connect(db_path) as conn:
			conn.execute("PRAGMA foreign_keys = OFF")
			for target_table in self._get_clear_sequence(database_name, table_name):
				row_count = self._count_rows(conn, target_table)
				conn.execute(f"DELETE FROM {self._quote_identifier(target_table)}")
				cleared_rows[target_table] = row_count
			conn.commit()
		return cleared_rows

	def clear_field(self, database_name: str, table_name: str, field_name: str) -> tuple[int, str]:
		db_path = self._get_database_path(database_name)
		self._ensure_database_schema(database_name)
		columns = {
			column_info["name"]: column_info
			for column_info in self._get_table_columns(db_path, table_name)
		}
		if field_name not in columns:
			raise ValueError(f"字段不存在：{table_name}.{field_name}")

		column_info = columns[field_name]
		if not self._is_column_clearable(column_info):
			raise ValueError(f"字段不支持清空：{table_name}.{field_name}")

		reset_expr, reset_desc = self._build_clear_field_expression(column_info)
		with sqlite3.connect(db_path) as conn:
			row_count = self._count_rows(conn, table_name)
			conn.execute(
				f"UPDATE {self._quote_identifier(table_name)} SET {self._quote_identifier(field_name)} = {reset_expr}"
			)
			conn.commit()
		return row_count, reset_desc

	def compare_databases(self, left_db_path: Path, right_db_path: Path) -> tuple[str, list[str]]:
		if not left_db_path.exists():
			raise FileNotFoundError(f"数据库不存在：{left_db_path}")
		if not right_db_path.exists():
			raise FileNotFoundError(f"数据库不存在：{right_db_path}")

		left_tables = set(self._list_tables(left_db_path))
		right_tables = set(self._list_tables(right_db_path))
		all_tables = sorted(left_tables | right_tables)

		structure_diff_count = 0
		content_diff_count = 0
		details: list[str] = []

		for table_name in all_tables:
			if table_name not in left_tables:
				structure_diff_count += 1
				details.append(f"[表缺失] 仅数据库B存在表 {table_name}")
				continue
			if table_name not in right_tables:
				structure_diff_count += 1
				details.append(f"[表缺失] 仅数据库A存在表 {table_name}")
				continue

			left_columns_info = self._get_table_columns(left_db_path, table_name)
			right_columns_info = self._get_table_columns(right_db_path, table_name)
			left_columns = [str(column_info["name"]) for column_info in left_columns_info]
			right_columns = [str(column_info["name"]) for column_info in right_columns_info]
			common_columns = [column_name for column_name in left_columns if column_name in right_columns]

			column_messages: list[str] = []
			left_only_columns = [column_name for column_name in left_columns if column_name not in right_columns]
			right_only_columns = [column_name for column_name in right_columns if column_name not in left_columns]
			if left_only_columns:
				column_messages.append(f"仅数据库A存在字段: {', '.join(left_only_columns)}")
			if right_only_columns:
				column_messages.append(f"仅数据库B存在字段: {', '.join(right_only_columns)}")
			if left_columns != right_columns:
				left_shared_order = [column_name for column_name in left_columns if column_name in common_columns]
				right_shared_order = [column_name for column_name in right_columns if column_name in common_columns]
				if left_shared_order != right_shared_order:
					column_messages.append("共同字段顺序不一致")
			if column_messages:
				structure_diff_count += 1
				details.append(f"[字段差异] {table_name}: {'；'.join(column_messages)}")

			if not common_columns:
				continue

			pk_columns = self._get_common_primary_key_columns(left_columns_info, right_columns_info)
			if pk_columns:
				table_diff_count, table_details = self._compare_table_rows_by_primary_key(
					left_db_path,
					right_db_path,
					table_name,
					common_columns,
					pk_columns,
				)
			else:
				table_diff_count, table_details = self._compare_table_rows_by_multiset(
					left_db_path,
					right_db_path,
					table_name,
					common_columns,
				)

			content_diff_count += table_diff_count
			details.extend(table_details)

		if details:
			summary = (
				f"比对完成：共检查 {len(all_tables)} 张表，"
				f"发现 {structure_diff_count} 项结构差异，{content_diff_count} 项内容差异。"
			)
		else:
			summary = f"比对完成：共检查 {len(all_tables)} 张表，未发现任何结构或内容差异。"
		return summary, details

	def _get_database_path(self, database_name: str) -> Path:
		if database_name not in self._database_paths:
			raise ValueError(f"不支持的数据库：{database_name}")
		return self._database_paths[database_name]

	def _ensure_database_schema(self, database_name: str) -> None:
		if database_name == "words.db":
			self._ensure_words_schema()
			return
		if database_name == "question.db":
			self._ensure_question_schema()
			return
		if database_name == "user.db":
			self._ensure_user_schema()
			return
		raise ValueError(f"不支持的数据库：{database_name}")

	def _list_tables(self, db_path: Path) -> list[str]:
		with sqlite3.connect(db_path) as conn:
			rows = conn.execute(
				"SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name"
			).fetchall()
		return [str(row[0]) for row in rows]

	def _get_table_columns(self, db_path: Path, table_name: str) -> list[dict[str, object]]:
		self._validate_table_name(db_path, table_name)
		with sqlite3.connect(db_path) as conn:
			rows = conn.execute(f"PRAGMA table_info({self._quote_identifier(table_name)})").fetchall()
		return [
			{
				"name": str(row[1]),
				"type": str(row[2] or ""),
				"not_null": int(row[3]),
				"default_value": row[4],
				"pk": int(row[5]),
			}
			for row in rows
		]

	def _validate_table_name(self, db_path: Path, table_name: str) -> None:
		if table_name not in self._list_tables(db_path):
			raise ValueError(f"数据表不存在：{table_name}")

	def _fetch_table_rows(self, db_path: Path, table_name: str, column_names: list[str]) -> list[dict[str, object]]:
		self._validate_table_name(db_path, table_name)
		columns_sql = ", ".join(self._quote_identifier(column_name) for column_name in column_names)
		with sqlite3.connect(db_path) as conn:
			conn.row_factory = sqlite3.Row
			rows = conn.execute(
				f"SELECT {columns_sql} FROM {self._quote_identifier(table_name)}"
			).fetchall()
		return [
			{column_name: row[column_name] for column_name in column_names}
			for row in rows
		]

	@staticmethod
	def _quote_identifier(identifier: str) -> str:
		return '"' + identifier.replace('"', '""') + '"'

	@staticmethod
	def _count_rows(conn: sqlite3.Connection, table_name: str) -> int:
		value = conn.execute(f"SELECT COUNT(*) FROM {DatabaseService._quote_identifier(table_name)}").fetchone()[0]
		return int(value)

	@staticmethod
	def _get_common_primary_key_columns(
		left_columns_info: list[dict[str, object]],
		right_columns_info: list[dict[str, object]],
	) -> list[str]:
		left_pk_columns = [
			str(column_info["name"])
			for column_info in sorted(left_columns_info, key=lambda item: int(item["pk"]))
			if int(column_info["pk"])
		]
		right_pk_columns = [
			str(column_info["name"])
			for column_info in sorted(right_columns_info, key=lambda item: int(item["pk"]))
			if int(column_info["pk"])
		]
		if left_pk_columns and left_pk_columns == right_pk_columns:
			return left_pk_columns
		return []

	def _compare_table_rows_by_primary_key(
		self,
		left_db_path: Path,
		right_db_path: Path,
		table_name: str,
		common_columns: list[str],
		pk_columns: list[str],
	) -> tuple[int, list[str]]:
		left_rows = self._fetch_table_rows(left_db_path, table_name, common_columns)
		right_rows = self._fetch_table_rows(right_db_path, table_name, common_columns)

		left_map = {self._build_row_key(row, pk_columns): row for row in left_rows}
		right_map = {self._build_row_key(row, pk_columns): row for row in right_rows}
		all_keys = sorted(set(left_map) | set(right_map), key=str)
		details: list[str] = []
		difference_count = 0

		for row_key in all_keys:
			if row_key not in left_map:
				difference_count += 1
				details.append(
					f"[内容差异] {table_name}: 主键 {self._format_row_key(row_key)} 仅数据库B存在"
				)
				continue
			if row_key not in right_map:
				difference_count += 1
				details.append(
					f"[内容差异] {table_name}: 主键 {self._format_row_key(row_key)} 仅数据库A存在"
				)
				continue

			left_row = left_map[row_key]
			right_row = right_map[row_key]
			for column_name in common_columns:
				if self._normalize_compare_value(left_row.get(column_name)) == self._normalize_compare_value(
					right_row.get(column_name)
				):
					continue
				difference_count += 1
				details.append(
					f"[内容差异] {table_name}: 主键 {self._format_row_key(row_key)} 字段 {column_name} 不一致，"
					f"数据库A={self._format_compare_value(left_row.get(column_name))}，"
					f"数据库B={self._format_compare_value(right_row.get(column_name))}"
				)
		return difference_count, details

	def _compare_table_rows_by_multiset(
		self,
		left_db_path: Path,
		right_db_path: Path,
		table_name: str,
		common_columns: list[str],
	) -> tuple[int, list[str]]:
		left_rows = self._fetch_table_rows(left_db_path, table_name, common_columns)
		right_rows = self._fetch_table_rows(right_db_path, table_name, common_columns)
		left_counter = Counter(self._serialize_row_signature(row, common_columns) for row in left_rows)
		right_counter = Counter(self._serialize_row_signature(row, common_columns) for row in right_rows)
		details: list[str] = []
		difference_count = 0

		for row_signature in sorted(set(left_counter) | set(right_counter)):
			left_count = left_counter.get(row_signature, 0)
			right_count = right_counter.get(row_signature, 0)
			if left_count == right_count:
				continue
			row_preview = self._truncate_text(row_signature, 220)
			if left_count > right_count:
				difference_count += left_count - right_count
				details.append(
					f"[内容差异] {table_name}: 数据库A 比数据库B 多 {left_count - right_count} 行，样例 {row_preview}"
				)
			else:
				difference_count += right_count - left_count
				details.append(
					f"[内容差异] {table_name}: 数据库B 比数据库A 多 {right_count - left_count} 行，样例 {row_preview}"
				)
		return difference_count, details

	@staticmethod
	def _build_row_key(row: dict[str, object], pk_columns: list[str]) -> tuple[object, ...]:
		return tuple(DatabaseService._normalize_compare_value(row.get(column_name)) for column_name in pk_columns)

	@staticmethod
	def _format_row_key(row_key: tuple[object, ...]) -> str:
		return json.dumps(list(row_key), ensure_ascii=False)

	@staticmethod
	def _normalize_compare_value(value: object) -> object:
		if isinstance(value, bytes):
			return {"__bytes__": value.hex()}
		return value

	@staticmethod
	def _format_compare_value(value: object) -> str:
		return json.dumps(DatabaseService._normalize_compare_value(value), ensure_ascii=False)

	@staticmethod
	def _serialize_row_signature(row: dict[str, object], column_names: list[str]) -> str:
		normalized_row = {
			column_name: DatabaseService._normalize_compare_value(row.get(column_name))
			for column_name in column_names
		}
		return json.dumps(normalized_row, ensure_ascii=False, sort_keys=True)

	@staticmethod
	def _truncate_text(text: str, limit: int) -> str:
		if len(text) <= limit:
			return text
		return text[: limit - 3] + "..."

	@staticmethod
	def _is_column_clearable(column_info: dict[str, object]) -> bool:
		if int(column_info["pk"]):
			return False
		if int(column_info["not_null"]) and column_info["default_value"] is None:
			return False
		return True

	@staticmethod
	def _build_clear_field_expression(column_info: dict[str, object]) -> tuple[str, str]:
		default_value = column_info["default_value"]
		if default_value is not None:
			return str(default_value), f"恢复默认值 {default_value}"
		return "NULL", "清空为 NULL"

	@staticmethod
	def _get_clear_sequence(database_name: str, table_name: str | None = None) -> list[str]:
		if database_name == "words.db":
			dependencies = {
				"word": ["word_example", "word_form", "word_meaning", "word"],
				"word_meaning": ["word_example", "word_meaning"],
				"word_form": ["word_form"],
				"word_example": ["word_example"],
			}
			if table_name is not None:
				return dependencies.get(table_name, [table_name])
			return ["word_example", "word_form", "word_meaning", "word"]

		if database_name == "question.db":
			return [table_name] if table_name is not None else ["question_bank"]

		user_tables = [
			"learning_stats_daily",
			"game_profile",
			"sync_state",
			"app_state",
			"word_learning_profile",
			"word_practice_history",
			"ai_speech_evaluations",
			"ai_detected_errors",
			"ai_messages",
			"ai_sessions",
			"game_rewards_log",
			"tasks",
			"vocab_items",
		]
		if database_name == "user.db":
			return [table_name] if table_name is not None else user_tables

		raise ValueError(f"不支持的数据库：{database_name}")

	def import_words(self, excel_files: Iterable[Path], mode: str) -> ImportSummary:
		self._ensure_words_schema()
		summary = ImportSummary(action=f"导入 {mode}")
		files = [path for path in excel_files if path.exists() and not path.name.startswith("~$")]
		if not files:
			summary.add_error("未选择有效的 Excel 文件。")
			return summary

		with sqlite3.connect(self.words_db_path) as conn:
			conn.execute("PRAGMA foreign_keys = ON")
			for path in files:
				file_summary = self._import_word_file(conn, path, mode)
				summary.merge(file_summary)
			conn.commit()
		return summary

	def import_questions(self, json_files: Iterable[Path]) -> ImportSummary:
		self._ensure_question_schema()
		summary = ImportSummary(action="导入 question.db")
		files = [path for path in json_files if path.exists()]
		if not files:
			summary.add_error("未选择有效的 JSON 文件。")
			return summary

		with sqlite3.connect(self.question_db_path) as conn:
			conn.execute("PRAGMA foreign_keys = ON")
			next_generated_id = self._fetch_scalar(self.question_db_path, "SELECT COALESCE(MAX(id), 0) FROM question_bank") + 1

			for path in files:
				try:
					questions = self._parse_questions(path)
				except Exception as exc:  # noqa: BLE001
					summary.add_error(f"{path.name} 解析失败：{exc}")
					continue

				for index, question in enumerate(questions, start=1):
					summary.processed += 1
					try:
						row, is_update, next_generated_id = self._normalize_question_row(
							conn=conn,
							question=question,
							source_path=path,
							next_generated_id=next_generated_id,
						)
						conn.execute(
							"""
							INSERT INTO question_bank (id, question_type, stage, difficulty, content_json)
							VALUES (?, ?, ?, ?, ?)
							ON CONFLICT(id) DO UPDATE SET
								question_type = excluded.question_type,
								stage = excluded.stage,
								difficulty = excluded.difficulty,
								content_json = excluded.content_json
							""",
							row,
						)
						if is_update:
							summary.updated += 1
						else:
							summary.added += 1
					except Exception as exc:  # noqa: BLE001
						summary.add_error(f"{path.name} 第 {index} 条题目导入失败：{exc}")

			conn.commit()
		return summary

	def _import_word_file(self, conn: sqlite3.Connection, path: Path, mode: str) -> ImportSummary:
		summary = ImportSummary(action=f"导入 {path.name}")
		read_result = self._read_excel_records(path)
		header_row_number = read_result.header_row_number or read_result.unmatched_header_row_number
		if read_result.unmatched_headers and header_row_number is not None:
			summary.add_detail(
				f"{path.name} 第 {header_row_number} 行未匹配字段：{', '.join(read_result.unmatched_headers)}"
			)
		if read_result.error_message:
			summary.add_error(read_result.error_message)
			return summary
		for issue in read_result.validation_issues:
			summary.add_error(issue)

		records = read_result.records
		fallback_stage = _detect_stage_from_name(path)

		for record in records:
			summary.processed += 1
			try:
				if mode == "word":
					result = self._upsert_word(conn, record)
					summary.added += result[0]
					summary.updated += result[1]
					summary.skipped += result[2]
				elif mode == "word_meaning":
					added, skipped = self._insert_word_meaning(conn, record, fallback_stage)
					summary.added += added
					summary.skipped += skipped
				elif mode == "word_form":
					added, skipped = self._insert_word_form(conn, record)
					summary.added += added
					summary.skipped += skipped
				elif mode == "word_example":
					added, skipped = self._insert_word_example(conn, record, fallback_stage)
					summary.added += added
					summary.skipped += skipped
				elif mode == "all":
					word_added, word_updated, word_skipped = self._upsert_word(conn, record)
					summary.added += word_added
					summary.updated += word_updated
					summary.skipped += word_skipped

					meaning_added, meaning_skipped = self._insert_word_meaning(conn, record, fallback_stage)
					summary.added += meaning_added
					summary.skipped += meaning_skipped

					form_added, form_skipped = self._insert_word_form(conn, record)
					summary.added += form_added
					summary.skipped += form_skipped

					example_added, example_skipped = self._insert_word_example(conn, record, fallback_stage)
					summary.added += example_added
					summary.skipped += example_skipped
				else:
					raise ValueError(f"不支持的导入模式：{mode}")
			except Exception as exc:  # noqa: BLE001
				row_no = record.get("_row_number", "?")
				summary.add_error(f"{path.name} 第 {row_no} 行失败：{exc}")
		return summary

	def _upsert_word(self, conn: sqlite3.Connection, record: dict[str, object]) -> tuple[int, int, int]:
		word = _to_text(record.get("word"))
		if not word:
			return 0, 0, 1

		phonetic = _to_text(record.get("phonetic"))
		word_type = _to_text(record.get("word_type"))
		image = _to_text(record.get("image"))

		cursor = conn.execute("SELECT id, phonetic, word_type, image FROM word WHERE word = ?", (word,))
		row = cursor.fetchone()
		if row is None:
			conn.execute(
				"INSERT INTO word (word, phonetic, word_type, image) VALUES (?, ?, ?, ?)",
				(word, phonetic or None, word_type or None, image or None),
			)
			return 1, 0, 0

		updates: list[str] = []
		params: list[object] = []
		if phonetic and not _to_text(row[1]):
			updates.append("phonetic = ?")
			params.append(phonetic)
		if word_type and not _to_text(row[2]):
			updates.append("word_type = ?")
			params.append(word_type)
		if image and not _to_text(row[3]):
			updates.append("image = ?")
			params.append(image)

		if not updates:
			return 0, 0, 1

		params.append(row[0])
		conn.execute(f"UPDATE word SET {', '.join(updates)} WHERE id = ?", params)
		return 0, 1, 0

	def _insert_word_meaning(
		self,
		conn: sqlite3.Connection,
		record: dict[str, object],
		fallback_stage: int | None,
	) -> tuple[int, int]:
		word = _to_text(record.get("word"))
		meaning_zh = _to_text(record.get("meaning_zh"))
		meaning_en = _to_text(record.get("meaning_en"))
		if not word or (not meaning_zh and not meaning_en):
			return 0, 1

		word_id = self._ensure_word_id(conn, record)
		stage = _parse_stage(record.get("stage"), fallback_stage)
		pos = _to_text(record.get("pos"))
		source = _to_text(record.get("source"))
		word_tag = _to_text(record.get("word_tag"))

		exists = conn.execute(
			"""
			SELECT id FROM word_meaning
			WHERE word_id = ?
			  AND COALESCE(stage, -1) = COALESCE(?, -1)
			  AND COALESCE(pos, '') = COALESCE(?, '')
			  AND COALESCE(meaning_en, '') = COALESCE(?, '')
			  AND COALESCE(meaning_zh, '') = COALESCE(?, '')
			  AND COALESCE(source, '') = COALESCE(?, '')
			  AND COALESCE(word_tag, '') = COALESCE(?, '')
			LIMIT 1
			""",
			(word_id, stage, pos, meaning_en, meaning_zh, source, word_tag),
		).fetchone()
		if exists:
			return 0, 1

		conn.execute(
			"""
			INSERT INTO word_meaning (
				word_id, stage, pos, meaning_en, meaning_zh, source, word_tag
			) VALUES (?, ?, ?, ?, ?, ?, ?)
			""",
			(word_id, stage, pos or None, meaning_en or None, meaning_zh or None, source or None, word_tag or None),
		)
		return 1, 0

	def _insert_word_form(self, conn: sqlite3.Connection, record: dict[str, object]) -> tuple[int, int]:
		word = _to_text(record.get("word"))
		form = _to_text(record.get("form"))
		if not word or not form:
			return 0, 1

		word_id = self._ensure_word_id(conn, record)
		form_type = _to_text(record.get("form_type"))
		exists = conn.execute(
			"""
			SELECT id FROM word_form
			WHERE word_id = ?
			  AND COALESCE(form_type, '') = COALESCE(?, '')
			  AND COALESCE(form, '') = COALESCE(?, '')
			LIMIT 1
			""",
			(word_id, form_type, form),
		).fetchone()
		if exists:
			return 0, 1

		conn.execute(
			"INSERT INTO word_form (word_id, form_type, form) VALUES (?, ?, ?)",
			(word_id, form_type or None, form),
		)
		return 1, 0

	def _insert_word_example(
		self,
		conn: sqlite3.Connection,
		record: dict[str, object],
		fallback_stage: int | None,
	) -> tuple[int, int]:
		word = _to_text(record.get("word"))
		example_en = _to_text(record.get("example_en"))
		example_zh = _to_text(record.get("example_zh"))
		if not word or (not example_en and not example_zh):
			return 0, 1

		word_id = self._ensure_word_id(conn, record)
		meaning_id = self._resolve_meaning_id(conn, word_id, record, fallback_stage)
		if meaning_id is None:
			meaning_added, _ = self._insert_word_meaning(conn, record, fallback_stage)
			if meaning_added == 0:
				return 0, 1
			meaning_id = self._resolve_meaning_id(conn, word_id, record, fallback_stage)
			if meaning_id is None:
				return 0, 1

		difficulty = _parse_int(record.get("difficulty"), 1) or 1
		image = _to_text(record.get("image"))
		example_tag = _to_text(record.get("example_tag"))
		selection_zh = _to_text(record.get("selection_zh"))
		selection_en = _to_text(record.get("selection_en"))

		exists = conn.execute(
			"""
			SELECT id FROM word_example
			WHERE meaning_id = ?
			  AND COALESCE(example_en, '') = COALESCE(?, '')
			  AND COALESCE(example_zh, '') = COALESCE(?, '')
			  AND COALESCE(difficulty, -1) = COALESCE(?, -1)
			  AND COALESCE(image, '') = COALESCE(?, '')
			  AND COALESCE(example_tag, '') = COALESCE(?, '')
			  AND COALESCE(selection_zh, '') = COALESCE(?, '')
			  AND COALESCE(selection_en, '') = COALESCE(?, '')
			LIMIT 1
			""",
			(meaning_id, example_en, example_zh, difficulty, image, example_tag, selection_zh, selection_en),
		).fetchone()
		if exists:
			return 0, 1

		conn.execute(
			"""
			INSERT INTO word_example (
				meaning_id, example_en, example_zh,
				difficulty, image, example_tag, selection_zh, selection_en
			) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
			""",
			(
				meaning_id,
				example_en or None,
				example_zh or None,
				difficulty,
				image or None,
				example_tag or None,
				selection_zh or None,
				selection_en or None,
			),
		)
		return 1, 0

	def _ensure_word_id(self, conn: sqlite3.Connection, record: dict[str, object]) -> int:
		word = _to_text(record.get("word"))
		if not word:
			raise ValueError("缺少 word 字段")
		self._upsert_word(conn, record)
		row = conn.execute("SELECT id FROM word WHERE word = ?", (word,)).fetchone()
		if row is None:
			raise ValueError(f"无法创建单词：{word}")
		return int(row[0])

	def _resolve_meaning_id(
		self,
		conn: sqlite3.Connection,
		word_id: int,
		record: dict[str, object],
		fallback_stage: int | None,
	) -> int | None:
		stage = _parse_stage(record.get("stage"), fallback_stage)
		meaning_zh = _to_text(record.get("meaning_zh"))
		meaning_en = _to_text(record.get("meaning_en"))
		pos = _to_text(record.get("pos"))

		row = conn.execute(
			"""
			SELECT id FROM word_meaning
			WHERE word_id = ?
			  AND COALESCE(stage, -1) = COALESCE(?, -1)
			  AND COALESCE(pos, '') = COALESCE(?, '')
			  AND COALESCE(meaning_en, '') = COALESCE(?, '')
			  AND COALESCE(meaning_zh, '') = COALESCE(?, '')
			ORDER BY id
			LIMIT 1
			""",
			(word_id, stage, pos, meaning_en, meaning_zh),
		).fetchone()
		if row is not None:
			return int(row[0])

		row = conn.execute(
			"SELECT id FROM word_meaning WHERE word_id = ? ORDER BY id LIMIT 1",
			(word_id,),
		).fetchone()
		if row is not None:
			return int(row[0])
		return None

	def _read_excel_records(self, path: Path) -> ExcelReadResult:
		rows = self._load_excel_rows(path)
		if not rows:
			return ExcelReadResult(error_message=f"{path.name} 表头识别失败：Excel 文件为空，未找到任何表头。")

		header_index, mapping, unmatched_headers, unmatched_header_index = self._detect_header(rows)
		if header_index is None:
			return ExcelReadResult(
				unmatched_header_row_number=unmatched_header_index + 1 if unmatched_header_index is not None else None,
				unmatched_headers=unmatched_headers,
				error_message=(
					f"{path.name} 表头识别失败：前 5 行内未找到合法表头。"
					f"表头必须存在，且字段名需与脚本字段名完全一致：{', '.join(EXACT_WORD_HEADERS)}"
				),
			)

		records: list[dict[str, object]] = []
		for row_number, row in enumerate(rows[header_index + 1 :], start=header_index + 2):
			if all(_is_blank(cell) for cell in row):
				continue
			record = self._record_from_mapped_row(path, row_number, row, mapping)
			if record is not None:
				records.append(record)
		validation_issues = self._validate_excel_word_records(records)
		return ExcelReadResult(
			records=records,
			header_row_number=header_index + 1,
			unmatched_header_row_number=header_index + 1 if unmatched_headers else None,
			unmatched_headers=unmatched_headers,
			validation_issues=validation_issues,
		)

	def _load_excel_rows(self, path: Path) -> list[list[object]]:
		suffix = path.suffix.lower()
		if suffix == ".xlsx":
			workbook = load_workbook(path, read_only=True, data_only=True)
			try:
				sheet = workbook[workbook.sheetnames[0]]
				return [list(row) for row in sheet.iter_rows(values_only=True)]
			finally:
				workbook.close()

		if suffix == ".xls":
			workbook = xlrd.open_workbook(path.as_posix())
			sheet = workbook.sheet_by_index(0)
			return [sheet.row_values(index) for index in range(sheet.nrows)]

		raise ValueError(f"不支持的 Excel 类型：{path.name}")

	def _detect_header(
		self,
		rows: list[list[object]],
	) -> tuple[int | None, dict[str, int], list[str], int | None]:
		max_scan = min(5, len(rows))
		best_unmatched_headers: list[str] = []
		best_unmatched_index: int | None = None
		best_matched_count = -1
		for index in range(max_scan):
			mapping, unmatched_headers = self._build_column_map(rows[index])
			if len(mapping) > best_matched_count:
				best_matched_count = len(mapping)
				best_unmatched_headers = unmatched_headers
				best_unmatched_index = index if unmatched_headers else None
			elif best_unmatched_index is None and unmatched_headers:
				best_unmatched_headers = unmatched_headers
				best_unmatched_index = index
			if len(mapping) >= 2 and "word" in mapping:
				return index, mapping, unmatched_headers, index
		return None, {}, best_unmatched_headers, best_unmatched_index

	def _build_column_map(self, header_row: list[object]) -> tuple[dict[str, int], list[str]]:
		mapping: dict[str, int] = {}
		unmatched_headers: list[str] = []
		for index, value in enumerate(header_row):
			header_name = _to_text(value)
			if not header_name:
				continue

			if header_name not in EXACT_WORD_HEADERS:
				if header_name not in unmatched_headers:
					unmatched_headers.append(header_name)
				continue

			mapping.setdefault(header_name, index)
		return mapping, unmatched_headers

	def _record_from_mapped_row(
		self,
		path: Path,
		row_number: int,
		row: list[object],
		mapping: dict[str, int],
	) -> dict[str, object] | None:
		record: dict[str, object] = {
			"_file": path.name,
			"_row_number": row_number,
		}
		for name, column_index in mapping.items():
			record[name] = row[column_index] if column_index < len(row) else None

		if all(_is_blank(record.get(key)) for key in ("word", "meaning_zh", "meaning_en", "form", "example_en", "example_zh")):
			return None
		return record

	def _validate_excel_word_records(self, records: list[dict[str, object]]) -> list[str]:
		issues: list[str] = []
		for record in records:
			issues.extend(self._validate_excel_word_record(record))
		return issues

	def _validate_excel_word_record(self, record: dict[str, object]) -> list[str]:
		word = _normalize_spaces(record.get("word"))
		if not word:
			return []

		file_name = _to_text(record.get("_file")) or "<unknown>"
		row_number = record.get("_row_number", "?")

		meaning_zh = _normalize_spaces(record.get("meaning_zh"))
		if not meaning_zh:
			return [
				f"{file_name} 第 {row_number} 行字段 meaning_zh 为空：word={word}"
			]

		return []

	def _parse_questions(self, file_path: Path) -> list[dict]:
		text = file_path.read_text(encoding="utf-8").strip()
		if not text:
			raise ValueError("JSON 文件为空")

		text = self._strip_json_line_comments(text)
		try:
			return self._extract_questions(json.loads(text))
		except json.JSONDecodeError as full_error:
			merged: list[dict] = []
			for block in self._split_top_level_blocks(text):
				try:
					merged.extend(self._extract_questions(json.loads(block)))
					continue
				except json.JSONDecodeError:
					pass
				for item_text in self._extract_question_item_blocks(block):
					try:
						item = json.loads(item_text)
					except json.JSONDecodeError:
						continue
					if isinstance(item, dict):
						merged.append(item)

			if not merged:
				raise ValueError(
					f"无法解析 JSON，首个错误位置：第 {full_error.lineno} 行，第 {full_error.colno} 列"
				) from full_error
			return merged

	def _normalize_question_row(
		self,
		conn: sqlite3.Connection,
		question: dict,
		source_path: Path,
		next_generated_id: int,
	) -> tuple[tuple[int, int, int | None, int, str], bool, int]:
		raw_id = question.get("id")
		if raw_id is None:
			question_id = next_generated_id
			next_generated_id += 1
		else:
			parsed = _parse_int(raw_id)
			if parsed is None:
				question_id = next_generated_id
				next_generated_id += 1
			else:
				question_id = parsed

		existing = conn.execute("SELECT 1 FROM question_bank WHERE id = ?", (question_id,)).fetchone() is not None

		question_type = _parse_int(question.get("question_type"))
		if question_type is None:
			question_type = random.randint(1, 10)

		stage = _parse_stage(question.get("stage"), _detect_stage_from_name(source_path))
		difficulty = _parse_int(question.get("difficulty"), 1) or 1

		content = question.get("content_json")
		if isinstance(content, str):
			try:
				parsed_content = json.loads(content)
			except json.JSONDecodeError:
				parsed_content = {"raw": content}
		elif isinstance(content, dict):
			parsed_content = dict(content)
		else:
			parsed_content = {}

		audio_path = (
			_to_text(question.get("audio_path"))
			or _to_text(parsed_content.get("audio_path"))
			or _to_text(question.get("audio"))
			or _to_text(parsed_content.get("audio"))
			or f"audio/question_{question_id}.ogg"
		)
		image_path = (
			_to_text(question.get("image_path"))
			or _to_text(parsed_content.get("image_path"))
			or self._pick_image_from_options(parsed_content)
			or f"image/question_{question_id}.png"
		)

		parsed_content["audio_path"] = audio_path
		parsed_content.setdefault("audio", audio_path)
		parsed_content["image_path"] = image_path
		if "answer" in question and "answer" not in parsed_content:
			parsed_content["answer"] = question["answer"]

		row = (
			question_id,
			question_type,
			stage,
			difficulty,
			json.dumps(parsed_content, ensure_ascii=False),
		)
		return row, existing, next_generated_id

	@staticmethod
	def _pick_image_from_options(content: dict) -> str:
		options = content.get("options")
		if not isinstance(options, list):
			return ""
		for option in options:
			if not isinstance(option, dict):
				continue
			image = _to_text(option.get("image"))
			if image:
				return image
		return ""

	@staticmethod
	def _strip_json_line_comments(text: str) -> str:
		out_chars: list[str] = []
		in_string = False
		escaped = False
		index = 0
		while index < len(text):
			char = text[index]
			if in_string:
				out_chars.append(char)
				if escaped:
					escaped = False
				elif char == "\\":
					escaped = True
				elif char == '"':
					in_string = False
				index += 1
				continue

			if char == '"':
				in_string = True
				out_chars.append(char)
				index += 1
				continue

			if char == "/" and index + 1 < len(text) and text[index + 1] == "/":
				index += 2
				while index < len(text) and text[index] not in "\r\n":
					index += 1
				continue

			out_chars.append(char)
			index += 1
		return "".join(out_chars)

	@staticmethod
	def _split_top_level_blocks(text: str) -> list[str]:
		blocks: list[str] = []
		index = 0
		while index < len(text):
			while index < len(text) and text[index].isspace():
				index += 1
			if index >= len(text) or text[index] not in "[{":
				index += 1
				continue

			start = index
			start_char = text[index]
			end_char = "]" if start_char == "[" else "}"
			depth = 0
			in_string = False
			escaped = False

			while index < len(text):
				char = text[index]
				if in_string:
					if escaped:
						escaped = False
					elif char == "\\":
						escaped = True
					elif char == '"':
						in_string = False
					index += 1
					continue

				if char == '"':
					in_string = True
					index += 1
					continue

				if char == start_char:
					depth += 1
				elif char == end_char:
					depth -= 1
					if depth == 0:
						index += 1
						blocks.append(text[start:index])
						break
				index += 1
		return blocks

	@staticmethod
	def _extract_question_item_blocks(block_text: str) -> list[str]:
		results: list[str] = []
		marker = '"questions"'
		search_position = 0
		while True:
			marker_index = block_text.find(marker, search_position)
			if marker_index == -1:
				break

			bracket_index = block_text.find("[", marker_index)
			if bracket_index == -1:
				break

			end_index = DatabaseService._find_matching_bracket(block_text, bracket_index)
			if end_index == -1:
				search_position = marker_index + len(marker)
				continue

			inner = block_text[bracket_index + 1 : end_index]
			results.extend(DatabaseService._split_object_items(inner))
			search_position = end_index + 1
		return results

	@staticmethod
	def _find_matching_bracket(text: str, start_index: int) -> int:
		depth = 0
		in_string = False
		escaped = False
		for index in range(start_index, len(text)):
			char = text[index]
			if in_string:
				if escaped:
					escaped = False
				elif char == "\\":
					escaped = True
				elif char == '"':
					in_string = False
				continue

			if char == '"':
				in_string = True
				continue

			if char == "[":
				depth += 1
			elif char == "]":
				depth -= 1
				if depth == 0:
					return index
		return -1

	@staticmethod
	def _split_object_items(text: str) -> list[str]:
		items: list[str] = []
		index = 0
		while index < len(text):
			while index < len(text) and text[index] != "{":
				index += 1
			if index >= len(text):
				break

			start = index
			depth = 0
			in_string = False
			escaped = False

			while index < len(text):
				char = text[index]
				if in_string:
					if escaped:
						escaped = False
					elif char == "\\":
						escaped = True
					elif char == '"':
						in_string = False
					index += 1
					continue

				if char == '"':
					in_string = True
					index += 1
					continue

				if char == "{":
					depth += 1
				elif char == "}":
					depth -= 1
					if depth == 0:
						index += 1
						items.append(text[start:index])
						break
				index += 1
		return items

	@staticmethod
	def _extract_questions(data: object) -> list[dict]:
		if isinstance(data, dict):
			if "questions" in data:
				parent_question_type = data.get("question_type")
				questions = data.get("questions")
				if not isinstance(questions, list):
					raise ValueError("questions 字段必须是数组")

				normalized: list[dict] = []
				for item in questions:
					if not isinstance(item, dict):
						continue
					row = dict(item)
					if "question_type" not in row and parent_question_type is not None:
						row["question_type"] = parent_question_type
					normalized.append(row)
				return normalized
			return [data]

		if isinstance(data, list):
			return [item for item in data if isinstance(item, dict)]
		raise ValueError("JSON 根节点必须是对象或数组")

	def _ensure_words_schema(self) -> None:
		with sqlite3.connect(self.words_db_path) as conn:
			conn.execute("PRAGMA foreign_keys = ON")
			conn.executescript(
				"""
				CREATE TABLE IF NOT EXISTS word (
					id INTEGER PRIMARY KEY,
					word TEXT NOT NULL UNIQUE,
					phonetic TEXT,
					word_type TEXT,
					image TEXT
				);

				CREATE TABLE IF NOT EXISTS word_meaning (
					id INTEGER PRIMARY KEY,
					word_id INTEGER NOT NULL,
					stage INTEGER,
					pos TEXT,
					meaning_en TEXT,
					meaning_zh TEXT,
					source TEXT,
					word_tag TEXT,
					FOREIGN KEY(word_id) REFERENCES word(id)
				);

				CREATE TABLE IF NOT EXISTS word_form (
					id INTEGER PRIMARY KEY,
					word_id INTEGER,
					form_type TEXT,
					form TEXT,
					FOREIGN KEY(word_id) REFERENCES word(id)
				);

				CREATE TABLE IF NOT EXISTS word_example (
					id INTEGER PRIMARY KEY,
					meaning_id INTEGER,
					example_en TEXT,
					example_zh TEXT,
					difficulty INTEGER,
					image TEXT,
					example_tag TEXT,
					selection_zh TEXT,
					selection_en TEXT,
					FOREIGN KEY(meaning_id) REFERENCES word_meaning(id)
				);

				CREATE INDEX IF NOT EXISTS idx_word_word ON word(word);
				CREATE INDEX IF NOT EXISTS idx_meaning_word ON word_meaning(word_id);
				CREATE INDEX IF NOT EXISTS idx_meaning_stage ON word_meaning(stage);
				CREATE INDEX IF NOT EXISTS idx_form_word ON word_form(word_id);
				CREATE INDEX IF NOT EXISTS idx_example_meaning ON word_example(meaning_id);
				"""
			)
			conn.commit()
			self._migrate_word_schema(conn)
			self._migrate_word_meaning_schema(conn)
			self._migrate_word_example_schema(conn)

	def _migrate_word_schema(self, conn: sqlite3.Connection) -> None:
		columns = [row[1] for row in conn.execute("PRAGMA table_info(word)").fetchall()]
		if "image" in columns:
			return

		conn.execute("ALTER TABLE word ADD COLUMN image TEXT")
		conn.commit()

	def _migrate_word_meaning_schema(self, conn: sqlite3.Connection) -> None:
		columns = [row[1] for row in conn.execute("PRAGMA table_info(word_meaning)").fetchall()]
		if "source" in columns and "image" not in columns:
			return

		if "source" in columns and "image" in columns:
			source_select = "COALESCE(source, image)"
		elif "source" in columns:
			source_select = "source"
		else:
			source_select = "image"
		conn.executescript(
			f"""
			CREATE TABLE word_meaning_new (
				id INTEGER PRIMARY KEY,
				word_id INTEGER NOT NULL,
				stage INTEGER,
				pos TEXT,
				meaning_en TEXT,
				meaning_zh TEXT,
				source TEXT,
				word_tag TEXT,
				FOREIGN KEY(word_id) REFERENCES word(id)
			);

			INSERT INTO word_meaning_new (
				id, word_id, stage, pos, meaning_en, meaning_zh, source, word_tag
			)
			SELECT
				id, word_id, stage, pos, meaning_en, meaning_zh, {source_select}, word_tag
			FROM word_meaning;

			DROP TABLE word_meaning;
			ALTER TABLE word_meaning_new RENAME TO word_meaning;
			CREATE INDEX IF NOT EXISTS idx_meaning_word ON word_meaning(word_id);
			CREATE INDEX IF NOT EXISTS idx_meaning_stage ON word_meaning(stage);
			"""
		)
		conn.commit()

	def _migrate_word_example_schema(self, conn: sqlite3.Connection) -> None:
		columns = [row[1] for row in conn.execute("PRAGMA table_info(word_example)").fetchall()]
		if (
			"stage" not in columns
			and "image" in columns
			and "source" not in columns
			and "audio_path" not in columns
			and "selection_zh" in columns
			and "selection_en" in columns
		):
			return

		image_select = "image" if "image" in columns else "source"
		selection_zh_select = "selection_zh" if "selection_zh" in columns else "''"
		selection_en_select = "selection_en" if "selection_en" in columns else "''"

		conn.executescript(
			f"""
			DROP INDEX IF EXISTS idx_example_stage;

			CREATE TABLE word_example_new (
				id INTEGER PRIMARY KEY,
				meaning_id INTEGER,
				example_en TEXT,
				example_zh TEXT,
				difficulty INTEGER,
				image TEXT,
				example_tag TEXT,
				selection_zh TEXT,
				selection_en TEXT,
				FOREIGN KEY(meaning_id) REFERENCES word_meaning(id)
			);

			INSERT INTO word_example_new (
				id, meaning_id, example_en, example_zh,
				difficulty, image, example_tag, selection_zh, selection_en
			)
			SELECT
				id, meaning_id, example_en, example_zh,
				difficulty, {image_select}, example_tag, {selection_zh_select}, {selection_en_select}
			FROM word_example;

			DROP TABLE word_example;
			ALTER TABLE word_example_new RENAME TO word_example;
			CREATE INDEX IF NOT EXISTS idx_example_meaning ON word_example(meaning_id);
			"""
		)
		conn.commit()

	def _ensure_question_schema(self) -> None:
		with sqlite3.connect(self.question_db_path) as conn:
			conn.execute("PRAGMA foreign_keys = ON")
			conn.executescript(
				"""
				CREATE TABLE IF NOT EXISTS question_bank (
					id INTEGER PRIMARY KEY,
					question_type INTEGER,
					stage INTEGER,
					difficulty INTEGER,
					content_json TEXT
				);

				CREATE INDEX IF NOT EXISTS idx_question_stage ON question_bank(stage);
				CREATE INDEX IF NOT EXISTS idx_question_difficulty ON question_bank(difficulty);
				CREATE INDEX IF NOT EXISTS idx_question_type ON question_bank(question_type);
				"""
			)
			conn.commit()

	def _ensure_user_schema(self) -> None:
		with closing(sqlite3.connect(self.user_db_path)) as conn:
			conn.execute("PRAGMA foreign_keys = ON")
			conn.executescript(
				"""
				CREATE TABLE IF NOT EXISTS vocab_items (
					id INTEGER PRIMARY KEY,
					user_id INTEGER,
					word_id INTEGER,
					added_at INTEGER,
					source TEXT,
					is_favorite INTEGER DEFAULT 0,
					is_difficult INTEGER DEFAULT 0,
					is_mastered INTEGER DEFAULT 0,
					is_deleted INTEGER DEFAULT 0
				);

				CREATE TABLE IF NOT EXISTS word_learning_profile (
					user_id INTEGER NOT NULL,
					word_id INTEGER NOT NULL,
					textbook_name TEXT NOT NULL,
					stage INTEGER DEFAULT 0,
					strength INTEGER DEFAULT 0,
					recall_score INTEGER DEFAULT 0,
					output_score INTEGER DEFAULT 0,
					next_review_at INTEGER DEFAULT 0,
					lapse_count INTEGER DEFAULT 0,
					last_practiced_at INTEGER DEFAULT 0,
					last_decay_at INTEGER DEFAULT 0,
					last_reviewed_at INTEGER DEFAULT 0,
					last_response_time_ms INTEGER DEFAULT 0,
					persistent_boost INTEGER DEFAULT 0,
					mastered INTEGER DEFAULT 0,
					PRIMARY KEY(user_id, word_id, textbook_name)
				);

				CREATE TABLE IF NOT EXISTS word_practice_history (
					id INTEGER PRIMARY KEY,
					user_id INTEGER NOT NULL,
					word_id INTEGER NOT NULL,
					question_type INTEGER DEFAULT 0,
					target_skill TEXT,
					review_type TEXT,
					rating INTEGER DEFAULT 0,
					response_time INTEGER DEFAULT 0,
					correct INTEGER DEFAULT 0,
					question_reason TEXT,
					practiced_at INTEGER DEFAULT 0
				);

				CREATE TABLE IF NOT EXISTS learned (
					user_id INTEGER NOT NULL,
					textbook_name TEXT NOT NULL,
					word_id INTEGER NOT NULL,
					correct_count INTEGER DEFAULT 0,
					wrong_count INTEGER DEFAULT 0,
					last_seen_at INTEGER DEFAULT 0,
					PRIMARY KEY (user_id, textbook_name, word_id)
				);

				CREATE TABLE IF NOT EXISTS word_practice_stats_daily (
					user_id INTEGER NOT NULL,
					date TEXT NOT NULL,
					textbook_name TEXT NOT NULL,
					total_count INTEGER DEFAULT 0,
					correct_count INTEGER DEFAULT 0,
					wrong_count INTEGER DEFAULT 0,
					pass_count INTEGER DEFAULT 0,
					fail_count INTEGER DEFAULT 0,
					PRIMARY KEY (user_id, date, textbook_name)
				);

				CREATE TABLE IF NOT EXISTS word_practice_daily_progress (
					user_id INTEGER NOT NULL,
					date TEXT NOT NULL,
					textbook_name TEXT NOT NULL,
					completed_words INTEGER DEFAULT 0,
					target_words INTEGER DEFAULT 0,
					progress_percent INTEGER DEFAULT 0,
					updated_at INTEGER DEFAULT 0,
					PRIMARY KEY (user_id, date, textbook_name)
				);

				CREATE TABLE IF NOT EXISTS word_practice_runtime_state (
					user_id INTEGER NOT NULL,
					textbook_name TEXT NOT NULL,
					completed_rounds INTEGER DEFAULT 0,
					last_round_passed INTEGER DEFAULT 0,
					last_round_at INTEGER DEFAULT 0,
					PRIMARY KEY (user_id, textbook_name)
				);

				CREATE TABLE IF NOT EXISTS app_state (
					user_id INTEGER NOT NULL,
					key TEXT NOT NULL,
					value TEXT NOT NULL,
					updated_at INTEGER DEFAULT 0,
					PRIMARY KEY (user_id, key)
				);

				CREATE TABLE IF NOT EXISTS ai_sessions (
					id INTEGER PRIMARY KEY,
					user_id INTEGER,
					session_type TEXT,
					topic TEXT,
					started_at INTEGER,
					ended_at INTEGER,
					total_duration INTEGER,
					created_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS ai_messages (
					id INTEGER PRIMARY KEY,
					session_id INTEGER,
					role TEXT,
					content TEXT,
					audio_path TEXT,
					created_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS ai_speech_evaluations (
					id INTEGER PRIMARY KEY,
					user_id INTEGER,
					session_id INTEGER,
					message_id INTEGER,
					pronunciation_score REAL,
					fluency_score REAL,
					grammar_score REAL,
					overall_score REAL,
					feedback_text TEXT,
					created_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS ai_detected_errors (
					id INTEGER PRIMARY KEY,
					user_id INTEGER,
					session_id INTEGER,
					message_id INTEGER,
					word_id INTEGER,
					error_type TEXT,
					severity INTEGER,
					created_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS learning_stats_daily (
					user_id INTEGER,
					date TEXT,
					reviews INTEGER DEFAULT 0,
					correct INTEGER DEFAULT 0,
					wrong INTEGER DEFAULT 0,
					new_words INTEGER DEFAULT 0,
					study_time_sec INTEGER DEFAULT 0,
					PRIMARY KEY (user_id, date)
				);

				CREATE TABLE IF NOT EXISTS tasks (
					id INTEGER PRIMARY KEY,
					user_id INTEGER,
					task_type TEXT,
					target_id INTEGER,
					title TEXT,
					description TEXT,
					start_at INTEGER,
					due_at INTEGER,
					is_completed INTEGER DEFAULT 0,
					is_deleted INTEGER DEFAULT 0,
					created_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS game_profile (
					user_id INTEGER PRIMARY KEY,
					level INTEGER DEFAULT 1,
					exp INTEGER DEFAULT 0,
					coins INTEGER DEFAULT 0,
					streak_days INTEGER DEFAULT 0,
					last_play_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS game_rewards_log (
					id INTEGER PRIMARY KEY,
					user_id INTEGER,
					reward_type TEXT,
					value INTEGER,
					reason TEXT,
					created_at INTEGER
				);

				CREATE TABLE IF NOT EXISTS sync_state (
					table_name TEXT PRIMARY KEY,
					last_sync_at INTEGER,
					last_row_id INTEGER
				);

				CREATE INDEX IF NOT EXISTS idx_vocab_user ON vocab_items(user_id);
				CREATE INDEX IF NOT EXISTS idx_vocab_word ON vocab_items(word_id);
				CREATE INDEX IF NOT EXISTS idx_word_practice_history_user_word ON word_practice_history(user_id, word_id, practiced_at);
				CREATE INDEX IF NOT EXISTS idx_tasks_user_deleted_due ON tasks(user_id, is_deleted, due_at);
				CREATE INDEX IF NOT EXISTS idx_tasks_deleted_done ON tasks(is_deleted, is_completed);
				CREATE INDEX IF NOT EXISTS idx_game_reward_user ON game_rewards_log(user_id);
				PRAGMA user_version = 4;
				"""
			)
			self._ensure_word_learning_profile_runtime_schema(conn)
			conn.commit()

	def _ensure_word_learning_profile_runtime_schema(self, conn: sqlite3.Connection) -> None:
		runtime_columns = {
			"textbook_name": "TEXT NOT NULL DEFAULT 'default'",
			"stage": "INTEGER DEFAULT 0",
			"strength": "INTEGER DEFAULT 0",
			"recall_score": "INTEGER DEFAULT 0",
			"output_score": "INTEGER DEFAULT 0",
			"next_review_at": "INTEGER DEFAULT 0",
			"lapse_count": "INTEGER DEFAULT 0",
			"last_practiced_at": "INTEGER DEFAULT 0",
			"last_decay_at": "INTEGER DEFAULT 0",
			"last_reviewed_at": "INTEGER DEFAULT 0",
			"last_response_time_ms": "INTEGER DEFAULT 0",
			"persistent_boost": "INTEGER DEFAULT 0",
			"mastered": "INTEGER DEFAULT 0",
		}
		columns = self._get_conn_table_columns(conn, "word_learning_profile")
		for column_name, column_sql in runtime_columns.items():
			if column_name not in columns:
				conn.execute(f"ALTER TABLE word_learning_profile ADD COLUMN {column_name} {column_sql}")
		columns = self._get_conn_table_columns(conn, "word_learning_profile")
		primary_key = [
			column_name
			for column_name, info in sorted(columns.items(), key=lambda item: int(item[1]["pk"]))
			if int(info["pk"])
		]
		if primary_key != ["user_id", "word_id", "textbook_name"]:
			self._rebuild_word_learning_profile_table(conn)
		conn.execute(
			"CREATE INDEX IF NOT EXISTS idx_word_learning_profile_user_next_review "
			"ON word_learning_profile(user_id, next_review_at)"
		)
		conn.execute("PRAGMA user_version = 4")

	def _rebuild_word_learning_profile_table(self, conn: sqlite3.Connection) -> None:
		conn.execute("SAVEPOINT rebuild_word_learning_profile")
		try:
			conn.execute("DROP TABLE IF EXISTS word_learning_profile__backup")
			conn.execute("CREATE TABLE word_learning_profile__backup AS SELECT * FROM word_learning_profile")
			conn.execute("DROP TABLE word_learning_profile")
			conn.execute(
				"""
				CREATE TABLE word_learning_profile (
					user_id INTEGER NOT NULL,
					word_id INTEGER NOT NULL,
					textbook_name TEXT NOT NULL,
					stage INTEGER DEFAULT 0,
					strength INTEGER DEFAULT 0,
					recall_score INTEGER DEFAULT 0,
					output_score INTEGER DEFAULT 0,
					next_review_at INTEGER DEFAULT 0,
					lapse_count INTEGER DEFAULT 0,
					last_practiced_at INTEGER DEFAULT 0,
					last_decay_at INTEGER DEFAULT 0,
					last_reviewed_at INTEGER DEFAULT 0,
					last_response_time_ms INTEGER DEFAULT 0,
					persistent_boost INTEGER DEFAULT 0,
					mastered INTEGER DEFAULT 0,
					PRIMARY KEY(user_id, word_id, textbook_name)
				)
				"""
			)
			conn.execute(
				"""
				INSERT OR REPLACE INTO word_learning_profile(
					user_id,
					word_id,
					textbook_name,
					stage,
					strength,
					recall_score,
					output_score,
					next_review_at,
					lapse_count,
					last_practiced_at,
					last_decay_at,
					last_reviewed_at,
					last_response_time_ms,
					persistent_boost,
					mastered
				)
				SELECT
					COALESCE(user_id, 0),
					COALESCE(word_id, 0),
					COALESCE(NULLIF(textbook_name, ''), 'default'),
					COALESCE(stage, 0),
					COALESCE(strength, 0),
					COALESCE(recall_score, 0),
					COALESCE(output_score, 0),
					COALESCE(next_review_at, 0),
					COALESCE(lapse_count, 0),
					COALESCE(last_practiced_at, 0),
					COALESCE(last_decay_at, 0),
					COALESCE(last_reviewed_at, 0),
					COALESCE(last_response_time_ms, 0),
					COALESCE(persistent_boost, 0),
					COALESCE(mastered, 0)
				FROM word_learning_profile__backup
				"""
			)
			conn.execute("DROP TABLE word_learning_profile__backup")
			conn.execute(
				"CREATE INDEX IF NOT EXISTS idx_word_learning_profile_user_next_review "
				"ON word_learning_profile(user_id, next_review_at)"
			)
			conn.execute(
				"CREATE INDEX IF NOT EXISTS idx_word_practice_history_user_word "
				"ON word_practice_history(user_id, word_id, practiced_at)"
			)
			conn.execute("RELEASE SAVEPOINT rebuild_word_learning_profile")
		except sqlite3.Error:
			conn.execute("ROLLBACK TO SAVEPOINT rebuild_word_learning_profile")
			conn.execute("RELEASE SAVEPOINT rebuild_word_learning_profile")
			raise

	@staticmethod
	def _get_conn_table_columns(conn: sqlite3.Connection, table_name: str) -> dict[str, dict[str, object]]:
		rows = conn.execute(f"PRAGMA table_info({DatabaseService._quote_identifier(table_name)})").fetchall()
		return {
			str(row[1]): {
				"type": str(row[2] or ""),
				"not_null": int(row[3]),
				"default_value": row[4],
				"pk": int(row[5]),
			}
			for row in rows
		}

	@staticmethod
	def _fetch_scalar(db_path: Path, sql: str) -> int:
		with sqlite3.connect(db_path) as conn:
			value = conn.execute(sql).fetchone()[0]
		return int(value)


class DatabaseCreateApp(QMainWindow):
	def __init__(self) -> None:
		super().__init__()
		self.setWindowTitle("DatabaseCreate 数据库创建与导入工具")
		self.resize(1180, 760)

		self.base_dir = Path(__file__).resolve().parent
		self.service = DatabaseService(self.base_dir)

		self.selected_excel_files: list[Path] = []
		self.selected_json_files: list[Path] = []
		self.clear_targets: dict[str, dict[str, list[str]]] = {}

		self.count_labels: dict[str, QLabel] = {}
		self._build_ui()
		self._initialize()

	def _build_ui(self) -> None:
		central = QWidget(self)
		self.setCentralWidget(central)

		root_layout = QVBoxLayout(central)
		root_layout.setContentsMargins(14, 14, 14, 14)
		root_layout.setSpacing(12)

		title_label = QLabel("数据库创建与数据导入")
		title_label.setStyleSheet("font-size: 20px; font-weight: 700;")
		root_layout.addWidget(title_label)

		output_label = QLabel(f"输出目录：{self.base_dir}")
		root_layout.addWidget(output_label)

		self.status_label = QLabel("未初始化数据库")
		self.status_label.setStyleSheet("color: #0b6b2f; font-weight: 600;")
		root_layout.addWidget(self.status_label)

		summary_layout = QHBoxLayout()
		summary_layout.setSpacing(8)
		for key, text in (
			("word", "word: -"),
			("word_meaning", "word_meaning: -"),
			("word_form", "word_form: -"),
			("word_example", "word_example: -"),
			("question_bank", "question_bank: -"),
		):
			label = QLabel(text)
			label.setFrameShape(QFrame.Shape.StyledPanel)
			label.setStyleSheet("padding: 6px 10px;")
			summary_layout.addWidget(label)
			self.count_labels[key] = label
		summary_layout.addStretch(1)
		root_layout.addLayout(summary_layout)

		action_layout = QHBoxLayout()
		create_button = QPushButton("创建/更新全部数据库")
		create_button.clicked.connect(self.create_all_databases)
		action_layout.addWidget(create_button)

		clear_all_button = QPushButton("清空全部数据库")
		clear_all_button.clicked.connect(self.clear_all_databases)
		action_layout.addWidget(clear_all_button)

		refresh_button = QPushButton("刷新统计")
		refresh_button.clicked.connect(self.refresh_counts)
		action_layout.addWidget(refresh_button)

		clear_log_button = QPushButton("清空日志")
		clear_log_button.clicked.connect(self.clear_log)
		action_layout.addWidget(clear_log_button)
		action_layout.addStretch(1)
		root_layout.addLayout(action_layout)

		root_layout.addWidget(self._create_clear_panel())
		root_layout.addWidget(self._create_compare_panel())

		splitter = QSplitter()
		root_layout.addWidget(splitter, 1)

		words_panel = self._create_words_panel()
		question_panel = self._create_question_panel()
		splitter.addWidget(words_panel)
		splitter.addWidget(question_panel)
		splitter.setSizes([760, 420])

		log_group = QGroupBox("执行日志")
		log_layout = QVBoxLayout(log_group)
		self.log_text = QPlainTextEdit()
		self.log_text.setReadOnly(True)
		self.log_text.setStyleSheet("font-family: Consolas, 'Courier New', monospace; font-size: 12px;")
		log_layout.addWidget(self.log_text)
		root_layout.addWidget(log_group, 1)

	def _create_clear_panel(self) -> QWidget:
		group = QGroupBox("清空数据")
		layout = QGridLayout(group)

		layout.addWidget(QLabel("数据库"), 0, 0)
		self.clear_database_combo = QComboBox()
		self.clear_database_combo.currentIndexChanged.connect(self._on_clear_database_changed)
		layout.addWidget(self.clear_database_combo, 0, 1)

		clear_database_button = QPushButton("清空所选数据库")
		clear_database_button.clicked.connect(self.clear_selected_database)
		layout.addWidget(clear_database_button, 0, 2)

		layout.addWidget(QLabel("数据表"), 1, 0)
		self.clear_table_combo = QComboBox()
		self.clear_table_combo.currentIndexChanged.connect(self._on_clear_table_changed)
		layout.addWidget(self.clear_table_combo, 1, 1)

		clear_table_button = QPushButton("清空所选表")
		clear_table_button.clicked.connect(self.clear_selected_table)
		layout.addWidget(clear_table_button, 1, 2)

		layout.addWidget(QLabel("字段"), 2, 0)
		self.clear_field_combo = QComboBox()
		layout.addWidget(self.clear_field_combo, 2, 1)

		clear_field_button = QPushButton("清空所选字段整列数据")
		clear_field_button.clicked.connect(self.clear_selected_field)
		layout.addWidget(clear_field_button, 2, 2)

		hint_label = QLabel("字段清空会清除该表该字段的整列内容；受数据库约束时会回退为默认值或 NULL。主键和非空且无默认值字段不会出现在可选列表中。")
		hint_label.setWordWrap(True)
		layout.addWidget(hint_label, 3, 0, 1, 3)
		return group

	def _create_compare_panel(self) -> QWidget:
		group = QGroupBox("数据库内容比对")
		layout = QGridLayout(group)

		layout.addWidget(QLabel("数据库A"), 0, 0)
		self.compare_left_path_edit = QLineEdit(str(self.service.words_db_path))
		layout.addWidget(self.compare_left_path_edit, 0, 1)
		left_browse_button = QPushButton("选择数据库A")
		left_browse_button.clicked.connect(lambda: self.pick_compare_database("left"))
		layout.addWidget(left_browse_button, 0, 2)

		layout.addWidget(QLabel("数据库B"), 1, 0)
		self.compare_right_path_edit = QLineEdit("")
		layout.addWidget(self.compare_right_path_edit, 1, 1)
		right_browse_button = QPushButton("选择数据库B")
		right_browse_button.clicked.connect(lambda: self.pick_compare_database("right"))
		layout.addWidget(right_browse_button, 1, 2)

		compare_button = QPushButton("比较两个数据库的全部表/字段/内容")
		compare_button.clicked.connect(self.compare_selected_databases)
		layout.addWidget(compare_button, 2, 0, 1, 3)

		hint_label = QLabel("按两份 SQLite 数据库的所有表、所有字段、所有记录做比对，结果会筛出缺失表、缺失字段以及内容不一致的记录。")
		hint_label.setWordWrap(True)
		layout.addWidget(hint_label, 3, 0, 1, 3)

		self.compare_result_text = QPlainTextEdit()
		self.compare_result_text.setReadOnly(True)
		self.compare_result_text.setPlaceholderText("比对结果会显示在这里")
		self.compare_result_text.setMaximumHeight(180)
		layout.addWidget(self.compare_result_text, 4, 0, 1, 3)
		return group

	def _set_combo_items(self, combo: QComboBox, items: list[str], current_text: str | None = None) -> None:
		combo.blockSignals(True)
		combo.clear()
		combo.addItems(items)
		if current_text:
			index = combo.findText(current_text)
			if index >= 0:
				combo.setCurrentIndex(index)
		combo.blockSignals(False)

	def refresh_clear_targets(self) -> None:
		current_database = self.clear_database_combo.currentText() if hasattr(self, "clear_database_combo") else ""
		current_table = self.clear_table_combo.currentText() if hasattr(self, "clear_table_combo") else ""
		current_field = self.clear_field_combo.currentText() if hasattr(self, "clear_field_combo") else ""

		self.clear_targets = self.service.get_clear_targets()
		database_names = list(self.clear_targets.keys())
		self._set_combo_items(self.clear_database_combo, database_names, current_database or "words.db")
		self._on_clear_database_changed(current_table=current_table, current_field=current_field)

	def _on_clear_database_changed(self, _index: int | None = None, current_table: str | None = None, current_field: str | None = None) -> None:
		database_name = self.clear_database_combo.currentText()
		tables = list(self.clear_targets.get(database_name, {}).keys())
		self._set_combo_items(self.clear_table_combo, tables, current_table)
		self._on_clear_table_changed(current_field=current_field)

	def _on_clear_table_changed(self, _index: int | None = None, current_field: str | None = None) -> None:
		database_name = self.clear_database_combo.currentText()
		table_name = self.clear_table_combo.currentText()
		fields = self.clear_targets.get(database_name, {}).get(table_name, [])
		if not fields:
			fields = ["无可清空字段"]
		self._set_combo_items(self.clear_field_combo, fields, current_field)

	def _create_words_panel(self) -> QWidget:
		group = QGroupBox("words.db / Excel 导入")
		layout = QVBoxLayout(group)

		layout.addWidget(QLabel(f"数据库文件：{self.service.words_db_path}"))

		selector_layout = QHBoxLayout()
		select_button = QPushButton("选择 Excel 文件")
		select_button.clicked.connect(self.pick_excel_files)
		selector_layout.addWidget(select_button)
		selector_layout.addWidget(QLabel("支持一次选择多个 .xls / .xlsx 文件"), 1)
		layout.addLayout(selector_layout)

		self.excel_listbox = QListWidget()
		self.excel_listbox.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
		layout.addWidget(self.excel_listbox, 1)

		instruction = QLabel("按目标表导入：Excel 必须包含表头，且表头名需与字段名完全一致；未匹配表头只记日志，不参与导入。word 字段会自动去重。")
		instruction.setWordWrap(True)
		layout.addWidget(instruction)

		button_grid = QGridLayout()
		buttons = (
			("导入 word\n(word/phonetic/word_type)", "word", 0, 0),
			("导入 word_meaning\n(stage/pos/meaning/source/tag)", "word_meaning", 0, 1),
			("导入 word_form\n(form_type/form)", "word_form", 1, 0),
			("导入 word_example\n(example/difficulty/image/audio)", "word_example", 1, 1),
		)
		for text, mode, row, column in buttons:
			button = QPushButton(text)
			button.clicked.connect(lambda _checked=False, import_mode=mode: self.run_word_import(import_mode))
			button_grid.addWidget(button, row, column)

		all_button = QPushButton("一键导入全部严格匹配字段")
		all_button.clicked.connect(lambda: self.run_word_import("all"))
		button_grid.addWidget(all_button, 2, 0, 1, 2)
		layout.addLayout(button_grid)
		return group

	def _create_question_panel(self) -> QWidget:
		group = QGroupBox("question.db / JSON 导入")
		layout = QVBoxLayout(group)

		layout.addWidget(QLabel(f"数据库文件：{self.service.question_db_path}"))

		selector_layout = QHBoxLayout()
		select_button = QPushButton("选择 JSON 文件")
		select_button.clicked.connect(self.pick_json_files)
		selector_layout.addWidget(select_button)
		selector_layout.addWidget(QLabel("支持一次选择多个 JSON 文件"), 1)
		layout.addLayout(selector_layout)

		self.json_listbox = QListWidget()
		self.json_listbox.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
		layout.addWidget(self.json_listbox, 1)

		instruction = QLabel("导入时会自动补齐缺失 question_type、audio_path、image_path，并写入 question.db。")
		instruction.setWordWrap(True)
		layout.addWidget(instruction)

		import_button = QPushButton("导入到 question.db")
		import_button.clicked.connect(self.run_question_import)
		layout.addWidget(import_button)
		return group

	def _initialize(self) -> None:
		try:
			self.service.ensure_all_databases()
			self.status_label.setText("数据库已初始化")
			self.refresh_counts()
			self.refresh_clear_targets()
			self.log("工具已启动，数据库结构已按 readme.md 建立或校验完成。")
		except Exception as exc:  # noqa: BLE001
			self.status_label.setText(f"初始化失败：{exc}")
			self.log(f"初始化失败：{exc}")

	def log(self, message: str) -> None:
		self.log_text.appendPlainText(message)

	def clear_log(self) -> None:
		self.log_text.clear()

	def _show_info(self, title: str, text: str) -> None:
		QMessageBox.information(self, title, text)

	def _show_warning(self, title: str, text: str) -> None:
		QMessageBox.warning(self, title, text)

	def _show_error(self, title: str, text: str) -> None:
		QMessageBox.critical(self, title, text)

	def _confirm_action(self, title: str, text: str) -> bool:
		result = QMessageBox.question(
			self,
			title,
			text,
			QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
			QMessageBox.StandardButton.No,
		)
		return result == QMessageBox.StandardButton.Yes

	def create_all_databases(self) -> None:
		try:
			self.service.ensure_all_databases()
			self.status_label.setText("数据库创建/更新完成")
			self.refresh_counts()
			self.refresh_clear_targets()
			self._show_info("完成", "words.db、question.db、user.db 已创建或更新完成")
			self.log("已创建或更新全部数据库、表和索引。")
		except Exception as exc:  # noqa: BLE001
			self._show_error("失败", str(exc))
			self.log(f"创建数据库失败：{exc}")

	def clear_words_database(self) -> None:
		if not self._confirm_action("确认删除", "确定要清空 words.db 中的全部数据吗？该操作不可恢复。"):
			self.log("已取消清空 words.db。")
			return

		try:
			self.service.clear_words_database()
			self.status_label.setText("words.db 已清空")
			self.refresh_counts()
			self.refresh_clear_targets()
			self.log("已清空 words.db 中的 word、word_meaning、word_form、word_example 数据。")
			self._show_info("完成", "words.db 已清空")
		except Exception as exc:  # noqa: BLE001
			self.log(f"清空 words.db 失败：{exc}")
			self._show_error("清空失败", str(exc))

	def clear_question_database(self) -> None:
		if not self._confirm_action("确认删除", "确定要清空 question.db 中的全部题库数据吗？该操作不可恢复。"):
			self.log("已取消清空 question.db。")
			return

		try:
			self.service.clear_question_database()
			self.status_label.setText("question.db 已清空")
			self.refresh_counts()
			self.refresh_clear_targets()
			self.log("已清空 question.db 中的 question_bank 数据。")
			self._show_info("完成", "question.db 已清空")
		except Exception as exc:  # noqa: BLE001
			self.log(f"清空 question.db 失败：{exc}")
			self._show_error("清空失败", str(exc))

	def clear_all_databases(self) -> None:
		if not self._confirm_action(
			"确认删除",
			"确定要清空 words.db、question.db、user.db 中的全部数据吗？该操作不可恢复。",
		):
			self.log("已取消清空全部数据库。")
			return

		try:
			self.service.clear_all_databases()
			self.status_label.setText("全部数据库已清空")
			self.refresh_counts()
			self.refresh_clear_targets()
			self.log("已清空 words.db、question.db、user.db 中的全部数据。")
			self._show_info("完成", "全部数据库已清空")
		except Exception as exc:  # noqa: BLE001
			self.log(f"清空全部数据库失败：{exc}")
			self._show_error("清空失败", str(exc))

	def clear_selected_database(self) -> None:
		database_name = self.clear_database_combo.currentText()
		if not database_name:
			self._show_warning("提示", "请先选择数据库")
			return

		if not self._confirm_action("确认删除", f"确定要清空 {database_name} 的全部数据吗？该操作不可恢复。"):
			self.log(f"已取消清空 {database_name}。")
			return

		try:
			cleared_rows = self.service.clear_database(database_name)
			self.status_label.setText(f"{database_name} 已清空")
			self.refresh_counts()
			self.refresh_clear_targets()
			detail = "，".join(f"{table}: {count} 行" for table, count in cleared_rows.items())
			message = f"已清空 {database_name}。{detail}"
			self.log(message)
			self._show_info("完成", message)
		except Exception as exc:  # noqa: BLE001
			self.log(f"清空 {database_name} 失败：{exc}")
			self._show_error("清空失败", str(exc))

	def clear_selected_table(self) -> None:
		database_name = self.clear_database_combo.currentText()
		table_name = self.clear_table_combo.currentText()
		if not database_name or not table_name:
			self._show_warning("提示", "请先选择数据库和数据表")
			return

		if not self._confirm_action(
			"确认删除",
			f"确定要清空 {database_name} 中的 {table_name} 吗？该操作不可恢复，关联表可能也会一并清空。",
		):
			self.log(f"已取消清空 {database_name}.{table_name}。")
			return

		try:
			cleared_rows = self.service.clear_table(database_name, table_name)
			self.status_label.setText(f"{database_name}.{table_name} 已清空")
			self.refresh_counts()
			self.refresh_clear_targets()
			detail = "，".join(f"{table}: {count} 行" for table, count in cleared_rows.items())
			message = f"已清空 {database_name} 中的 {table_name}。{detail}"
			self.log(message)
			self._show_info("完成", message)
		except Exception as exc:  # noqa: BLE001
			self.log(f"清空 {database_name}.{table_name} 失败：{exc}")
			self._show_error("清空失败", str(exc))

	def clear_selected_field(self) -> None:
		database_name = self.clear_database_combo.currentText()
		table_name = self.clear_table_combo.currentText()
		field_name = self.clear_field_combo.currentText()
		if not database_name or not table_name or not field_name or field_name == "无可清空字段":
			self._show_warning("提示", "当前表没有可清空字段，或尚未完成选择")
			return

		if not self._confirm_action(
			"确认清空字段",
			f"确定要清空下拉菜单当前选择的 {database_name}.{table_name}.{field_name} 的全部数据吗？",
		):
			self.log(f"已取消清空字段 {database_name}.{table_name}.{field_name}。")
			return

		try:
			row_count, reset_desc = self.service.clear_field(database_name, table_name, field_name)
			self.status_label.setText(f"{database_name}.{table_name}.{field_name} 已清空")
			self.refresh_counts()
			self.refresh_clear_targets()
			message = (
				f"已清空 {database_name}.{table_name}.{field_name} 的全部数据，影响 {row_count} 行，"
				f"处理方式：{reset_desc}。"
			)
			self.log(message)
			self._show_info("完成", message)
		except Exception as exc:  # noqa: BLE001
			self.log(f"清空字段 {database_name}.{table_name}.{field_name} 失败：{exc}")
			self._show_error("清空失败", str(exc))

	def refresh_counts(self) -> None:
		try:
			counts = self.service.get_counts()
		except Exception as exc:  # noqa: BLE001
			self.log(f"刷新统计失败：{exc}")
			return

		self.count_labels["word"].setText(f"word: {counts['word']}")
		self.count_labels["word_meaning"].setText(f"word_meaning: {counts['word_meaning']}")
		self.count_labels["word_form"].setText(f"word_form: {counts['word_form']}")
		self.count_labels["word_example"].setText(f"word_example: {counts['word_example']}")
		self.count_labels["question_bank"].setText(f"question_bank: {counts['question_bank']}")

	def pick_compare_database(self, target: str) -> None:
		path_edit = self.compare_left_path_edit if target == "left" else self.compare_right_path_edit
		current_path = Path(path_edit.text().strip()) if path_edit.text().strip() else self.base_dir
		start_dir = current_path.parent if current_path.suffix else current_path
		file_path, _ = QFileDialog.getOpenFileName(
			self,
			"选择要比较的 SQLite 数据库",
			str(start_dir),
			"SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*.*)",
		)
		if file_path:
			path_edit.setText(file_path)

	def compare_selected_databases(self) -> None:
		left_text = self.compare_left_path_edit.text().strip()
		right_text = self.compare_right_path_edit.text().strip()
		if not left_text or not right_text:
			self._show_warning("提示", "请先选择两份要比较的数据库文件")
			return

		left_db_path = Path(left_text)
		right_db_path = Path(right_text)
		try:
			summary, details = self.service.compare_databases(left_db_path, right_db_path)
			result_lines = [summary, ""]
			if details:
				result_lines.extend(details)
				self.log(f"数据库比对完成：发现 {len(details)} 条差异，详见“数据库内容比对”结果框。")
			else:
				result_lines.append("未发现不一致内容。")
				self.log("数据库比对完成：未发现不一致内容。")
			self.compare_result_text.setPlainText("\n".join(result_lines))
			self.status_label.setText("数据库比对完成")
			self._show_info("比对完成", summary)
		except Exception as exc:  # noqa: BLE001
			self.compare_result_text.setPlainText(f"比对失败：{exc}")
			self.log(f"数据库比对失败：{exc}")
			self._show_error("比对失败", str(exc))

	def pick_excel_files(self) -> None:
		files, _ = QFileDialog.getOpenFileNames(
			self,
			"选择一个或多个 Excel 文件",
			str(self.base_dir / "excel"),
			"Excel 文件 (*.xlsx *.xls);;所有文件 (*.*)",
		)
		if not files:
			return

		self.selected_excel_files = [Path(file) for file in files if not Path(file).name.startswith("~$")]
		self.excel_listbox.clear()
		for file in self.selected_excel_files:
			self.excel_listbox.addItem(str(file))
		self.log(f"已选择 {len(self.selected_excel_files)} 个 Excel 文件。")

	def pick_json_files(self) -> None:
		files, _ = QFileDialog.getOpenFileNames(
			self,
			"选择一个或多个 JSON 文件",
			str(self.base_dir),
			"JSON 文件 (*.json);;所有文件 (*.*)",
		)
		if not files:
			return

		self.selected_json_files = [Path(file) for file in files]
		self.json_listbox.clear()
		for file in self.selected_json_files:
			self.json_listbox.addItem(str(file))
		self.log(f"已选择 {len(self.selected_json_files)} 个 JSON 文件。")

	def run_word_import(self, mode: str) -> None:
		if not self.selected_excel_files:
			self._show_warning("提示", "请先选择 Excel 文件")
			return

		mode_name = {
			"word": "word",
			"word_meaning": "word_meaning",
			"word_form": "word_form",
			"word_example": "word_example",
			"all": "全部表",
		}.get(mode, mode)
		self.log(f"开始导入 {mode_name}，文件数：{len(self.selected_excel_files)}")

		try:
			summary = self.service.import_words(self.selected_excel_files, mode)
			self.refresh_counts()
			self.log(summary.to_message())
			message = summary.to_message(max_details=20)
			if summary.failed:
				self._show_warning("导入完成", message)
			else:
				self._show_info("导入完成", message)
		except Exception as exc:  # noqa: BLE001
			self.log(f"导入失败：{exc}")
			self._show_error("导入失败", str(exc))

	def run_question_import(self) -> None:
		if not self.selected_json_files:
			self._show_warning("提示", "请先选择 JSON 文件")
			return

		self.log(f"开始导入 question.db，文件数：{len(self.selected_json_files)}")
		try:
			summary = self.service.import_questions(self.selected_json_files)
			self.refresh_counts()
			self.log(summary.to_message())
			message = summary.to_message(max_details=20)
			if summary.failed:
				self._show_warning("导入完成", message)
			else:
				self._show_info("导入完成", message)
		except Exception as exc:  # noqa: BLE001
			self.log(f"导入 question.db 失败：{exc}")
			self._show_error("导入失败", str(exc))


if __name__ == "__main__":
	app = QApplication(sys.argv)
	window = DatabaseCreateApp()
	window.show()
	sys.exit(app.exec())
