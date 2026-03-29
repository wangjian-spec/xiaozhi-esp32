from __future__ import annotations

import base64
import io
import importlib
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
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


_ensure_dependency("requests")
_ensure_dependency("PIL.Image", "Pillow")
_ensure_dependency("PySide6.QtCore", "PySide6")

import requests

from PIL import Image, ImageOps
from PySide6.QtCore import QEvent, QObject, QSize, QThread, Qt, QTimer, Signal, Slot
from PySide6.QtGui import QAction, QImage, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
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
    QStatusBar,
    QVBoxLayout,
    QWidget,
)


ROOT_DIR = Path(__file__).resolve().parent
DEFAULT_JSON_PATH = ROOT_DIR / "stage1" / "record1_1079.json"
DEFAULT_WORDS_OUTPUT_DIR = ROOT_DIR / "generated_images" / "stage1" / "words"
DEFAULT_EXAMPLES_OUTPUT_DIR = ROOT_DIR / "generated_images" / "stage1" / "example"
DEFAULT_API_URL = "https://open.bigmodel.cn/api/paas/v4/images/generations"
DEFAULT_MODEL = "glm-image"
DEFAULT_PROMPT_API_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
DEFAULT_PROMPT_MODEL = "glm-5"
DEFAULT_API_KEY = "22008338eea846c99387608f53ad2a0e.Hc4SU0LCpnuFk7GN"
TARGET_SIZE = (1280, 1280)
REQUEST_TIMEOUT_SECONDS = 180
RECORD_LIST_ITEM_HEIGHT = 84
RECORDS_PER_PAGE = 5

DEFAULT_FIXED_PROMPT = """分辨率1280*1280
适合墨水屏显示
适合儿童，小学教学用途
简单的黑白卡通插画
使用粗黑线条勾勒轮廓
细节尽量少，画面简洁
纯白背景，黑色绘图
高对比度（黑白分明）
不要阴影
不要光影效果
不要灰度（只有纯黑和纯白）
图片中不能出现任何文字
构图居中
主题明确、突出
含义直观，一眼能理解
不要抽象表达
手绘风格
简笔画风格
卡通风格
不要写实风格
不要三维效果
不要水印
不要标志（logo）
不要字母或单词"""

DEFAULT_VARIABLE_TEMPLATE = """
"""


@dataclass(slots=True)
class ExampleRecord:
    list_index: int
    example_id: int | None
    meaning_id: int | None
    example_en: str
    example_zh: str
    difficulty: int | None
    example_tag: str
    image_prompt: str
    raw: dict[str, Any]

    @property
    def display_text(self) -> str:
        example_id_text = str(self.example_id) if self.example_id is not None else f"idx{self.list_index}"
        summary = self.example_en or self.example_zh or "无例句"
        return f"{self.list_index:02d}. [{example_id_text}] {summary}"

    @property
    def file_id_text(self) -> str:
        return str(self.example_id) if self.example_id is not None else f"idx{self.list_index}"

@dataclass(slots=True)
class WordRecord:
    list_index: int
    word_id: int | None
    meaning_id: int | None
    word: str
    phonetic: str
    word_type: str
    pos: str
    meaning_en: str
    meaning_zh: str
    image_hint: str
    word_tag: str
    examples: list[ExampleRecord]
    raw: dict[str, Any]

    @property
    def display_text(self) -> str:
        word_id_text = str(self.word_id) if self.word_id is not None else "?"
        pos_text = self.pos or "未标注词性"
        meaning_text = self.meaning_zh or self.meaning_en or "无释义"
        return f"{self.list_index:04d}. [{word_id_text}] {self.word} | {pos_text} | {meaning_text}"

@dataclass(slots=True)
class GenerationResult:
    prompt: str
    request_payload: dict[str, Any]
    response_payload: dict[str, Any]
    image_bytes: bytes
    image_suffix: str


@dataclass(slots=True)
class PromptGenerationResult:
    prompt_text: str
    request_payload: dict[str, Any]
    response_payload: dict[str, Any]


@dataclass(slots=True)
class GenerationTask:
    task_id: int
    task_type: str
    display_text: str
    record: WordRecord
    prompt: str
    output_dir: Path
    file_stem: str
    metadata: dict[str, Any]
    status_text: str


class QueueTriggerTextEdit(QPlainTextEdit):
    queueRequested = Signal()

    def mouseDoubleClickEvent(self, event) -> None:  # type: ignore[override]
        super().mouseDoubleClickEvent(event)
        self.queueRequested.emit()


class RecordListItemWidget(QWidget):
    selected = Signal(int)
    rowSelected = Signal(int, int)
    queueRequested = Signal(int, int)
    deletePromptRequested = Signal(int, int)
    promptGenerateRequested = Signal(int, int)

    def __init__(
        self,
        record_index: int,
        record: WordRecord,
        selected_row_code: int | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._record_index = record_index
        self._record = record
        self._selected_row_code = selected_row_code
        self._prompt_buttons: dict[int, QPushButton] = {}

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 2, 4, 2)
        layout.setSpacing(1)

        self.meaning_row_widget = QWidget(self)
        self.meaning_row_widget.setObjectName(f"meaningRow_{record_index}")
        first_row = QHBoxLayout(self.meaning_row_widget)
        first_row.setContentsMargins(4, 2, 4, 2)
        first_row.setSpacing(4)
        self.meaning_label = QLabel(self._build_meaning_text(record), self)
        self.meaning_label.setWordWrap(False)
        first_row.addWidget(self.meaning_label, stretch=1)
        meaning_delete_button = QPushButton("删除提示词", self)
        meaning_delete_button.setFixedHeight(20)
        meaning_delete_button.clicked.connect(lambda: self._on_delete_clicked(-1))
        first_row.addWidget(meaning_delete_button)
        meaning_button = QPushButton("生成提示词", self)
        meaning_button.setFixedHeight(20)
        meaning_button.clicked.connect(lambda: self._on_generate_prompt_clicked(-1))
        self._prompt_buttons[-1] = meaning_button
        first_row.addWidget(meaning_button)
        layout.addWidget(self.meaning_row_widget)

        self.example_one_row_widget = QWidget(self)
        self.example_one_row_widget.setObjectName(f"exampleOneRow_{record_index}")
        second_row = QHBoxLayout(self.example_one_row_widget)
        second_row.setContentsMargins(4, 2, 4, 2)
        second_row.setSpacing(4)
        self.example_one_label = QLabel(self._build_example_text(record, 0), self)
        self.example_one_label.setWordWrap(False)
        second_row.addWidget(self.example_one_label, stretch=1)
        example_one_delete_button = QPushButton("删除提示词", self)
        example_one_delete_button.setFixedHeight(20)
        example_one_delete_button.setEnabled(len(record.examples) >= 1)
        example_one_delete_button.clicked.connect(lambda: self._on_delete_clicked(0))
        second_row.addWidget(example_one_delete_button)
        example_one_button = QPushButton("生成提示词", self)
        example_one_button.setFixedHeight(20)
        example_one_button.setEnabled(len(record.examples) >= 1)
        example_one_button.clicked.connect(lambda: self._on_generate_prompt_clicked(0))
        self._prompt_buttons[0] = example_one_button
        second_row.addWidget(example_one_button)
        layout.addWidget(self.example_one_row_widget)

        self.example_two_row_widget = QWidget(self)
        self.example_two_row_widget.setObjectName(f"exampleTwoRow_{record_index}")
        third_row = QHBoxLayout(self.example_two_row_widget)
        third_row.setContentsMargins(4, 2, 4, 2)
        third_row.setSpacing(4)
        self.example_two_label = QLabel(self._build_example_text(record, 1), self)
        self.example_two_label.setWordWrap(False)
        third_row.addWidget(self.example_two_label, stretch=1)
        example_two_delete_button = QPushButton("删除提示词", self)
        example_two_delete_button.setFixedHeight(20)
        example_two_delete_button.setEnabled(len(record.examples) >= 2)
        example_two_delete_button.clicked.connect(lambda: self._on_delete_clicked(1))
        third_row.addWidget(example_two_delete_button)
        example_two_button = QPushButton("生成提示词", self)
        example_two_button.setFixedHeight(20)
        example_two_button.setEnabled(len(record.examples) >= 2)
        example_two_button.clicked.connect(lambda: self._on_generate_prompt_clicked(1))
        self._prompt_buttons[1] = example_two_button
        third_row.addWidget(example_two_button)
        layout.addWidget(self.example_two_row_widget)

        self.setFixedHeight(RECORD_LIST_ITEM_HEIGHT + 12)
        self.installEventFilter(self)
        self.meaning_row_widget.installEventFilter(self)
        self.example_one_row_widget.installEventFilter(self)
        self.example_two_row_widget.installEventFilter(self)
        self.meaning_label.installEventFilter(self)
        self.example_one_label.installEventFilter(self)
        self.example_two_label.installEventFilter(self)
        self._refresh_row_styles()

    def sizeHint(self) -> QSize:  # type: ignore[override]
        return QSize(0, RECORD_LIST_ITEM_HEIGHT + 12)

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:  # type: ignore[override]
        if event.type() == QEvent.MouseButtonPress:
            if watched in {self, self.meaning_row_widget, self.meaning_label}:
                self._on_row_selected(-1)
            elif watched in {self.example_one_row_widget, self.example_one_label}:
                self._on_row_selected(0)
            elif watched in {self.example_two_row_widget, self.example_two_label}:
                self._on_row_selected(1)
            else:
                return super().eventFilter(watched, event)
            self.selected.emit(self._record_index)
            return True
        if event.type() == QEvent.MouseButtonDblClick:
            if watched in {self, self.meaning_row_widget, self.meaning_label}:
                self._on_queue_clicked(-1)
            elif watched in {self.example_one_row_widget, self.example_one_label}:
                self._on_queue_clicked(0)
            elif watched in {self.example_two_row_widget, self.example_two_label}:
                self._on_queue_clicked(1)
            else:
                return super().eventFilter(watched, event)
            return True
        return super().eventFilter(watched, event)

    def set_selected_row_code(self, row_code: int | None) -> None:
        self._selected_row_code = row_code
        self._refresh_row_styles()

    def set_prompt_generation_state(self, row_code: int | None, elapsed_seconds: int | None) -> None:
        for current_row_code, button in self._prompt_buttons.items():
            is_available = current_row_code == -1 or current_row_code < len(self._record.examples)
            is_active = row_code == current_row_code and elapsed_seconds is not None
            button.setEnabled(is_available and not is_active)
            if is_active:
                button.setText(f"生成中 {elapsed_seconds}s")
            else:
                button.setText("生成提示词")

    def _on_row_selected(self, row_code: int) -> None:
        self._selected_row_code = row_code
        self._refresh_row_styles()
        self.selected.emit(self._record_index)
        self.rowSelected.emit(self._record_index, row_code)

    def _on_queue_clicked(self, row_code: int) -> None:
        self._on_row_selected(row_code)
        self.queueRequested.emit(self._record_index, row_code)

    def _on_delete_clicked(self, row_code: int) -> None:
        self._on_row_selected(row_code)
        self.deletePromptRequested.emit(self._record_index, row_code)

    def _on_generate_prompt_clicked(self, row_code: int) -> None:
        self._on_row_selected(row_code)
        self.promptGenerateRequested.emit(self._record_index, row_code)

    def _refresh_row_styles(self) -> None:
        self._apply_row_style(self.meaning_row_widget, self.meaning_label, -1)
        self._apply_row_style(self.example_one_row_widget, self.example_one_label, 0)
        self._apply_row_style(self.example_two_row_widget, self.example_two_label, 1)

    def _apply_row_style(self, row_widget: QWidget, label: QLabel, row_code: int) -> None:
        prompt_empty = self._row_prompt_empty(row_code)
        has_image = self._row_has_generated_image(row_code)
        is_selected = self._selected_row_code == row_code

        background = "#ffffff"
        border = "#c8c8c8"
        text_color = "#222222"
        if prompt_empty:
            background = "#fdeaea"
            border = "#d9534f"
            text_color = "#a94442"
        elif has_image:
            background = "#e8f6e8"
            border = "#3c9a4d"
            text_color = "#256c2f"

        if is_selected:
            border = "#1d70b8"

        row_widget.setStyleSheet(
            f"background: {background}; border: 2px solid {border}; border-radius: 4px;"
        )
        label.setStyleSheet(f"color: {text_color}; background: transparent; border: none;")

    def _row_prompt_empty(self, row_code: int) -> bool:
        if row_code == -1:
            return not bool(self._record.image_hint.strip())
        if row_code >= len(self._record.examples):
            return False
        return not bool(self._record.examples[row_code].image_prompt.strip())

    def _row_has_generated_image(self, row_code: int) -> bool:
        if row_code == -1:
            return _resolve_existing_preview_path(
                DEFAULT_WORDS_OUTPUT_DIR,
                _build_meaning_file_stem(self._record),
            ) is not None
        if row_code >= len(self._record.examples):
            return False
        return _resolve_existing_preview_path(
            DEFAULT_EXAMPLES_OUTPUT_DIR,
            _build_example_file_stem(self._record, self._record.examples[row_code]),
        ) is not None

    def _build_meaning_text(self, record: WordRecord) -> str:
        meaning_text = record.meaning_zh or record.meaning_en or "-"
        prompt_text = _compact_text(record.image_hint or "-", 44)
        return f"{record.list_index:04d}. {record.word or '-'} {meaning_text} | image({prompt_text})"

    def _build_example_text(self, record: WordRecord, example_offset: int) -> str:
        if example_offset >= len(record.examples):
            return f"例句{example_offset + 1}: - | image(-)"

        example = record.examples[example_offset]
        example_text = _compact_text(example.example_en or example.example_zh or "-", 48)
        prompt_text = _compact_text(example.image_prompt or "-", 28)
        return f"例句{example_offset + 1}: {example_text} | image({prompt_text})"


class RecordRepository:
    def load(self, json_path: Path) -> list[WordRecord]:
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("JSON 中缺少 records 数组")

        parsed_records: list[WordRecord] = []
        for index, item in enumerate(records, start=1):
            if not isinstance(item, dict):
                continue

            word_data = item.get("word") or {}
            meaning_data = item.get("word_meaning") or {}
            example_items = item.get("word_example") or []
            if not isinstance(word_data, dict) or not isinstance(meaning_data, dict):
                continue

            examples: list[ExampleRecord] = []
            if isinstance(example_items, list):
                for example_index, example in enumerate(example_items, start=1):
                    if not isinstance(example, dict):
                        continue
                    examples.append(
                        ExampleRecord(
                            list_index=example_index,
                            example_id=_safe_int(example.get("id")),
                            meaning_id=_safe_int(example.get("meaning_id")),
                            example_en=str(example.get("example_en") or "").strip(),
                            example_zh=str(example.get("example_zh") or "").strip(),
                            difficulty=_safe_int(example.get("difficulty")),
                            example_tag=str(example.get("example_tag") or "").strip(),
                            image_prompt=str(example.get("image") or "").strip(),
                            raw=example,
                        )
                    )

            parsed_records.append(
                WordRecord(
                    list_index=index,
                    word_id=_safe_int(word_data.get("id")),
                    meaning_id=_safe_int(meaning_data.get("id")),
                    word=str(word_data.get("word") or "").strip(),
                    phonetic=str(word_data.get("phonetic") or "").strip(),
                    word_type=str(word_data.get("word_type") or "").strip(),
                    pos=str(meaning_data.get("pos") or "").strip(),
                    meaning_en=str(meaning_data.get("meaning_en") or "").strip(),
                    meaning_zh=str(meaning_data.get("meaning_zh") or "").strip(),
                    image_hint=str(meaning_data.get("image") or "").strip(),
                    word_tag=str(meaning_data.get("word_tag") or "").strip(),
                    examples=examples,
                    raw=item,
                )
            )

        if not parsed_records:
            raise ValueError("JSON 中没有可用 record")
        return parsed_records


class PromptBuilder:
    def build(self, record: WordRecord, fixed_prompt: str, variable_prompt: str) -> str:
        del record
        fixed_part = fixed_prompt.strip()
        variable_part = variable_prompt.strip()
        if fixed_part and variable_part:
            return f"{fixed_part}\n\n{variable_part}"
        return fixed_part or variable_part


class BinImageWriter:
    def save_outputs(self, image_bytes: bytes, file_stem: str, output_dir: Path) -> dict[str, Path]:
        output_dir.mkdir(parents=True, exist_ok=True)

        with Image.open(io.BytesIO(image_bytes)) as image:
            rgba_image = image.convert("RGBA")
            rgba_image = ImageOps.fit(rgba_image, TARGET_SIZE, method=Image.LANCZOS, centering=(0.5, 0.5))

            raw_png_path = output_dir / f"{file_stem}.png"
            rgba_image.save(raw_png_path, format="PNG")

            eink_preview = self._prepare_eink_image(rgba_image)
            eink_png_path = output_dir / f"{file_stem}_eink.png"
            eink_preview.save(eink_png_path, format="PNG")

            bin_path = output_dir / f"{file_stem}.bin"
            packed = self._pack_1bit(eink_preview)
            header = TARGET_SIZE[0].to_bytes(2, "little") + TARGET_SIZE[1].to_bytes(2, "little")
            bin_path.write_bytes(header + packed)

        return {
            "png": raw_png_path,
            "eink_png": eink_png_path,
            "bin": bin_path,
        }

    def _prepare_eink_image(self, image: Image.Image) -> Image.Image:
        background = Image.new("RGBA", image.size, (255, 255, 255, 255))
        composite = Image.alpha_composite(background, image).convert("L")
        composite = ImageOps.autocontrast(composite)
        return composite.convert("1", dither=Image.FLOYDSTEINBERG)

    def _pack_1bit(self, image: Image.Image) -> bytes:
        if image.mode != "1":
            image = image.convert("1")

        width, height = image.size
        stride = (width + 7) // 8
        packed = bytearray(stride * height)
        pixels = image.load()
        for y in range(height):
            row_offset = y * stride
            for x in range(width):
                if pixels[x, y] == 0:
                    byte_index = row_offset + (x >> 3)
                    packed[byte_index] |= 0x80 >> (x & 7)
        return bytes(packed)


class ZhipuImageClient:
    def generate(
        self,
        *,
        api_key: str,
        api_url: str,
        model: str,
        prompt: str,
        extra_body_text: str,
    ) -> GenerationResult:
        if not api_key.strip():
            raise ValueError("请先输入智谱 API Key")

        payload: dict[str, Any] = {
            "model": model.strip() or DEFAULT_MODEL,
            "prompt": prompt,
            "size": f"{TARGET_SIZE[0]}x{TARGET_SIZE[1]}",
            "watermark_enabled": False,
        }
        self._merge_extra_body(payload, extra_body_text)

        response = requests.post(
            api_url.strip() or DEFAULT_API_URL,
            headers={
                "Authorization": f"Bearer {api_key.strip()}",
                "Content-Type": "application/json",
            },
            json=payload,
            timeout=REQUEST_TIMEOUT_SECONDS,
        )
        if not response.ok:
            raise ValueError(self._format_http_error(response))

        response_payload = response.json()
        image_bytes, image_suffix = self._extract_image(response_payload)
        return GenerationResult(
            prompt=prompt,
            request_payload=payload,
            response_payload=response_payload,
            image_bytes=image_bytes,
            image_suffix=image_suffix,
        )

    def _merge_extra_body(self, payload: dict[str, Any], extra_body_text: str) -> None:
        extra_body_text = extra_body_text.strip()
        if not extra_body_text:
            return

        extra_payload = json.loads(extra_body_text)
        if not isinstance(extra_payload, dict):
            raise ValueError("额外请求 JSON 必须是对象，例如 {\"user_id\": \"demo\"}")
        payload.update(extra_payload)

    def _extract_image(self, response_payload: dict[str, Any]) -> tuple[bytes, str]:
        data_items = response_payload.get("data")
        if isinstance(data_items, list):
            for item in data_items:
                if not isinstance(item, dict):
                    continue
                url = item.get("url")
                if isinstance(url, str) and url.strip():
                    return self._download_remote_image(url.strip()), ".png"

                for base64_key in ("b64_json", "base64", "image_base64"):
                    encoded = item.get(base64_key)
                    if isinstance(encoded, str) and encoded.strip():
                        return base64.b64decode(encoded), ".png"

        if isinstance(response_payload.get("url"), str):
            return self._download_remote_image(str(response_payload["url"])), ".png"

        raise ValueError(f"未在响应中找到图片数据: {json.dumps(response_payload, ensure_ascii=False, indent=2)}")

    def _download_remote_image(self, url: str) -> bytes:
        response = requests.get(url, timeout=REQUEST_TIMEOUT_SECONDS)
        if not response.ok:
            raise ValueError(self._format_http_error(response))
        return response.content

    def _format_http_error(self, response: requests.Response) -> str:
        detail = response.text.strip()
        try:
            payload = response.json()
            detail = json.dumps(payload, ensure_ascii=False, indent=2)
        except ValueError:
            pass
        return f"接口请求失败，HTTP {response.status_code}: {detail}"


class ZhipuPromptClient:
    def generate(
        self,
        *,
        api_key: str,
        api_url: str,
        model: str,
        system_prompt: str,
        user_prompt: str,
    ) -> PromptGenerationResult:
        if not api_key.strip():
            raise ValueError("请先输入智谱 API Key")

        payload: dict[str, Any] = {
            "model": model.strip() or DEFAULT_PROMPT_MODEL,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            "temperature": 0.6,
        }

        response = requests.post(
            api_url.strip() or DEFAULT_PROMPT_API_URL,
            headers={
                "Authorization": f"Bearer {api_key.strip()}",
                "Content-Type": "application/json",
            },
            json=payload,
            timeout=REQUEST_TIMEOUT_SECONDS,
        )
        if not response.ok:
            raise ValueError(self._format_http_error(response))

        response_payload = response.json()
        prompt_text = self._extract_prompt_text(response_payload)
        if not prompt_text:
            raise ValueError("模型没有返回可用提示词")
        return PromptGenerationResult(
            prompt_text=prompt_text,
            request_payload=payload,
            response_payload=response_payload,
        )

    def _extract_prompt_text(self, response_payload: dict[str, Any]) -> str:
        choices = response_payload.get("choices")
        if not isinstance(choices, list) or not choices:
            raise ValueError(f"提示词接口返回缺少 choices: {json.dumps(response_payload, ensure_ascii=False, indent=2)}")

        message = choices[0].get("message")
        if not isinstance(message, dict):
            raise ValueError(f"提示词接口返回缺少 message: {json.dumps(response_payload, ensure_ascii=False, indent=2)}")

        content = _extract_message_text(message.get("content"))
        return _normalize_generated_prompt(content)

    def _format_http_error(self, response: requests.Response) -> str:
        detail = response.text.strip()
        try:
            payload = response.json()
            detail = json.dumps(payload, ensure_ascii=False, indent=2)
        except ValueError:
            pass
        return f"提示词接口请求失败，HTTP {response.status_code}: {detail}"


class GenerationWorker(QObject):
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(
        self,
        client: ZhipuImageClient,
        exporter: BinImageWriter,
        record: WordRecord,
        prompt: str,
        api_key: str,
        api_url: str,
        model: str,
        extra_body_text: str,
        file_stem: str,
        metadata: dict[str, Any],
        output_dir: Path,
    ) -> None:
        super().__init__()
        self._client = client
        self._exporter = exporter
        self._record = record
        self._prompt = prompt
        self._api_key = api_key
        self._api_url = api_url
        self._model = model
        self._extra_body_text = extra_body_text
        self._file_stem = file_stem
        self._metadata = metadata
        self._output_dir = output_dir

    @Slot()
    def run(self) -> None:
        try:
            generation = self._client.generate(
                api_key=self._api_key,
                api_url=self._api_url,
                model=self._model,
                prompt=self._prompt,
                extra_body_text=self._extra_body_text,
            )
            written_paths = self._exporter.save_outputs(
                image_bytes=generation.image_bytes,
                file_stem=self._file_stem,
                output_dir=self._output_dir,
            )
            meta_path = self._output_dir / f"{self._file_stem}.json"
            meta_payload = {
                "word": self._record.word,
                "word_id": self._record.word_id,
                "meaning_id": self._record.meaning_id,
                **self._metadata,
                "prompt": generation.prompt,
                "request_payload": generation.request_payload,
                "response_payload": generation.response_payload,
                "output_files": {name: str(path) for name, path in written_paths.items()},
            }
            meta_path.write_text(json.dumps(meta_payload, ensure_ascii=False, indent=2), encoding="utf-8")
            written_paths["meta"] = meta_path
            self.finished.emit({"paths": written_paths, "image_bytes": generation.image_bytes})
        except Exception as exc:
            self.failed.emit(str(exc))


class PromptGenerationWorker(QObject):
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(
        self,
        client: ZhipuPromptClient,
        api_key: str,
        api_url: str,
        model: str,
        system_prompt: str,
        user_prompt: str,
        record_index: int,
        row_code: int,
    ) -> None:
        super().__init__()
        self._client = client
        self._api_key = api_key
        self._api_url = api_url
        self._model = model
        self._system_prompt = system_prompt
        self._user_prompt = user_prompt
        self._record_index = record_index
        self._row_code = row_code

    @Slot()
    def run(self) -> None:
        try:
            result = self._client.generate(
                api_key=self._api_key,
                api_url=self._api_url,
                model=self._model,
                system_prompt=self._system_prompt,
                user_prompt=self._user_prompt,
            )
            self.finished.emit(
                {
                    "record_index": self._record_index,
                    "row_code": self._row_code,
                    "prompt_text": result.prompt_text,
                    "request_payload": result.request_payload,
                    "response_payload": result.response_payload,
                }
            )
        except Exception as exc:
            self.failed.emit(str(exc))


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("智谱图片生成器")
        self.resize(1500, 980)

        self._repository = RecordRepository()
        self._prompt_builder = PromptBuilder()
        self._client = ZhipuImageClient()
        self._prompt_client = ZhipuPromptClient()
        self._exporter = BinImageWriter()

        self._records: list[WordRecord] = []
        self._filtered_record_indices: list[int] = []
        self._word_task_queue: list[GenerationTask] = []
        self._example_task_queue: list[GenerationTask] = []
        self._active_task: GenerationTask | None = None
        self._current_record_index: int | None = None
        self._current_row_code = -1
        self._preserve_selected_row = False
        self._queue_running = False
        self._record_page = 0
        self._record_items_per_page = 1
        self._next_task_id = 1
        self._worker_thread: QThread | None = None
        self._worker: GenerationWorker | None = None
        self._prompt_worker_thread: QThread | None = None
        self._prompt_worker: PromptGenerationWorker | None = None
        self._prompt_generation_target: tuple[int, int] | None = None
        self._prompt_generation_started_at: float | None = None
        self._prompt_timer = QTimer(self)
        self._prompt_timer.setInterval(1000)
        self._prompt_timer.timeout.connect(self._refresh_prompt_generation_ui)

        self._build_ui()
        self._refresh_task_queue_view()
        self._update_generate_buttons()
        self._load_default_state()

    def _build_ui(self) -> None:
        central_widget = QWidget(self)
        self.setCentralWidget(central_widget)

        main_layout = QVBoxLayout(central_widget)
        top_splitter = QSplitter(Qt.Horizontal, self)
        main_layout.addWidget(top_splitter)

        left_panel = QWidget(self)
        left_layout = QVBoxLayout(left_panel)
        left_layout.addWidget(self._build_config_group())
        left_layout.addWidget(self._build_record_group(), stretch=1)
        top_splitter.addWidget(left_panel)

        right_panel = QWidget(self)
        right_layout = QVBoxLayout(right_panel)
        right_layout.addWidget(self._build_task_queue_group(), stretch=1)
        right_layout.addWidget(self._build_preview_group(), stretch=1)
        top_splitter.addWidget(right_panel)
        top_splitter.setStretchFactor(0, 5)
        top_splitter.setStretchFactor(1, 5)

        self.setStatusBar(QStatusBar(self))
        self.statusBar().showMessage("请选择 JSON 并加载 record")

        reload_action = QAction("重新加载 JSON", self)
        reload_action.triggered.connect(self.load_records)
        self.addAction(reload_action)

    def _build_config_group(self) -> QGroupBox:
        group = QGroupBox("文件与生成", self)
        layout = QFormLayout(group)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setHorizontalSpacing(6)
        layout.setVerticalSpacing(4)

        self.json_path_edit = QLineEdit(str(DEFAULT_JSON_PATH), group)
        json_row = QHBoxLayout()
        json_row.addWidget(self.json_path_edit)
        json_browse_button = QPushButton("选择", group)
        json_browse_button.clicked.connect(self.choose_json_file)
        json_row.addWidget(json_browse_button)
        json_row_widget = QWidget(group)
        json_row_widget.setLayout(json_row)
        layout.addRow("JSON 文件", json_row_widget)

        output_hint = QLabel(
            "释义图输出到 words，例句图输出到 example",
            group,
        )
        output_hint.setWordWrap(True)
        output_hint.setStyleSheet("color: #666; font-size: 11px;")
        layout.addRow("输出位置", output_hint)

        button_row = QHBoxLayout()
        self.load_button = QPushButton("加载 Record", group)
        self.load_button.clicked.connect(self.load_records)
        button_row.addWidget(self.load_button)

        self.generate_meaning_button = QPushButton("生成当前单词释义提示词", group)
        self.generate_meaning_button.clicked.connect(self.regenerate_current_meaning_prompt)
        button_row.addWidget(self.generate_meaning_button)

        self.generate_example_button = QPushButton("生成当前单词例句提示词", group)
        self.generate_example_button.clicked.connect(self.regenerate_current_example_prompt)
        button_row.addWidget(self.generate_example_button)
        button_row_widget = QWidget(group)
        button_row_widget.setLayout(button_row)
        layout.addRow("", button_row_widget)

        group.setMaximumHeight(132)

        return group

    def _build_task_queue_group(self) -> QGroupBox:
        group = QGroupBox("任务队列", self)
        layout = QVBoxLayout(group)

        self.queue_status_label = QLabel("队列已停止，当前无执行中的任务", group)
        self.queue_status_label.setWordWrap(True)
        layout.addWidget(self.queue_status_label)

        queue_control_row = QHBoxLayout()
        self.start_queue_button = QPushButton("开始任务队列", group)
        self.start_queue_button.clicked.connect(self.start_task_queue)
        queue_control_row.addWidget(self.start_queue_button)
        self.stop_queue_button = QPushButton("停止任务队列", group)
        self.stop_queue_button.clicked.connect(self.stop_task_queue)
        queue_control_row.addWidget(self.stop_queue_button)
        queue_control_widget = QWidget(group)
        queue_control_widget.setLayout(queue_control_row)
        layout.addWidget(queue_control_widget)

        queue_splitter = QSplitter(Qt.Vertical, group)

        word_queue_group = QGroupBox("单词任务队列", group)
        word_queue_layout = QVBoxLayout(word_queue_group)
        self.word_task_queue_list = QListWidget(group)
        word_queue_layout.addWidget(self.word_task_queue_list)
        word_button_row = QHBoxLayout()
        self.remove_word_task_button = QPushButton("移除选中单词任务", group)
        self.remove_word_task_button.clicked.connect(self.remove_selected_word_task)
        word_button_row.addWidget(self.remove_word_task_button)
        self.clear_word_queue_button = QPushButton("清空单词队列", group)
        self.clear_word_queue_button.clicked.connect(self.clear_word_tasks)
        word_button_row.addWidget(self.clear_word_queue_button)
        word_button_widget = QWidget(group)
        word_button_widget.setLayout(word_button_row)
        word_queue_layout.addWidget(word_button_widget)
        queue_splitter.addWidget(word_queue_group)

        example_queue_group = QGroupBox("例句任务队列", group)
        example_queue_layout = QVBoxLayout(example_queue_group)
        self.example_task_queue_list = QListWidget(group)
        example_queue_layout.addWidget(self.example_task_queue_list)
        example_button_row = QHBoxLayout()
        self.remove_example_task_button = QPushButton("移除选中例句任务", group)
        self.remove_example_task_button.clicked.connect(self.remove_selected_example_task)
        example_button_row.addWidget(self.remove_example_task_button)
        self.clear_example_queue_button = QPushButton("清空例句队列", group)
        self.clear_example_queue_button.clicked.connect(self.clear_example_tasks)
        example_button_row.addWidget(self.clear_example_queue_button)
        example_button_widget = QWidget(group)
        example_button_widget.setLayout(example_button_row)
        example_queue_layout.addWidget(example_button_widget)
        queue_splitter.addWidget(example_queue_group)

        queue_splitter.setStretchFactor(0, 1)
        queue_splitter.setStretchFactor(1, 1)
        layout.addWidget(queue_splitter)

        return group

    def _build_record_group(self) -> QGroupBox:
        group = QGroupBox("Record 选择", self)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(4, 2, 4, 4)
        layout.setSpacing(2)

        self.record_list = QListWidget(group)
        self.record_list.setSpacing(12)
        self.record_list.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.record_list.currentItemChanged.connect(self.on_record_selection_changed)
        self.record_list.viewport().installEventFilter(self)
        self.record_list.setMinimumHeight(self._record_list_height())
        self.record_list.setMaximumHeight(self._record_list_height())
        layout.addWidget(self.record_list, stretch=1)

        self.record_summary_label = QLabel("尚未加载数据", group)
        self.record_summary_label.setAlignment(Qt.AlignCenter)
        self.record_summary_label.setMinimumHeight(18)
        self.record_summary_label.setMaximumHeight(18)
        self.record_summary_label.setStyleSheet("color: #666; font-size: 11px;")
        layout.addWidget(self.record_summary_label)

        pager_row = QHBoxLayout()
        pager_row.setContentsMargins(0, 0, 0, 0)
        pager_row.setSpacing(4)
        self.record_prev_page_button = QPushButton("上一页", group)
        self.record_prev_page_button.setFixedHeight(24)
        self.record_prev_page_button.setMinimumWidth(56)
        self.record_prev_page_button.clicked.connect(self.show_previous_record_page)
        pager_row.addWidget(self.record_prev_page_button)
        self.record_next_page_button = QPushButton("下一页", group)
        self.record_next_page_button.setFixedHeight(24)
        self.record_next_page_button.setMinimumWidth(56)
        self.record_next_page_button.clicked.connect(self.show_next_record_page)
        pager_row.addWidget(self.record_next_page_button)
        pager_widget = QWidget(group)
        pager_widget.setLayout(pager_row)
        pager_widget.setMaximumHeight(28)
        layout.addWidget(pager_widget)

        return group

    def _build_preview_group(self) -> QGroupBox:
        group = QGroupBox("结果预览", self)
        layout = QVBoxLayout(group)

        layout.addWidget(QLabel("这里显示当前选中条目的已有图片，或最近完成的任务图片", group))

        self.image_preview_label = QLabel("暂无图片", group)
        self.image_preview_label.setAlignment(Qt.AlignCenter)
        self.image_preview_label.setMinimumHeight(360)
        self.image_preview_label.setStyleSheet("border: 1px solid #999; background: #fff;")
        layout.addWidget(self.image_preview_label)

        self.result_info_edit = QPlainTextEdit(group)
        self.result_info_edit.setReadOnly(True)
        self.result_info_edit.setPlaceholderText("生成完成后，这里会显示输出路径和请求信息。")
        layout.addWidget(self.result_info_edit)

        return group

    def _load_default_state(self) -> None:
        return

    @Slot()
    def choose_json_file(self) -> None:
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 JSON 文件",
            str(DEFAULT_JSON_PATH.parent),
            "JSON Files (*.json)",
        )
        if file_path:
            self.json_path_edit.setText(file_path)

    @Slot()
    def load_records(self) -> None:
        try:
            json_path = Path(self.json_path_edit.text().strip())
            self._records = self._repository.load(json_path)
        except Exception as exc:
            QMessageBox.critical(self, "加载失败", str(exc))
            return
        self._record_page = 0
        self._current_record_index = 0 if self._records else None
        self._current_row_code = -1
        self.record_summary_label.setText(f"共加载 {len(self._records)} 条记录")
        self.statusBar().showMessage(f"已加载 {len(self._records)} 条记录")
        self._filtered_record_indices = list(range(len(self._records)))
        self._record_items_per_page = self._calculate_record_items_per_page()
        self._ensure_record_page_valid()
        self._render_record_page()
        self._update_generate_buttons()
        self._refresh_task_queue_view()

    @Slot(QListWidgetItem, QListWidgetItem)
    def on_record_selection_changed(
        self,
        current: QListWidgetItem | None,
        previous: QListWidgetItem | None,
    ) -> None:
        del previous
        if current is None:
            self._current_record_index = None
            self._current_row_code = -1
            self._clear_preview("暂无图片")
            self._update_generate_buttons()
            return

        previous_record_index = self._current_record_index
        self._current_record_index = int(current.data(Qt.UserRole))
        if not self._preserve_selected_row and previous_record_index != self._current_record_index:
            self._current_row_code = -1
        self._refresh_visible_record_item_states()
        self._update_preview_for_current_selection()
        self._update_generate_buttons()

    @Slot(QListWidgetItem, QListWidgetItem)
    def on_example_selection_changed(
        self,
        current: QListWidgetItem | None,
        previous: QListWidgetItem | None,
    ) -> None:
        del current, previous
        self._update_generate_buttons()

    @Slot()
    def refresh_prompt_preview(self) -> None:
        self._update_generate_buttons()

    @Slot()
    def queue_current_meaning_image(self) -> None:
        record = self.current_record()
        if record is None:
            QMessageBox.warning(self, "未选择 Record", "请先在左侧选择一个 record。")
            return

        try:
            prompt = self._build_meaning_prompt(record)
        except Exception as exc:
            QMessageBox.warning(self, "提示词错误", str(exc))
            return

        if not prompt:
            QMessageBox.warning(self, "提示词为空", "当前单词释义图片提示词为空。")
            return

        self._enqueue_tasks([self._build_meaning_task(record, prompt)])

    @Slot()
    def queue_current_example_image(self) -> None:
        record = self.current_record()
        if record is None:
            QMessageBox.warning(self, "未选择 Record", "请先在左侧选择一个 record。")
            return

        example = self.current_example()
        if example is None:
            QMessageBox.warning(self, "未选择例句", "请先选择要生成图片的例句。")
            return

        try:
            prompt = self._build_example_prompt(record, example)
        except Exception as exc:
            QMessageBox.warning(self, "提示词错误", str(exc))
            return

        if not prompt:
            QMessageBox.warning(self, "提示词为空", "当前单词例句图片提示词为空。")
            return

        self._enqueue_tasks([self._build_example_task(record, example, prompt)])

    @Slot()
    def regenerate_current_meaning_prompt(self) -> None:
        record = self.current_record()
        if record is None:
            QMessageBox.warning(self, "未选择 Record", "请先在左侧选择一个 record。")
            return
        self._start_prompt_generation(self._current_record_index, -1)

    @Slot()
    def regenerate_current_example_prompt(self) -> None:
        record = self.current_record()
        if record is None:
            QMessageBox.warning(self, "未选择 Record", "请先在左侧选择一个 record。")
            return
        example = self.current_example()
        if example is None:
            QMessageBox.warning(self, "未选择例句", "请先选择要生成提示词的例句。")
            return
        del example
        self._start_prompt_generation(self._current_record_index, self._current_row_code)

    def _enqueue_tasks(self, tasks: list[GenerationTask]) -> None:
        if not tasks:
            return

        for task in tasks:
            target_queue = self._word_task_queue if task.task_type == "word_meaning" else self._example_task_queue
            target_queue.append(task)

        self._refresh_task_queue_view()
        if len(tasks) == 1:
            summary_text = f"已加入任务队列: #{tasks[0].task_id} {tasks[0].display_text}"
        else:
            task_ids = ", ".join(f"#{task.task_id}" for task in tasks)
            summary_text = f"已加入 {len(tasks)} 个任务: {task_ids}"
        self.result_info_edit.setPlainText(
            f"{summary_text}\n当前等待任务数: {self._pending_task_count()}\n点击“开始任务队列”后执行。"
        )

        if self._queue_running and self._worker_thread is None:
            self._start_next_queued_task()
        elif self._queue_running:
            self.statusBar().showMessage(
                f"已加入队列，当前任务完成后继续执行。等待任务数: {self._pending_task_count()}"
            )
        else:
            self.statusBar().showMessage(
                f"已加入队列，等待任务数: {self._pending_task_count()}。点击“开始任务队列”后执行。"
            )

    @Slot()
    def start_task_queue(self) -> None:
        if self._pending_task_count() == 0 and self._worker_thread is None:
            self.statusBar().showMessage("当前没有可执行的队列任务")
            self._refresh_task_queue_view()
            return

        self._queue_running = True
        self._refresh_task_queue_view()
        self._update_generate_buttons()
        if self._worker_thread is None:
            self._start_next_queued_task()
        else:
            self.statusBar().showMessage("当前任务完成后继续执行后续队列")

    @Slot()
    def stop_task_queue(self) -> None:
        self._queue_running = False
        self._refresh_task_queue_view()
        self._update_generate_buttons()
        if self._worker_thread is None:
            self.statusBar().showMessage("任务队列已停止")
        else:
            self.statusBar().showMessage("已停止后续队列任务，当前任务完成后不再继续")

    def _start_next_queued_task(self) -> None:
        if self._worker_thread is not None:
            self._refresh_task_queue_view()
            return

        if not self._queue_running:
            self._refresh_task_queue_view()
            self._update_generate_buttons()
            return

        task = self._pop_next_task()
        if task is None:
            self._queue_running = False
            self._refresh_task_queue_view()
            self._update_generate_buttons()
            return
        self._active_task = task
        self._refresh_task_queue_view()

        self._worker_thread = QThread(self)
        self._worker = GenerationWorker(
            client=self._client,
            exporter=self._exporter,
            record=task.record,
            prompt=task.prompt,
            api_key=self._resolve_api_key(),
            api_url=DEFAULT_API_URL,
            model=DEFAULT_MODEL,
            extra_body_text="",
            file_stem=task.file_stem,
            metadata=task.metadata,
            output_dir=task.output_dir,
        )
        self._worker.moveToThread(self._worker_thread)
        self._worker_thread.started.connect(self._worker.run)
        self._worker.finished.connect(self.on_generation_finished)
        self._worker.failed.connect(self.on_generation_failed)
        self._worker_thread.start()

        self.statusBar().showMessage(f"{task.status_text} 队列剩余 {self._pending_task_count()} 项")
        self._update_generate_buttons()

    @Slot(dict)
    def on_generation_finished(self, payload: dict[str, Any]) -> None:
        image_bytes = payload["image_bytes"]
        paths = payload["paths"]
        completed_task = self._active_task

        image = QImage.fromData(image_bytes)
        if not image.isNull():
            pixmap = QPixmap.fromImage(image).scaled(
                self.image_preview_label.size(),
                Qt.KeepAspectRatio,
                Qt.SmoothTransformation,
            )
            self.image_preview_label.setPixmap(pixmap)

        info_lines = ["生成完成:"]
        if completed_task is not None:
            info_lines.append(f"任务: #{completed_task.task_id} {completed_task.display_text}")
        for name, path in paths.items():
            info_lines.append(f"{name}: {path}")
        existing_text = self.result_info_edit.toPlainText().strip()
        new_entry = "\n".join(info_lines)
        if existing_text:
            self.result_info_edit.setPlainText(f"{existing_text}\n\n{new_entry}")
        else:
            self.result_info_edit.setPlainText(new_entry)
        self.statusBar().showMessage(
            f"生成完成，已输出到 {paths['bin']}。队列剩余 {self._pending_task_count()} 项"
        )
        self._active_task = None
        self._render_record_page()
        self._update_preview_for_current_selection()
        self._cleanup_worker()
        if self._queue_running:
            self._start_next_queued_task()

    @Slot(str)
    def on_generation_failed(self, error_text: str) -> None:
        failed_task = self._active_task
        QMessageBox.critical(self, "生成失败", error_text)
        self._queue_running = False
        self._active_task = None
        self._cleanup_worker()
        self._refresh_task_queue_view()
        if failed_task is None:
            self.statusBar().showMessage("生成失败")
            return
        self.statusBar().showMessage(
            f"任务 #{failed_task.task_id} 生成失败，剩余 {self._pending_task_count()} 项等待手动处理"
        )

    @Slot()
    def _cleanup_worker(self) -> None:
        if self._worker_thread is not None:
            self._worker_thread.quit()
            self._worker_thread.wait()
            self._worker_thread.deleteLater()
        if self._worker is not None:
            self._worker.deleteLater()
        self._worker_thread = None
        self._worker = None
        self._update_generate_buttons()
        self._refresh_task_queue_view()

    @Slot()
    def _cleanup_prompt_worker(self) -> None:
        if self._prompt_worker_thread is not None:
            self._prompt_worker_thread.quit()
            self._prompt_worker_thread.wait()
            self._prompt_worker_thread.deleteLater()
        if self._prompt_worker is not None:
            self._prompt_worker.deleteLater()
        self._prompt_worker_thread = None
        self._prompt_worker = None
        self._prompt_generation_target = None
        self._prompt_generation_started_at = None
        self._prompt_timer.stop()
        self._refresh_prompt_generation_ui()
        self._update_generate_buttons()
        self._refresh_task_queue_view()

    @Slot()
    def remove_selected_word_task(self) -> None:
        current_item = self.word_task_queue_list.currentItem()
        if current_item is None:
            return

        task_id = int(current_item.data(Qt.UserRole))
        self._word_task_queue = [task for task in self._word_task_queue if task.task_id != task_id]
        self._refresh_task_queue_view()
        self.statusBar().showMessage(f"已移除队列任务 #{task_id}")

    @Slot()
    def clear_word_tasks(self) -> None:
        cleared_count = len(self._word_task_queue)
        self._word_task_queue.clear()
        self._refresh_task_queue_view()
        self.statusBar().showMessage(f"已清空 {cleared_count} 个单词任务")

    @Slot()
    def remove_selected_example_task(self) -> None:
        current_item = self.example_task_queue_list.currentItem()
        if current_item is None:
            return

        task_id = int(current_item.data(Qt.UserRole))
        self._example_task_queue = [task for task in self._example_task_queue if task.task_id != task_id]
        self._refresh_task_queue_view()
        self.statusBar().showMessage(f"已移除例句任务 #{task_id}")

    @Slot()
    def clear_example_tasks(self) -> None:
        cleared_count = len(self._example_task_queue)
        self._example_task_queue.clear()
        self._refresh_task_queue_view()
        self.statusBar().showMessage(f"已清空 {cleared_count} 个例句任务")

    def current_record(self) -> WordRecord | None:
        if self._current_record_index is None:
            return None
        return self._records[self._current_record_index]

    def current_example(self) -> ExampleRecord | None:
        record = self.current_record()
        if record is None or self._current_row_code < 0 or self._current_row_code >= len(record.examples):
            return None
        return record.examples[self._current_row_code]

    def _record_from_item(self, item: QListWidgetItem) -> WordRecord:
        index = int(item.data(Qt.UserRole))
        return self._records[index]

    def _example_from_item(self, item: QListWidgetItem) -> ExampleRecord:
        record = self.current_record()
        if record is None:
            raise ValueError("当前没有选中的 record")
        index = int(item.data(Qt.UserRole))
        return record.examples[index]

    def _populate_examples(self, record: WordRecord | None) -> None:
        del record

    def _format_selected_example_text(self, example: ExampleRecord) -> str:
        lines = [
            f"例句序号: {example.list_index}",
            f"Example ID: {example.example_id if example.example_id is not None else f'idx{example.list_index}'}",
            f"英文例句: {example.example_en or '-'}",
            f"中文例句: {example.example_zh or '-'}",
            f"标签: {example.example_tag or '-'}",
            f"难度: {example.difficulty if example.difficulty is not None else '-'}",
        ]
        if example.image_prompt:
            lines.append(f"例句图片提示词: {example.image_prompt}")
        lines.append("")
        lines.append("完整字段:")
        lines.append(self._format_json_payload(example.raw))
        return "\n".join(lines)

    def _update_generate_buttons(self) -> None:
        has_record = self.current_record() is not None
        active_target = self._prompt_generation_target
        current_record_index = self._current_record_index
        meaning_busy = active_target is not None and active_target == (current_record_index, -1)
        example_busy = active_target is not None and active_target == (current_record_index, self._current_row_code)
        self.generate_meaning_button.setEnabled(has_record and not meaning_busy)
        self.generate_example_button.setEnabled(self.current_example() is not None and not example_busy)
        self.start_queue_button.setEnabled(self._pending_task_count() > 0 and not self._queue_running)
        self.stop_queue_button.setEnabled(self._queue_running)
        self.remove_word_task_button.setEnabled(bool(self._word_task_queue))
        self.clear_word_queue_button.setEnabled(bool(self._word_task_queue))
        self.remove_example_task_button.setEnabled(bool(self._example_task_queue))
        self.clear_example_queue_button.setEnabled(bool(self._example_task_queue))

    def _resolve_api_key(self) -> str:
        return str(os.environ.get("ZHIPU_API_KEY") or DEFAULT_API_KEY).strip()

    def _build_meaning_file_stem(self, record: WordRecord) -> str:
        return _build_meaning_file_stem(record)

    def _build_example_file_stem(self, record: WordRecord, example: ExampleRecord) -> str:
        return _build_example_file_stem(record, example)

    def _format_selected_record_text(self, record: WordRecord) -> str:
        lines = [
            f"序号: {record.list_index}",
            f"Word ID: {record.word_id if record.word_id is not None else '-'}",
            f"Meaning ID: {record.meaning_id if record.meaning_id is not None else '-'}",
            f"单词: {record.word or '-'}",
            f"音标: {record.phonetic or '-'}",
            f"词性: {record.pos or '-'}",
            f"中文释义: {record.meaning_zh or '-'}",
            f"英文释义: {record.meaning_en or '-'}",
            f"分类: {record.word_tag or '-'}",
            f"例句数量: {len(record.examples)}",
        ]
        if record.image_hint:
            lines.append(f"现有图像提示: {record.image_hint}")
        lines.append("")
        lines.append("完整字段:")
        lines.append(self._format_json_payload(record.raw))
        return "\n".join(lines)

    def _allocate_task_id(self) -> int:
        task_id = self._next_task_id
        self._next_task_id += 1
        return task_id

    def _build_meaning_prompt(self, record: WordRecord) -> str:
        return self._prompt_builder.build(
            record,
            DEFAULT_FIXED_PROMPT,
            record.image_hint or DEFAULT_VARIABLE_TEMPLATE,
        ).strip()

    def _build_example_prompt(self, record: WordRecord, example: ExampleRecord) -> str:
        return self._prompt_builder.build(
            record,
            DEFAULT_FIXED_PROMPT,
            example.image_prompt or DEFAULT_VARIABLE_TEMPLATE,
        ).strip()

    def _build_meaning_task(self, record: WordRecord, prompt: str) -> GenerationTask:
        return GenerationTask(
            task_id=self._allocate_task_id(),
            task_type="word_meaning",
            display_text=f"单词释义 | {record.word} | meaning_id={record.meaning_id if record.meaning_id is not None else '-'}",
            record=record,
            prompt=prompt,
            output_dir=DEFAULT_WORDS_OUTPUT_DIR,
            file_stem=self._build_meaning_file_stem(record),
            metadata={"generation_type": "word_meaning", "example": None},
            status_text=f"正在生成 {record.word} 的释义图片...",
        )

    def _build_example_task(self, record: WordRecord, example: ExampleRecord, prompt: str) -> GenerationTask:
        return GenerationTask(
            task_id=self._allocate_task_id(),
            task_type="word_example",
            display_text=(
                f"单词例句 | {record.word} | example_id="
                f"{example.example_id if example.example_id is not None else f'idx{example.list_index}'}"
            ),
            record=record,
            prompt=prompt,
            output_dir=DEFAULT_EXAMPLES_OUTPUT_DIR,
            file_stem=self._build_example_file_stem(record, example),
            metadata={
                "generation_type": "word_example",
                "example": {
                    "example_id": example.example_id,
                    "meaning_id": example.meaning_id,
                    "example_en": example.example_en,
                    "example_zh": example.example_zh,
                    "example_tag": example.example_tag,
                },
            },
            status_text=f"正在生成 {record.word} 的例句图片...",
        )

    def _calculate_record_items_per_page(self) -> int:
        return RECORDS_PER_PAGE

    def _record_list_height(self) -> int:
        if not hasattr(self, "record_list"):
            return (RECORD_LIST_ITEM_HEIGHT + 12) * RECORDS_PER_PAGE + 100
        row_height = RECORD_LIST_ITEM_HEIGHT + 12
        spacing = self.record_list.spacing()
        frame = self.record_list.frameWidth() * 2
        return row_height * RECORDS_PER_PAGE + spacing * max(0, RECORDS_PER_PAGE - 1) + frame + 100

    def _current_prompt_elapsed_seconds(self) -> int | None:
        if self._prompt_generation_started_at is None:
            return None
        return max(0, int(time.monotonic() - self._prompt_generation_started_at))

    @Slot()
    def _refresh_prompt_generation_ui(self) -> None:
        active_target = self._prompt_generation_target
        elapsed_seconds = self._current_prompt_elapsed_seconds()

        meaning_text = "生成当前单词释义提示词"
        example_text = "生成当前单词例句提示词"
        if active_target is not None and elapsed_seconds is not None and self._current_record_index is not None:
            if active_target == (self._current_record_index, -1):
                meaning_text = f"生成中 {elapsed_seconds}s"
            if active_target == (self._current_record_index, self._current_row_code):
                example_text = f"生成中 {elapsed_seconds}s"

        self.generate_meaning_button.setText(meaning_text)
        self.generate_example_button.setText(example_text)

        for row in range(self.record_list.count()):
            item = self.record_list.item(row)
            widget = self.record_list.itemWidget(item)
            if not isinstance(widget, RecordListItemWidget):
                continue
            record_index = int(item.data(Qt.UserRole))
            if active_target is not None and record_index == active_target[0]:
                widget.set_prompt_generation_state(active_target[1], elapsed_seconds)
            else:
                widget.set_prompt_generation_state(None, None)

    def _max_record_page(self) -> int:
        if not self._filtered_record_indices:
            return 0
        return max(0, (len(self._filtered_record_indices) - 1) // self._record_items_per_page)

    def _ensure_record_page_valid(self) -> None:
        if not self._filtered_record_indices:
            self._record_page = 0
            return

        self._record_page = min(self._record_page, self._max_record_page())
        if self._current_record_index in self._filtered_record_indices:
            selected_position = self._filtered_record_indices.index(self._current_record_index)
            self._record_page = selected_position // self._record_items_per_page

    def _current_page_record_indices(self) -> list[int]:
        start = self._record_page * self._record_items_per_page
        end = start + self._record_items_per_page
        return self._filtered_record_indices[start:end]

    def _render_record_page(self) -> None:
        self.record_list.blockSignals(True)
        self.record_list.clear()

        selected_item: QListWidgetItem | None = None
        for record_index in self._current_page_record_indices():
            record = self._records[record_index]
            item = QListWidgetItem()
            item.setData(Qt.UserRole, record_index)
            item.setSizeHint(QSize(0, RECORD_LIST_ITEM_HEIGHT + 12))
            self.record_list.addItem(item)

            selected_row_code = self._current_row_code if record_index == self._current_record_index else None
            item_widget = RecordListItemWidget(record_index, record, selected_row_code, self.record_list)
            item_widget.selected.connect(self._select_record_by_index)
            item_widget.rowSelected.connect(self._on_record_row_selected)
            item_widget.queueRequested.connect(self._queue_record_row_by_index)
            item_widget.deletePromptRequested.connect(self._delete_record_prompt_by_index)
            item_widget.promptGenerateRequested.connect(self._generate_record_prompt_by_index)
            self.record_list.setItemWidget(item, item_widget)

            if record_index == self._current_record_index:
                selected_item = item

        if selected_item is None and self.record_list.count() > 0:
            selected_item = self.record_list.item(0)
            self._current_record_index = int(selected_item.data(Qt.UserRole))

        if selected_item is not None:
            self.record_list.setCurrentItem(selected_item)

        self.record_list.blockSignals(False)
        self._update_record_pagination_controls()
        self.on_record_selection_changed(selected_item, None)
        self._refresh_prompt_generation_ui()

    def _refresh_visible_record_item_states(self) -> None:
        for row in range(self.record_list.count()):
            item = self.record_list.item(row)
            widget = self.record_list.itemWidget(item)
            if not isinstance(widget, RecordListItemWidget):
                continue
            record_index = int(item.data(Qt.UserRole))
            selected_row_code = self._current_row_code if record_index == self._current_record_index else None
            widget.set_selected_row_code(selected_row_code)

    def _update_record_pagination_controls(self) -> None:
        total_pages = self._max_record_page() + 1 if self._filtered_record_indices else 0
        current_page = self._record_page + 1 if total_pages else 0
        self.record_summary_label.setText(
            f"第 {current_page} / {total_pages} 页 | 共 {len(self._records)} 条 | 本页 {self.record_list.count()} 条"
        )
        self.record_prev_page_button.setEnabled(self._record_page > 0)
        self.record_next_page_button.setEnabled(self._record_page < self._max_record_page())

    def _select_record_by_index(self, record_index: int) -> None:
        if record_index not in self._filtered_record_indices:
            return

        self._current_record_index = record_index
        target_page = self._filtered_record_indices.index(record_index) // self._record_items_per_page
        if target_page != self._record_page:
            self._record_page = target_page
            self._render_record_page()
            return

        for row in range(self.record_list.count()):
            item = self.record_list.item(row)
            if int(item.data(Qt.UserRole)) == record_index:
                self.record_list.setCurrentItem(item)
                return

    @Slot(int, int)
    def _on_record_row_selected(self, record_index: int, row_code: int) -> None:
        self._current_row_code = row_code
        self._preserve_selected_row = True
        try:
            self._select_record_by_index(record_index)
        finally:
            self._preserve_selected_row = False
        self._refresh_visible_record_item_states()
        self._update_preview_for_current_selection()
        self._update_generate_buttons()

    def _queue_record_meaning_by_index(self, record_index: int) -> None:
        self._on_record_row_selected(record_index, -1)
        record = self._records[record_index]
        try:
            prompt = self._build_meaning_prompt(record)
        except Exception as exc:
            QMessageBox.warning(self, "提示词错误", str(exc))
            return

        if not prompt:
            QMessageBox.warning(self, "提示词为空", "当前单词释义图片提示词为空。")
            return
        self._enqueue_tasks([self._build_meaning_task(record, prompt)])

    def _queue_record_example_by_index(self, record_index: int, example_offset: int) -> None:
        self._on_record_row_selected(record_index, example_offset)
        record = self._records[record_index]
        if example_offset >= len(record.examples):
            QMessageBox.warning(self, "无可用例句", f"当前 record 没有例句{example_offset + 1}。")
            return

        example = record.examples[example_offset]
        try:
            prompt = self._build_example_prompt(record, example)
        except Exception as exc:
            QMessageBox.warning(self, "提示词错误", str(exc))
            return

        if not prompt:
            QMessageBox.warning(self, "提示词为空", f"当前例句{example_offset + 1}提示词为空。")
            return

        self._enqueue_tasks([self._build_example_task(record, example, prompt)])

    @Slot(int, int)
    def _queue_record_row_by_index(self, record_index: int, row_code: int) -> None:
        if row_code == -1:
            self._queue_record_meaning_by_index(record_index)
            return
        self._queue_record_example_by_index(record_index, row_code)

    @Slot(int, int)
    def _generate_record_prompt_by_index(self, record_index: int, row_code: int) -> None:
        self._on_record_row_selected(record_index, row_code)
        self._start_prompt_generation(record_index, row_code)

    @Slot(int, int)
    def _delete_record_prompt_by_index(self, record_index: int, row_code: int) -> None:
        self._on_record_row_selected(record_index, row_code)
        record = self._records[record_index]

        try:
            if row_code == -1:
                if not record.image_hint.strip():
                    self.statusBar().showMessage("当前释义提示词已经为空")
                    return
                self._update_record_prompt_in_json(record_index, row_code, "")
                record.image_hint = ""
                meaning_data = record.raw.get("word_meaning")
                if isinstance(meaning_data, dict):
                    meaning_data["image"] = ""
                self.statusBar().showMessage(f"已删除 {record.word or '-'} 的释义提示词")
            else:
                if row_code >= len(record.examples):
                    return
                example = record.examples[row_code]
                if not example.image_prompt.strip():
                    self.statusBar().showMessage(f"当前例句{row_code + 1}提示词已经为空")
                    return
                self._update_record_prompt_in_json(record_index, row_code, "")
                example.image_prompt = ""
                example.raw["image"] = ""
                self.statusBar().showMessage(f"已删除 {record.word or '-'} 的例句{row_code + 1}提示词")
        except Exception as exc:
            QMessageBox.critical(self, "删除提示词失败", str(exc))
            return

        self._render_record_page()
        self._update_preview_for_current_selection()

    def _update_record_prompt_in_json(self, record_index: int, row_code: int, prompt_text: str) -> None:
        json_path = Path(self.json_path_edit.text().strip())
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list) or record_index >= len(records):
            raise ValueError("当前 JSON 记录索引无效，无法更新 image 字段")

        record_payload = records[record_index]
        if not isinstance(record_payload, dict):
            raise ValueError("当前 JSON 记录格式无效，无法更新 image 字段")

        if row_code == -1:
            meaning_payload = record_payload.get("word_meaning")
            if not isinstance(meaning_payload, dict):
                raise ValueError("当前记录缺少 word_meaning，无法更新 image 字段")
            meaning_payload["image"] = prompt_text
        else:
            example_payloads = record_payload.get("word_example")
            if not isinstance(example_payloads, list) or row_code >= len(example_payloads):
                raise ValueError("当前记录缺少对应例句，无法更新 image 字段")
            example_payload = example_payloads[row_code]
            if not isinstance(example_payload, dict):
                raise ValueError("当前例句格式无效，无法更新 image 字段")
            example_payload["image"] = prompt_text

        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _start_prompt_generation(self, record_index: int | None, row_code: int) -> None:
        if record_index is None:
            return
        if self._prompt_worker_thread is not None:
            QMessageBox.information(self, "提示词生成中", "当前已有提示词生成任务，请等待完成后再试。")
            return

        record = self._records[record_index]
        if row_code >= 0 and row_code >= len(record.examples):
            QMessageBox.warning(self, "无可用例句", f"当前 record 没有例句{row_code + 1}。")
            return

        system_prompt, user_prompt = self._build_prompt_generation_messages(record, row_code)
        target_text = f"{record.word or '-'} 释义" if row_code == -1 else f"{record.word or '-'} 例句{row_code + 1}"
        self.result_info_edit.setPlainText(f"正在用 GLM-5 生成 {target_text} 的提示词，请稍候...")
        self.statusBar().showMessage(f"正在用 GLM-5 生成 {target_text} 的提示词...")
        self._prompt_generation_target = (record_index, row_code)
        self._prompt_generation_started_at = time.monotonic()
        self._refresh_prompt_generation_ui()
        self._prompt_timer.start()

        self._prompt_worker_thread = QThread(self)
        self._prompt_worker = PromptGenerationWorker(
            client=self._prompt_client,
            api_key=self._resolve_api_key(),
            api_url=DEFAULT_PROMPT_API_URL,
            model=DEFAULT_PROMPT_MODEL,
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            record_index=record_index,
            row_code=row_code,
        )
        self._prompt_worker.moveToThread(self._prompt_worker_thread)
        self._prompt_worker_thread.started.connect(self._prompt_worker.run)
        self._prompt_worker.finished.connect(self.on_prompt_generation_finished)
        self._prompt_worker.failed.connect(self.on_prompt_generation_failed)
        self._prompt_worker.finished.connect(self._prompt_worker_thread.quit)
        self._prompt_worker.failed.connect(self._prompt_worker_thread.quit)
        self._prompt_worker_thread.start()
        self._update_generate_buttons()

    def _build_prompt_generation_messages(self, record: WordRecord, row_code: int) -> tuple[str, str]:
        system_prompt = (
            "你是英语教学插画提示词生成助手。"
            "请根据给定单词释义或例句，生成一段适合图片生成模型使用的主体内容提示词。"
            "只返回最终提示词正文，不要解释、不要标题、不要编号、不要 Markdown、不要引号。"
            "不要写风格、分辨率、黑白、墨水屏、卡通等通用画风要求，这些已有固定模板处理。"
            "提示词要聚焦可直接画出的具体主体、动作、场景和关键物体，含义直观，适合儿童英语教学。"
        )
        if row_code == -1:
            user_prompt = (
                "请为下面这个英语单词释义生成图片提示词变量部分。\n"
                f"单词: {record.word or '-'}\n"
                f"词性: {record.pos or '-'}\n"
                f"中文释义: {record.meaning_zh or '-'}\n"
                f"英文释义: {record.meaning_en or '-'}\n"
                f"现有提示词: {record.image_hint or '-'}\n"
                "要求: 直接输出 1 到 2 句中文提示词，重点描述最适合表达该释义的场景。"
            )
            return system_prompt, user_prompt

        example = record.examples[row_code]
        user_prompt = (
            "请为下面这个英语单词例句生成图片提示词变量部分。\n"
            f"单词: {record.word or '-'}\n"
            f"词性: {record.pos or '-'}\n"
            f"中文释义: {record.meaning_zh or '-'}\n"
            f"英文释义: {record.meaning_en or '-'}\n"
            f"英文例句: {example.example_en or '-'}\n"
            f"中文例句: {example.example_zh or '-'}\n"
            f"现有提示词: {example.image_prompt or '-'}\n"
            "要求: 直接输出 1 到 2 句中文提示词，优先表现例句里的核心动作和对象，画面一眼能看懂。"
        )
        return system_prompt, user_prompt

    @Slot(dict)
    def on_prompt_generation_finished(self, payload: dict[str, Any]) -> None:
        record_index = int(payload["record_index"])
        row_code = int(payload["row_code"])
        prompt_text = str(payload["prompt_text"]).strip()
        record = self._records[record_index]

        try:
            self._update_record_prompt_in_json(record_index, row_code, prompt_text)
            if row_code == -1:
                record.image_hint = prompt_text
                meaning_payload = record.raw.get("word_meaning")
                if isinstance(meaning_payload, dict):
                    meaning_payload["image"] = prompt_text
                target_text = f"{record.word or '-'} 释义"
            else:
                example = record.examples[row_code]
                example.image_prompt = prompt_text
                example.raw["image"] = prompt_text
                target_text = f"{record.word or '-'} 例句{row_code + 1}"
        except Exception as exc:
            QMessageBox.critical(self, "提示词写入失败", str(exc))
            return

        request_payload = payload["request_payload"]
        response_payload = payload["response_payload"]
        self.result_info_edit.setPlainText(
            "\n".join(
                [
                    f"已更新 {target_text} 的提示词:",
                    prompt_text,
                    "",
                    "request_payload:",
                    json.dumps(request_payload, ensure_ascii=False, indent=2),
                    "",
                    "response_payload:",
                    json.dumps(response_payload, ensure_ascii=False, indent=2),
                ]
            )
        )
        self.statusBar().showMessage(f"已用 GLM-5 更新 {target_text} 的提示词，并写回 JSON")
        self._render_record_page()
        self._update_preview_for_current_selection()
        self._refresh_prompt_generation_ui()
        self._update_generate_buttons()
        self._cleanup_prompt_worker()

    @Slot(str)
    def on_prompt_generation_failed(self, error_text: str) -> None:
        self.result_info_edit.setPlainText(f"生成提示词失败:\n{error_text}")
        QMessageBox.critical(self, "生成提示词失败", error_text)
        self.statusBar().showMessage("生成提示词失败")
        self._refresh_prompt_generation_ui()
        self._update_generate_buttons()
        self._cleanup_prompt_worker()

    def _update_preview_for_current_selection(self) -> None:
        record = self.current_record()
        if record is None:
            self._clear_preview("暂无图片")
            return

        preview_path: Path | None
        placeholder: str
        if self._current_row_code == -1:
            preview_path = _resolve_existing_preview_path(
                DEFAULT_WORDS_OUTPUT_DIR,
                self._build_meaning_file_stem(record),
            )
            placeholder = f"{record.word or '-'} 释义\n暂无已生成图片"
        elif 0 <= self._current_row_code < len(record.examples):
            example = record.examples[self._current_row_code]
            preview_path = _resolve_existing_preview_path(
                DEFAULT_EXAMPLES_OUTPUT_DIR,
                self._build_example_file_stem(record, example),
            )
            placeholder = f"{record.word or '-'} 例句{self._current_row_code + 1}\n暂无已生成图片"
        else:
            self._clear_preview("暂无图片")
            return

        if preview_path is None:
            self._clear_preview(placeholder)
            return

        pixmap = QPixmap(str(preview_path))
        if pixmap.isNull():
            self._clear_preview(placeholder)
            return

        scaled = pixmap.scaled(
            self.image_preview_label.size(),
            Qt.KeepAspectRatio,
            Qt.SmoothTransformation,
        )
        self.image_preview_label.setText("")
        self.image_preview_label.setPixmap(scaled)

    def _clear_preview(self, text: str) -> None:
        self.image_preview_label.clear()
        self.image_preview_label.setText(text)

    @Slot()
    def show_previous_record_page(self) -> None:
        if self._record_page <= 0:
            return
        self._record_page -= 1
        page_indices = self._current_page_record_indices()
        if page_indices and self._current_record_index not in page_indices:
            self._current_record_index = page_indices[0]
        self._render_record_page()

    @Slot()
    def show_next_record_page(self) -> None:
        if self._record_page >= self._max_record_page():
            return
        self._record_page += 1
        page_indices = self._current_page_record_indices()
        if page_indices and self._current_record_index not in page_indices:
            self._current_record_index = page_indices[0]
        self._render_record_page()

    def _refresh_task_queue_view(self) -> None:
        self.word_task_queue_list.clear()
        for task in self._word_task_queue:
            item = QListWidgetItem(f"#{task.task_id} [等待] {task.display_text}")
            item.setData(Qt.UserRole, task.task_id)
            self.word_task_queue_list.addItem(item)

        self.example_task_queue_list.clear()
        for task in self._example_task_queue:
            item = QListWidgetItem(f"#{task.task_id} [等待] {task.display_text}")
            item.setData(Qt.UserRole, task.task_id)
            self.example_task_queue_list.addItem(item)

        state_text = "队列已停止"
        if self._queue_running:
            state_text = "队列运行中"
        elif self._active_task is not None:
            state_text = "当前任务执行中，后续队列已停止"

        active_text = "当前无执行中的任务"
        if self._active_task is not None:
            active_text = f"执行中: #{self._active_task.task_id} {self._active_task.display_text}"

        pending_text = (
            f"单词队列: {len(self._word_task_queue)} 项    例句队列: {len(self._example_task_queue)} 项"
        )
        hint_text = "点击“开始任务队列”后执行"
        if self._queue_running:
            hint_text = "队列会按顺序持续执行"
        elif self._active_task is None and self._pending_task_count() == 0:
            hint_text = "队列为空"
        self.queue_status_label.setText(f"{state_text}\n{active_text}\n{pending_text}\n{hint_text}")

        if self.word_task_queue_list.count() > 0:
            self.word_task_queue_list.setCurrentRow(0)
        if self.example_task_queue_list.count() > 0:
            self.example_task_queue_list.setCurrentRow(0)
        self._update_generate_buttons()

    def _pending_task_count(self) -> int:
        return len(self._word_task_queue) + len(self._example_task_queue)

    def _pop_next_task(self) -> GenerationTask | None:
        if not self._word_task_queue and not self._example_task_queue:
            return None
        if not self._word_task_queue:
            return self._example_task_queue.pop(0)
        if not self._example_task_queue:
            return self._word_task_queue.pop(0)
        if self._word_task_queue[0].task_id < self._example_task_queue[0].task_id:
            return self._word_task_queue.pop(0)
        return self._example_task_queue.pop(0)

    def _format_json_payload(self, payload: Any) -> str:
        return json.dumps(payload, ensure_ascii=False, indent=2)

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:  # type: ignore[override]
        if hasattr(self, "record_list") and watched is self.record_list.viewport() and event.type() == QEvent.Resize:
            new_items_per_page = self._calculate_record_items_per_page()
            if new_items_per_page != self._record_items_per_page:
                self._record_items_per_page = new_items_per_page
                self._ensure_record_page_valid()
                self._render_record_page()
        return super().eventFilter(watched, event)


def _safe_int(value: Any) -> int | None:
    try:
        if value is None or value == "":
            return None
        return int(value)
    except (TypeError, ValueError):
        return None


def _sanitize_stem_part(value: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z_-]+", "_", value or "").strip("_")
    return sanitized or "record"


def _extract_message_text(content: object) -> str:
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
    if content is None:
        return ""
    return str(content)


def _normalize_generated_prompt(text: str) -> str:
    normalized = text.strip()
    if normalized.startswith("```"):
        normalized = re.sub(r"^```(?:\w+)?\s*", "", normalized)
        normalized = re.sub(r"\s*```$", "", normalized)
    normalized = normalized.strip()
    if normalized.startswith('"') and normalized.endswith('"') and len(normalized) >= 2:
        normalized = normalized[1:-1].strip()
    if normalized.startswith("“") and normalized.endswith("”") and len(normalized) >= 2:
        normalized = normalized[1:-1].strip()
    return normalized


def _build_meaning_file_stem(record: WordRecord) -> str:
    meaning_id_text = str(record.meaning_id) if record.meaning_id is not None else f"idx{record.list_index}"
    word_id_text = str(record.word_id) if record.word_id is not None else f"idx{record.list_index}"
    return "_".join([meaning_id_text, word_id_text, _sanitize_stem_part(record.word)])


def _build_example_file_stem(record: WordRecord, example: ExampleRecord) -> str:
    meaning_id_text = str(record.meaning_id) if record.meaning_id is not None else f"idx{record.list_index}"
    return "_".join([example.file_id_text, meaning_id_text, _sanitize_stem_part(record.word)])


def _resolve_existing_preview_path(output_dir: Path, file_stem: str) -> Path | None:
    preview_candidates = [
        output_dir / f"{file_stem}.png",
        output_dir / f"{file_stem}_eink.png",
    ]
    for candidate in preview_candidates:
        if candidate.exists():
            return candidate
    return None


def _compact_text(value: str, max_length: int = 40) -> str:
    compacted = re.sub(r"\s+", " ", value or "").strip()
    if not compacted:
        return "-"
    if len(compacted) <= max_length:
        return compacted
    return f"{compacted[: max_length - 1]}..."

def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())