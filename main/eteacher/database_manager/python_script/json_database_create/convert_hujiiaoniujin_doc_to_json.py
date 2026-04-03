from __future__ import annotations

import importlib
import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path


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


_ensure_dependency("win32com.client", "pywin32")

import win32com.client


BASE_DIR = Path(__file__).resolve().parent
BOOK_DIR = BASE_DIR / "book"
DOC_PATH = BOOK_DIR / "沪教牛津版小学单词表(3-6年级).doc"
REFERENCE_JSON_PATH = BOOK_DIR / "人教版小学单词表（3-6年级）.json"
OUTPUT_JSON_PATH = BOOK_DIR / "沪教牛津版小学单词表（3-6年级）.json"
PUBLISHER = "沪教牛津版"

BOOK_PATTERN = re.compile(r"([三四五六])年级(上册|下册|上|下)")
UNIT_PATTERN = re.compile(r"\bUnit\s*:?\s*(\d+)\b", re.IGNORECASE)
TOKEN_PATTERN = r"(?:\([A-Za-z]+\)|…?[A-Za-z0-9][A-Za-z0-9’'`().:/\-…]*)"
ENTRY_START_PATTERN = re.compile(
    rf"(?:(?<=^)|(?<=\s))(?P<eng>(?:\([A-Za-z]+\)\s*)?[A-Za-z][A-Za-z0-9’'`().:/\-…]*(?:\s+{TOKEN_PATTERN})*)"
)
CHINESE_PATTERN = re.compile(r"[\u3400-\u9fff]")
CHINESE_THEN_ENG_PATTERN = re.compile(
    rf"^(?P<meaning>[\u3400-\u9fff（）…；、，？！《》“”‘’·：:;\-]+)\s+"
    rf"(?P<eng>(?:\([A-Za-z]+\)\s*)?[A-Za-z][A-Za-z0-9’'`().:/\-…]*(?:\s+{TOKEN_PATTERN})*)$"
)
WHITESPACE_PATTERN = re.compile(r"\s+")


def has_chinese(value: str) -> bool:
    return bool(CHINESE_PATTERN.search(value))


def normalize_whitespace(value: str) -> str:
    return WHITESPACE_PATTERN.sub(" ", value).strip()


def extract_lines_from_doc(doc_path: Path) -> list[str]:
    word_app = win32com.client.DispatchEx("Word.Application")
    word_app.Visible = False
    document = None
    try:
        document = word_app.Documents.Open(str(doc_path))
        text = document.Content.Text.replace("\r\x07", "\n").replace("\r", "\n")
        return [line.strip() for line in text.split("\n") if line.strip()]
    finally:
        if document is not None:
            try:
                document.Close(False)
            except Exception:
                pass
        try:
            word_app.Quit()
        except Exception:
            pass


def normalize_book_label(line: str) -> str | None:
    match = BOOK_PATTERN.search(line)
    if not match:
        return None

    grade, term = match.groups()
    if term == "上":
        term = "上册"
    elif term == "下":
        term = "下册"
    return f"{grade}年级{term}"


def parse_entries(segment: str) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    matches = list(ENTRY_START_PATTERN.finditer(segment))
    for index, match in enumerate(matches):
        english = normalize_whitespace(match.group("eng"))
        meaning_start = match.end("eng")
        while meaning_start < len(segment) and segment[meaning_start].isspace():
            meaning_start += 1
        if meaning_start >= len(segment):
            continue

        meaning_end = len(segment)
        for next_match in matches[index + 1 :]:
            candidate_start = next_match.start("eng")
            if has_chinese(segment[meaning_start:candidate_start]):
                meaning_end = candidate_start
                break

        meaning = normalize_whitespace(segment[meaning_start:meaning_end])
        if not meaning or not has_chinese(meaning):
            continue
        entries.append((english, meaning))
    return entries


def parse_records(lines: list[str]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    current_book: str | None = None
    current_unit: str | None = None
    pending_english: tuple[str, str, str] | None = None

    def append_record(book: str, unit: str, english: str, meaning: str) -> None:
        record_id = len(records) + 1
        source = f"{PUBLISHER} | {book} | {unit}" if unit else f"{PUBLISHER} | {book}"
        records.append(
            {
                "word": {
                    "id": record_id,
                    "word": english,
                    "phonetic": None,
                    "word_type": None,
                    "image": None,
                },
                "word_meaning": {
                    "id": record_id,
                    "word_id": record_id,
                    "stage": 1,
                    "pos": None,
                    "meaning_en": None,
                    "meaning_zh": meaning,
                    "source": source,
                    "word_tag": None,
                },
                "word_form": [],
                "word_example": [],
            }
        )

    for line_number, line in enumerate(lines, start=1):
        book_label = normalize_book_label(line)
        if book_label:
            current_book = book_label
            current_unit = None
            pending_english = None
            continue

        if current_book is None:
            raise ValueError(f"第 {line_number} 行未识别到册别: {line}")

        segments: list[tuple[str | None, str]] = []
        unit_match = UNIT_PATTERN.search(line)
        if unit_match:
            prefix = line[: unit_match.start()].strip()
            if prefix:
                segments.append((current_unit, prefix))
            current_unit = f"Unit {unit_match.group(1)}"
            suffix = line[unit_match.end() :].strip(" ：:.")
            if suffix:
                segments.append((current_unit, suffix))
        else:
            segments.append((current_unit, line))

        for segment_unit, segment in segments:
            if segment_unit is None:
                raise ValueError(f"第 {line_number} 行未识别到单元: {segment}")

            if pending_english and has_chinese(segment) and not ENTRY_START_PATTERN.search(segment):
                append_record(pending_english[0], pending_english[1], pending_english[2], normalize_whitespace(segment))
                pending_english = None
                continue

            chinese_then_english = CHINESE_THEN_ENG_PATTERN.match(segment)
            if chinese_then_english and records:
                records[-1]["word_meaning"]["meaning_zh"] += normalize_whitespace(chinese_then_english.group("meaning"))
                pending_english = (
                    current_book,
                    segment_unit,
                    normalize_whitespace(chinese_then_english.group("eng")),
                )
                continue

            entries = parse_entries(segment)
            if not entries:
                raise ValueError(f"第 {line_number} 行无法解析: {segment}")

            pending_english = None
            for english, meaning in entries:
                append_record(current_book, segment_unit, english, meaning)

    return records


def build_output(records: list[dict[str, object]]) -> dict[str, object]:
    reference = json.loads(REFERENCE_JSON_PATH.read_text(encoding="utf-8"))
    return {
        "metadata": {
            "source_database": str(DOC_PATH),
            "source_workbook": str(DOC_PATH),
            "exported_at": datetime.now().isoformat(timespec="seconds"),
            "record_count": len(records),
            "root_table": "word_meaning",
            "related_tables": ["word", "word_meaning", "word_form", "word_example"],
            "matched_meaning_count": len(records),
            "unmatched_meaning_count": 0,
        },
        "schema": reference["schema"],
        "records": records,
    }


def main() -> int:
    if not DOC_PATH.exists():
        raise FileNotFoundError(f"找不到源文件: {DOC_PATH}")
    if not REFERENCE_JSON_PATH.exists():
        raise FileNotFoundError(f"找不到参考 JSON: {REFERENCE_JSON_PATH}")

    lines = extract_lines_from_doc(DOC_PATH)
    records = parse_records(lines)
    output = build_output(records)
    OUTPUT_JSON_PATH.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"已生成 {OUTPUT_JSON_PATH}，共 {len(records)} 条记录。")
    return 0


if __name__ == "__main__":
    sys.exit(main())