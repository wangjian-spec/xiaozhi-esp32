from __future__ import annotations

import json
import sqlite3
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "words.db"
OUTPUT_PATH = BASE_DIR / "duplicate_word_example_regenerated_gpt54.json"
REPORT_PATH = BASE_DIR / "duplicate_word_example_regenerated_gpt54_report.json"
RELATED_TABLES = ("word", "word_meaning", "word_form", "word_example")


@dataclass(slots=True)
class DuplicateExampleRow:
    example_id: int
    meaning_id: int
    word_id: int
    word: str
    stage: int | None
    pos: str
    meaning_en: str
    meaning_zh: str
    difficulty: int | None
    example_en: str
    example_zh: str


def _normalize_space(value: Any) -> str:
    return " ".join(str(value or "").strip().split())


def _quote_text(value: str) -> str:
    return value.replace('"', "'")


def _term_label(word: str) -> str:
    stripped = word.strip()
    if any(character in stripped for character in (" ", "-", "(", ")", "/", ".", "'")):
        return "expression"
    return "word"


def _english_gloss(row: DuplicateExampleRow) -> str:
    gloss = _normalize_space(row.meaning_en) or _normalize_space(row.meaning_zh)
    if gloss:
        return _quote_text(gloss)
    return "the target meaning"


def _chinese_gloss(row: DuplicateExampleRow) -> str:
    gloss = _normalize_space(row.meaning_zh) or _normalize_space(row.meaning_en)
    if gloss:
        return gloss
    return "目标含义"


def _derive_difficulty(row: DuplicateExampleRow) -> int:
    if row.difficulty is not None:
        return row.difficulty
    if row.stage is None:
        return 2
    if row.stage <= 1:
        return 1
    if row.stage == 2:
        return 2
    if row.stage == 3:
        return 3
    if row.stage <= 5:
        return 4
    return 5


def _build_candidate_pairs(row: DuplicateExampleRow) -> list[tuple[str, str]]:
    word = _quote_text(_normalize_space(row.word)) or "the target word"
    english_gloss = _english_gloss(row)
    chinese_gloss = _chinese_gloss(row)
    label = _term_label(word)

    return [
        (
            f'In this lesson, the {label} "{word}" means "{english_gloss}."',
            f'在这一课里，{label == "expression" and "表达" or "单词"}“{word}”表示“{chinese_gloss}”。',
        ),
        (
            f'Here, we use "{word}" to express "{english_gloss}."',
            f'这里我们用“{word}”来表达“{chinese_gloss}”。',
        ),
        (
            f'In this unit, "{word}" is used for "{english_gloss}."',
            f'在这一单元里，“{word}”用于表示“{chinese_gloss}”。',
        ),
        (
            f'This example shows how "{word}" can mean "{english_gloss}."',
            f'这个例句展示了“{word}”如何表达“{chinese_gloss}”。',
        ),
        (
            f'We learn the {label} "{word}" here as "{english_gloss}."',
            f'这里我们学习{label == "expression" and "表达" or "单词"}“{word}”，意思是“{chinese_gloss}”。',
        ),
        (
            f'In today\'s class, "{word}" carries the idea of "{english_gloss}."',
            f'在今天这节课中，“{word}”表达的是“{chinese_gloss}”。',
        ),
        (
            f'The {label} "{word}" is the key term for "{english_gloss}" in this passage.',
            f'在这段内容中，{label == "expression" and "表达" or "单词"}“{word}”是“{chinese_gloss}”这个意思的关键词。',
        ),
        (
            f'In this exercise, "{word}" is the correct choice for "{english_gloss}."',
            f'在这个练习里，“{word}”是表达“{chinese_gloss}”的正确选择。',
        ),
    ]


def _generate_unique_pair(
    row: DuplicateExampleRow,
    used_examples: set[str],
) -> tuple[str, str]:
    candidates = _build_candidate_pairs(row)
    start_index = row.meaning_id % len(candidates)

    for offset in range(len(candidates)):
        example_en, example_zh = candidates[(start_index + offset) % len(candidates)]
        normalized = _normalize_space(example_en)
        if normalized not in used_examples:
            used_examples.add(normalized)
            return example_en, example_zh

    word = _quote_text(_normalize_space(row.word)) or "the target word"
    chinese_gloss = _chinese_gloss(row)
    english_gloss = _english_gloss(row)
    fallback_index = 1
    while True:
        example_en = (
            f'In lesson note {fallback_index}, "{word}" is used to mean "{english_gloss}."'
        )
        normalized = _normalize_space(example_en)
        if normalized not in used_examples:
            used_examples.add(normalized)
            example_zh = f'在课程说明 {fallback_index} 中，“{word}”表示“{chinese_gloss}”。'
            return example_en, example_zh
        fallback_index += 1


def _fetch_schema(conn: sqlite3.Connection) -> dict[str, dict[str, Any]]:
    schema: dict[str, dict[str, Any]] = {}
    for table_name in RELATED_TABLES:
        columns = [
            dict(row)
            for row in conn.execute(f"PRAGMA table_info({table_name})").fetchall()
        ]
        create_row = conn.execute(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name = ?",
            (table_name,),
        ).fetchone()
        index_rows = conn.execute(
            "SELECT sql FROM sqlite_master WHERE type='index' AND tbl_name = ? AND sql IS NOT NULL ORDER BY name",
            (table_name,),
        ).fetchall()
        foreign_keys = [
            dict(row)
            for row in conn.execute(f"PRAGMA foreign_key_list({table_name})").fetchall()
        ]
        schema[table_name] = {
            "columns": columns,
            "create_sql": create_row[0] if create_row else "",
            "indexes": [row[0] for row in index_rows if row[0]],
            "foreign_keys": foreign_keys,
        }
    return schema


def _fetch_duplicate_rows(conn: sqlite3.Connection) -> list[DuplicateExampleRow]:
    rows = conn.execute(
        """
        WITH duplicate_examples AS (
            SELECT TRIM(example_en) AS example_en
            FROM word_example
            WHERE TRIM(COALESCE(example_en, '')) <> ''
            GROUP BY TRIM(example_en)
            HAVING COUNT(*) > 1
        )
        SELECT
            we.id AS example_id,
            we.meaning_id,
            wm.word_id,
            COALESCE(w.word, '') AS word,
            wm.stage,
            COALESCE(wm.pos, '') AS pos,
            COALESCE(wm.meaning_en, '') AS meaning_en,
            COALESCE(wm.meaning_zh, '') AS meaning_zh,
            we.difficulty,
            COALESCE(we.example_en, '') AS example_en,
            COALESCE(we.example_zh, '') AS example_zh
        FROM word_example we
        JOIN duplicate_examples de
          ON de.example_en = TRIM(we.example_en)
        JOIN word_meaning wm
          ON wm.id = we.meaning_id
        JOIN word w
          ON w.id = wm.word_id
        ORDER BY TRIM(we.example_en), we.meaning_id, we.id
        """
    ).fetchall()

    return [DuplicateExampleRow(**dict(row)) for row in rows]


def _load_lookup(conn: sqlite3.Connection, query: str, key_name: str) -> dict[int, dict[str, Any]]:
    return {
        int(row[key_name]): dict(row)
        for row in conn.execute(query).fetchall()
        if row[key_name] is not None
    }


def _build_payload(
    conn: sqlite3.Connection,
    duplicate_rows: list[DuplicateExampleRow],
) -> tuple[dict[str, Any], dict[str, Any]]:
    schema = _fetch_schema(conn)
    words = _load_lookup(conn, "SELECT * FROM word ORDER BY id", "id")
    meanings = _load_lookup(conn, "SELECT * FROM word_meaning ORDER BY id", "id")

    forms_by_word: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in conn.execute("SELECT * FROM word_form ORDER BY word_id, id").fetchall():
        word_id = row["word_id"]
        if word_id is not None:
            forms_by_word[int(word_id)].append(dict(row))

    examples_by_meaning: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in conn.execute("SELECT * FROM word_example ORDER BY meaning_id, id").fetchall():
        meaning_id = row["meaning_id"]
        if meaning_id is not None:
            examples_by_meaning[int(meaning_id)].append(dict(row))

    duplicate_rows_by_meaning = {row.meaning_id: row for row in duplicate_rows}
    duplicate_phrases = sorted({_normalize_space(row.example_en) for row in duplicate_rows})
    used_examples = {
        _normalize_space(row["example_en"])
        for row in conn.execute("SELECT example_en FROM word_example WHERE TRIM(COALESCE(example_en, '')) <> ''").fetchall()
    }

    records: list[dict[str, Any]] = []
    replacement_report: list[dict[str, Any]] = []

    for meaning_id in sorted(duplicate_rows_by_meaning):
        duplicate_row = duplicate_rows_by_meaning[meaning_id]
        meaning = dict(meanings[meaning_id])
        word_id = int(meaning["word_id"])
        word = dict(words[word_id])
        examples = [dict(item) for item in examples_by_meaning.get(meaning_id, [])]

        for example in examples:
            if int(example.get("id") or 0) != duplicate_row.example_id:
                continue
            new_example_en, new_example_zh = _generate_unique_pair(duplicate_row, used_examples)
            example["example_en"] = new_example_en
            example["example_zh"] = new_example_zh
            example["difficulty"] = _derive_difficulty(duplicate_row)
            replacement_report.append(
                {
                    "example_id": duplicate_row.example_id,
                    "meaning_id": duplicate_row.meaning_id,
                    "word_id": duplicate_row.word_id,
                    "word": duplicate_row.word,
                    "meaning_en": duplicate_row.meaning_en,
                    "meaning_zh": duplicate_row.meaning_zh,
                    "old_example_en": duplicate_row.example_en,
                    "old_example_zh": duplicate_row.example_zh,
                    "new_example_en": new_example_en,
                    "new_example_zh": new_example_zh,
                    "difficulty": example["difficulty"],
                }
            )
            break

        records.append(
            {
                "word": word,
                "word_meaning": meaning,
                "word_form": [dict(item) for item in forms_by_word.get(word_id, [])],
                "word_example": examples,
            }
        )

    payload = {
        "metadata": {
            "source_database": str(DB_PATH),
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "generated_by": "GPT-5.4 offline duplicate example generator",
            "record_count": len(records),
            "duplicate_phrase_count": len(duplicate_phrases),
            "duplicate_row_count": len(duplicate_rows),
            "root_table": "word_meaning",
            "related_tables": list(RELATED_TABLES),
            "notes": [
                "Only records whose word_example.example_en was duplicated are included.",
                "Each affected meaning keeps its full word_example list so JSON import can replace child rows safely.",
            ],
        },
        "schema": schema,
        "records": records,
    }

    report = {
        "metadata": {
            "source_database": str(DB_PATH),
            "generated_at": payload["metadata"]["generated_at"],
            "generated_by": payload["metadata"]["generated_by"],
            "duplicate_phrase_count": len(duplicate_phrases),
            "duplicate_row_count": len(duplicate_rows),
            "affected_meaning_count": len(records),
        },
        "duplicate_phrases": duplicate_phrases,
        "replacements": replacement_report,
    }
    return payload, report


def main() -> None:
    if not DB_PATH.exists():
        raise FileNotFoundError(f"Database not found: {DB_PATH}")

    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        duplicate_rows = _fetch_duplicate_rows(conn)
        payload, report = _build_payload(conn, duplicate_rows)

    OUTPUT_PATH.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    print(
        json.dumps(
            {
                "output_json": str(OUTPUT_PATH),
                "report_json": str(REPORT_PATH),
                "record_count": payload["metadata"]["record_count"],
                "duplicate_phrase_count": payload["metadata"]["duplicate_phrase_count"],
                "duplicate_row_count": payload["metadata"]["duplicate_row_count"],
            },
            ensure_ascii=False,
            indent=2,
        )
    )


if __name__ == "__main__":
    main()