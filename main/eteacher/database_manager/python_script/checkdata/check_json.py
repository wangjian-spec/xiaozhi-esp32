from __future__ import annotations

import importlib
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict
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
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)


ROOT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_JSON_PATH = ROOT_DIR / "stage1" / "book" / "record_stage1_gpt5.4_generated.json"
DEFAULT_SECONDARY_JSON_PATH = ROOT_DIR / "stage1" / "book" / "record_stage1_gpt5.4_generated_2201_4096.json"
DEFAULT_WORD_IMAGE_DIR = Path(r"D:\王健备份\个人\英语口语教师\图片和音频资源\images\stage1\words")
APP_TITLE = "JSON 记录校验工具"
WORD_TEXT_PATTERN = re.compile(r"^[A-Za-z]+(?: [A-Za-z]+)*$")

SIMPLIFIED_WORD_TAG_CATEGORIES = [
    "名词",
    "动词",
    "形容词",
    "副词",
    "数词",
    "限定词",
    "基础句型",
    "时态",
    "句式",
    "连接",
    "问候/告别",
    "自我介绍",
    "礼貌表达",
    "信息交换",
    "态度与情绪",
    "行为驱动",
    "人",
    "日常生活",
    "学校与学习",
    "出行与地点",
    "自然与环境",
    "物品与科技",
    "娱乐与文化",
    "抽象概念",
]

SIMPLIFIED_POS_CATEGORIES = [
    "noun",
    "verb",
    "adjective",
    "adverb",
    "pronoun",
    "determiner",
    "preposition",
    "conjunction",
    "interjection",
    "numeral",
    "auxiliary_verb",
]

SIMPLIFIED_POS_ALIASES = {
    "n": "noun",
    "n.": "noun",
    "noun": "noun",
    "v": "verb",
    "v.": "verb",
    "verb": "verb",
    "adj": "adjective",
    "adj.": "adjective",
    "adjective": "adjective",
    "adv": "adverb",
    "adv.": "adverb",
    "adverb": "adverb",
    "pron": "pronoun",
    "pron.": "pronoun",
    "pronoun": "pronoun",
    "det": "determiner",
    "det.": "determiner",
    "determiner": "determiner",
    "prep": "preposition",
    "prep.": "preposition",
    "preposition": "preposition",
    "conj": "conjunction",
    "conj.": "conjunction",
    "conjunction": "conjunction",
    "interj": "interjection",
    "interj.": "interjection",
    "interjection": "interjection",
    "num": "numeral",
    "num.": "numeral",
    "numeral": "numeral",
    "aux": "auxiliary_verb",
    "aux.": "auxiliary_verb",
    "auxiliary": "auxiliary_verb",
    "auxiliary verb": "auxiliary_verb",
    "auxiliary_verb": "auxiliary_verb",
    "modal verb": "auxiliary_verb",
    "modal_verb": "auxiliary_verb",
}


def normalize_text(value: Any) -> str:
    if value is None:
        return ""
    text = str(value).replace("\u3000", " ").strip()
    return " ".join(text.split())


def is_empty_text(value: Any) -> bool:
    return normalize_text(value) == ""


def normalize_word_key(value: Any) -> str:
    return normalize_text(value).replace("_", " ").lower()


def contains_any_keyword(text: str, keywords: tuple[str, ...]) -> bool:
    return any(keyword in text for keyword in keywords)


def split_category_candidates(text: str) -> list[str]:
    return [part.strip() for part in re.split(r"[;,/&，；、]+", text) if part.strip()]


def normalize_pos_candidate(text: str) -> str:
    return normalize_text(text).lower().replace("-", "_")


def map_pos_text_to_category(text: str) -> str:
    normalized_text = normalize_text(text)
    if normalized_text == "":
        return ""

    candidates = split_category_candidates(normalized_text)
    if not candidates:
        candidates = [normalized_text]

    for candidate in candidates:
        normalized_candidate = normalize_pos_candidate(candidate)
        alias_value = SIMPLIFIED_POS_ALIASES.get(normalized_candidate)
        if alias_value:
            return alias_value

    lowered_text = normalized_text.lower()
    if contains_any_keyword(lowered_text, ("助动词", "情态动词")):
        return "auxiliary_verb"
    if contains_any_keyword(lowered_text, ("代词", "物主代词", "反身代词", "不定代词", "疑问代词")):
        return "pronoun"
    if contains_any_keyword(lowered_text, ("限定词", "冠词", "限定", "指示词", "形容词性物主代词")):
        return "determiner"
    if contains_any_keyword(lowered_text, ("数词", "序数词", "数量", "量词")):
        return "numeral"
    if contains_any_keyword(lowered_text, ("介词",)):
        return "preposition"
    if contains_any_keyword(lowered_text, ("连词",)):
        return "conjunction"
    if contains_any_keyword(lowered_text, ("感叹词", "拟声词", "问候语", "告别语", "礼貌用语", "祝福语", "应答语", "称呼语", "称呼", "日常问候")):
        return "interjection"
    if contains_any_keyword(lowered_text, ("形容词",)):
        return "adjective"
    if contains_any_keyword(lowered_text, ("副词",)):
        return "adverb"
    if contains_any_keyword(lowered_text, ("动词", "过去式", "不及物动词")):
        return "verb"
    if contains_any_keyword(lowered_text, ("名词", "专有名词")):
        return "noun"
    return ""


def simplify_word_tag(record: dict[str, Any]) -> str:
    word_meaning = record.get("word_meaning") if isinstance(record.get("word_meaning"), dict) else {}
    raw_tag = normalize_text(word_meaning.get("word_tag"))
    if raw_tag == "":
        return ""

    pos = normalize_text(word_meaning.get("pos"))
    word = record.get("word") if isinstance(record.get("word"), dict) else {}
    word_type = normalize_text(word.get("word_type"))
    tag_text = raw_tag.lower()

    if contains_any_keyword(tag_text, ("问候", "告别", "告别语", "招呼", "再见")):
        return "问候/告别"
    if contains_any_keyword(tag_text, ("自我介绍", "介绍自己")):
        return "自我介绍"
    if contains_any_keyword(tag_text, ("礼貌", "礼貌用语", "感谢", "道歉", "客套")):
        return "礼貌表达"
    if contains_any_keyword(tag_text, ("请求", "建议", "指令", "命令", "邀请", "提议", "需求", "要求")):
        return "行为驱动"
    if contains_any_keyword(tag_text, ("喜好", "偏好", "意愿", "计划", "情绪", "情感", "评价", "比较", "感受", "态度", "判断")):
        return "态度与情绪"
    if contains_any_keyword(tag_text, ("提问", "问句", "疑问", "回答", "应答", "回应", "描述", "解释", "交流", "交际", "表达", "书信")):
        return "信息交换"

    if contains_any_keyword(tag_text, ("时态", "过去", "现在", "将来")):
        return "时态"
    if contains_any_keyword(tag_text, ("存在句", "比较句", "条件句")):
        return "句式"
    if contains_any_keyword(tag_text, ("连词", "连接")):
        return "连接"
    if contains_any_keyword(tag_text, ("句型", "否定", "陈述")):
        return "基础句型"

    if contains_any_keyword(tag_text, ("代词", "冠词", "限定词", "指示", "物主", "所属关系", "人称")):
        return "限定词"
    if contains_any_keyword(tag_text, ("数词", "序数", "数量", "分数")):
        return "数词"
    if contains_any_keyword(tag_text, ("副词",)):
        return "副词"
    if contains_any_keyword(tag_text, ("形容词",)):
        return "形容词"
    if contains_any_keyword(tag_text, ("动词", "动作", "变化动作", "感官动作", "思维动作")):
        return "动词"
    if contains_any_keyword(tag_text, ("名词",)):
        return "名词"

    if contains_any_keyword(tag_text, ("人物", "家庭", "家人", "家庭成员", "外貌", "外形", "性格", "年龄", "职业", "身份", "身体部位", "人物关系", "人物特征", "人物称谓")):
        return "人"
    if contains_any_keyword(tag_text, ("学校", "学习", "学科", "教室", "课堂", "文具", "教学用品", "学习用品", "学校生活", "学校活动", "书籍读物")):
        return "学校与学习"
    if contains_any_keyword(tag_text, ("交通", "出行", "地点", "方位", "路线", "城市", "国家", "地区", "地理", "场所", "位置", "旅行", "建筑", "地名", "名胜古迹", "空间")):
        return "出行与地点"
    if contains_any_keyword(tag_text, ("天气", "季节", "动物", "植物", "自然", "环境", "天体", "天文", "宇宙", "地形", "农业")):
        return "自然与环境"
    if contains_any_keyword(tag_text, ("节日", "艺术", "音乐", "游戏", "娱乐", "媒体", "文化", "乐器")):
        return "娱乐与文化"
    if contains_any_keyword(tag_text, ("家具", "家电", "电器", "电子", "设备", "工具", "容器", "物品", "用品", "家居", "外观")):
        return "物品与科技"
    if contains_any_keyword(tag_text, ("饮食", "食物", "饮料", "起居", "家务", "购物", "金钱", "价格", "健康", "兴趣爱好", "兴趣", "休息", "体育", "劳动", "味道", "味觉")):
        return "日常生活"
    if contains_any_keyword(tag_text, ("时间", "数量", "程度", "关系", "因果", "条件", "规则", "变化", "原因", "安全", "和平", "性质", "品质", "形状", "颜色", "大小", "尺寸", "单位", "图形", "其他")):
        return "抽象概念"

    pos_text = pos.lower()
    if contains_any_keyword(pos_text, ("代词", "冠词", "限定词", "数词")):
        return "限定词" if contains_any_keyword(pos_text, ("代词", "冠词", "限定词")) else "数词"
    if contains_any_keyword(pos_text, ("副词",)):
        return "副词"
    if contains_any_keyword(pos_text, ("形容词",)):
        return "形容词"
    if contains_any_keyword(pos_text, ("动词",)):
        return "动词"
    if contains_any_keyword(pos_text, ("名词",)):
        return "名词"
    if contains_any_keyword(word_type.lower(), ("句子", "词组", "短语")):
        return "基础句型"
    return "抽象概念"


def simplify_pos(record: dict[str, Any]) -> str:
    word_meaning = record.get("word_meaning") if isinstance(record.get("word_meaning"), dict) else {}
    raw_pos = normalize_text(word_meaning.get("pos"))
    if raw_pos == "":
        return ""

    normalized_pos = map_pos_text_to_category(raw_pos)
    if normalized_pos:
        return normalized_pos

    raw_word_tag = normalize_text(word_meaning.get("word_tag"))
    if raw_word_tag:
        normalized_from_tag = map_pos_text_to_category(raw_word_tag)
        if normalized_from_tag:
            return normalized_from_tag

    word = record.get("word") if isinstance(record.get("word"), dict) else {}
    word_type = normalize_text(word.get("word_type"))
    normalized_from_word_type = map_pos_text_to_category(word_type)
    if normalized_from_word_type:
        return normalized_from_word_type

    return ""


def build_distinct_value_summary(values: list[str], empty_count: int, extra: dict[str, Any] | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "value_count": len(values),
        "values": values,
        "empty_count": empty_count,
    }
    if extra:
        result.update(extra)
    return result


def build_example_preview(example_en: str, example_zh: str) -> str:
    preview_parts = [part for part in [example_en, example_zh] if part]
    preview = " / ".join(preview_parts)
    return preview if len(preview) <= 120 else f"{preview[:117]}..."


def load_json_records(file_path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    with file_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    records = payload.get("records")
    if not isinstance(records, list):
        raise ValueError("JSON 中缺少 records 数组")
    return payload, records


def write_json(file_path: Path, payload: dict[str, Any]) -> None:
    with file_path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


def ensure_backup(file_path: Path) -> Path:
    backup_path = file_path.with_name(f"{file_path.stem}.backup{file_path.suffix}")
    if not backup_path.exists():
        shutil.copy2(file_path, backup_path)
    return backup_path


def build_default_report_path(file_path: Path) -> Path:
    return file_path.with_name(f"{file_path.stem}_duplicate_examples_report.json")


def build_validation_report_path(file_path: Path) -> Path:
    return file_path.with_name(f"{file_path.stem}_validation_report.json")


def build_word_image_report_path(file_path: Path) -> Path:
    return file_path.with_name(f"{file_path.stem}_word_image_report.json")


def build_all_words_output_path(file_path: Path) -> Path:
    return file_path.with_name(f"{file_path.stem}_all_words.json")


def build_default_merge_output_path(file_path: Path) -> Path:
    return file_path.with_name(f"{file_path.stem}_merged{file_path.suffix}")


def list_image_files(image_dir: Path) -> list[Path]:
    allowed_suffixes = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif"}
    return sorted(
        [path for path in image_dir.iterdir() if path.is_file() and path.suffix.lower() in allowed_suffixes],
        key=lambda item: item.name.lower(),
    )


def parse_image_filename(image_path: Path) -> tuple[int, str, str] | None:
    stem = image_path.stem.strip()
    if "_" not in stem:
        return None

    word_id_text, raw_word = stem.split("_", 1)
    if not word_id_text.isdigit():
        return None

    display_word = normalize_text(raw_word.replace("_", " "))
    if display_word == "":
        return None

    return int(word_id_text), normalize_word_key(display_word), display_word


def build_record_lookup(records: list[dict[str, Any]]) -> dict[tuple[int, str], list[tuple[int, dict[str, Any]]]]:
    lookup: dict[tuple[int, str], list[tuple[int, dict[str, Any]]]] = defaultdict(list)
    for record_index, record in enumerate(records):
        word = record.get("word") if isinstance(record.get("word"), dict) else {}
        if not isinstance(word, dict):
            continue

        word_id = word.get("id")
        word_text = normalize_word_key(word.get("word"))
        if not isinstance(word_id, int) or word_text == "":
            continue

        lookup[(word_id, word_text)].append((record_index, record))
    return lookup


def build_unique_target_path(target_dir: Path, file_name: str) -> Path:
    candidate = target_dir / file_name
    if not candidate.exists():
        return candidate

    stem = Path(file_name).stem
    suffix = Path(file_name).suffix
    counter = 1
    while True:
        candidate = target_dir / f"{stem}_{counter}{suffix}"
        if not candidate.exists():
            return candidate
        counter += 1


def collect_stats(records: list[dict[str, Any]]) -> dict[str, int]:
    hidden_true = 0
    hidden_false = 0
    empty_image = 0
    non_empty_image = 0
    example_count = 0

    for record in records:
        if bool(record.get("hidden")):
            hidden_true += 1
        else:
            hidden_false += 1

        image_value = ((record.get("word") or {}) if isinstance(record.get("word"), dict) else {}).get("image")
        if is_empty_text(image_value):
            empty_image += 1
        else:
            non_empty_image += 1

        examples = record.get("word_example")
        if isinstance(examples, list):
            example_count += len(examples)

    return {
        "record_count": len(records),
        "hidden_true": hidden_true,
        "hidden_false": hidden_false,
        "empty_image": empty_image,
        "non_empty_image": non_empty_image,
        "example_count": example_count,
    }


def set_all_hidden_false(records: list[dict[str, Any]]) -> dict[str, int]:
    changed = 0
    for record in records:
        if record.get("hidden") is not False:
            changed += 1
        record["hidden"] = False
    return {"changed": changed, "matched": len(records)}


def set_hidden_true_for_empty_word_image(records: list[dict[str, Any]]) -> dict[str, int]:
    matched = 0
    changed = 0
    for record in records:
        word = record.get("word") if isinstance(record.get("word"), dict) else {}
        image_value = word.get("image") if isinstance(word, dict) else None
        if is_empty_text(image_value):
            matched += 1
            if record.get("hidden") is not True:
                changed += 1
            record["hidden"] = True
    return {"changed": changed, "matched": matched}


def set_hidden_false_for_non_empty_word_image(records: list[dict[str, Any]]) -> dict[str, int]:
    matched = 0
    changed = 0
    for record in records:
        word = record.get("word") if isinstance(record.get("word"), dict) else {}
        image_value = word.get("image") if isinstance(word, dict) else None
        if not is_empty_text(image_value):
            matched += 1
            if record.get("hidden") is not False:
                changed += 1
            record["hidden"] = False
    return {"changed": changed, "matched": matched}


def write_back_simplified_word_tags(records: list[dict[str, Any]]) -> dict[str, int]:
    matched = 0
    changed = 0
    cleared = 0

    for record in records:
        if not isinstance(record, dict):
            continue

        word_meaning = record.get("word_meaning")
        if not isinstance(word_meaning, dict):
            word_meaning = {}
            record["word_meaning"] = word_meaning

        matched += 1
        previous_value = normalize_text(word_meaning.get("word_tag"))
        next_value = simplify_word_tag(record)
        if previous_value != next_value:
            changed += 1
        if previous_value and next_value == "":
            cleared += 1
        word_meaning["word_tag"] = next_value

    return {"changed": changed, "matched": matched, "cleared": cleared}


def write_back_simplified_pos(records: list[dict[str, Any]]) -> dict[str, int]:
    matched = 0
    changed = 0
    cleared = 0

    for record in records:
        if not isinstance(record, dict):
            continue

        word_meaning = record.get("word_meaning")
        if not isinstance(word_meaning, dict):
            word_meaning = {}
            record["word_meaning"] = word_meaning

        matched += 1
        previous_value = normalize_text(word_meaning.get("pos"))
        next_value = simplify_pos(record)
        if previous_value != next_value:
            changed += 1
        if previous_value and next_value == "":
            cleared += 1
        word_meaning["pos"] = next_value

    return {"changed": changed, "matched": matched, "cleared": cleared}


def set_hidden_true_for_records_matched_by_images(
    records: list[dict[str, Any]], image_dir: Path
) -> dict[str, int]:
    lookup = build_record_lookup(records)
    matched_record_indices: set[int] = set()
    changed = 0
    matched_images = 0
    unmatched_images = 0
    invalid_name_images = 0

    for image_path in list_image_files(image_dir):
        parsed = parse_image_filename(image_path)
        if parsed is None:
            invalid_name_images += 1
            unmatched_images += 1
            continue

        word_id, word_key, _ = parsed
        matched_records = lookup.get((word_id, word_key), [])
        if not matched_records:
            unmatched_images += 1
            continue

        matched_images += 1
        for record_index, record in matched_records:
            matched_record_indices.add(record_index)
            if record.get("hidden") is not True:
                changed += 1
            record["hidden"] = True

    return {
        "changed": changed,
        "matched": len(matched_record_indices),
        "matched_images": matched_images,
        "unmatched_images": unmatched_images,
        "invalid_name_images": invalid_name_images,
    }


def move_unmatched_images(image_dir: Path, records: list[dict[str, Any]]) -> dict[str, int]:
    lookup = build_record_lookup(records)
    unmatched_dir = image_dir / "_unmatched_no_word_id"
    unmatched_dir.mkdir(parents=True, exist_ok=True)

    moved = 0
    matched_images = 0
    invalid_name_images = 0

    for image_path in list_image_files(image_dir):
        parsed = parse_image_filename(image_path)
        is_matched = False
        if parsed is None:
            invalid_name_images += 1
        else:
            word_id, word_key, _ = parsed
            is_matched = (word_id, word_key) in lookup

        if is_matched:
            matched_images += 1
            continue

        target_path = build_unique_target_path(unmatched_dir, image_path.name)
        shutil.move(str(image_path), str(target_path))
        moved += 1

    return {
        "changed": moved,
        "matched": moved,
        "moved": moved,
        "matched_images": matched_images,
        "invalid_name_images": invalid_name_images,
    }


def format_result_details(result: dict[str, Any]) -> str:
    detail_parts: list[str] = []
    extras = [
        ("cleared", "清空字段"),
        ("matched_images", "匹配图片"),
        ("unmatched_images", "未匹配图片"),
        ("invalid_name_images", "命名异常图片"),
        ("moved", "移动文件"),
    ]
    for key, label in extras:
        value = result.get(key)
        if isinstance(value, int):
            detail_parts.append(f"{label} {value} 个")
    return "；".join(detail_parts)


def collect_distinct_field_values(records: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    field_extractors = {
        "word.word_type": lambda record: ((record.get("word") or {}) if isinstance(record.get("word"), dict) else {}).get("word_type"),
        "word_meaning.word_tag": lambda record: ((record.get("word_meaning") or {}) if isinstance(record.get("word_meaning"), dict) else {}).get("word_tag"),
        "word_meaning.pos": lambda record: ((record.get("word_meaning") or {}) if isinstance(record.get("word_meaning"), dict) else {}).get("pos"),
    }

    result: dict[str, dict[str, Any]] = {}
    for field_name, extractor in field_extractors.items():
        values: set[str] = set()
        empty_count = 0

        for record in records:
            normalized_value = normalize_text(extractor(record))
            if normalized_value == "":
                empty_count += 1
                continue
            values.add(normalized_value)

        sorted_values = sorted(values, key=str.lower)
        result[field_name] = build_distinct_value_summary(sorted_values, empty_count)

    raw_word_tag_summary = result["word_meaning.word_tag"]
    simplified_word_tags = sorted(
        {simplify_word_tag(record) for record in records if simplify_word_tag(record)},
        key=SIMPLIFIED_WORD_TAG_CATEGORIES.index,
    )
    result["word_meaning.word_tag"] = build_distinct_value_summary(
        simplified_word_tags,
        raw_word_tag_summary["empty_count"],
        {
            "normalized": True,
            "normalized_from_raw_value_count": raw_word_tag_summary["value_count"],
            "normalization_rule": "language_form_communicative_function_topics_v1",
        },
    )
    result["word_meaning.word_tag_raw"] = raw_word_tag_summary

    raw_pos_summary = result["word_meaning.pos"]
    simplified_pos_values = sorted(
        {simplify_pos(record) for record in records if simplify_pos(record)},
        key=SIMPLIFIED_POS_CATEGORIES.index,
    )
    result["word_meaning.pos"] = build_distinct_value_summary(
        simplified_pos_values,
        raw_pos_summary["empty_count"],
        {
            "normalized": True,
            "normalized_from_raw_value_count": raw_pos_summary["value_count"],
            "normalization_rule": "core_pos_v1",
        },
    )
    result["word_meaning.pos_raw"] = raw_pos_summary

    return result


def build_word_groups(records: list[dict[str, Any]]) -> dict[tuple[int, str], dict[str, Any]]:
    groups: dict[tuple[int, str], dict[str, Any]] = {}
    for record_index, record in enumerate(records, start=1):
        word = record.get("word") if isinstance(record.get("word"), dict) else None
        if word is None:
            continue

        word_id = word.get("id")
        word_text = normalize_text(word.get("word"))
        word_key = normalize_word_key(word_text)
        if not isinstance(word_id, int) or word_key == "":
            continue

        group_key = (word_id, word_key)
        if group_key not in groups:
            groups[group_key] = {
                "word_id": word_id,
                "word": word_text,
                "records": [],
                "record_indices": [],
                "meaning_ids": set(),
                "meaning_zh_values": set(),
                "example_count": 0,
            }

        group = groups[group_key]
        group["records"].append(record)
        group["record_indices"].append(record_index)

        word_meaning = record.get("word_meaning") if isinstance(record.get("word_meaning"), dict) else None
        if word_meaning is not None:
            meaning_id = word_meaning.get("id")
            if isinstance(meaning_id, int):
                group["meaning_ids"].add(meaning_id)

            meaning_zh = normalize_text(word_meaning.get("meaning_zh"))
            if meaning_zh:
                group["meaning_zh_values"].add(meaning_zh)

        examples = record.get("word_example")
        if isinstance(examples, list):
            group["example_count"] += len(examples)

    return groups


def scan_image_directory(image_dir: Path) -> dict[str, Any]:
    image_map: dict[tuple[int, str], list[str]] = defaultdict(list)
    invalid_name_files: list[str] = []

    for image_path in list_image_files(image_dir):
        parsed = parse_image_filename(image_path)
        if parsed is None:
            invalid_name_files.append(image_path.name)
            continue

        word_id, word_key, _ = parsed
        image_map[(word_id, word_key)].append(image_path.name)

    duplicate_image_groups = [
        {
            "word_id": word_id,
            "word": word_key,
            "image_files": sorted(file_names, key=str.lower),
        }
        for (word_id, word_key), file_names in sorted(image_map.items(), key=lambda item: (item[0][0], item[0][1]))
        if len(file_names) > 1
    ]

    return {
        "image_map": {key: sorted(value, key=str.lower) for key, value in image_map.items()},
        "invalid_name_files": sorted(invalid_name_files, key=str.lower),
        "duplicate_image_groups": duplicate_image_groups,
    }


def build_default_word_image_prompt(word_text: str, meaning_text: str) -> str:
    normalized_word = normalize_text(word_text)
    normalized_meaning = normalize_text(meaning_text)
    if normalized_meaning:
        return (
            f"一张清晰展示 {normalized_word}（{normalized_meaning}）核心含义的英语词汇图片，"
            "主体突出，背景简洁，适合英语教学配图。"
        )
    return f"一张清晰展示 {normalized_word} 核心含义的英语词汇图片，主体突出，背景简洁，适合英语教学配图。"


def extract_invalid_word_characters(word_text: str) -> list[str]:
    normalized = normalize_text(word_text)
    invalid_chars = {
        character
        for character in normalized
        if character != " " and not ("A" <= character <= "Z" or "a" <= character <= "z")
    }
    return sorted(invalid_chars)


def has_special_characters_in_word(word_text: str) -> bool:
    normalized = normalize_text(word_text)
    return normalized != "" and WORD_TEXT_PATTERN.fullmatch(normalized) is None


def synchronize_word_image_prompts(
    payload: dict[str, Any],
    records: list[dict[str, Any]],
    image_dir: Path,
    source_json_path: Path,
) -> dict[str, Any]:
    word_groups = build_word_groups(records)
    image_scan = scan_image_directory(image_dir)
    image_map = image_scan["image_map"]

    prompt_added_words = 0
    prompt_cleared_words = 0
    changed_record_count = 0
    unchanged_words = 0
    words_with_images = 0
    words_without_images = 0
    report_entries: list[dict[str, Any]] = []
    special_character_words: list[dict[str, Any]] = []

    unmatched_image_files = [
        file_name
        for group_key, file_names in sorted(image_map.items(), key=lambda item: (item[0][0], item[0][1]))
        if group_key not in word_groups
        for file_name in file_names
    ]

    for group_key, group in sorted(word_groups.items(), key=lambda item: (item[1]["word_id"], item[1]["word"])):
        image_files = image_map.get(group_key, [])
        has_image_file = bool(image_files)
        if has_image_file:
            words_with_images += 1
        else:
            words_without_images += 1

        existing_prompts = []
        for record in group["records"]:
            word = record.get("word") if isinstance(record.get("word"), dict) else None
            if word is None:
                continue
            prompt_text = normalize_text(word.get("image"))
            if prompt_text:
                existing_prompts.append(prompt_text)

        prompt_before = existing_prompts[0] if existing_prompts else ""
        meaning_text = next(iter(sorted(group["meaning_zh_values"])), "")

        if has_image_file:
            if prompt_before == "":
                prompt_after = build_default_word_image_prompt(group["word"], meaning_text)
                action = "added_default_prompt"
                prompt_added_words += 1
            else:
                prompt_after = prompt_before
                action = "kept_existing_prompt"
                unchanged_words += 1
        else:
            prompt_after = ""
            if prompt_before:
                action = "cleared_prompt_without_image"
                prompt_cleared_words += 1
            else:
                action = "kept_empty_prompt"
                unchanged_words += 1

        for record in group["records"]:
            word = record.get("word") if isinstance(record.get("word"), dict) else None
            if word is None:
                continue
            previous_value = normalize_text(word.get("image"))
            if previous_value != prompt_after:
                changed_record_count += 1
            word["image"] = prompt_after

        special_characters = extract_invalid_word_characters(group["word"])
        if has_special_characters_in_word(group["word"]):
            special_character_words.append(
                {
                    "word_id": group["word_id"],
                    "word": group["word"],
                    "invalid_characters": special_characters,
                    "record_indices": group["record_indices"],
                    "meaning_ids": sorted(group["meaning_ids"]),
                    "meaning_zh_values": sorted(group["meaning_zh_values"]),
                    "image_files": image_files,
                }
            )

        report_entries.append(
            {
                "word_id": group["word_id"],
                "word": group["word"],
                "record_indices": group["record_indices"],
                "meaning_ids": sorted(group["meaning_ids"]),
                "meaning_zh_values": sorted(group["meaning_zh_values"]),
                "image_files": image_files,
                "has_image_file": has_image_file,
                "prompt_before": prompt_before,
                "prompt_after": prompt_after,
                "action": action,
                "example_count": group["example_count"],
                "special_characters": special_characters,
            }
        )

    all_words_output = [entry["word"] for entry in report_entries]

    report = {
        "metadata": {
            "source_json": str(source_json_path),
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "record_count": len(records),
            "unique_word_count": len(report_entries),
            "image_file_count": sum(len(file_names) for file_names in image_map.values()),
            "words_with_images": words_with_images,
            "words_without_images": words_without_images,
            "prompt_added_word_count": prompt_added_words,
            "prompt_cleared_word_count": prompt_cleared_words,
            "changed_record_count": changed_record_count,
            "invalid_image_file_count": len(image_scan["invalid_name_files"]),
            "unmatched_image_file_count": len(unmatched_image_files),
            "special_character_word_count": len(special_character_words),
            "source_record_range": get_metadata_record_range(payload),
        },
        "summary": {
            "invalid_image_files": image_scan["invalid_name_files"],
            "unmatched_image_files": unmatched_image_files,
            "duplicate_image_groups": image_scan["duplicate_image_groups"],
        },
        "special_character_words": special_character_words,
        "word_image_entries": report_entries,
    }

    return {
        "report": report,
        "all_words_output": all_words_output,
        "changed_record_count": changed_record_count,
        "prompt_added_word_count": prompt_added_words,
        "prompt_cleared_word_count": prompt_cleared_words,
        "words_with_images": words_with_images,
        "words_without_images": words_without_images,
        "special_character_word_count": len(special_character_words),
        "invalid_image_file_count": len(image_scan["invalid_name_files"]),
        "unmatched_image_file_count": len(unmatched_image_files),
        "unique_word_count": len(report_entries),
    }


def get_metadata_record_range(payload: dict[str, Any]) -> tuple[int, int] | None:
    metadata = payload.get("metadata") if isinstance(payload.get("metadata"), dict) else None
    if metadata is None:
        return None

    source_record_range = metadata.get("source_record_range")
    if not isinstance(source_record_range, list) or len(source_record_range) != 2:
        return None

    start, end = source_record_range
    if not isinstance(start, int) or not isinstance(end, int) or start > end:
        return None

    return start, end


def get_record_word_id(record: dict[str, Any]) -> int | None:
    word = record.get("word") if isinstance(record.get("word"), dict) else None
    if word is None:
        return None
    word_id = word.get("id")
    return word_id if isinstance(word_id, int) else None


def get_record_meaning_id(record: dict[str, Any]) -> int | None:
    word_meaning = record.get("word_meaning") if isinstance(record.get("word_meaning"), dict) else None
    if word_meaning is None:
        return None
    meaning_id = word_meaning.get("id")
    return meaning_id if isinstance(meaning_id, int) else None


def format_number_ranges(numbers: list[int], limit: int = 30) -> str:
    if not numbers:
        return "无"

    sorted_numbers = sorted(dict.fromkeys(numbers))
    if len(sorted_numbers) > limit:
        shown_numbers = sorted_numbers[:limit]
        suffix = f" ... 共 {len(sorted_numbers)} 个"
    else:
        shown_numbers = sorted_numbers
        suffix = ""

    ranges: list[str] = []
    start = shown_numbers[0]
    end = shown_numbers[0]
    for number in shown_numbers[1:]:
        if number == end + 1:
            end = number
            continue

        ranges.append(str(start) if start == end else f"{start}-{end}")
        start = number
        end = number

    ranges.append(str(start) if start == end else f"{start}-{end}")
    return ", ".join(ranges) + suffix


def build_json_validation_report(source_path: Path, payload: dict[str, Any], records: list[dict[str, Any]]) -> dict[str, Any]:
    issues: list[dict[str, Any]] = []
    record_id_positions: dict[int, list[int]] = defaultdict(list)
    record_ids: list[int] = []
    distinct_field_values = collect_distinct_field_values(records)
    duplicate_example_report = build_duplicate_example_report(source_path, records)

    def add_issue(record_index: int, field: str, message: str, record_id: int | None = None) -> None:
        issues.append(
            {
                "record_index": record_index,
                "record_id": record_id,
                "field": field,
                "message": message,
            }
        )

    for record_index, record in enumerate(records, start=1):
        if not isinstance(record, dict):
            add_issue(record_index, "record", "记录必须是对象")
            continue

        word = record.get("word")
        word_meaning = record.get("word_meaning")
        word_form = record.get("word_form")
        word_example = record.get("word_example")
        hidden = record.get("hidden")

        if not isinstance(word, dict):
            add_issue(record_index, "word", "word 必须是对象")
            word = {}
        if not isinstance(word_meaning, dict):
            add_issue(record_index, "word_meaning", "word_meaning 必须是对象")
            word_meaning = {}
        if not isinstance(word_form, list):
            add_issue(record_index, "word_form", "word_form 必须是数组")
            word_form = []
        if not isinstance(word_example, list):
            add_issue(record_index, "word_example", "word_example 必须是数组")
            word_example = []
        if not isinstance(hidden, bool):
            add_issue(record_index, "hidden", "hidden 必须是布尔值")

        word_id = word.get("id")
        word_text = word.get("word")
        meaning_id = word_meaning.get("id")
        meaning_word_id = word_meaning.get("word_id")

        if not isinstance(word_id, int):
            add_issue(record_index, "word.id", "word.id 必须是整数", meaning_id if isinstance(meaning_id, int) else None)
        if is_empty_text(word_text):
            add_issue(record_index, "word.word", "word.word 不能为空", meaning_id if isinstance(meaning_id, int) else None)

        if not isinstance(meaning_id, int):
            add_issue(record_index, "word_meaning.id", "word_meaning.id 必须是整数")
        else:
            record_ids.append(meaning_id)
            record_id_positions[meaning_id].append(record_index)

        if not isinstance(meaning_word_id, int):
            add_issue(record_index, "word_meaning.word_id", "word_meaning.word_id 必须是整数", meaning_id if isinstance(meaning_id, int) else None)
        elif isinstance(word_id, int) and meaning_word_id != word_id:
            add_issue(
                record_index,
                "word_meaning.word_id",
                f"word_meaning.word_id={meaning_word_id} 与 word.id={word_id} 不一致",
                meaning_id if isinstance(meaning_id, int) else None,
            )

        if isinstance(word_example, list):
            for example_index, example in enumerate(word_example, start=1):
                if not isinstance(example, dict):
                    add_issue(record_index, f"word_example[{example_index}]", "例句记录必须是对象", meaning_id if isinstance(meaning_id, int) else None)
                    continue

                example_meaning_id = example.get("meaning_id")
                if example_meaning_id is not None and not isinstance(example_meaning_id, int):
                    add_issue(
                        record_index,
                        f"word_example[{example_index}].meaning_id",
                        "meaning_id 必须是整数或 null",
                        meaning_id if isinstance(meaning_id, int) else None,
                    )
                elif isinstance(meaning_id, int) and isinstance(example_meaning_id, int) and example_meaning_id != meaning_id:
                    add_issue(
                        record_index,
                        f"word_example[{example_index}].meaning_id",
                        f"例句 meaning_id={example_meaning_id} 与 word_meaning.id={meaning_id} 不一致",
                        meaning_id,
                    )

        if isinstance(word_form, list):
            for form_index, form in enumerate(word_form, start=1):
                if not isinstance(form, dict):
                    add_issue(record_index, f"word_form[{form_index}]", "词形记录必须是对象", meaning_id if isinstance(meaning_id, int) else None)

    duplicate_record_ids = sorted(record_id for record_id, positions in record_id_positions.items() if len(positions) > 1)

    duplicate_occurrences = duplicate_example_report["duplicate_groups"]
    for duplicate_group in duplicate_occurrences:
        occurrences = duplicate_group.get("occurrences", [])
        if not isinstance(occurrences, list) or len(occurrences) <= 1:
            continue

        related_record_ids = sorted(
            {
                occurrence.get("word_meaning_id")
                for occurrence in occurrences
                if isinstance(occurrence, dict) and isinstance(occurrence.get("word_meaning_id"), int)
            }
        )
        related_words = sorted(
            {
                normalize_text(occurrence.get("word"))
                for occurrence in occurrences
                if isinstance(occurrence, dict) and normalize_text(occurrence.get("word"))
            }
        )
        duplicate_message = (
            f"例句重复，共 {len(occurrences)} 次；涉及词义ID {format_number_ranges(related_record_ids)}；"
            f"单词 {', '.join(related_words)}；内容：{build_example_preview(duplicate_group.get('example_en', ''), duplicate_group.get('example_zh', ''))}"
        )

        for occurrence in occurrences:
            if not isinstance(occurrence, dict):
                continue
            add_issue(
                occurrence.get("record_index") if isinstance(occurrence.get("record_index"), int) else 0,
                f"word_example[{occurrence.get('example_index')}]",
                duplicate_message,
                occurrence.get("word_meaning_id") if isinstance(occurrence.get("word_meaning_id"), int) else None,
            )

    issues.sort(key=lambda item: (item.get("record_index") or 0, str(item.get("field") or ""), str(item.get("message") or "")))

    metadata = payload.get("metadata") if isinstance(payload.get("metadata"), dict) else {}
    source_range = get_metadata_record_range(payload)
    missing_record_ids: list[int] = []
    out_of_range_record_ids: list[int] = []
    record_id_gaps: list[int] = []

    unique_record_ids = sorted(record_id_positions.keys())
    if source_range is not None:
        expected_start, expected_end = source_range
        expected_ids = set(range(expected_start, expected_end + 1))
        current_ids = set(unique_record_ids)
        missing_record_ids = sorted(expected_ids - current_ids)
        out_of_range_record_ids = sorted(
            record_id for record_id in unique_record_ids if record_id < expected_start or record_id > expected_end
        )
    elif len(unique_record_ids) >= 2:
        for previous, current in zip(unique_record_ids, unique_record_ids[1:]):
            if current - previous > 1:
                record_id_gaps.extend(range(previous + 1, current))

    batch_record_count = metadata.get("batch_record_count")
    record_count_matches_metadata = batch_record_count == len(records) if isinstance(batch_record_count, int) else None

    return {
        "metadata": {
            "source_json": str(source_path),
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "record_count": len(records),
            "unique_record_id_count": len(unique_record_ids),
            "issue_count": len(issues),
            "duplicate_record_id_count": len(duplicate_record_ids),
            "duplicate_example_group_count": duplicate_example_report["metadata"]["duplicate_group_count"],
            "duplicate_example_count": duplicate_example_report["metadata"]["duplicate_example_count"],
            "duplicate_example_occurrence_count": duplicate_example_report["metadata"]["duplicate_occurrence_count"],
            "duplicate_example_record_id_count": duplicate_example_report["metadata"]["duplicate_record_id_count"],
            "missing_record_id_count": len(missing_record_ids),
            "out_of_range_record_id_count": len(out_of_range_record_ids),
            "record_id_gap_count": len(record_id_gaps),
            "batch_record_count": batch_record_count,
            "record_count_matches_batch_record_count": record_count_matches_metadata,
            "source_record_range": list(source_range) if source_range is not None else None,
        },
        "summary": {
            "duplicate_record_ids": duplicate_record_ids,
            "missing_record_ids": missing_record_ids,
            "out_of_range_record_ids": out_of_range_record_ids,
            "record_id_gaps": record_id_gaps,
            "duplicate_record_id_ranges": format_number_ranges(duplicate_record_ids),
            "missing_record_id_ranges": format_number_ranges(missing_record_ids),
            "out_of_range_record_id_ranges": format_number_ranges(out_of_range_record_ids),
            "record_id_gap_ranges": format_number_ranges(record_id_gaps),
            "distinct_field_values": distinct_field_values,
            "duplicate_word_example_ids": duplicate_example_report["duplicate_word_example_ids"],
            "duplicate_example_record_ids": duplicate_example_report["duplicate_record_ids"],
        },
        "duplicate_examples": duplicate_example_report,
        "issues": issues,
    }


def build_validation_summary_text(report: dict[str, Any]) -> str:
    metadata = report["metadata"]
    summary = report["summary"]
    distinct_field_values = summary.get("distinct_field_values", {})
    parts = [
        f"记录 {metadata['record_count']} 条",
        f"问题 {metadata['issue_count']} 条",
        f"重复记录ID {metadata['duplicate_record_id_count']} 个",
        f"重复例句 {metadata['duplicate_example_group_count']} 组",
        f"缺失记录ID {metadata['missing_record_id_count']} 个",
    ]
    if metadata["out_of_range_record_id_count"]:
        parts.append(f"越界记录ID {metadata['out_of_range_record_id_count']} 个")
    if metadata["record_id_gap_count"] and metadata["source_record_range"] is None:
        parts.append(f"内部断号 {metadata['record_id_gap_count']} 个")
    if metadata["record_count_matches_batch_record_count"] is False:
        parts.append("batch_record_count 与实际记录数不一致")

    detail_parts: list[str] = []
    if metadata["duplicate_record_id_count"]:
        detail_parts.append(f"重复ID: {summary['duplicate_record_id_ranges']}")
    if metadata["duplicate_example_group_count"]:
        detail_parts.append(f"重复例句涉及词义ID {metadata['duplicate_example_record_id_count']} 个")
    if metadata["missing_record_id_count"]:
        detail_parts.append(f"缺失ID: {summary['missing_record_id_ranges']}")
    if metadata["out_of_range_record_id_count"]:
        detail_parts.append(f"越界ID: {summary['out_of_range_record_id_ranges']}")
    if metadata["record_id_gap_count"] and metadata["source_record_range"] is None:
        detail_parts.append(f"断号: {summary['record_id_gap_ranges']}")

    for field_name in ["word.word_type", "word_meaning.word_tag", "word_meaning.pos"]:
        field_summary = distinct_field_values.get(field_name)
        if isinstance(field_summary, dict):
            detail_parts.append(f"{field_name} 类型 {field_summary.get('value_count', 0)} 个")

    summary_text = " | ".join(parts)
    if detail_parts:
        summary_text = f"{summary_text} | " + " | ".join(detail_parts)
    return summary_text


def merge_json_files(primary_path: Path, secondary_path: Path, output_path: Path) -> dict[str, Any]:
    primary_payload, primary_records = load_json_records(primary_path)
    secondary_payload, secondary_records = load_json_records(secondary_path)

    primary_report = build_json_validation_report(primary_path, primary_payload, primary_records)
    secondary_report = build_json_validation_report(secondary_path, secondary_payload, secondary_records)

    merged_records = sorted(
        [*primary_records, *secondary_records],
        key=lambda record: (get_record_meaning_id(record) is None, get_record_meaning_id(record) or sys.maxsize),
    )
    merged_report = build_json_validation_report(output_path, {"metadata": {}}, merged_records)
    duplicate_record_ids = merged_report["summary"]["duplicate_record_ids"]
    if duplicate_record_ids:
        raise ValueError(f"合并失败，记录 ID 存在重复: {format_number_ranges(duplicate_record_ids)}")

    primary_metadata = primary_payload.get("metadata") if isinstance(primary_payload.get("metadata"), dict) else {}
    secondary_metadata = secondary_payload.get("metadata") if isinstance(secondary_payload.get("metadata"), dict) else {}

    merged_payload: dict[str, Any] = {
        key: value for key, value in primary_payload.items() if key not in {"metadata", "records"}
    }

    range_candidates = [
        record_range
        for record_range in [get_metadata_record_range(primary_payload), get_metadata_record_range(secondary_payload)]
        if record_range is not None
    ]
    merged_range = [min(item[0] for item in range_candidates), max(item[1] for item in range_candidates)] if range_candidates else None

    merged_model_values = {
        normalize_text(metadata.get("model"))
        for metadata in [primary_metadata, secondary_metadata]
        if normalize_text(metadata.get("model"))
    }
    merged_payload["metadata"] = {
        "source_file": primary_metadata.get("source_file") or secondary_metadata.get("source_file") or str(primary_path),
        "generated_file": str(output_path),
        "generated_at": datetime.now().date().isoformat(),
        "model": next(iter(merged_model_values)) if len(merged_model_values) == 1 else "mixed",
        "batch_record_count": len(merged_records),
        "source_record_range": merged_range,
        "merged_from": [str(primary_path), str(secondary_path)],
        "merged_file_count": 2,
        "note": "Merged by check_json.py",
    }
    merged_payload["records"] = merged_records

    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_json(output_path, merged_payload)

    return {
        "output_path": output_path,
        "record_count": len(merged_records),
        "primary_report": primary_report,
        "secondary_report": secondary_report,
        "merged_report": build_json_validation_report(output_path, merged_payload, merged_records),
    }


def build_duplicate_example_report(source_path: Path, records: list[dict[str, Any]]) -> dict[str, Any]:
    grouped_examples: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)

    for record_index, record in enumerate(records, start=1):
        word = record.get("word") if isinstance(record.get("word"), dict) else {}
        word_id = word.get("id")
        word_text = normalize_text(word.get("word"))
        if word_id is None or word_text == "":
            continue

        word_meaning = record.get("word_meaning") if isinstance(record.get("word_meaning"), dict) else {}

        examples = record.get("word_example")
        if not isinstance(examples, list):
            continue

        for example_index, example in enumerate(examples, start=1):
            if not isinstance(example, dict):
                continue

            example_en = normalize_text(example.get("example_en"))
            example_zh = normalize_text(example.get("example_zh"))
            if example_en == "" and example_zh == "":
                continue

            example_key = (example_en, example_zh)
            grouped_examples[example_key].append(
                {
                    "record_index": record_index,
                    "example_index": example_index,
                    "example_id": example.get("id"),
                    "meaning_id": example.get("meaning_id"),
                    "hidden": bool(record.get("hidden")),
                    "word_id": word_id,
                    "word": word_text,
                    "word_meaning_id": word_meaning.get("id"),
                    "meaning_zh": normalize_text(word_meaning.get("meaning_zh")),
                    "source": normalize_text(word_meaning.get("source")),
                }
            )

    duplicate_groups: list[dict[str, Any]] = []
    duplicate_example_count = 0
    duplicate_occurrence_count = 0
    duplicate_word_example_ids: set[int] = set()
    duplicate_record_ids: set[int] = set()

    report_rows: list[dict[str, Any]] = []

    for (example_en, example_zh), occurrences in sorted(grouped_examples.items(), key=lambda item: (item[0][0], item[0][1])):
        if len(occurrences) <= 1:
            continue

        duplicate_example_count += 1
        duplicate_occurrence_count += len(occurrences)

        for occurrence in occurrences:
            example_id = occurrence.get("example_id")
            if isinstance(example_id, int):
                duplicate_word_example_ids.add(example_id)

            word_meaning_id = occurrence.get("word_meaning_id")
            if isinstance(word_meaning_id, int):
                duplicate_record_ids.add(word_meaning_id)

        duplicate_word_example_ids = sorted(
            {
                occurrence.get("example_id")
                for occurrence in occurrences
                if isinstance(occurrence.get("example_id"), int)
            }
        )
        sorted_occurrences = sorted(
            occurrences,
            key=lambda item: (
                item.get("record_index") if isinstance(item.get("record_index"), int) else sys.maxsize,
                item.get("example_index") if isinstance(item.get("example_index"), int) else sys.maxsize,
            ),
        )
        sequence = len(duplicate_groups) + 1

        duplicate_groups.append(
            {
                "sequence": sequence,
                "duplicate_word_example_ids": duplicate_word_example_ids,
                "duplicate_example_content": {
                    "example_en": example_en,
                    "example_zh": example_zh,
                    "preview": build_example_preview(example_en, example_zh),
                },
                "example_en": example_en,
                "example_zh": example_zh,
                "preview": build_example_preview(example_en, example_zh),
                "occurrence_count": len(occurrences),
                "words": sorted(
                    {
                        normalize_text(occurrence.get("word"))
                        for occurrence in occurrences
                        if normalize_text(occurrence.get("word"))
                    }
                ),
                "word_ids": sorted(
                    {
                        occurrence.get("word_id")
                        for occurrence in occurrences
                        if isinstance(occurrence.get("word_id"), int)
                    }
                ),
                "word_meaning_ids": sorted(
                    {
                        occurrence.get("word_meaning_id")
                        for occurrence in occurrences
                        if isinstance(occurrence.get("word_meaning_id"), int)
                    }
                ),
                "occurrences": sorted_occurrences,
            }
        )
        report_rows.append(
            {
                "sequence": sequence,
                "duplicate_word_example_ids": duplicate_word_example_ids,
                "duplicate_example_content": {
                    "example_en": example_en,
                    "example_zh": example_zh,
                    "preview": build_example_preview(example_en, example_zh),
                },
            }
        )

    return {
        "metadata": {
            "source_json": str(source_path),
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "duplicate_group_count": len(duplicate_groups),
            "duplicate_example_count": duplicate_example_count,
            "duplicate_occurrence_count": duplicate_occurrence_count,
            "duplicate_word_example_id_count": len(duplicate_word_example_ids),
            "duplicate_record_id_count": len(duplicate_record_ids),
            "record_count": len(records),
        },
        "duplicate_word_example_ids": sorted(duplicate_word_example_ids),
        "duplicate_record_ids": sorted(duplicate_record_ids),
        "report_rows": report_rows,
        "duplicate_groups": duplicate_groups,
    }


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(APP_TITLE)
        self.resize(980, 720)

        central = QWidget(self)
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)

        file_group = QGroupBox("文件设置")
        file_layout = QGridLayout(file_group)
        root_layout.addWidget(file_group)

        self.json_path_input = QLineEdit(str(DEFAULT_JSON_PATH))
        self.secondary_json_path_input = QLineEdit(str(DEFAULT_SECONDARY_JSON_PATH))
        self.merge_output_path_input = QLineEdit(str(build_default_merge_output_path(DEFAULT_JSON_PATH)))
        self.report_path_input = QLineEdit(str(build_default_report_path(DEFAULT_JSON_PATH)))
        self.image_dir_input = QLineEdit(str(DEFAULT_WORD_IMAGE_DIR))
        self.summary_label = QLabel()
        self.summary_label.setWordWrap(True)

        json_browse_button = QPushButton("选择 JSON")
        secondary_json_browse_button = QPushButton("第二个 JSON")
        merge_output_browse_button = QPushButton("合并输出")
        report_browse_button = QPushButton("报告路径")
        image_dir_browse_button = QPushButton("图片目录")
        refresh_button = QPushButton("刷新统计")

        file_layout.addWidget(QLabel("主 JSON 文件"), 0, 0)
        file_layout.addWidget(self.json_path_input, 0, 1)
        file_layout.addWidget(json_browse_button, 0, 2)
        file_layout.addWidget(QLabel("第二个 JSON 文件"), 1, 0)
        file_layout.addWidget(self.secondary_json_path_input, 1, 1)
        file_layout.addWidget(secondary_json_browse_button, 1, 2)
        file_layout.addWidget(QLabel("合并输出文件"), 2, 0)
        file_layout.addWidget(self.merge_output_path_input, 2, 1)
        file_layout.addWidget(merge_output_browse_button, 2, 2)
        file_layout.addWidget(QLabel("重复报告文件"), 3, 0)
        file_layout.addWidget(self.report_path_input, 3, 1)
        file_layout.addWidget(report_browse_button, 3, 2)
        file_layout.addWidget(QLabel("单词图片目录"), 4, 0)
        file_layout.addWidget(self.image_dir_input, 4, 1)
        file_layout.addWidget(image_dir_browse_button, 4, 2)
        file_layout.addWidget(refresh_button, 5, 2)
        file_layout.addWidget(self.summary_label, 6, 0, 1, 3)

        action_group = QGroupBox("批量操作")
        action_layout = QGridLayout(action_group)
        root_layout.addWidget(action_group)

        self.reset_hidden_button = QPushButton("全部 hidden=false")
        self.hide_empty_image_button = QPushButton("空 image -> hidden=true")
        self.show_non_empty_image_button = QPushButton("非空 image -> hidden=false")
        self.validate_records_button = QPushButton("校验记录和缺号")
        self.scan_duplicates_button = QPushButton("扫描重复例句并生成报告")
        self.write_back_word_tag_button = QPushButton("写回统一 word_tag")
        self.write_back_pos_button = QPushButton("写回统一 pos")
        self.merge_json_button = QPushButton("合并两个 JSON")
        self.hide_by_image_files_button = QPushButton("匹配图片记录 -> hidden=true")
        self.move_unmatched_images_button = QPushButton("移动未匹配图片")
        self.sync_word_images_button = QPushButton("同步图片提示词并导出单词")

        action_buttons = [
            self.reset_hidden_button,
            self.hide_empty_image_button,
            self.show_non_empty_image_button,
            self.validate_records_button,
            self.scan_duplicates_button,
            self.write_back_word_tag_button,
            self.write_back_pos_button,
            self.merge_json_button,
            self.hide_by_image_files_button,
            self.move_unmatched_images_button,
            self.sync_word_images_button,
        ]
        for index, button in enumerate(action_buttons):
            action_layout.addWidget(button, index // 4, index % 4)

        self.log_output = QPlainTextEdit()
        self.log_output.setReadOnly(True)
        self.log_output.setPlaceholderText("运行日志会显示在这里。")
        root_layout.addWidget(self.log_output, stretch=1)

        status_bar = QStatusBar(self)
        self.setStatusBar(status_bar)

        json_browse_button.clicked.connect(self.choose_json_file)
        secondary_json_browse_button.clicked.connect(self.choose_secondary_json_file)
        merge_output_browse_button.clicked.connect(self.choose_merge_output_path)
        report_browse_button.clicked.connect(self.choose_report_path)
        image_dir_browse_button.clicked.connect(self.choose_image_dir)
        refresh_button.clicked.connect(self.refresh_summary)
        self.reset_hidden_button.clicked.connect(self.handle_reset_hidden)
        self.hide_empty_image_button.clicked.connect(self.handle_hide_empty_image)
        self.show_non_empty_image_button.clicked.connect(self.handle_show_non_empty_image)
        self.validate_records_button.clicked.connect(self.handle_validate_records)
        self.scan_duplicates_button.clicked.connect(self.handle_scan_duplicates)
        self.write_back_word_tag_button.clicked.connect(self.handle_write_back_word_tags)
        self.write_back_pos_button.clicked.connect(self.handle_write_back_pos)
        self.merge_json_button.clicked.connect(self.handle_merge_json_files)
        self.hide_by_image_files_button.clicked.connect(self.handle_hide_by_image_files)
        self.move_unmatched_images_button.clicked.connect(self.handle_move_unmatched_images)
        self.sync_word_images_button.clicked.connect(self.handle_sync_word_images)

        self.refresh_summary(initial=True)

    def append_log(self, message: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_output.appendPlainText(f"[{timestamp}] {message}")
        self.statusBar().showMessage(message, 5000)

    def get_json_path(self) -> Path:
        path = Path(self.json_path_input.text().strip())
        if not path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {path}")
        if not path.is_file():
            raise FileNotFoundError(f"目标不是文件: {path}")
        return path

    def get_report_path(self) -> Path:
        raw_path = self.report_path_input.text().strip()
        if raw_path == "":
            json_path = self.get_json_path()
            report_path = build_default_report_path(json_path)
            self.report_path_input.setText(str(report_path))
            return report_path
        return Path(raw_path)

    def get_secondary_json_path(self) -> Path:
        raw_path = self.secondary_json_path_input.text().strip()
        if raw_path == "":
            raise FileNotFoundError("第二个 JSON 文件不能为空")

        path = Path(raw_path)
        if not path.exists():
            raise FileNotFoundError(f"第二个 JSON 文件不存在: {path}")
        if not path.is_file():
            raise FileNotFoundError(f"第二个 JSON 目标不是文件: {path}")
        return path

    def get_merge_output_path(self) -> Path:
        raw_path = self.merge_output_path_input.text().strip()
        if raw_path == "":
            output_path = build_default_merge_output_path(self.get_json_path())
            self.merge_output_path_input.setText(str(output_path))
            return output_path
        return Path(raw_path)

    def get_image_dir(self) -> Path:
        path = Path(self.image_dir_input.text().strip())
        if not path.exists():
            raise FileNotFoundError(f"图片目录不存在: {path}")
        if not path.is_dir():
            raise NotADirectoryError(f"目标不是目录: {path}")
        return path

    def choose_json_file(self) -> None:
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 JSON 文件",
            str(self.get_json_path().parent if Path(self.json_path_input.text().strip()).exists() else ROOT_DIR),
            "JSON Files (*.json)",
        )
        if not file_path:
            return
        json_path = Path(file_path)
        self.json_path_input.setText(str(json_path))
        self.report_path_input.setText(str(build_default_report_path(json_path)))
        self.merge_output_path_input.setText(str(build_default_merge_output_path(json_path)))
        self.append_log(f"已选择 JSON: {json_path}")
        self.refresh_summary()

    def choose_secondary_json_file(self) -> None:
        raw_path = self.secondary_json_path_input.text().strip()
        initial_dir = Path(raw_path).parent if raw_path and Path(raw_path).exists() else ROOT_DIR
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择第二个 JSON 文件",
            str(initial_dir),
            "JSON Files (*.json)",
        )
        if not file_path:
            return

        self.secondary_json_path_input.setText(file_path)
        self.append_log(f"第二个 JSON 已更新: {file_path}")

    def choose_merge_output_path(self) -> None:
        suggested_path = self.get_merge_output_path()
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "选择合并输出路径",
            str(suggested_path),
            "JSON Files (*.json)",
        )
        if not file_path:
            return

        self.merge_output_path_input.setText(file_path)
        self.append_log(f"合并输出路径已更新: {file_path}")

    def choose_report_path(self) -> None:
        suggested_path = self.get_report_path()
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "选择报告输出路径",
            str(suggested_path),
            "JSON Files (*.json)",
        )
        if not file_path:
            return
        self.report_path_input.setText(file_path)
        self.append_log(f"报告输出路径已更新: {file_path}")

    def choose_image_dir(self) -> None:
        current_dir = self.image_dir_input.text().strip()
        selected_dir = QFileDialog.getExistingDirectory(
            self,
            "选择单词图片目录",
            current_dir if Path(current_dir).exists() else str(ROOT_DIR),
        )
        if not selected_dir:
            return
        self.image_dir_input.setText(selected_dir)
        self.append_log(f"图片目录已更新: {selected_dir}")

    def refresh_summary(self, initial: bool = False) -> None:
        try:
            json_path = self.get_json_path()
            _, records = load_json_records(json_path)
            stats = collect_stats(records)
            summary_text = (
                f"记录数: {stats['record_count']} | hidden=true: {stats['hidden_true']} | "
                f"hidden=false: {stats['hidden_false']} | 空 image: {stats['empty_image']} | "
                f"非空 image: {stats['non_empty_image']} | 例句数: {stats['example_count']}"
            )
            self.summary_label.setText(summary_text)
            if not initial:
                self.append_log(f"统计已刷新: {summary_text}")
        except Exception as exc:
            self.summary_label.setText(f"读取失败: {exc}")
            if not initial:
                self.show_error("刷新统计失败", exc)

    def run_mutation(self, action_name: str, mutate) -> None:
        try:
            json_path = self.get_json_path()
            payload, records = load_json_records(json_path)
            backup_path = ensure_backup(json_path)
            result = mutate(records)
            write_json(json_path, payload)
            detail_text = format_result_details(result)
            message = (
                f"{action_name}完成: 命中 {result['matched']} 条，实际修改 {result['changed']} 条；备份文件: {backup_path}"
            )
            if detail_text:
                message = f"{message}；{detail_text}"
            self.append_log(message)
            self.refresh_summary()
        except Exception as exc:
            self.show_error(f"{action_name}失败", exc)

    def handle_reset_hidden(self) -> None:
        self.run_mutation("全部 hidden=false", set_all_hidden_false)

    def handle_hide_empty_image(self) -> None:
        self.run_mutation("空 image -> hidden=true", set_hidden_true_for_empty_word_image)

    def handle_show_non_empty_image(self) -> None:
        self.run_mutation("非空 image -> hidden=false", set_hidden_false_for_non_empty_word_image)

    def handle_write_back_word_tags(self) -> None:
        self.run_mutation("写回统一 word_tag", write_back_simplified_word_tags)

    def handle_write_back_pos(self) -> None:
        self.run_mutation("写回统一 pos", write_back_simplified_pos)

    def handle_hide_by_image_files(self) -> None:
        image_dir = self.get_image_dir()
        self.run_mutation(
            "匹配图片记录 -> hidden=true",
            lambda records: set_hidden_true_for_records_matched_by_images(records, image_dir),
        )

    def handle_move_unmatched_images(self) -> None:
        try:
            image_dir = self.get_image_dir()
            _, records = load_json_records(self.get_json_path())
            result = move_unmatched_images(image_dir, records)
            detail_text = format_result_details(result)
            message = f"移动未匹配图片完成: 已移动 {result['moved']} 个文件"
            if detail_text:
                message = f"{message}；{detail_text}"
            self.append_log(message)
        except Exception as exc:
            self.show_error("移动未匹配图片失败", exc)

    def handle_sync_word_images(self) -> None:
        try:
            json_path = self.get_json_path()
            image_dir = self.get_image_dir()
            payload, records = load_json_records(json_path)
            backup_path = ensure_backup(json_path)

            result = synchronize_word_image_prompts(payload, records, image_dir, json_path)
            report_path = build_word_image_report_path(json_path)
            all_words_output_path = build_all_words_output_path(json_path)

            write_json(json_path, payload)
            write_json(report_path, result["report"])
            write_json(all_words_output_path, result["all_words_output"])

            self.append_log(
                "单词图片提示词同步完成: "
                f"唯一单词 {result['unique_word_count']} 个，"
                f"补充提示词 {result['prompt_added_word_count']} 个，"
                f"清空无图提示词 {result['prompt_cleared_word_count']} 个，"
                f"特殊字符单词 {result['special_character_word_count']} 个，"
                f"未匹配图片 {result['unmatched_image_file_count']} 个，"
                f"命名异常图片 {result['invalid_image_file_count']} 个；"
                f"备份文件: {backup_path}；报告: {report_path}；全部单词: {all_words_output_path}"
            )
            self.refresh_summary()
            QMessageBox.information(
                self,
                APP_TITLE,
                "同步完成。\n\n"
                f"报告文件:\n{report_path}\n\n"
                f"全部单词文件:\n{all_words_output_path}",
            )
        except Exception as exc:
            self.show_error("同步图片提示词失败", exc)

    def handle_scan_duplicates(self) -> None:
        try:
            json_path = self.get_json_path()
            report_path = self.get_report_path()
            payload, records = load_json_records(json_path)
            report = build_duplicate_example_report(json_path, records)
            report_path.parent.mkdir(parents=True, exist_ok=True)
            write_json(report_path, report)

            metadata = report["metadata"]
            self.append_log(
                "重复例句扫描完成: "
                f"重复分组 {metadata['duplicate_group_count']} 个，"
                f"重复例句 {metadata['duplicate_example_count']} 条，"
                f"重复出现次数 {metadata['duplicate_occurrence_count']} 次，"
                f"重复 word_example_id {metadata['duplicate_word_example_id_count']} 个；"
                f"报告已输出到 {report_path}"
            )

            if metadata["duplicate_group_count"] == 0:
                QMessageBox.information(self, APP_TITLE, "未发现重复例句，已生成空报告。")
            else:
                QMessageBox.information(self, APP_TITLE, f"扫描完成，报告已生成:\n{report_path}")

            if isinstance(payload.get("metadata"), dict):
                source_name = payload["metadata"].get("source_file") or json_path.name
                self.append_log(f"本次扫描源信息: {source_name}")
        except Exception as exc:
            self.show_error("扫描重复例句失败", exc)

    def handle_validate_records(self) -> None:
        try:
            json_path = self.get_json_path()
            payload, records = load_json_records(json_path)
            report = build_json_validation_report(json_path, payload, records)
            report_path = build_validation_report_path(json_path)
            write_json(report_path, report)

            summary_text = build_validation_summary_text(report)
            self.append_log(f"记录校验完成: {summary_text}；报告已输出到 {report_path}")

            metadata = report["metadata"]
            if metadata["issue_count"] == 0 and metadata["missing_record_id_count"] == 0 and metadata["duplicate_record_id_count"] == 0:
                QMessageBox.information(self, APP_TITLE, f"校验通过，未发现结构错误和缺号。\n报告已生成:\n{report_path}")
            else:
                QMessageBox.warning(self, APP_TITLE, f"校验完成，发现问题。\n{summary_text}\n\n报告已生成:\n{report_path}")
        except Exception as exc:
            self.show_error("校验记录失败", exc)

    def handle_merge_json_files(self) -> None:
        try:
            primary_path = self.get_json_path()
            secondary_path = self.get_secondary_json_path()
            output_path = self.get_merge_output_path()

            if primary_path == secondary_path:
                raise ValueError("两个 JSON 文件不能是同一个文件")

            result = merge_json_files(primary_path, secondary_path, output_path)
            merged_summary_text = build_validation_summary_text(result["merged_report"])
            self.append_log(
                f"JSON 合并完成: 共 {result['record_count']} 条记录；{merged_summary_text}；输出文件: {output_path}"
            )
            self.json_path_input.setText(str(output_path))
            self.report_path_input.setText(str(build_default_report_path(output_path)))
            self.merge_output_path_input.setText(str(output_path))
            self.refresh_summary()
            QMessageBox.information(self, APP_TITLE, f"合并完成，输出文件:\n{output_path}")
        except Exception as exc:
            self.show_error("合并 JSON 失败", exc)

    def show_error(self, title: str, error: Exception) -> None:
        self.append_log(f"{title}: {error}")
        QMessageBox.critical(self, title, str(error))


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())