from __future__ import annotations

import json
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

try:
    import xlrd
except ModuleNotFoundError as exc:
    raise SystemExit("缺少 xlrd，无法读取 .xls 文件。") from exc


BASE_DIR = Path(__file__).resolve().parent
BOOK_DIR = BASE_DIR / "book"
SOURCE_XLS_PATH = BOOK_DIR / "外研版小学单词表（1-6年级）.xls"
REFERENCE_JSON_PATH = BOOK_DIR / "人教版小学单词表（3-6年级）.json"
FALLBACK_SCHEMA_PATHS = [
    REFERENCE_JSON_PATH,
    BOOK_DIR / "沪教牛津版小学单词表（3-6年级）.json",
    BOOK_DIR / "外研版小学单词表（1-6年级）.json",
    BASE_DIR / "words_stage1.json",
]
OUTPUT_JSON_PATH = BOOK_DIR / "外研版小学单词表（1-6年级）.json"
PUBLISHER = "外研版"
EXPECTED_HEADERS = ["序列", "单词", "音标", "词性", "词意", "课目", "册数"]
WHITESPACE_PATTERN = re.compile(r"\s+")

BOOK_LABELS = {
    "一": "一年级上册",
    "二": "一年级下册",
    "三": "二年级上册",
    "四": "二年级下册",
    "五": "三年级上册",
    "六": "三年级下册",
    "七": "四年级上册",
    "八": "四年级下册",
    "九": "五年级上册",
    "十": "五年级下册",
    "十一": "六年级上册",
    "十二": "六年级下册",
}


def normalize_text(value: Any) -> str:
    if value is None:
        return ""
    text = str(value).replace("\xa0", " ").strip()
    return WHITESPACE_PATTERN.sub(" ", text)


def load_rows(xls_path: Path) -> list[list[Any]]:
    workbook = xlrd.open_workbook(xls_path.as_posix())
    sheet = workbook.sheet_by_index(0)
    return [sheet.row_values(index) for index in range(sheet.nrows)]


def detect_header_map(rows: list[list[Any]]) -> tuple[int, dict[str, int]]:
    for row_index, row in enumerate(rows[:5]):
        normalized = [normalize_text(cell) for cell in row]
        mapping = {
            header: normalized.index(header)
            for header in EXPECTED_HEADERS
            if header in normalized
        }
        if len(mapping) == len(EXPECTED_HEADERS):
            return row_index, mapping
    raise ValueError(f"前 5 行内未找到完整表头，要求字段: {', '.join(EXPECTED_HEADERS)}")


def build_source(book_no: str, module: str) -> str:
    book_label = BOOK_LABELS.get(book_no, f"第{book_no}册")
    if module:
        return f"{PUBLISHER} | {book_label} | {module}"
    return f"{PUBLISHER} | {book_label}"


def load_reference_schema() -> dict[str, Any]:
    errors: list[str] = []
    for path in FALLBACK_SCHEMA_PATHS:
        if not path.exists():
            errors.append(f"{path}: 文件不存在")
            continue

        payload: dict[str, Any] | None = None
        for encoding in ("utf-8", "utf-8-sig"):
            try:
                payload = json.loads(path.read_text(encoding=encoding))
                break
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                errors.append(f"{path} ({encoding}): {exc}")

        if not isinstance(payload, dict):
            continue

        schema = payload.get("schema")
        if isinstance(schema, dict) and all(table_name in schema for table_name in ("word", "word_meaning", "word_form", "word_example")):
            return schema

        errors.append(f"{path}: 缺少完整 schema")

    raise ValueError("无法从参考 JSON 加载 schema:\n" + "\n".join(errors))


def parse_records(rows: list[list[Any]]) -> tuple[list[dict[str, Any]], int]:
    header_row_index, mapping = detect_header_map(rows)
    records: list[dict[str, Any]] = []
    word_id_by_key: dict[str, int] = {}
    word_payload_by_key: dict[str, dict[str, Any]] = {}
    next_word_id = 1
    next_meaning_id = 1

    for row_number, row in enumerate(rows[header_row_index + 1 :], start=header_row_index + 2):
        if all(not normalize_text(cell) for cell in row):
            continue

        word = normalize_text(row[mapping["单词"]])
        phonetic = normalize_text(row[mapping["音标"]])
        pos = normalize_text(row[mapping["词性"]])
        meaning_zh = normalize_text(row[mapping["词意"]])
        module = normalize_text(row[mapping["课目"]])
        book_no = normalize_text(row[mapping["册数"]])

        if not word:
            continue
        if not meaning_zh:
            raise ValueError(f"第 {row_number} 行缺少词意: {row}")

        word_key = word.casefold()
        if word_key not in word_id_by_key:
            word_id_by_key[word_key] = next_word_id
            word_payload_by_key[word_key] = {
                "id": next_word_id,
                "word": word,
                "phonetic": phonetic or None,
                "word_type": None,
            }
            next_word_id += 1
        else:
            existing_word = word_payload_by_key[word_key]
            if not existing_word.get("phonetic") and phonetic:
                existing_word["phonetic"] = phonetic

        word_id = word_id_by_key[word_key]
        meaning_id = next_meaning_id
        next_meaning_id += 1

        records.append(
            {
                "word": dict(word_payload_by_key[word_key]),
                "word_meaning": {
                    "id": meaning_id,
                    "word_id": word_id,
                    "stage": 1,
                    "pos": pos or None,
                    "meaning_en": None,
                    "meaning_zh": meaning_zh,
                    "source": build_source(book_no, module),
                    "word_tag": None,
                },
                "word_form": [],
                "word_example": [],
            }
        )
        records[-1]["word"].setdefault("image", None)

    return records, len(word_id_by_key)


def build_output(records: list[dict[str, Any]], unique_word_count: int) -> dict[str, Any]:
    schema = load_reference_schema()
    return {
        "metadata": {
            "source_database": str(SOURCE_XLS_PATH),
            "source_workbook": str(SOURCE_XLS_PATH),
            "exported_at": datetime.now().isoformat(timespec="seconds"),
            "record_count": len(records),
            "root_table": "word_meaning",
            "related_tables": ["word", "word_meaning", "word_form", "word_example"],
            "matched_meaning_count": len(records),
            "unmatched_meaning_count": 0,
            "unique_word_count": unique_word_count,
            "publisher": PUBLISHER,
        },
        "schema": schema,
        "records": records,
    }


def main() -> int:
    if not SOURCE_XLS_PATH.exists():
        raise FileNotFoundError(f"找不到源文件: {SOURCE_XLS_PATH}")

    rows = load_rows(SOURCE_XLS_PATH)
    records, unique_word_count = parse_records(rows)
    output = build_output(records, unique_word_count)
    OUTPUT_JSON_PATH.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(
        f"已生成 {OUTPUT_JSON_PATH}，共 {len(records)} 条 records，"
        f"去重后 {unique_word_count} 个唯一单词。"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())