from __future__ import annotations

import importlib
import json
import os
import re
import sqlite3
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
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


_ensure_dependency("PySide6.QtCore", "PySide6")

from PySide6.QtCore import QObject, QThread, Qt, Signal, Slot
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


DEFAULT_DB_PATH = Path(__file__).resolve().parent / "words.db"
DEFAULT_API_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
DEFAULT_API_KEY = os.getenv(
    "ZHIPUAI_API_KEY",
    "22008338eea846c99387608f53ad2a0e.Hc4SU0LCpnuFk7GN",
)
DEFAULT_MODEL = "glm-4.5-flash"
DEFAULT_IMAGE = ""
DEFAULT_EXAMPLE_TAG = "ai_generated"
DEFAULT_AUDIO_PATH_TEMPLATE = ""
DEFAULT_FAILURE_LOG_PATH = Path(__file__).resolve().parent / "zhipu_example_generator_failures.log"
DEFAULT_TIMEOUT_SECONDS = 60
DEFAULT_TARGET_EXAMPLES = 2
DEFAULT_MAX_MEANINGS = 0
DEFAULT_REQUEST_BATCH_SIZE = 10
DEFAULT_REQUEST_PACING_MODE = "response-driven"
DEFAULT_REQUEST_INTERVAL_SECONDS = 1.0
DEFAULT_RATE_LIMIT_RETRY_COUNT = 5
DEFAULT_RATE_LIMIT_BACKOFF_SECONDS = 5
DEFAULT_DIFFICULTY = 2
DEFAULT_TEMPERATURE = 0.8

DEFAULT_PROMPT_TEMPLATE = """请为下面 {task_count} 个单词释义分别生成英语例句，并返回严格 JSON。

任务列表 JSON:
{tasks_json}

要求:
1. 每个任务都必须根据其 word、meaning_zh、meaning_en、pos、stage 生成准确例句，不要混淆不同任务。
2. 每个任务生成的例句数量必须等于该任务的 required_count。
3. 例句自然、地道、简洁，适合英语学习者，尽量控制在 6 到 18 个英文单词。
4. 每条例句都要给出对应中文翻译。
5. 不要重复，不要编号，不要解释，不要 Markdown。
6. 严格输出如下 JSON 结构:
{{
    "results": [
        {{
            "meaning_id": 123,
            "examples": [
                {{"example_en": "...", "example_zh": "..."}}
            ]
        }}
    ]
}}
"""

CODE_FENCE_PATTERN = re.compile(r"^```(?:json)?\s*|\s*```$", re.IGNORECASE)
WHITESPACE_PATTERN = re.compile(r"\s+")
SAFE_TOKEN_PATTERN = re.compile(r"[^a-zA-Z0-9_\-.]+")


@dataclass(slots=True)
class MeaningTask:
    meaning_id: int
    word_id: int
    word: str
    stage: int | None
    pos: str
    meaning_en: str
    meaning_zh: str
    image: str
    word_tag: str
    existing_examples: int


@dataclass(slots=True)
class GeneratedExample:
    example_en: str
    example_zh: str


@dataclass(slots=True)
class BatchApiResult:
    prompt_text: str
    response_body: str
    message_content: str
    examples_by_meaning_id: dict[int, list[GeneratedExample]]


class RateLimitError(RuntimeError):
    pass


@dataclass(slots=True)
class GeneratorConfig:
    db_path: Path
    failure_log_path: Path
    api_key: str
    api_url: str
    model: str
    temperature: float
    timeout_seconds: int
    target_examples: int
    max_meanings: int
    request_batch_size: int
    request_pacing_mode: str
    request_interval_seconds: float
    rate_limit_retry_count: int
    rate_limit_backoff_seconds: int
    difficulty: int
    image: str
    example_tag: str
    audio_path_template: str
    stage_filter: str
    word_keyword: str
    fill_to_target: bool
    skip_existing_exact: bool
    dry_run: bool
    prompt_template: str


def normalize_text(value: object) -> str:
    if value is None:
        return ""
    return WHITESPACE_PATTERN.sub(" ", str(value).strip())


def safe_token(value: object, fallback: str) -> str:
    normalized = normalize_text(value)
    if not normalized:
        return fallback
    cleaned = SAFE_TOKEN_PATTERN.sub("_", normalized)
    cleaned = cleaned.strip("._")
    return cleaned or fallback


def parse_stage_filter(text: str) -> list[int]:
    stripped = text.strip()
    if not stripped:
        return []
    values: list[int] = []
    for item in stripped.split(","):
        token = item.strip()
        if not token:
            continue
        try:
            values.append(int(token))
        except ValueError as exc:
            raise ValueError(f"阶段筛选无效: {token}") from exc
    return values


def extract_message_text(content: object) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts: list[str] = []
        for item in content:
            if isinstance(item, dict):
                text = item.get("text")
                if text:
                    parts.append(str(text))
            elif item is not None:
                parts.append(str(item))
        return "\n".join(parts)
    return str(content)


def extract_json_text(raw_text: str) -> str:
    stripped = raw_text.strip()
    stripped = CODE_FENCE_PATTERN.sub("", stripped)
    if stripped.startswith("{") and stripped.endswith("}"):
        return stripped
    start = stripped.find("{")
    end = stripped.rfind("}")
    if start != -1 and end != -1 and start < end:
        return stripped[start : end + 1]
    raise ValueError(f"未从模型响应中解析出 JSON: {raw_text[:300]}")


def build_prompt(tasks: list[MeaningTask], required_counts: dict[int, int], prompt_template: str) -> str:
    serialized_tasks = []
    for task in tasks:
        serialized_tasks.append(
            {
                "meaning_id": task.meaning_id,
                "word_id": task.word_id,
                "word": task.word,
                "stage": task.stage,
                "pos": task.pos,
                "meaning_en": task.meaning_en,
                "meaning_zh": task.meaning_zh,
                "required_count": required_counts.get(task.meaning_id, 0),
            }
        )
    template = prompt_template.strip() or DEFAULT_PROMPT_TEMPLATE
    return template.format(
        task_count=len(tasks),
        tasks_json=json.dumps(serialized_tasks, ensure_ascii=False, indent=2),
    )


def _parse_example_list(items: object, expected_count: int) -> list[GeneratedExample]:
    if not isinstance(items, list):
        raise ValueError("examples 字段不是数组")

    normalized: list[GeneratedExample] = []
    seen: set[tuple[str, str]] = set()
    for item in items:
        if not isinstance(item, dict):
            continue
        example_en = normalize_text(item.get("example_en") or item.get("english"))
        example_zh = normalize_text(item.get("example_zh") or item.get("chinese"))
        if not example_en or not example_zh:
            continue
        key = (example_en.casefold(), example_zh.casefold())
        if key in seen:
            continue
        seen.add(key)
        normalized.append(GeneratedExample(example_en=example_en, example_zh=example_zh))
        if len(normalized) >= expected_count:
            break

    return normalized


def parse_examples_from_response(
    raw_text: str,
    expected_counts: dict[int, int],
) -> dict[int, list[GeneratedExample]]:
    payload_text = extract_json_text(raw_text)
    data = json.loads(payload_text)
    if isinstance(data, dict):
        results = data.get("results")
    else:
        results = data
    if not isinstance(results, list):
        raise ValueError(f"模型返回格式不正确: {payload_text[:300]}")

    parsed: dict[int, list[GeneratedExample]] = {}
    for item in results:
        if not isinstance(item, dict):
            continue
        meaning_id_value = item.get("meaning_id")
        try:
            meaning_id = int(meaning_id_value)
        except (TypeError, ValueError):
            continue
        expected_count = expected_counts.get(meaning_id)
        if expected_count is None:
            continue
        examples = _parse_example_list(item.get("examples"), expected_count)
        if examples:
            parsed[meaning_id] = examples

    if not parsed:
        raise ValueError(f"模型未返回有效例句: {payload_text[:300]}")
    return parsed


def call_zhipu_api(
    config: GeneratorConfig,
    tasks: list[MeaningTask],
    required_counts: dict[int, int],
) -> BatchApiResult:
    prompt = build_prompt(tasks, required_counts, config.prompt_template)
    payload = {
        "model": config.model,
        "temperature": config.temperature,
        "messages": [
            {
                "role": "system",
                "content": "你是一个严谨的英语教学例句生成助手，只返回用户要求的 JSON。",
            },
            {"role": "user", "content": prompt},
        ],
    }

    request = urllib.request.Request(
        config.api_url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {config.api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=config.timeout_seconds) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        error_body = exc.read().decode("utf-8", errors="replace")
        if exc.code == 429 or '"code":"1302"' in error_body or '"code": "1302"' in error_body:
            raise RateLimitError(f"智谱 API 请求失败: HTTP {exc.code} {error_body[:500]}") from exc
        raise RuntimeError(f"智谱 API 请求失败: HTTP {exc.code} {error_body[:500]}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"智谱 API 网络错误: {exc}") from exc

    try:
        data = json.loads(body)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"智谱 API 返回了非 JSON 数据: {body[:500]}") from exc

    choices = data.get("choices")
    if not isinstance(choices, list) or not choices:
        raise RuntimeError(f"智谱 API 返回缺少 choices: {body[:500]}")

    message = choices[0].get("message")
    if not isinstance(message, dict):
        raise RuntimeError(f"智谱 API 返回缺少 message: {body[:500]}")

    content = extract_message_text(message.get("content"))
    try:
        examples_by_meaning_id = parse_examples_from_response(content, required_counts)
    except Exception as exc:
        raise RuntimeError(
            "智谱 API 返回内容解析失败:\n"
            f"message_content={content[:2000]}\n"
            f"response_body={body[:4000]}"
        ) from exc

    return BatchApiResult(
        prompt_text=prompt,
        response_body=body,
        message_content=content,
        examples_by_meaning_id=examples_by_meaning_id,
    )


def chunk_tasks(tasks: list[MeaningTask], chunk_size: int) -> list[list[MeaningTask]]:
    if chunk_size <= 0:
        raise ValueError("批量请求数量必须大于 0")
    return [tasks[index : index + chunk_size] for index in range(0, len(tasks), chunk_size)]


def build_audio_path(template: str, task: MeaningTask, index: int) -> str:
    if not template.strip():
        return ""
    values = {
        "word": safe_token(task.word, f"word_{task.word_id}"),
        "meaning_id": str(task.meaning_id),
        "word_id": str(task.word_id),
        "stage": str(task.stage or 0),
        "index": str(index),
        "pos": safe_token(task.pos, "pos"),
    }
    try:
        return template.format(**values).strip()
    except KeyError as exc:
        raise ValueError(f"audio_path 模板包含未知占位符: {exc.args[0]}") from exc


def required_example_count(task: MeaningTask, config: GeneratorConfig) -> int:
    if config.fill_to_target:
        return max(0, config.target_examples - task.existing_examples)
    return config.target_examples


def load_meaning_tasks(config: GeneratorConfig) -> list[MeaningTask]:
    if not config.db_path.exists():
        raise FileNotFoundError(f"数据库不存在: {config.db_path}")

    stages = parse_stage_filter(config.stage_filter)
    clauses = [
        "TRIM(COALESCE(w.word, '')) <> ''",
        "TRIM(COALESCE(wm.meaning_zh, '')) <> ''",
    ]
    params: list[object] = []

    if stages:
        placeholders = ", ".join("?" for _ in stages)
        clauses.append(f"wm.stage IN ({placeholders})")
        params.extend(stages)

    if config.word_keyword.strip():
        clauses.append("w.word LIKE ?")
        params.append(f"%{config.word_keyword.strip()}%")

    where_clause = " AND ".join(clauses)
    limit_clause = ""
    if config.max_meanings > 0:
        limit_clause = " LIMIT ?"
        params.append(config.max_meanings)

    query = f"""
        SELECT
            wm.id AS meaning_id,
            wm.word_id AS word_id,
            w.word AS word,
            wm.stage AS stage,
            COALESCE(wm.pos, '') AS pos,
            COALESCE(wm.meaning_en, '') AS meaning_en,
            COALESCE(wm.meaning_zh, '') AS meaning_zh,
            COALESCE(wm.image, '') AS image,
            COALESCE(wm.word_tag, '') AS word_tag,
            COUNT(we.id) AS existing_examples
        FROM word_meaning AS wm
        JOIN word AS w ON w.id = wm.word_id
        LEFT JOIN word_example AS we ON we.meaning_id = wm.id
        WHERE {where_clause}
        GROUP BY wm.id, wm.word_id, w.word, wm.stage, wm.pos, wm.meaning_en, wm.meaning_zh, wm.image, wm.word_tag
        ORDER BY COALESCE(wm.stage, 999), w.word, wm.id
        {limit_clause}
    """

    tasks: list[MeaningTask] = []
    with sqlite3.connect(config.db_path) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(query, params).fetchall()
        for row in rows:
            tasks.append(
                MeaningTask(
                    meaning_id=int(row["meaning_id"]),
                    word_id=int(row["word_id"]),
                    word=normalize_text(row["word"]),
                    stage=int(row["stage"]) if row["stage"] is not None else None,
                    pos=normalize_text(row["pos"]),
                    meaning_en=normalize_text(row["meaning_en"]),
                    meaning_zh=normalize_text(row["meaning_zh"]),
                    image=normalize_text(row["image"]),
                    word_tag=normalize_text(row["word_tag"]),
                    existing_examples=int(row["existing_examples"]),
                )
            )
    return tasks


class GeneratorWorker(QObject):
    log = Signal(str)
    progress = Signal(int, int, int, int)
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(self, config: GeneratorConfig) -> None:
        super().__init__()
        self.config = config
        self._cancel_requested = False

    def cancel(self) -> None:
        self._cancel_requested = True

    @Slot()
    def run(self) -> None:
        try:
            result = self._run()
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.finished.emit(result)

    def _reset_failure_log(self) -> None:
        self.config.failure_log_path.parent.mkdir(parents=True, exist_ok=True)
        self.config.failure_log_path.write_text("", encoding="utf-8")

    def _append_failure_log(
        self,
        batch_index: int,
        task_reasons: list[tuple[MeaningTask, str]],
    ) -> None:
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        lines = [f"[{timestamp}] batch={batch_index}"]
        for task, reason in task_reasons:
            lines.append(
                "  "
                f"meaning_id={task.meaning_id} word={task.word} stage={task.stage} "
                f"meaning_zh={task.meaning_zh} existing_examples={task.existing_examples} "
                f"reason={reason}"
            )
        lines.append("")
        with self.config.failure_log_path.open("a", encoding="utf-8") as file:
            file.write("\n".join(lines))

    def _load_existing_pairs(self, conn: sqlite3.Connection, meaning_id: int) -> set[tuple[str, str]]:
        rows = conn.execute(
            """
            SELECT COALESCE(example_en, '') AS example_en, COALESCE(example_zh, '') AS example_zh
            FROM word_example
            WHERE meaning_id = ?
            """,
            (meaning_id,),
        ).fetchall()
        return {
            (normalize_text(row[0]).casefold(), normalize_text(row[1]).casefold())
            for row in rows
            if normalize_text(row[0]) or normalize_text(row[1])
        }

    def _sleep_with_cancel(self, seconds: float) -> bool:
        remaining = max(0.0, seconds)
        while remaining > 0:
            if self._cancel_requested:
                return False
            chunk = min(0.2, remaining)
            time.sleep(chunk)
            remaining -= chunk
        return True

    def _wait_before_regular_request(self) -> bool:
        if self.config.request_pacing_mode != "fixed-interval":
            return True
        if self.config.request_interval_seconds <= 0:
            return True
        return self._sleep_with_cancel(self.config.request_interval_seconds)

    def _generate_batch_examples(
        self,
        batch_tasks: list[MeaningTask],
        existing_pairs_by_meaning: dict[int, set[tuple[str, str]]],
    ) -> dict[int, list[GeneratedExample]]:
        required_counts = {
            task.meaning_id: required_example_count(task, self.config) for task in batch_tasks
        }
        local_pairs_by_meaning = {
            meaning_id: set(pairs) for meaning_id, pairs in existing_pairs_by_meaning.items()
        }
        if self._cancel_requested:
            return {task.meaning_id: [] for task in batch_tasks}

        rate_limit_retry = 0
        while True:
            if not self._wait_before_regular_request():
                return {task.meaning_id: [] for task in batch_tasks}
            try:
                api_result = call_zhipu_api(self.config, batch_tasks, required_counts)
                break
            except RateLimitError as exc:
                rate_limit_retry += 1
                if rate_limit_retry > self.config.rate_limit_retry_count:
                    raise RuntimeError(
                        f"连续触发速率限制，已重试 {self.config.rate_limit_retry_count} 次后放弃。原始错误: {exc}"
                    ) from exc
                wait_seconds = self.config.rate_limit_backoff_seconds * rate_limit_retry
                self.log.emit(
                    f"触发速率限制，第 {rate_limit_retry}/{self.config.rate_limit_retry_count} 次退避，"
                    f"等待 {wait_seconds} 秒后重试同一批次请求。原始错误: {exc}"
                )
                if not self._sleep_with_cancel(wait_seconds):
                    return {task.meaning_id: [] for task in batch_tasks}

        self.log.emit(
            f"批次单次请求完成，本次请求包含 {len(batch_tasks)} 个释义。请求 prompt:\n{api_result.prompt_text}"
        )
        self.log.emit(f"批次原始响应:\n{api_result.response_body}")

        collected = {task.meaning_id: [] for task in batch_tasks}
        for task in batch_tasks:
            returned_items = api_result.examples_by_meaning_id.get(task.meaning_id, [])
            local_pairs = local_pairs_by_meaning[task.meaning_id]
            for item in returned_items:
                key = (item.example_en.casefold(), item.example_zh.casefold())
                if self.config.skip_existing_exact and key in local_pairs:
                    continue
                local_pairs.add(key)
                collected[task.meaning_id].append(item)
                if len(collected[task.meaning_id]) >= required_counts[task.meaning_id]:
                    break

        return collected

    def _insert_examples(
        self,
        conn: sqlite3.Connection,
        task: MeaningTask,
        examples: list[GeneratedExample],
        existing_pairs: set[tuple[str, str]],
    ) -> tuple[int, int]:
        inserted = 0
        skipped = 0
        next_index = task.existing_examples + 1

        for item in examples:
            key = (item.example_en.casefold(), item.example_zh.casefold())
            if self.config.skip_existing_exact and key in existing_pairs:
                skipped += 1
                continue
            audio_path = build_audio_path(self.config.audio_path_template, task, next_index)
            conn.execute(
                """
                INSERT INTO word_example (
                    meaning_id, example_en, example_zh,
                    difficulty, image, example_tag, audio_path
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    task.meaning_id,
                    item.example_en,
                    item.example_zh,
                    self.config.difficulty,
                    self.config.image or None,
                    self.config.example_tag or None,
                    audio_path or None,
                ),
            )
            existing_pairs.add(key)
            inserted += 1
            next_index += 1

        return inserted, skipped

    def _run(self) -> dict[str, int | bool]:
        self._reset_failure_log()
        tasks = load_meaning_tasks(self.config)
        candidate_tasks = [task for task in tasks if required_example_count(task, self.config) > 0]
        total_tasks = len(candidate_tasks)
        total_expected = sum(required_example_count(task, self.config) for task in candidate_tasks)
        batches = chunk_tasks(candidate_tasks, self.config.request_batch_size) if candidate_tasks else []

        if total_tasks == 0:
            return {
                "processed_meanings": 0,
                "candidate_meanings": 0,
                "planned_examples": 0,
                "inserted_examples": 0,
                "skipped_duplicates": 0,
                "failed_meanings": 0,
                "cancelled": False,
                "dry_run": self.config.dry_run,
            }

        self.log.emit(
            f"待处理释义 {total_tasks} 条，计划生成例句 {total_expected} 条。"
            f"当前每次请求 {self.config.request_batch_size} 个释义，共 {len(batches)} 批。"
        )

        if self.config.dry_run:
            for index, task in enumerate(candidate_tasks, start=1):
                self.progress.emit(index, total_tasks, 0, 0)
                needed = required_example_count(task, self.config)
                self.log.emit(
                    f"[{index}/{total_tasks}] 预览: word={task.word} meaning_id={task.meaning_id} 需生成 {needed} 条"
                )
            return {
                "processed_meanings": total_tasks,
                "candidate_meanings": total_tasks,
                "planned_examples": total_expected,
                "inserted_examples": 0,
                "skipped_duplicates": 0,
                "failed_meanings": 0,
                "cancelled": False,
                "dry_run": True,
            }

        inserted_examples = 0
        skipped_duplicates = 0
        failed_meanings = 0
        processed_meanings = 0

        with sqlite3.connect(self.config.db_path) as conn:
            conn.execute("PRAGMA foreign_keys = ON")
            for batch_index, batch_tasks in enumerate(batches, start=1):
                if self._cancel_requested:
                    self.log.emit("检测到取消请求，已停止后续任务。")
                    return {
                        "processed_meanings": processed_meanings,
                        "candidate_meanings": total_tasks,
                        "planned_examples": total_expected,
                        "inserted_examples": inserted_examples,
                        "skipped_duplicates": skipped_duplicates,
                        "failed_meanings": failed_meanings,
                        "cancelled": True,
                        "dry_run": False,
                    }

                try:
                    batch_start = processed_meanings + 1
                    batch_end = processed_meanings + len(batch_tasks)
                    self.log.emit(
                        f"开始处理批次 {batch_index}/{len(batches)}，覆盖释义序号 {batch_start}-{batch_end}。"
                    )

                    existing_pairs_by_meaning = {
                        task.meaning_id: self._load_existing_pairs(conn, task.meaning_id) for task in batch_tasks
                    }
                    for task in batch_tasks:
                        needed = required_example_count(task, self.config)
                        self.log.emit(
                            f"批次 {batch_index}: word={task.word} meaning_id={task.meaning_id} "
                            f"meaning_zh={task.meaning_zh} 需生成 {needed} 条，已有 {task.existing_examples} 条。"
                        )
                    try:
                        examples_by_meaning = self._generate_batch_examples(batch_tasks, existing_pairs_by_meaning)
                    except Exception as exc:
                        failed_meanings += len(batch_tasks)
                        batch_reason = str(exc) or exc.__class__.__name__
                        self._append_failure_log(
                            batch_index,
                            [(task, batch_reason) for task in batch_tasks],
                        )
                        for task in batch_tasks:
                            processed_meanings += 1
                            self.progress.emit(processed_meanings, total_tasks, inserted_examples, failed_meanings)
                            self.log.emit(
                                f"[{processed_meanings}/{total_tasks}] 失败: {task.word} / {task.meaning_zh} "
                                f"失败原因: {batch_reason}"
                            )
                        continue

                    for task in batch_tasks:
                        try:
                            needed = required_example_count(task, self.config)
                            existing_pairs = existing_pairs_by_meaning[task.meaning_id]
                            examples = examples_by_meaning.get(task.meaning_id, [])
                            if len(examples) < needed:
                                self.log.emit(
                                    f"批次 {batch_index}: {task.word} / {task.meaning_zh} 返回有效例句不足，"
                                    f"目标 {needed}，实际 {len(examples)}。"
                                )
                            inserted, skipped = self._insert_examples(conn, task, examples, existing_pairs)
                            conn.commit()
                            inserted_examples += inserted
                            skipped_duplicates += skipped
                            processed_meanings += 1
                            if inserted < needed:
                                shortfall_reason = (
                                    f"模型返回或有效入库例句不足，目标 {needed} 条，实际入库 {inserted} 条，"
                                    f"重复跳过 {skipped} 条"
                                )
                                failed_meanings += 1
                                self._append_failure_log(batch_index, [(task, shortfall_reason)])
                                self.progress.emit(processed_meanings, total_tasks, inserted_examples, failed_meanings)
                                self.log.emit(
                                    f"[{processed_meanings}/{total_tasks}] 部分失败: {task.word} / {task.meaning_zh} "
                                    f"原因: {shortfall_reason}"
                                )
                                continue
                            self.progress.emit(processed_meanings, total_tasks, inserted_examples, failed_meanings)
                            self.log.emit(
                                f"[{processed_meanings}/{total_tasks}] 完成: {task.word} / {task.meaning_zh} "
                                f"新增 {inserted} 条，跳过重复 {skipped} 条。"
                                f" 当前累计已写入 {inserted_examples} 条，失败 {failed_meanings} 条。"
                            )
                        except Exception as exc:
                            conn.rollback()
                            failed_meanings += 1
                            processed_meanings += 1
                            self.progress.emit(processed_meanings, total_tasks, inserted_examples, failed_meanings)
                            meaning_reason = str(exc) or exc.__class__.__name__
                            self._append_failure_log(batch_index, [(task, meaning_reason)])
                            self.log.emit(
                                f"[{processed_meanings}/{total_tasks}] 失败: {task.word} / {task.meaning_zh} "
                                f"失败原因: {meaning_reason}"
                            )
                except Exception as exc:
                    failed_meanings += len(batch_tasks)
                    batch_reason = str(exc) or exc.__class__.__name__
                    self._append_failure_log(
                        batch_index,
                        [(task, batch_reason) for task in batch_tasks],
                    )
                    for task in batch_tasks:
                        processed_meanings += 1
                        self.progress.emit(processed_meanings, total_tasks, inserted_examples, failed_meanings)
                        self.log.emit(
                            f"[{processed_meanings}/{total_tasks}] 失败: {task.word} / {task.meaning_zh} "
                            f"失败原因: {batch_reason}"
                        )

        return {
            "processed_meanings": processed_meanings,
            "candidate_meanings": total_tasks,
            "planned_examples": total_expected,
            "inserted_examples": inserted_examples,
            "skipped_duplicates": skipped_duplicates,
            "failed_meanings": failed_meanings,
            "failure_log_path": str(self.config.failure_log_path),
            "cancelled": False,
            "dry_run": False,
        }


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("words.db 智谱例句生成器")
        self.resize(980, 840)

        self.worker_thread: QThread | None = None
        self.worker: GeneratorWorker | None = None

        central = QWidget(self)
        self.setCentralWidget(central)

        layout = QVBoxLayout(central)
        form_layout = QGridLayout()
        layout.addLayout(form_layout)

        self.db_path_edit = QLineEdit(str(DEFAULT_DB_PATH))
        self.api_key_edit = QLineEdit(DEFAULT_API_KEY)
        self.api_key_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self.api_url_edit = QLineEdit(DEFAULT_API_URL)
        self.model_edit = QLineEdit(DEFAULT_MODEL)
        self.word_keyword_edit = QLineEdit("")
        self.stage_filter_edit = QLineEdit("")
        self.image_edit = QLineEdit(DEFAULT_IMAGE)
        self.example_tag_edit = QLineEdit(DEFAULT_EXAMPLE_TAG)
        self.audio_path_template_edit = QLineEdit(DEFAULT_AUDIO_PATH_TEMPLATE)

        self.temperature_spin = QDoubleSpinBox()
        self.temperature_spin.setRange(0.0, 2.0)
        self.temperature_spin.setSingleStep(0.1)
        self.temperature_spin.setDecimals(2)
        self.temperature_spin.setValue(DEFAULT_TEMPERATURE)

        self.timeout_spin = QSpinBox()
        self.timeout_spin.setRange(5, 300)
        self.timeout_spin.setValue(DEFAULT_TIMEOUT_SECONDS)

        self.request_batch_spin = QSpinBox()
        self.request_batch_spin.setRange(1, 100)
        self.request_batch_spin.setValue(DEFAULT_REQUEST_BATCH_SIZE)

        self.request_pacing_mode_combo = QComboBox()
        self.request_pacing_mode_combo.addItem("收到响应后发送下一条", "response-driven")
        self.request_pacing_mode_combo.addItem("固定间隔发送", "fixed-interval")
        self.request_pacing_mode_combo.setCurrentIndex(0)

        self.request_interval_spin = QDoubleSpinBox()
        self.request_interval_spin.setRange(0.0, 30.0)
        self.request_interval_spin.setSingleStep(0.1)
        self.request_interval_spin.setDecimals(1)
        self.request_interval_spin.setValue(DEFAULT_REQUEST_INTERVAL_SECONDS)

        self.rate_limit_retry_spin = QSpinBox()
        self.rate_limit_retry_spin.setRange(0, 20)
        self.rate_limit_retry_spin.setValue(DEFAULT_RATE_LIMIT_RETRY_COUNT)

        self.rate_limit_backoff_spin = QSpinBox()
        self.rate_limit_backoff_spin.setRange(1, 120)
        self.rate_limit_backoff_spin.setValue(DEFAULT_RATE_LIMIT_BACKOFF_SECONDS)

        self.target_examples_spin = QSpinBox()
        self.target_examples_spin.setRange(1, 20)
        self.target_examples_spin.setValue(DEFAULT_TARGET_EXAMPLES)

        self.max_meanings_spin = QSpinBox()
        self.max_meanings_spin.setRange(0, 100000)
        self.max_meanings_spin.setValue(DEFAULT_MAX_MEANINGS)
        self.max_meanings_spin.setSpecialValueText("全部")

        self.difficulty_spin = QSpinBox()
        self.difficulty_spin.setRange(1, 10)
        self.difficulty_spin.setValue(DEFAULT_DIFFICULTY)

        choose_db_button = QPushButton("选择数据库")
        choose_db_button.clicked.connect(self.select_db_path)

        form_layout.addWidget(QLabel("words.db 路径"), 0, 0)
        form_layout.addWidget(self.db_path_edit, 0, 1)
        form_layout.addWidget(choose_db_button, 0, 2)

        form_layout.addWidget(QLabel("智谱 API Key"), 1, 0)
        form_layout.addWidget(self.api_key_edit, 1, 1, 1, 2)

        form_layout.addWidget(QLabel("API URL"), 2, 0)
        form_layout.addWidget(self.api_url_edit, 2, 1, 1, 2)

        form_layout.addWidget(QLabel("模型"), 3, 0)
        form_layout.addWidget(self.model_edit, 3, 1, 1, 2)

        form_layout.addWidget(QLabel("temperature"), 4, 0)
        form_layout.addWidget(self.temperature_spin, 4, 1)
        form_layout.addWidget(QLabel("超时秒数"), 4, 2)
        form_layout.addWidget(self.timeout_spin, 4, 3)

        form_layout.addWidget(QLabel("每个释义目标例句数"), 5, 0)
        form_layout.addWidget(self.target_examples_spin, 5, 1)
        form_layout.addWidget(QLabel("每次请求释义数"), 5, 2)
        form_layout.addWidget(self.request_batch_spin, 5, 3)

        form_layout.addWidget(QLabel("最大处理释义数"), 6, 0)
        form_layout.addWidget(self.max_meanings_spin, 6, 1)
        form_layout.addWidget(QLabel("阶段筛选"), 6, 2)
        form_layout.addWidget(self.stage_filter_edit, 6, 3)

        form_layout.addWidget(QLabel("请求发送模式"), 7, 0)
        form_layout.addWidget(self.request_pacing_mode_combo, 7, 1)
        form_layout.addWidget(QLabel("请求间隔秒数"), 7, 2)
        form_layout.addWidget(self.request_interval_spin, 7, 3)

        form_layout.addWidget(QLabel("限流重试次数"), 8, 0)
        form_layout.addWidget(self.rate_limit_retry_spin, 8, 1)
        form_layout.addWidget(QLabel("限流退避秒数"), 8, 2)
        form_layout.addWidget(self.rate_limit_backoff_spin, 8, 3)

        form_layout.addWidget(QLabel("写入 difficulty"), 9, 0)
        form_layout.addWidget(self.difficulty_spin, 9, 1)
        form_layout.addWidget(QLabel("写入 image"), 9, 2)
        form_layout.addWidget(self.image_edit, 9, 3)

        form_layout.addWidget(QLabel("写入 example_tag"), 10, 0)
        form_layout.addWidget(self.example_tag_edit, 10, 1)
        form_layout.addWidget(QLabel("audio_path 模板"), 10, 2)
        form_layout.addWidget(self.audio_path_template_edit, 10, 3)

        form_layout.addWidget(QLabel("单词关键字筛选"), 11, 0)
        form_layout.addWidget(self.word_keyword_edit, 11, 1, 1, 3)

        self.fill_to_target_checkbox = QCheckBox("仅补齐到目标数量")
        self.fill_to_target_checkbox.setChecked(True)
        self.skip_existing_checkbox = QCheckBox("跳过完全重复的例句")
        self.skip_existing_checkbox.setChecked(True)
        self.dry_run_checkbox = QCheckBox("仅预演，不写入数据库")
        self.dry_run_checkbox.setChecked(False)

        layout.addWidget(self.fill_to_target_checkbox)
        layout.addWidget(self.skip_existing_checkbox)
        layout.addWidget(self.dry_run_checkbox)

        layout.addWidget(QLabel("Prompt 模板，可使用占位符: {task_count} {tasks_json}"))
        self.prompt_edit = QPlainTextEdit()
        self.prompt_edit.setPlainText(DEFAULT_PROMPT_TEMPLATE)
        self.prompt_edit.setMinimumHeight(220)
        layout.addWidget(self.prompt_edit)

        hint_label = QLabel(
            "说明:\n"
            "1. 例句表不再单独存 stage，阶段统一从 word_meaning.stage 获取。\n"
            "2. image、example_tag、difficulty、audio_path 由界面参数统一写入。\n"
            "3. audio_path 模板可选，占位符支持 {word} {meaning_id} {word_id} {stage} {index} {pos}。\n"
            "4. 阶段筛选支持逗号分隔，例如 1,2,4。\n"
            "5. 每个批次只发送 1 条 API 请求，这条请求里包含 N 个释义；默认 N=10。\n"
            "6. 默认模式是收到当前请求响应后，再发送下一条请求。\n"
            "7. 只有选择固定间隔发送时，才会使用请求间隔秒数。\n"
            "8. 如果出现 HTTP 429，请降低每次请求释义数，或切换到固定间隔并提高间隔秒数。"
        )
        hint_label.setWordWrap(True)
        hint_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        layout.addWidget(hint_label)

        button_layout = QHBoxLayout()
        self.preview_button = QPushButton("预览任务")
        self.preview_button.clicked.connect(self.preview_tasks)
        self.start_button = QPushButton("开始生成并写库")
        self.start_button.clicked.connect(self.start_generation)
        self.cancel_button = QPushButton("取消")
        self.cancel_button.clicked.connect(self.cancel_generation)
        self.cancel_button.setEnabled(False)

        button_layout.addWidget(self.preview_button)
        button_layout.addWidget(self.start_button)
        button_layout.addWidget(self.cancel_button)
        layout.addLayout(button_layout)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        layout.addWidget(self.progress_bar)

        self.status_label = QLabel("等待开始")
        layout.addWidget(self.status_label)

        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        layout.addWidget(self.log_edit)

    def build_config(self) -> GeneratorConfig:
        db_path = Path(self.db_path_edit.text().strip())
        return GeneratorConfig(
            db_path=db_path,
            failure_log_path=db_path.parent / DEFAULT_FAILURE_LOG_PATH.name,
            api_key=self.api_key_edit.text().strip(),
            api_url=self.api_url_edit.text().strip() or DEFAULT_API_URL,
            model=self.model_edit.text().strip() or DEFAULT_MODEL,
            temperature=float(self.temperature_spin.value()),
            timeout_seconds=int(self.timeout_spin.value()),
            target_examples=int(self.target_examples_spin.value()),
            max_meanings=int(self.max_meanings_spin.value()),
            request_batch_size=int(self.request_batch_spin.value()),
            request_pacing_mode=str(self.request_pacing_mode_combo.currentData()),
            request_interval_seconds=float(self.request_interval_spin.value()),
            rate_limit_retry_count=int(self.rate_limit_retry_spin.value()),
            rate_limit_backoff_seconds=int(self.rate_limit_backoff_spin.value()),
            difficulty=int(self.difficulty_spin.value()),
            image=self.image_edit.text().strip(),
            example_tag=self.example_tag_edit.text().strip(),
            audio_path_template=self.audio_path_template_edit.text().strip(),
            stage_filter=self.stage_filter_edit.text().strip(),
            word_keyword=self.word_keyword_edit.text().strip(),
            fill_to_target=self.fill_to_target_checkbox.isChecked(),
            skip_existing_exact=self.skip_existing_checkbox.isChecked(),
            dry_run=self.dry_run_checkbox.isChecked(),
            prompt_template=self.prompt_edit.toPlainText(),
        )

    def append_log(self, message: str) -> None:
        self.log_edit.appendPlainText(message)

    @Slot()
    def select_db_path(self) -> None:
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 words.db",
            str(Path(self.db_path_edit.text()).parent),
            "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*.*)",
        )
        if file_path:
            self.db_path_edit.setText(file_path)

    @Slot()
    def preview_tasks(self) -> None:
        try:
            config = self.build_config()
            tasks = load_meaning_tasks(config)
            candidate_tasks = [task for task in tasks if required_example_count(task, config) > 0]
            planned_examples = sum(required_example_count(task, config) for task in candidate_tasks)
            message = (
                f"已扫描释义 {len(tasks)} 条，可处理 {len(candidate_tasks)} 条，"
                f"计划生成例句 {planned_examples} 条，"
                f"按每批 1 条请求、每条包含 {config.request_batch_size} 个释义，"
                f"发送模式 {config.request_pacing_mode}。"
            )
            self.status_label.setText(message)
            self.append_log(message)
            if candidate_tasks:
                first_task = candidate_tasks[0]
                self.append_log(
                    f"首条任务示例: word={first_task.word}, meaning_id={first_task.meaning_id}, "
                    f"meaning_zh={first_task.meaning_zh}, existing={first_task.existing_examples}"
                )
        except Exception as exc:
            QMessageBox.critical(self, "预览失败", str(exc))

    @Slot()
    def start_generation(self) -> None:
        try:
            config = self.build_config()
            if not config.db_path.exists():
                raise FileNotFoundError(f"数据库不存在: {config.db_path}")
            if not config.api_key:
                raise ValueError("请输入智谱 API Key，或设置环境变量 ZHIPUAI_API_KEY")
            if config.request_batch_size <= 0:
                raise ValueError("每次请求释义数必须大于 0")
            parse_stage_filter(config.stage_filter)
            build_prompt(
                [
                    MeaningTask(
                        meaning_id=1,
                        word_id=1,
                        word="demo",
                        stage=1,
                        pos="n.",
                        meaning_en="demo",
                        meaning_zh="示例",
                        image="",
                        word_tag="",
                        existing_examples=0,
                    )
                ],
                {1: config.target_examples},
                config.prompt_template,
            )
            if config.audio_path_template:
                build_audio_path(
                    config.audio_path_template,
                    MeaningTask(
                        meaning_id=1,
                        word_id=1,
                        word="demo",
                        stage=1,
                        pos="n.",
                        meaning_en="demo",
                        meaning_zh="示例",
                        image="",
                        word_tag="",
                        existing_examples=0,
                    ),
                    1,
                )
        except Exception as exc:
            QMessageBox.critical(self, "参数错误", str(exc))
            return

        self.log_edit.clear()
        self.progress_bar.setValue(0)
        self.status_label.setText("准备开始...")
        self.append_log(
            f"启动任务：每批只发 1 条请求，这条请求包含 {config.request_batch_size} 个释义；"
            f"发送模式 {config.request_pacing_mode}；"
            f"固定间隔模式下间隔 {config.request_interval_seconds:.1f} 秒，"
            f"限流最多重试 {config.rate_limit_retry_count} 次，退避 {config.rate_limit_backoff_seconds} 秒起。"
        )
        self.start_button.setEnabled(False)
        self.preview_button.setEnabled(False)
        self.cancel_button.setEnabled(True)

        self.worker_thread = QThread(self)
        self.worker = GeneratorWorker(config)
        self.worker.moveToThread(self.worker_thread)

        self.worker_thread.started.connect(self.worker.run)
        self.worker.log.connect(self.append_log)
        self.worker.progress.connect(self.update_progress)
        self.worker.finished.connect(self.on_finished)
        self.worker.failed.connect(self.on_failed)
        self.worker.finished.connect(self.worker_thread.quit)
        self.worker.failed.connect(self.worker_thread.quit)
        self.worker_thread.finished.connect(self.cleanup_worker)

        self.worker_thread.start()

    @Slot()
    def cancel_generation(self) -> None:
        if self.worker is not None:
            self.worker.cancel()
            self.cancel_button.setEnabled(False)
            self.status_label.setText("正在取消...")
            self.append_log("已发送取消请求，等待当前任务结束。")

    @Slot(int, int, int, int)
    def update_progress(self, current: int, total: int, inserted: int, failed: int) -> None:
        percent = 0 if total <= 0 else int(current * 100 / total)
        self.progress_bar.setValue(percent)
        self.status_label.setText(
            f"处理中 {current}/{total}，已写入 {inserted} 条例句，失败 {failed} 条释义"
        )

    @Slot(dict)
    def on_finished(self, result: dict) -> None:
        self.progress_bar.setValue(100)
        if result.get("dry_run"):
            summary = (
                f"预演完成：释义 {result['processed_meanings']} 条，"
                f"计划生成 {result['planned_examples']} 条。"
            )
        else:
            summary = (
                f"完成：处理释义 {result['processed_meanings']}/{result['candidate_meanings']} 条，"
                f"新增例句 {result['inserted_examples']} 条，"
                f"跳过重复 {result['skipped_duplicates']} 条，"
                f"失败 {result['failed_meanings']} 条。"
            )
            if result.get("failed_meanings"):
                summary += f" 失败详情见 {result['failure_log_path']}"
        if result.get("cancelled"):
            summary = f"已取消。{summary}"
        self.status_label.setText(summary)
        self.append_log(summary)
        QMessageBox.information(self, "执行完成", summary)

    @Slot(str)
    def on_failed(self, message: str) -> None:
        self.status_label.setText("执行失败")
        self.append_log(f"执行失败: {message}")
        QMessageBox.critical(self, "执行失败", message)

    @Slot()
    def cleanup_worker(self) -> None:
        self.start_button.setEnabled(True)
        self.preview_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        if self.worker is not None:
            self.worker.deleteLater()
            self.worker = None
        if self.worker_thread is not None:
            self.worker_thread.deleteLater()
            self.worker_thread = None


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())