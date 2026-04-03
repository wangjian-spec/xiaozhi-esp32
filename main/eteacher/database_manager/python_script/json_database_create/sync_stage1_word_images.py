from __future__ import annotations

import argparse
import json
import re
import shutil
import unicodedata
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_RECORD_JSON_PATH = SCRIPT_DIR.parent / "stage1" / "book" / "record_stage1_merge_output.json"
DEFAULT_IMAGES_DIR = Path(r"D:\王健备份\个人\英语口语教师\图片和音频资源\images\stage1\words")
DEFAULT_UNMATCHED_DIR_NAME = "_unmatched_no_word_id"
DEFAULT_CONFLICT_DIR_NAME = "_matched_name_conflicts"

SUPPORTED_IMAGE_SUFFIXES = {
    ".png",
    ".jpg",
    ".jpeg",
    ".webp",
    ".bmp",
    ".gif",
    ".tif",
    ".tiff",
}
RELATED_NON_IMAGE_SUFFIXES = {".json", ".bin"}


@dataclass(slots=True)
class WordEntry:
    word_id: int
    word: str
    record_indices: list[int] = field(default_factory=list)


@dataclass(slots=True)
class MatchResult:
    entry: WordEntry | None
    reason: str


@dataclass(slots=True)
class RenamePlan:
    rename_pairs: list[tuple[Path, Path]]
    has_conflict: bool
    conflict_paths: list[Path]


def normalize_text(value: str) -> str:
    text = unicodedata.normalize("NFKC", value or "")
    text = text.replace("’", "'").replace("‘", "'")
    text = text.replace("–", "-").replace("—", "-")
    text = re.sub(r"\s+", " ", text).strip()
    return text


def normalize_lookup_key(value: str) -> str:
    text = normalize_text(value).casefold()
    text = text.replace("_", " ")
    text = re.sub(r"\s*=\s*", " = ", text)
    text = re.sub(r"[^0-9a-z'\- =]+", " ", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text


def build_lookup_keys(value: str) -> set[str]:
    normalized = normalize_lookup_key(value)
    if not normalized:
        return set()

    stripped = normalized.replace("'", "")
    space_variant = stripped.replace("-", " ")
    compact_variant = re.sub(r"[\s\-='\.]", "", stripped)
    underscore_variant = space_variant.replace(" ", "_")
    hyphen_variant = space_variant.replace(" ", "-")

    return {
        normalized,
        stripped,
        space_variant,
        compact_variant,
        underscore_variant,
        hyphen_variant,
    } - {""}


def sanitize_stem_part(value: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z_-]+", "_", value or "").strip("_")
    return sanitized or "record"


def json_load(path: Path) -> Any:
    for encoding in ("utf-8-sig", "utf-8"):
        try:
            return json.loads(path.read_text(encoding=encoding))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
    return json.loads(path.read_text(encoding="utf-8-sig"))


def json_dump(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def build_word_index(
    records: list[dict[str, Any]],
) -> tuple[dict[str, list[WordEntry]], dict[str, list[WordEntry]], dict[int, WordEntry]]:
    key_to_entries: dict[str, list[WordEntry]] = defaultdict(list)
    exact_word_to_entries: dict[str, dict[int, WordEntry]] = defaultdict(dict)
    id_to_entry: dict[int, WordEntry] = {}

    for record_index, record in enumerate(records):
        word_payload = record.get("word")
        if not isinstance(word_payload, dict):
            continue

        word_id = word_payload.get("id")
        word_text = normalize_text(str(word_payload.get("word") or ""))
        if not isinstance(word_id, int) or not word_text:
            continue

        entry = id_to_entry.get(word_id)
        if entry is None:
            entry = WordEntry(word_id=word_id, word=word_text)
            id_to_entry[word_id] = entry
            exact_word_to_entries[normalize_lookup_key(word_text)][word_id] = entry
            for key in build_lookup_keys(word_text):
                key_to_entries[key].append(entry)

        entry.record_indices.append(record_index)

    return key_to_entries, {key: list(entries.values()) for key, entries in exact_word_to_entries.items()}, id_to_entry


def strip_eink_suffix(stem: str) -> tuple[str, bool]:
    if stem.endswith("_eink"):
        return stem[:-5], True
    return stem, False


def candidate_words_from_stem(stem: str) -> list[str]:
    base_stem, _ = strip_eink_suffix(normalize_text(stem))
    parts = [part for part in base_stem.split("_") if part]
    candidates: list[str] = []

    def add_candidate(candidate: str) -> None:
        normalized = normalize_text(candidate)
        if normalized and normalized not in candidates:
            candidates.append(normalized)

    add_candidate(base_stem)
    if parts:
        for start_index in range(len(parts)):
            add_candidate("_".join(parts[start_index:]))
        non_numeric_index = next(
            (index for index, part in enumerate(parts) if not re.fullmatch(r"(?:idx)?\d+", part, flags=re.IGNORECASE)),
            len(parts),
        )
        if non_numeric_index < len(parts):
            add_candidate("_".join(parts[non_numeric_index:]))

    return candidates


def resolve_word_entry(
    stem: str,
    key_to_entries: dict[str, list[WordEntry]],
    exact_word_to_entries: dict[str, list[WordEntry]],
) -> MatchResult:
    for candidate in candidate_words_from_stem(stem):
        seen_ids: set[int] = set()

        def collect_entries(keys: list[str]) -> list[WordEntry]:
            matched_entries: list[WordEntry] = []
            seen_ids.clear()
            for key in keys:
                for entry in key_to_entries.get(key, []):
                    if entry.word_id not in seen_ids:
                        seen_ids.add(entry.word_id)
                        matched_entries.append(entry)
            return matched_entries

        exact_key = normalize_lookup_key(candidate)
        if exact_key:
            exact_entries = exact_word_to_entries.get(exact_key, [])
            if len(exact_entries) == 1:
                return MatchResult(entry=exact_entries[0], reason=f"matched:{candidate}")
            if len(exact_entries) > 1:
                return MatchResult(entry=None, reason=f"ambiguous:{candidate}")

        relaxed_keys = [
            key
            for key in (
                exact_key,
                exact_key.replace("'", "") if exact_key else "",
                exact_key.replace("-", " ") if exact_key else "",
                exact_key.replace("'", "").replace("-", " ") if exact_key else "",
            )
            if key
        ]
        relaxed_entries = collect_entries(relaxed_keys)
        if len(relaxed_entries) == 1:
            return MatchResult(entry=relaxed_entries[0], reason=f"matched:{candidate}")
        if len(relaxed_entries) > 1:
            return MatchResult(entry=None, reason=f"ambiguous:{candidate}")

        fuzzy_entries = collect_entries(sorted(build_lookup_keys(candidate)))
        if len(fuzzy_entries) == 1:
            return MatchResult(entry=fuzzy_entries[0], reason=f"matched:{candidate}")
        if len(fuzzy_entries) > 1:
            return MatchResult(entry=None, reason=f"ambiguous:{candidate}")

    return MatchResult(entry=None, reason="not-found")


def iter_image_files(images_dir: Path) -> list[Path]:
    return sorted(
        [path for path in images_dir.iterdir() if path.is_file() and path.suffix.lower() in SUPPORTED_IMAGE_SUFFIXES],
        key=lambda item: item.name.casefold(),
    )


def collect_group_files(images_dir: Path, logical_stem: str) -> list[Path]:
    group_files: list[Path] = []
    suffixes = SUPPORTED_IMAGE_SUFFIXES | RELATED_NON_IMAGE_SUFFIXES
    for suffix in suffixes:
        for candidate_stem in (logical_stem, f"{logical_stem}_eink"):
            candidate = images_dir / f"{candidate_stem}{suffix}"
            if candidate.exists() and candidate.is_file():
                group_files.append(candidate)
    return sorted(group_files, key=lambda item: item.name.casefold())


def pick_primary_image_name(images_dir: Path, logical_stem: str, target_stem: str) -> str | None:
    for suffix in (".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff"):
        candidate = images_dir / f"{logical_stem}{suffix}"
        if candidate.exists():
            return f"{target_stem}{suffix}"
    return None


def extract_prompt_from_sidecar(sidecar_path: Path) -> str:
    if not sidecar_path.exists():
        return ""

    try:
        payload = json_load(sidecar_path)
    except Exception:
        return ""

    prompt = payload.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip():
        request_payload = payload.get("request_payload")
        if isinstance(request_payload, dict):
            prompt = request_payload.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip():
        return ""

    normalized = prompt.replace("\r\n", "\n").strip()
    paragraphs = [part.strip() for part in re.split(r"\n\s*\n", normalized) if part.strip()]
    if not paragraphs:
        return ""
    return paragraphs[-1]


def ensure_word_image_schema(payload: dict[str, Any]) -> None:
    schema = payload.get("schema")
    if not isinstance(schema, dict):
        return

    word_schema = schema.get("word")
    if not isinstance(word_schema, dict):
        return

    columns = word_schema.get("columns")
    if not isinstance(columns, list):
        return

    if any(str(column.get("name")) == "word_image" for column in columns if isinstance(column, dict)):
        return

    columns.append(
        {
            "cid": len(columns),
            "name": "word_image",
            "type": "TEXT",
            "notnull": 0,
            "dflt_value": None,
            "pk": 0,
        }
    )

    create_sql = word_schema.get("create_sql")
    if isinstance(create_sql, str) and "word_image TEXT" not in create_sql:
        word_schema["create_sql"] = create_sql.replace(
            "\n\t\t\t\t\timage TEXT\n",
            "\n\t\t\t\t\timage TEXT,\n\t\t\t\t\tword_image TEXT\n",
        )


def update_record_payload(
    payload: dict[str, Any],
    id_to_entry: dict[int, WordEntry],
    prompt_by_word_id: dict[int, str],
    image_name_by_word_id: dict[int, str],
) -> tuple[int, int]:
    records = payload.get("records")
    if not isinstance(records, list):
        raise ValueError("JSON 中缺少 records 数组")

    ensure_word_image_schema(payload)

    updated_word_count = 0
    updated_prompt_count = 0

    for record in records:
        if not isinstance(record, dict):
            continue
        word_payload = record.get("word")
        if not isinstance(word_payload, dict):
            continue
        word_id = word_payload.get("id")
        if not isinstance(word_id, int) or word_id not in id_to_entry:
            continue

        image_name = image_name_by_word_id.get(word_id)
        if image_name and word_payload.get("image") != image_name:
            word_payload["image"] = image_name
            updated_word_count += 1

        prompt_text = prompt_by_word_id.get(word_id, "")
        if prompt_text and word_payload.get("word_image") != prompt_text:
            word_payload["word_image"] = prompt_text
            updated_prompt_count += 1

    return updated_word_count, updated_prompt_count


def plan_group_renames(
    images_dir: Path,
    logical_stem: str,
    target_stem: str,
 ) -> RenamePlan:
    rename_pairs: list[tuple[Path, Path]] = []
    source_group_paths = collect_group_files(images_dir, logical_stem)
    source_group_set = {path.resolve() for path in source_group_paths}
    conflict_paths: list[Path] = []
    for source_path in source_group_paths:
        source_base, is_eink = strip_eink_suffix(source_path.stem)
        del source_base
        destination_stem = f"{target_stem}_eink" if is_eink else target_stem
        destination_path = source_path.with_name(f"{destination_stem}{source_path.suffix}")
        rename_pairs.append((source_path, destination_path))

    for source_path, destination_path in rename_pairs:
        if source_path == destination_path:
            continue
        if destination_path.exists() and destination_path.resolve() not in source_group_set:
            conflict_paths.append(destination_path)

    return RenamePlan(
        rename_pairs=rename_pairs,
        has_conflict=bool(conflict_paths),
        conflict_paths=sorted(conflict_paths, key=lambda item: item.name.casefold()),
    )


def rename_group_files(
    images_dir: Path,
    logical_stem: str,
    target_stem: str,
    dry_run: bool,
) -> list[tuple[Path, Path]]:
    rename_plan = plan_group_renames(images_dir, logical_stem, target_stem)
    if rename_plan.has_conflict:
        conflict_text = ", ".join(str(path) for path in rename_plan.conflict_paths)
        raise FileExistsError(f"目标文件已存在，无法覆盖: {conflict_text}")

    if not dry_run:
        for source_path, destination_path in rename_plan.rename_pairs:
            if source_path != destination_path:
                shutil.move(str(source_path), str(destination_path))

    return rename_plan.rename_pairs


def move_group_to_folder(
    group_files: list[Path],
    destination_dir: Path,
    dry_run: bool,
) -> list[tuple[Path, Path]]:
    move_pairs: list[tuple[Path, Path]] = []
    for source_path in group_files:
        destination_path = destination_dir / source_path.name
        if destination_path.exists():
            raise FileExistsError(f"目标目录中已存在同名文件: {destination_path}")
        move_pairs.append((source_path, destination_path))

    if not dry_run and move_pairs:
        destination_dir.mkdir(parents=True, exist_ok=True)
        for source_path, destination_path in move_pairs:
            shutil.move(str(source_path), str(destination_path))

    return move_pairs


def move_group_to_unmatched(
    images_dir: Path,
    logical_stem: str,
    unmatched_dir: Path,
    dry_run: bool,
) -> list[tuple[Path, Path]]:
    return move_group_to_folder(collect_group_files(images_dir, logical_stem), unmatched_dir, dry_run)


def update_sidecar_metadata(sidecar_path: Path, word_id: int, word: str, target_stem: str, dry_run: bool) -> bool:
    if not sidecar_path.exists():
        return False

    payload = json_load(sidecar_path)
    if not isinstance(payload, dict):
        return False

    changed = False
    if payload.get("word") != word:
        payload["word"] = word
        changed = True
    if payload.get("word_id") != word_id:
        payload["word_id"] = word_id
        changed = True

    output_files = payload.get("output_files")
    if isinstance(output_files, dict):
        expected_output_files = {
            "png": str(sidecar_path.with_name(f"{target_stem}.png")),
            "eink_png": str(sidecar_path.with_name(f"{target_stem}_eink.png")),
            "bin": str(sidecar_path.with_name(f"{target_stem}.bin")),
        }
        for key, expected_value in expected_output_files.items():
            if output_files.get(key) != expected_value:
                output_files[key] = expected_value
                changed = True

    if changed and not dry_run:
        json_dump(sidecar_path, payload)
    return changed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="同步 stage1 单词图片命名、提示词和 JSON 字段")
    parser.add_argument(
        "--record-json",
        type=Path,
        default=DEFAULT_RECORD_JSON_PATH,
        help=f"词库 JSON 路径，默认: {DEFAULT_RECORD_JSON_PATH}",
    )
    parser.add_argument(
        "--images-dir",
        type=Path,
        default=DEFAULT_IMAGES_DIR,
        help=f"图片目录，默认: {DEFAULT_IMAGES_DIR}",
    )
    parser.add_argument(
        "--unmatched-dir-name",
        default=DEFAULT_UNMATCHED_DIR_NAME,
        help=f"未匹配文件夹名称，默认: {DEFAULT_UNMATCHED_DIR_NAME}",
    )
    parser.add_argument(
        "--conflict-dir-name",
        default=DEFAULT_CONFLICT_DIR_NAME,
        help=f"命名冲突文件夹名称，默认: {DEFAULT_CONFLICT_DIR_NAME}",
    )
    parser.add_argument("--dry-run", action="store_true", help="只输出计划，不改文件")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    record_json_path = args.record_json.expanduser().resolve()
    images_dir = args.images_dir.expanduser().resolve()
    unmatched_dir = images_dir / args.unmatched_dir_name
    conflict_dir = images_dir / args.conflict_dir_name
    dry_run = bool(args.dry_run)

    if not record_json_path.exists():
        raise FileNotFoundError(f"词库 JSON 不存在: {record_json_path}")
    if not images_dir.exists() or not images_dir.is_dir():
        raise FileNotFoundError(f"图片目录不存在: {images_dir}")

    payload = json_load(record_json_path)
    if not isinstance(payload, dict):
        raise ValueError("词库 JSON 顶层必须是对象")

    records = payload.get("records")
    if not isinstance(records, list):
        raise ValueError("词库 JSON 中缺少 records 数组")

    key_to_entries, exact_word_to_entries, id_to_entry = build_word_index(records)
    image_files = iter_image_files(images_dir)

    logical_stems = sorted({strip_eink_suffix(path.stem)[0] for path in image_files}, key=str.casefold)
    prompt_by_word_id: dict[int, str] = {}
    image_name_by_word_id: dict[int, str] = {}

    renamed_group_count = 0
    unmatched_group_count = 0
    renamed_file_count = 0
    moved_file_count = 0
    conflict_group_count = 0
    conflict_file_count = 0
    updated_sidecar_count = 0
    ambiguous_stems: list[str] = []
    unmatched_stems: list[str] = []
    conflicted_stems: list[str] = []

    for logical_stem in logical_stems:
        match = resolve_word_entry(logical_stem, key_to_entries, exact_word_to_entries)
        if match.entry is None:
            if match.reason.startswith("ambiguous:"):
                ambiguous_stems.append(f"{logical_stem} ({match.reason})")
            else:
                unmatched_stems.append(logical_stem)
            move_pairs = move_group_to_unmatched(images_dir, logical_stem, unmatched_dir, dry_run)
            unmatched_group_count += 1
            moved_file_count += len(move_pairs)
            continue

        entry = match.entry
        target_stem = f"{entry.word_id}_{sanitize_stem_part(entry.word)}"
        rename_plan = plan_group_renames(images_dir, logical_stem, target_stem)
        if rename_plan.has_conflict:
            move_pairs = move_group_to_folder(collect_group_files(images_dir, logical_stem), conflict_dir, dry_run)
            conflict_group_count += 1
            conflict_file_count += len(move_pairs)
            conflicted_stems.append(f"{logical_stem} -> {target_stem}")
            continue

        rename_pairs = rename_group_files(images_dir, logical_stem, target_stem, dry_run)
        renamed_group_count += 1
        renamed_file_count += sum(1 for source_path, destination_path in rename_pairs if source_path != destination_path)

        sidecar_path = images_dir / f"{target_stem}.json"
        sidecar_source_path = sidecar_path
        if dry_run and not sidecar_source_path.exists():
            fallback_sidecar_path = images_dir / f"{logical_stem}.json"
            if fallback_sidecar_path.exists():
                sidecar_source_path = fallback_sidecar_path

        prompt_text = extract_prompt_from_sidecar(sidecar_source_path)
        if prompt_text:
            prompt_by_word_id[entry.word_id] = prompt_text

        image_name = pick_primary_image_name(images_dir, logical_stem, target_stem)
        if image_name:
            image_name_by_word_id[entry.word_id] = image_name

        if sidecar_source_path.exists() and update_sidecar_metadata(
            sidecar_source_path,
            entry.word_id,
            entry.word,
            target_stem,
            dry_run,
        ):
            updated_sidecar_count += 1

    updated_word_count, updated_prompt_count = update_record_payload(
        payload,
        id_to_entry,
        prompt_by_word_id,
        image_name_by_word_id,
    )
    if not dry_run:
        json_dump(record_json_path, payload)

    print(f"词库 JSON: {record_json_path}")
    print(f"图片目录: {images_dir}")
    print(f"dry_run: {dry_run}")
    print(f"已匹配分组: {renamed_group_count}")
    print(f"未匹配分组: {unmatched_group_count}")
    print(f"重命名文件数: {renamed_file_count}")
    print(f"移动文件数: {moved_file_count}")
    print(f"冲突分组数: {conflict_group_count}")
    print(f"冲突移动文件数: {conflict_file_count}")
    print(f"更新 sidecar 数: {updated_sidecar_count}")
    print(f"更新 word.image 数: {updated_word_count}")
    print(f"更新 word.word_image 数: {updated_prompt_count}")
    print(f"提取到提示词 word_id 数: {len(prompt_by_word_id)}")

    if unmatched_stems:
        print("未匹配 stem:")
        for stem in unmatched_stems:
            print(f"- {stem}")

    if ambiguous_stems:
        print("歧义 stem:")
        for stem in ambiguous_stems:
            print(f"- {stem}")

    if conflicted_stems:
        print("命名冲突 stem:")
        for stem in conflicted_stems:
            print(f"- {stem}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())