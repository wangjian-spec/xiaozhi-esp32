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
from PySide6.QtCore import QEvent, QObject, QPoint, QRect, QSize, QThread, Qt, QTimer, Signal, Slot
from PySide6.QtGui import QAction, QColor, QImage, QMouseEvent, QPainter, QPen
from PySide6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QSpinBox,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)


ROOT_DIR = Path(__file__).resolve().parent
DEFAULT_JSON_PATH = Path(r"C:\Users\wj\xiaozhi-esp32\main\eteacher\database_manager\python_script\stage1\book\record_stage1_gpt5.4_generated.json")
DEFAULT_IMAGE_OUTPUT_ROOT = Path(r"D:\王健备份\个人\英语口语教师\图片和音频资源\images")
DEFAULT_WORDS_OUTPUT_DIR = DEFAULT_IMAGE_OUTPUT_ROOT / "stage1" / "words"
DEFAULT_API_URL = "https://open.bigmodel.cn/api/paas/v4/images/generations"
DEFAULT_MODEL = "glm-image"
DEFAULT_PROMPT_API_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
DEFAULT_PROMPT_MODEL = "glm-5"
DEFAULT_API_KEY = "b32773de044d4576aa0db2698cfd8a4e.yu4AMMf4iWxk4tBj"
TARGET_SIZE = (1280, 1280)
REQUEST_TIMEOUT_SECONDS = 180
RECORD_LIST_ITEM_HEIGHT = 112
RECORD_LIST_ITEM_SPACING = 2
RECORDS_PER_PAGE = 5

DEFAULT_FIXED_PROMPT = """生成图片要求：分辨率1280*1280
适合墨水屏显示
适合儿童，小学教学用途
简单的黑白卡通插画
细节尽量少，画面简洁
纯白背景，黑色绘图
高对比度（黑白分明）
不要灰度（只有纯黑和纯白）
图片中不能出现任何文字，字母或单词；图片中展示内容为：
"""

DEFAULT_VARIABLE_TEMPLATE = """
"""


@dataclass(slots=True)
class ExampleRecord:
    list_index: int
    example_id: int | None
    meaning_id: int | None
    source_record_index: int | None
    source_example_index: int | None
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
class MeaningRecord:
    source_record_index: int
    meaning_id: int | None
    pos: str
    meaning_en: str
    meaning_zh: str
    word_tag: str
    raw: dict[str, Any]

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
    hidden: bool
    meanings: list[MeaningRecord]
    examples: list[ExampleRecord]
    source_record_indices: list[int]
    source_records: list[dict[str, Any]]
    raw: dict[str, Any]

    @property
    def display_text(self) -> str:
        word_id_text = str(self.word_id) if self.word_id is not None else "?"
        meaning_text = _compact_text(_build_meaning_summary_text(self), 96)
        return f"{self.list_index:04d}. [{word_id_text}] {self.word} | {meaning_text}"

    @property
    def supports_image_generation(self) -> bool:
        return _record_supports_image_generation(self)

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
    row_code: int
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


class ImageEditorCanvas(QWidget):
    statusChanged = Signal(str)
    dirtyChanged = Signal(bool)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._image = QImage()
        self._image_path: Path | None = None
        self._placeholder_text = "暂无图片"
        self._tool = "select"
        self._line_width = 10
        self._eraser_size = 24
        self._dirty = False
        self._undo_stack: list[QImage] = []
        self._selection_rect: QRect | None = None
        self._selection_image: QImage | None = None
        self._drag_mode: str | None = None
        self._drag_start = QPoint()
        self._drag_current = QPoint()
        self._last_erase_point: QPoint | None = None
        self._move_anchor_offset = QPoint()
        self._move_preview_rect: QRect | None = None
        self.setMinimumHeight(420)
        self.setMouseTracking(True)

    def has_image(self) -> bool:
        return not self._image.isNull()

    def is_modified(self) -> bool:
        return self._dirty

    def can_undo(self) -> bool:
        return bool(self._undo_stack)

    def current_image_path(self) -> Path | None:
        return self._image_path

    def set_tool(self, tool: str) -> None:
        self._tool = tool
        self.statusChanged.emit(f"当前工具: {self._tool_label(tool)}")
        self.update()

    def set_line_width(self, value: int) -> None:
        self._line_width = max(1, value)
        self.statusChanged.emit(f"线宽已调整为 {self._line_width}")

    def set_eraser_size(self, value: int) -> None:
        self._eraser_size = max(1, value)
        self.statusChanged.emit(f"橡皮擦大小已调整为 {self._eraser_size}")

    def clear_image(self, placeholder_text: str = "暂无图片") -> None:
        self._image = QImage()
        self._image_path = None
        self._placeholder_text = placeholder_text
        self._undo_stack.clear()
        self._selection_rect = None
        self._selection_image = None
        self._move_preview_rect = None
        self._set_dirty(False)
        self.update()

    def load_image(self, image_path: Path) -> bool:
        image = QImage(str(image_path))
        if image.isNull():
            self.clear_image(f"无法加载图片:\n{image_path}")
            self.statusChanged.emit(f"无法加载图片: {image_path}")
            return False

        self._image = image.convertToFormat(QImage.Format_ARGB32)
        self._image_path = image_path
        self._placeholder_text = ""
        self._undo_stack.clear()
        self._selection_rect = None
        self._selection_image = None
        self._move_preview_rect = None
        self._set_dirty(False)
        self.statusChanged.emit(f"已加载图片: {image_path.name}")
        self.update()
        return True

    def save_image(self) -> Path | None:
        if self._image.isNull():
            self.statusChanged.emit("当前没有可保存的图片")
            return None

        target_path = self._image_path
        if target_path is None:
            file_path, _ = QFileDialog.getSaveFileName(
                self,
                "保存图片",
                str(DEFAULT_WORDS_OUTPUT_DIR),
                "PNG Files (*.png)",
            )
            if not file_path:
                return None
            target_path = Path(file_path)
            if target_path.suffix.lower() != ".png":
                target_path = target_path.with_suffix(".png")
            self._image_path = target_path

        target_path.parent.mkdir(parents=True, exist_ok=True)
        if not self._image.save(str(target_path), "PNG"):
            raise ValueError(f"保存图片失败: {target_path}")
        self._set_dirty(False)
        self.statusChanged.emit(f"已保存图片: {target_path}")
        return target_path

    def undo_last_action(self) -> bool:
        if not self._undo_stack:
            self.statusChanged.emit("当前没有可取消的编辑")
            return False
        self._image = self._undo_stack.pop()
        self._selection_rect = None
        self._selection_image = None
        self._move_preview_rect = None
        self._set_dirty(bool(self._undo_stack))
        self.statusChanged.emit("已取消上一步编辑")
        self.update()
        return True

    def paintEvent(self, event) -> None:  # type: ignore[override]
        del event
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#f3f4f6"))

        if self._image.isNull():
            painter.setPen(QColor("#6b7280"))
            painter.drawText(self.rect().adjusted(20, 20, -20, -20), Qt.AlignCenter, self._placeholder_text)
            return

        draw_rect = self._image_draw_rect()
        painter.fillRect(draw_rect, QColor("#ffffff"))
        painter.drawImage(draw_rect, self._image)
        painter.setPen(QPen(QColor("#9ca3af"), 1))
        painter.drawRect(draw_rect)

        overlay_rect = None
        overlay_color = QColor("#2563eb")
        if self._drag_mode == "select_rect":
            overlay_rect = self._image_selection_rect_from_points(self._drag_start, self._drag_current)
        elif self._drag_mode == "delete_rect":
            overlay_rect = self._image_selection_rect_from_points(self._drag_start, self._drag_current)
            overlay_color = QColor("#dc2626")
        elif self._drag_mode in {"draw_rect", "draw_ellipse"}:
            overlay_rect = self._image_selection_rect_from_points(self._drag_start, self._drag_current)
            overlay_color = QColor("#111827")
        elif self._drag_mode == "move_selection" and self._move_preview_rect is not None:
            overlay_rect = self._move_preview_rect
            overlay_color = QColor("#d97706")
        elif self._drag_mode == "copy_selection" and self._move_preview_rect is not None:
            overlay_rect = self._move_preview_rect
            overlay_color = QColor("#059669")
        elif self._selection_rect is not None:
            overlay_rect = self._selection_rect

        if self._drag_mode == "copy_selection" and self._move_preview_rect is not None and self._selection_image is not None:
            preview_rect = self._map_image_rect_to_widget_rect(self._move_preview_rect)
            painter.save()
            painter.setOpacity(0.65)
            painter.drawImage(preview_rect, self._selection_image)
            painter.restore()

        if overlay_rect is not None and not overlay_rect.isNull():
            painter.setPen(QPen(overlay_color, 2, Qt.DashLine))
            widget_rect = self._map_image_rect_to_widget_rect(overlay_rect)
            if self._drag_mode == "draw_rect":
                radius = min(widget_rect.width(), widget_rect.height()) * 0.08
                painter.drawRoundedRect(widget_rect, radius, radius)
            else:
                painter.drawRect(widget_rect)

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() != Qt.LeftButton or self._image.isNull():
            super().mousePressEvent(event)
            return

        image_point = self._map_to_image_point(event.position().toPoint(), clamp=False)
        if image_point is None:
            super().mousePressEvent(event)
            return

        if self._tool.startswith("arrow_"):
            self._push_undo_state()
            self._draw_arrow(image_point, self._tool)
            self._set_dirty(True)
            self.update()
            return

        if self._tool == "eraser":
            self._push_undo_state()
            self._drag_mode = "erase"
            self._last_erase_point = image_point
            self._erase_segment(image_point, image_point)
            self._set_dirty(True)
            self.update()
            return

        self._drag_start = image_point
        self._drag_current = image_point

        if self._tool in {"select", "copy_rect"}:
            if self._selection_rect is not None and self._selection_rect.contains(image_point):
                self._drag_mode = "move_selection" if self._tool == "select" else "copy_selection"
                self._move_anchor_offset = image_point - self._selection_rect.topLeft()
                self._move_preview_rect = QRect(self._selection_rect)
                if self._selection_image is None:
                    self._selection_image = self._image.copy(self._selection_rect)
            else:
                self._drag_mode = "select_rect"
                self._selection_rect = None
                self._selection_image = None
                self._move_preview_rect = None
        elif self._tool == "delete_rect":
            self._drag_mode = "delete_rect"
        elif self._tool == "draw_rect":
            self._drag_mode = "draw_rect"
        elif self._tool == "draw_ellipse":
            self._drag_mode = "draw_ellipse"

        self.update()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if self._image.isNull() or self._drag_mode is None:
            super().mouseMoveEvent(event)
            return

        image_point = self._map_to_image_point(event.position().toPoint(), clamp=True)
        if image_point is None:
            super().mouseMoveEvent(event)
            return

        self._drag_current = image_point
        if self._drag_mode == "erase":
            if self._last_erase_point is None:
                self._last_erase_point = image_point
            self._erase_segment(self._last_erase_point, image_point)
            self._last_erase_point = image_point
            self._set_dirty(True)
        elif self._drag_mode in {"move_selection", "copy_selection"} and self._selection_rect is not None:
            target_top_left = self._bounded_top_left(image_point - self._move_anchor_offset, self._selection_rect.size())
            self._move_preview_rect = QRect(target_top_left, self._selection_rect.size())

        self.update()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() != Qt.LeftButton or self._image.isNull():
            super().mouseReleaseEvent(event)
            return

        if self._drag_mode == "erase":
            self._drag_mode = None
            self._last_erase_point = None
            self.update()
            return

        if self._drag_mode == "select_rect":
            rect = self._image_selection_rect_from_points(self._drag_start, self._drag_current)
            if rect.width() > 1 and rect.height() > 1:
                self._selection_rect = rect
                self._selection_image = self._image.copy(rect)
            else:
                self._selection_rect = None
                self._selection_image = None
        elif self._drag_mode == "move_selection" and self._selection_rect is not None and self._selection_image is not None:
            target_rect = self._move_preview_rect or self._selection_rect
            if target_rect.topLeft() != self._selection_rect.topLeft():
                self._push_undo_state()
                painter = QPainter(self._image)
                painter.fillRect(self._selection_rect, QColor("#ffffff"))
                painter.drawImage(target_rect.topLeft(), self._selection_image)
                painter.end()
                self._selection_rect = QRect(target_rect.topLeft(), self._selection_image.size())
                self._selection_image = self._image.copy(self._selection_rect)
                self._set_dirty(True)
        elif self._drag_mode == "copy_selection" and self._selection_rect is not None and self._selection_image is not None:
            target_rect = self._move_preview_rect or self._selection_rect
            if target_rect.topLeft() != self._selection_rect.topLeft():
                self._push_undo_state()
                painter = QPainter(self._image)
                painter.drawImage(target_rect.topLeft(), self._selection_image)
                painter.end()
                self._selection_rect = QRect(target_rect.topLeft(), self._selection_image.size())
                self._selection_image = self._image.copy(self._selection_rect)
                self._set_dirty(True)
        elif self._drag_mode == "delete_rect":
            rect = self._image_selection_rect_from_points(self._drag_start, self._drag_current)
            if rect.width() > 1 and rect.height() > 1:
                self._push_undo_state()
                painter = QPainter(self._image)
                painter.fillRect(rect, QColor("#ffffff"))
                painter.end()
                self._selection_rect = None
                self._selection_image = None
                self._set_dirty(True)
        elif self._drag_mode in {"draw_rect", "draw_ellipse"}:
            rect = self._image_selection_rect_from_points(self._drag_start, self._drag_current)
            if rect.width() > 1 and rect.height() > 1:
                self._push_undo_state()
                painter = QPainter(self._image)
                painter.setRenderHint(QPainter.Antialiasing, True)
                pen = QPen(QColor("#000000"), self._line_width)
                painter.setPen(pen)
                if self._drag_mode == "draw_rect":
                    radius = min(rect.width(), rect.height()) * 0.08
                    painter.drawRoundedRect(rect, radius, radius)
                else:
                    painter.drawEllipse(rect)
                painter.end()
                self._set_dirty(True)

        self._drag_mode = None
        self._move_preview_rect = None
        self.update()

    def _image_draw_rect(self) -> QRect:
        content_rect = self.rect().adjusted(12, 12, -12, -12)
        if self._image.isNull() or content_rect.width() <= 0 or content_rect.height() <= 0:
            return QRect()
        scaled_size = self._image.size().scaled(content_rect.size(), Qt.KeepAspectRatio)
        x = content_rect.x() + (content_rect.width() - scaled_size.width()) // 2
        y = content_rect.y() + (content_rect.height() - scaled_size.height()) // 2
        return QRect(x, y, scaled_size.width(), scaled_size.height())

    def _map_to_image_point(self, widget_point: QPoint, *, clamp: bool) -> QPoint | None:
        draw_rect = self._image_draw_rect()
        if draw_rect.isNull():
            return None
        if not clamp and not draw_rect.contains(widget_point):
            return None

        x = widget_point.x()
        y = widget_point.y()
        if clamp:
            x = max(draw_rect.left(), min(draw_rect.right(), x))
            y = max(draw_rect.top(), min(draw_rect.bottom(), y))

        relative_x = 0.0 if draw_rect.width() <= 1 else (x - draw_rect.x()) / max(1, draw_rect.width() - 1)
        relative_y = 0.0 if draw_rect.height() <= 1 else (y - draw_rect.y()) / max(1, draw_rect.height() - 1)
        image_x = round(relative_x * max(0, self._image.width() - 1))
        image_y = round(relative_y * max(0, self._image.height() - 1))
        return QPoint(
            max(0, min(self._image.width() - 1, image_x)),
            max(0, min(self._image.height() - 1, image_y)),
        )

    def _map_image_rect_to_widget_rect(self, image_rect: QRect) -> QRect:
        draw_rect = self._image_draw_rect()
        if draw_rect.isNull() or image_rect.isNull():
            return QRect()

        scale_x = draw_rect.width() / max(1, self._image.width())
        scale_y = draw_rect.height() / max(1, self._image.height())
        x = draw_rect.x() + round(image_rect.x() * scale_x)
        y = draw_rect.y() + round(image_rect.y() * scale_y)
        width = max(1, round(image_rect.width() * scale_x))
        height = max(1, round(image_rect.height() * scale_y))
        return QRect(x, y, width, height)

    def _image_selection_rect_from_points(self, start: QPoint, end: QPoint) -> QRect:
        left = min(start.x(), end.x())
        top = min(start.y(), end.y())
        right = max(start.x(), end.x())
        bottom = max(start.y(), end.y())
        return QRect(left, top, right - left + 1, bottom - top + 1)

    def _bounded_top_left(self, top_left: QPoint, size: QSize) -> QPoint:
        max_x = max(0, self._image.width() - size.width())
        max_y = max(0, self._image.height() - size.height())
        return QPoint(
            max(0, min(max_x, top_left.x())),
            max(0, min(max_y, top_left.y())),
        )

    def _push_undo_state(self) -> None:
        if self._image.isNull():
            return
        self._undo_stack.append(self._image.copy())
        if len(self._undo_stack) > 20:
            self._undo_stack.pop(0)

    def _erase_segment(self, start: QPoint, end: QPoint) -> None:
        painter = QPainter(self._image)
        painter.setRenderHint(QPainter.Antialiasing, True)
        pen = QPen(QColor("#ffffff"), self._eraser_size, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)
        painter.setPen(pen)
        painter.drawLine(start, end)
        painter.end()

    def _draw_arrow(self, center: QPoint, tool: str) -> None:
        size = max(48, self._line_width * 10)
        head = max(16, size // 3)
        half = size // 2
        if tool == "arrow_up":
            start = QPoint(center.x(), center.y() + half)
            end = QPoint(center.x(), center.y() - half)
            wing_a = QPoint(end.x() - head, end.y() + head)
            wing_b = QPoint(end.x() + head, end.y() + head)
        elif tool == "arrow_down":
            start = QPoint(center.x(), center.y() - half)
            end = QPoint(center.x(), center.y() + half)
            wing_a = QPoint(end.x() - head, end.y() - head)
            wing_b = QPoint(end.x() + head, end.y() - head)
        elif tool == "arrow_left":
            start = QPoint(center.x() + half, center.y())
            end = QPoint(center.x() - half, center.y())
            wing_a = QPoint(end.x() + head, end.y() - head)
            wing_b = QPoint(end.x() + head, end.y() + head)
        else:
            start = QPoint(center.x() - half, center.y())
            end = QPoint(center.x() + half, center.y())
            wing_a = QPoint(end.x() - head, end.y() - head)
            wing_b = QPoint(end.x() - head, end.y() + head)

        painter = QPainter(self._image)
        painter.setRenderHint(QPainter.Antialiasing, True)
        pen = QPen(QColor("#000000"), self._line_width, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)
        painter.setPen(pen)
        painter.drawLine(start, end)
        painter.drawLine(end, wing_a)
        painter.drawLine(end, wing_b)
        painter.end()

    def _set_dirty(self, dirty: bool) -> None:
        if self._dirty == dirty:
            return
        self._dirty = dirty
        self.dirtyChanged.emit(dirty)

    def _tool_label(self, tool: str) -> str:
        return {
            "select": "选择移动",
            "copy_rect": "复制区域",
            "delete_rect": "矩形删除",
            "draw_rect": "矩形框",
            "draw_ellipse": "圆形",
            "eraser": "橡皮擦",
            "arrow_up": "上箭头",
            "arrow_down": "下箭头",
            "arrow_left": "左箭头",
            "arrow_right": "右箭头",
        }.get(tool, tool)


class RecordListItemWidget(QWidget):
    selected = Signal(int)
    rowSelected = Signal(int, int)
    queueRequested = Signal(int, int)
    deletePromptRequested = Signal(int, int)
    promptGenerateRequested = Signal(int, int)
    editWordPromptRequested = Signal(int)
    renameWordImageRequested = Signal(int)
    confirmRequested = Signal(int)
    deleteAllPromptsRequested = Signal(int)
    batchSelectionChanged = Signal(int, bool)

    def __init__(
        self,
        record_index: int,
        record: WordRecord,
        selected_row_code: int | None = None,
        queued_row_codes: set[int] | None = None,
        batch_selected: bool = False,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._record_index = record_index
        self._record = record
        self._selected_row_code = selected_row_code
        self._queued_row_codes = set(queued_row_codes or set())
        self._batch_selected = batch_selected
        self._prompt_buttons: dict[int, QPushButton] = {}
        self._confirm_button: QPushButton | None = None
        self._batch_checkbox: QCheckBox | None = None

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

        footer_widget = QWidget(self)
        footer_row = QHBoxLayout(footer_widget)
        footer_row.setContentsMargins(4, 2, 4, 2)
        footer_row.setSpacing(4)
        self._batch_checkbox = QCheckBox("批量选择", self)
        self._batch_checkbox.setChecked(self._batch_selected)
        self._batch_checkbox.toggled.connect(self._on_batch_selection_toggled)
        footer_row.addWidget(self._batch_checkbox)
        edit_prompt_button = QPushButton("编辑单词提示词", self)
        edit_prompt_button.setFixedHeight(22)
        edit_prompt_button.clicked.connect(self._on_edit_word_prompt_clicked)
        footer_row.addWidget(edit_prompt_button)
        rename_image_button = QPushButton("修改图片名字", self)
        rename_image_button.setFixedHeight(22)
        rename_image_button.clicked.connect(self._on_rename_word_image_clicked)
        footer_row.addWidget(rename_image_button)
        footer_row.addStretch(1)
        self._confirm_button = QPushButton("确认当前记录", self)
        self._confirm_button.setFixedHeight(22)
        self._confirm_button.setText("取消确认当前记录" if record.hidden else "确认当前记录")
        self._confirm_button.clicked.connect(self._on_confirm_clicked)
        footer_row.addWidget(self._confirm_button)
        delete_all_prompts_button = QPushButton("删除所有提示词", self)
        delete_all_prompts_button.setFixedHeight(22)
        delete_all_prompts_button.clicked.connect(self._on_delete_all_prompts_clicked)
        footer_row.addWidget(delete_all_prompts_button)
        layout.addWidget(footer_widget)

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

    def set_queued_row_codes(self, row_codes: set[int] | None) -> None:
        self._queued_row_codes = set(row_codes or set())
        self._refresh_row_styles()

    def set_batch_selected(self, checked: bool) -> None:
        self._batch_selected = checked
        if self._batch_checkbox is not None and self._batch_checkbox.isChecked() != checked:
            self._batch_checkbox.blockSignals(True)
            self._batch_checkbox.setChecked(checked)
            self._batch_checkbox.blockSignals(False)

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

    def _on_confirm_clicked(self) -> None:
        self._on_row_selected(-1)
        self.confirmRequested.emit(self._record_index)

    def _on_delete_all_prompts_clicked(self) -> None:
        self._on_row_selected(-1)
        self.deleteAllPromptsRequested.emit(self._record_index)

    def _on_edit_word_prompt_clicked(self) -> None:
        self._on_row_selected(-1)
        self.editWordPromptRequested.emit(self._record_index)

    def _on_rename_word_image_clicked(self) -> None:
        self._on_row_selected(-1)
        self.renameWordImageRequested.emit(self._record_index)

    def _on_batch_selection_toggled(self, checked: bool) -> None:
        self._batch_selected = checked
        self.batchSelectionChanged.emit(self._record_index, checked)

    def _refresh_row_styles(self) -> None:
        self._apply_row_style(self.meaning_row_widget, self.meaning_label, -1)
        self._apply_row_style(self.example_one_row_widget, self.example_one_label, 0)
        self._apply_row_style(self.example_two_row_widget, self.example_two_label, 1)

    def _apply_row_style(self, row_widget: QWidget, label: QLabel, row_code: int) -> None:
        row_available = self._row_available(row_code)
        has_prompt = self._row_has_prompt(row_code)
        has_image = self._row_has_generated_image(row_code)
        is_selected = self._selected_row_code == row_code
        is_queued = row_code in self._queued_row_codes

        background = "#f3f4f6"
        border = "#d1d5db"
        text_color = "#6b7280"
        if row_available and not has_prompt and not has_image:
            background = "#fdeaea"
            border = "#d9534f"
            text_color = "#a94442"
        elif row_available and has_image:
            background = "#e8f6e8"
            border = "#3c9a4d"
            text_color = "#256c2f"
        elif row_available and has_prompt:
            background = "#eaf2fd"
            border = "#2f73d9"
            text_color = "#1f4f96"

        if is_queued:
            background = "#fff4cc"
            border = "#d4a017"
            text_color = "#7a5d00"

        if is_selected:
            border = "#005ea5"

        row_widget.setStyleSheet(
            f"background: {background}; border: 2px solid {border}; border-radius: 4px;"
        )
        label.setStyleSheet(f"color: {text_color}; background: transparent; border: none;")

    def _row_available(self, row_code: int) -> bool:
        return row_code == -1 or row_code < len(self._record.examples)

    def _row_has_prompt(self, row_code: int) -> bool:
        if row_code == -1:
            return bool(self._record.image_hint.strip())
        if row_code >= len(self._record.examples):
            return False
        return bool(self._record.examples[row_code].image_prompt.strip())

    def _row_has_generated_image(self, row_code: int) -> bool:
        if row_code == -1:
            if not self._record.supports_image_generation:
                return False
            return _resolve_existing_meaning_preview_path(
                DEFAULT_WORDS_OUTPUT_DIR,
                self._record,
            ) is not None
        return False

    def _build_meaning_text(self, record: WordRecord) -> str:
        hidden_prefix = "[已隐藏] " if record.hidden else ""
        meaning_text = _compact_text(record.meaning_zh or _build_meaning_summary_text(record), 28)
        prompt_text = _compact_text(record.image_hint or "-", 52)
        return (
            f"{record.list_index:04d}. {hidden_prefix}{record.word or '-'}"
            f" | 中文释义: {meaning_text} | image({prompt_text})"
        )

    def _build_example_text(self, record: WordRecord, example_offset: int) -> str:
        if example_offset >= len(record.examples):
            return f"例句{example_offset + 1}: - | image(-)"

        example = record.examples[example_offset]
        meaning_text = _compact_text(record.meaning_zh or "-", 12)
        example_en_text = _compact_text(example.example_en or "-", 30)
        example_zh_text = _compact_text(example.example_zh or "-", 18)
        prompt_text = _compact_text(example.image_prompt or "-", 28)
        return (
            f"例句{example_offset + 1}: {meaning_text} | {example_en_text} | {example_zh_text}"
            f" | image({prompt_text})"
        )


class RecordRepository:
    def load(self, json_path: Path) -> list[WordRecord]:
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("JSON 中缺少 records 数组")

        grouped_records: dict[tuple[int | None, str], dict[str, Any]] = {}
        fallback_order = 0

        for source_record_index, item in enumerate(records):
            if not isinstance(item, dict):
                continue

            word_data = item.get("word") or {}
            meaning_data = item.get("word_meaning") or {}
            example_items = item.get("word_example") or []
            if not isinstance(word_data, dict) or not isinstance(meaning_data, dict):
                continue

            word_id = _safe_int(word_data.get("id"))
            word_text = str(word_data.get("word") or "").strip()
            group_key = (word_id, word_text.casefold())
            group = grouped_records.get(group_key)
            if group is None:
                fallback_order += 1
                group = {
                    "word_id": word_id,
                    "word": word_text,
                    "phonetic": str(word_data.get("phonetic") or "").strip(),
                    "word_type": str(word_data.get("word_type") or "").strip(),
                    "image_hint": str(word_data.get("image") or "").strip(),
                    "word_tag": str(meaning_data.get("word_tag") or "").strip(),
                    "hidden_values": [bool(item.get("hidden", False))],
                    "meanings": [],
                    "examples": [],
                    "source_record_indices": [],
                    "source_records": [],
                    "primary_raw": item,
                    "fallback_order": fallback_order,
                }
                grouped_records[group_key] = group
            else:
                group["hidden_values"].append(bool(item.get("hidden", False)))
                if not group["phonetic"]:
                    group["phonetic"] = str(word_data.get("phonetic") or "").strip()
                if not group["word_type"]:
                    group["word_type"] = str(word_data.get("word_type") or "").strip()
                if not group["image_hint"]:
                    group["image_hint"] = str(word_data.get("image") or "").strip()
                if not group["word_tag"]:
                    group["word_tag"] = str(meaning_data.get("word_tag") or "").strip()

            group["source_record_indices"].append(source_record_index)
            group["source_records"].append(item)
            group["meanings"].append(
                MeaningRecord(
                    source_record_index=source_record_index,
                    meaning_id=_safe_int(meaning_data.get("id")),
                    pos=str(meaning_data.get("pos") or "").strip(),
                    meaning_en=str(meaning_data.get("meaning_en") or "").strip(),
                    meaning_zh=str(meaning_data.get("meaning_zh") or "").strip(),
                    word_tag=str(meaning_data.get("word_tag") or "").strip(),
                    raw=meaning_data,
                )
            )

            if isinstance(example_items, list):
                for source_example_index, example in enumerate(example_items):
                    if not isinstance(example, dict):
                        continue
                    group["examples"].append(
                        ExampleRecord(
                            list_index=len(group["examples"]) + 1,
                            example_id=_safe_int(example.get("id")),
                            meaning_id=_safe_int(example.get("meaning_id")),
                            source_record_index=source_record_index,
                            source_example_index=source_example_index,
                            example_en=str(example.get("example_en") or "").strip(),
                            example_zh=str(example.get("example_zh") or "").strip(),
                            difficulty=_safe_int(example.get("difficulty")),
                            example_tag=str(example.get("example_tag") or "").strip(),
                            image_prompt=str(example.get("image") or "").strip(),
                            raw=example,
                        )
                    )

        parsed_records: list[WordRecord] = []
        sorted_groups = sorted(
            grouped_records.values(),
            key=lambda group: (
                group["word_id"] is None,
                group["word_id"] if group["word_id"] is not None else 10**9,
                str(group["word"]).casefold(),
                group["fallback_order"],
            ),
        )

        for index, group in enumerate(sorted_groups, start=1):
            meanings = sorted(
                group["meanings"],
                key=lambda meaning: (
                    meaning.meaning_id is None,
                    meaning.meaning_id if meaning.meaning_id is not None else 10**9,
                    meaning.source_record_index,
                ),
            )
            primary_meaning = meanings[0] if meanings else None

            parsed_records.append(
                WordRecord(
                    list_index=index,
                    word_id=group["word_id"],
                    meaning_id=primary_meaning.meaning_id if primary_meaning is not None else None,
                    word=group["word"],
                    phonetic=group["phonetic"],
                    word_type=group["word_type"],
                    pos=primary_meaning.pos if primary_meaning is not None else "",
                    meaning_en=primary_meaning.meaning_en if primary_meaning is not None else "",
                    meaning_zh=primary_meaning.meaning_zh if primary_meaning is not None else "",
                    image_hint=group["image_hint"],
                    word_tag=group["word_tag"],
                    hidden=all(group["hidden_values"]),
                    meanings=meanings,
                    examples=group["examples"],
                    source_record_indices=group["source_record_indices"],
                    source_records=group["source_records"],
                    raw=group["primary_raw"],
                )
            )

        if not parsed_records:
            raise ValueError("JSON 中没有可用 record")
        return parsed_records


class PromptBuilder:
    def build(self, record: WordRecord, fixed_prompt: str, variable_prompt: str) -> str:
        # 移除主题部分，避免在 prompt 中直接包含描述性主题文本
        fixed_part = fixed_prompt.strip()
        variable_part = variable_prompt.strip()
        parts = [part for part in (fixed_part, variable_part) if part]
        return "\n\n".join(parts)


class BinImageWriter:
    def save_outputs(self, image_bytes: bytes, file_stem: str, output_dir: Path) -> dict[str, Path]:
        output_dir.mkdir(parents=True, exist_ok=True)

        with Image.open(io.BytesIO(image_bytes)) as image:
            rgba_image = image.convert("RGBA")
            rgba_image = ImageOps.fit(rgba_image, TARGET_SIZE, method=Image.LANCZOS, centering=(0.5, 0.5))

            raw_png_path = output_dir / f"{file_stem}.png"
            rgba_image.save(raw_png_path, format="PNG")

        return {"png": raw_png_path}


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
            self.finished.emit(
                {
                    "paths": written_paths,
                    "image_bytes": generation.image_bytes,
                    "prompt": generation.prompt,
                    "request_payload": generation.request_payload,
                    "response_payload": generation.response_payload,
                }
            )
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
        self._show_hidden_records = False
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
        self._prompt_generation_queue: list[tuple[int, int]] = []
        self._prompt_generation_total = 0
        self._prompt_generation_completed = 0
        self._batch_selected_record_indices: set[int] = set()
        self._editor_tool_buttons: dict[str, QPushButton] = {}
        self._last_info_text = ""
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
        main_layout.setContentsMargins(4, 4, 4, 4)
        main_layout.setSpacing(4)
        vertical_splitter = QSplitter(Qt.Vertical, self)
        vertical_splitter.setChildrenCollapsible(False)
        main_layout.addWidget(vertical_splitter)

        top_row_splitter = QSplitter(Qt.Horizontal, self)
        top_row_splitter.setChildrenCollapsible(False)
        top_row_splitter.addWidget(self._build_config_group())
        top_row_splitter.addWidget(self._build_task_queue_group())
        top_row_splitter.setStretchFactor(0, 1)
        top_row_splitter.setStretchFactor(1, 1)
        top_row_splitter.setSizes([850, 850])
        vertical_splitter.addWidget(top_row_splitter)

        bottom_row_splitter = QSplitter(Qt.Horizontal, self)
        bottom_row_splitter.setChildrenCollapsible(False)
        bottom_row_splitter.addWidget(self._build_record_group())
        bottom_row_splitter.addWidget(self._build_preview_group())
        bottom_row_splitter.setStretchFactor(0, 1)
        bottom_row_splitter.setStretchFactor(1, 1)
        bottom_row_splitter.setSizes([850, 850])
        vertical_splitter.addWidget(bottom_row_splitter)
        vertical_splitter.setStretchFactor(0, 1)
        vertical_splitter.setStretchFactor(1, 24)
        vertical_splitter.setSizes([1, 1059])

        self.setStatusBar(QStatusBar(self))
        self.statusBar().showMessage("请选择 JSON 并加载 record")

        reload_action = QAction("重新加载 JSON", self)
        reload_action.triggered.connect(self.load_records)
        self.addAction(reload_action)

    def _build_config_group(self) -> QGroupBox:
        group = QGroupBox("文件与生成", self)
        layout = QFormLayout(group)
        layout.setContentsMargins(6, 4, 6, 4)
        layout.setHorizontalSpacing(6)
        layout.setVerticalSpacing(2)

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
            "只输出智谱生成的 PNG 图片到 words 目录，不再生成 .bin、_eink.png、.json 文件",
            group,
        )
        output_hint.setWordWrap(True)
        output_hint.setStyleSheet("color: #666; font-size: 11px;")
        layout.addRow("输出位置", output_hint)

        button_row = QHBoxLayout()
        self.load_button = QPushButton("加载 Record", group)
        self.load_button.setFixedHeight(18)
        self.load_button.clicked.connect(self.load_records)
        button_row.addWidget(self.load_button)

        self.generate_meaning_button = QPushButton("生成当前单词释义提示词", group)
        self.generate_meaning_button.setFixedHeight(18)
        self.generate_meaning_button.clicked.connect(self.regenerate_current_meaning_prompt)
        button_row.addWidget(self.generate_meaning_button)
        button_row_widget = QWidget(group)
        button_row_widget.setLayout(button_row)
        layout.addRow("", button_row_widget)

        batch_button_row = QHBoxLayout()
        self.generate_selected_meaning_button = QPushButton("开始生成选中单词提示词", group)
        self.generate_selected_meaning_button.setFixedHeight(18)
        self.generate_selected_meaning_button.clicked.connect(self.generate_selected_meaning_prompts)
        batch_button_row.addWidget(self.generate_selected_meaning_button)
        self.clear_selected_meaning_button = QPushButton("清空批量选择", group)
        self.clear_selected_meaning_button.setFixedHeight(18)
        self.clear_selected_meaning_button.clicked.connect(self.clear_batch_prompt_selection)
        batch_button_row.addWidget(self.clear_selected_meaning_button)
        batch_button_widget = QWidget(group)
        batch_button_widget.setLayout(batch_button_row)
        layout.addRow("", batch_button_widget)

        return group

    def _build_task_queue_group(self) -> QGroupBox:
        group = QGroupBox("任务队列", self)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 4, 6, 4)
        layout.setSpacing(4)

        # 隐藏原先的队列状态提示，保留控件引用以避免修改其他逻辑
        self.queue_status_label = QLabel("", group)
        self.queue_status_label.setMaximumHeight(0)
        layout.addWidget(self.queue_status_label)

        queue_control_row = QHBoxLayout()
        self.start_queue_button = QPushButton("开始任务队列", group)
        self.start_queue_button.setFixedHeight(18)
        self.start_queue_button.clicked.connect(self.start_task_queue)
        queue_control_row.addWidget(self.start_queue_button)
        self.stop_queue_button = QPushButton("停止任务队列", group)
        self.stop_queue_button.setFixedHeight(18)
        self.stop_queue_button.clicked.connect(self.stop_task_queue)
        queue_control_row.addWidget(self.stop_queue_button)
        queue_control_widget = QWidget(group)
        queue_control_widget.setLayout(queue_control_row)
        layout.addWidget(queue_control_widget)

        queue_splitter = QSplitter(Qt.Vertical, group)
        queue_splitter.setMaximumHeight(200)

        word_queue_group = QGroupBox("单词任务队列", group)
        word_queue_layout = QVBoxLayout(word_queue_group)
        self.word_task_queue_list = QListWidget(group)
        self.word_task_queue_list.setMaximumHeight(140)
        word_queue_layout.addWidget(self.word_task_queue_list)
        word_button_row = QHBoxLayout()
        self.remove_word_task_button = QPushButton("移除选中单词任务", group)
        self.remove_word_task_button.setFixedHeight(18)
        self.remove_word_task_button.clicked.connect(self.remove_selected_word_task)
        word_button_row.addWidget(self.remove_word_task_button)
        self.clear_word_queue_button = QPushButton("清空单词队列", group)
        self.clear_word_queue_button.setFixedHeight(18)
        self.clear_word_queue_button.clicked.connect(self.clear_word_tasks)
        word_button_row.addWidget(self.clear_word_queue_button)
        word_button_widget = QWidget(group)
        word_button_widget.setLayout(word_button_row)
        word_button_widget.setMaximumHeight(28)
        word_queue_layout.addWidget(word_button_widget)
        queue_splitter.addWidget(word_queue_group)
        queue_splitter.setStretchFactor(0, 1)
        layout.addWidget(queue_splitter)

        return group

    def _build_record_group(self) -> QGroupBox:
        group = QGroupBox("Record 选择", self)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(4, 2, 4, 4)
        layout.setSpacing(2)

        self.record_list = QListWidget(group)
        self.record_list.setSpacing(RECORD_LIST_ITEM_SPACING)
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
        pager_row.addWidget(QLabel("页码", group))
        self.record_page_edit = QLineEdit(group)
        self.record_page_edit.setFixedHeight(24)
        self.record_page_edit.setFixedWidth(56)
        self.record_page_edit.setAlignment(Qt.AlignCenter)
        self.record_page_edit.setPlaceholderText("1")
        self.record_page_edit.returnPressed.connect(self.jump_to_record_page)
        pager_row.addWidget(self.record_page_edit)
        self.record_jump_page_button = QPushButton("跳转", group)
        self.record_jump_page_button.setFixedHeight(24)
        self.record_jump_page_button.setMinimumWidth(48)
        self.record_jump_page_button.clicked.connect(self.jump_to_record_page)
        pager_row.addWidget(self.record_jump_page_button)
        pager_row.addStretch(1)
        pager_widget = QWidget(group)
        pager_widget.setLayout(pager_row)
        pager_widget.setMaximumHeight(28)
        layout.addWidget(pager_widget)

        self.show_hidden_records_checkbox = QCheckBox("显示隐藏记录", group)
        self.show_hidden_records_checkbox.toggled.connect(self._on_show_hidden_records_toggled)
        layout.addWidget(self.show_hidden_records_checkbox)

        return group

    def _build_preview_group(self) -> QGroupBox:
        group = QGroupBox("图片编辑工具", self)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(6)

        tool_group = QGroupBox("编辑工具", group)
        tool_layout = QGridLayout(tool_group)

        self.open_image_button = QPushButton("打开图片", tool_group)
        self.open_image_button.clicked.connect(self.open_image_for_editing)
        tool_layout.addWidget(self.open_image_button, 0, 0)

        self.save_image_button = QPushButton("保存", tool_group)
        self.save_image_button.clicked.connect(self.save_current_image)
        tool_layout.addWidget(self.save_image_button, 0, 1)

        self.cancel_image_edit_button = QPushButton("取消", tool_group)
        self.cancel_image_edit_button.clicked.connect(self.undo_current_image_edit)
        tool_layout.addWidget(self.cancel_image_edit_button, 0, 2)

        tool_button_group = QButtonGroup(tool_group)
        tool_button_group.setExclusive(True)
        tool_specs = [
            ("选择移动", "select", 1, 0),
            ("复制区域", "copy_rect", 1, 1),
            ("矩形删除", "delete_rect", 1, 2),
            ("矩形框", "draw_rect", 1, 3),
            ("圆形", "draw_ellipse", 1, 4),
            ("橡皮擦", "eraser", 1, 5),
            ("上箭头", "arrow_up", 2, 0),
            ("下箭头", "arrow_down", 2, 1),
            ("左箭头", "arrow_left", 2, 2),
            ("右箭头", "arrow_right", 2, 3),
        ]
        for text, tool_name, row, column in tool_specs:
            button = QPushButton(text, tool_group)
            button.setCheckable(True)
            button.clicked.connect(lambda checked, name=tool_name: self.set_editor_tool(name) if checked else None)
            tool_button_group.addButton(button)
            tool_layout.addWidget(button, row, column)
            self._editor_tool_buttons[tool_name] = button

        tool_layout.addWidget(QLabel("线宽", tool_group), 2, 4)
        self.line_width_spinbox = QSpinBox(tool_group)
        self.line_width_spinbox.setRange(1, 64)
        self.line_width_spinbox.setValue(10)
        self.line_width_spinbox.valueChanged.connect(self._on_line_width_changed)
        tool_layout.addWidget(self.line_width_spinbox, 2, 5)

        tool_layout.addWidget(QLabel("橡皮大小", tool_group), 2, 6)
        self.eraser_size_spinbox = QSpinBox(tool_group)
        self.eraser_size_spinbox.setRange(1, 128)
        self.eraser_size_spinbox.setValue(24)
        self.eraser_size_spinbox.valueChanged.connect(self._on_eraser_size_changed)
        tool_layout.addWidget(self.eraser_size_spinbox, 2, 7)
        layout.addWidget(tool_group)

        self.image_editor = ImageEditorCanvas(group)
        self.image_editor.statusChanged.connect(self._on_editor_status_changed)
        self.image_editor.dirtyChanged.connect(self._on_editor_dirty_changed)
        layout.addWidget(self.image_editor)

        self.set_editor_tool("select")
        self.image_editor.set_eraser_size(self.eraser_size_spinbox.value())
        self._update_editor_action_buttons()

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
        self._batch_selected_record_indices.clear()
        self.record_summary_label.setText(f"共加载 {len(self._records)} 条记录")
        self.statusBar().showMessage(f"已加载 {len(self._records)} 条记录")
        self._rebuild_filtered_record_indices()
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
        self._update_selection_details()
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

        if not record.supports_image_generation:
            QMessageBox.information(self, "不支持生成", "当前记录不是单词，不生成图片。")
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
        QMessageBox.information(self, "不支持生成", "例句不生成图片。")

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

    @Slot()
    def generate_selected_meaning_prompts(self) -> None:
        selected_indices = self._selected_batch_record_indices_in_order()
        if not selected_indices:
            QMessageBox.information(self, "未选择记录", "请先勾选要批量生成提示词的单词记录。")
            return

        targets = [(record_index, -1) for record_index in selected_indices if self._records[record_index].supports_image_generation]
        if not targets:
            QMessageBox.information(self, "无可生成记录", "当前勾选记录中没有可生成单词提示词的条目。")
            return

        self._start_prompt_generation_batch(targets)

    @Slot()
    def clear_batch_prompt_selection(self) -> None:
        if not self._batch_selected_record_indices:
            self.statusBar().showMessage("当前没有已勾选的批量记录")
            return

        self._batch_selected_record_indices.clear()
        self._refresh_visible_record_item_states()
        self._update_record_pagination_controls()
        self._update_generate_buttons()
        self.statusBar().showMessage("已清空批量选择")

    def _enqueue_tasks(self, tasks: list[GenerationTask]) -> None:
        if not tasks:
            return

        for task in tasks:
            self._word_task_queue.append(task)

        self._refresh_task_queue_view()
        if len(tasks) == 1:
            summary_text = f"已加入任务队列: #{tasks[0].task_id} {tasks[0].display_text}"
        else:
            task_ids = ", ".join(f"#{task.task_id}" for task in tasks)
            summary_text = f"已加入 {len(tasks)} 个任务: {task_ids}"
        self._set_info_text(
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
        paths = payload["paths"]
        prompt_text = str(payload.get("prompt") or "").strip()
        request_payload = payload.get("request_payload")
        response_payload = payload.get("response_payload")
        completed_task = self._active_task
        self._load_image_into_editor(Path(paths["png"]), prompt_on_dirty=True)

        info_lines = ["生成完成:"]
        if completed_task is not None:
            info_lines.append(f"任务: #{completed_task.task_id} {completed_task.display_text}")
        for name, path in paths.items():
            info_lines.append(f"{name}: {path}")
        if prompt_text:
            info_lines.extend(["", "prompt:", prompt_text])
        if request_payload is not None:
            info_lines.extend(["", "request_payload:", json.dumps(request_payload, ensure_ascii=False, indent=2)])
        if response_payload is not None:
            info_lines.extend(["", "response_payload:", json.dumps(response_payload, ensure_ascii=False, indent=2)])
        new_entry = "\n".join(info_lines)
        self._append_info_text(new_entry)
        self.statusBar().showMessage(
            f"生成完成，已输出到 {paths['png']}。队列剩余 {self._pending_task_count()} 项"
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

    @Slot(int, bool)
    def _on_batch_selection_changed(self, record_index: int, checked: bool) -> None:
        if checked:
            self._batch_selected_record_indices.add(record_index)
        else:
            self._batch_selected_record_indices.discard(record_index)
        self._update_record_pagination_controls()
        self._update_generate_buttons()

    def _selected_batch_record_indices_in_order(self) -> list[int]:
        visible_set = set(self._filtered_record_indices)
        ordered_visible = [index for index in self._filtered_record_indices if index in self._batch_selected_record_indices]
        ordered_hidden = sorted(
            (index for index in self._batch_selected_record_indices if index not in visible_set),
            key=lambda index: self._records[index].list_index,
        )
        return [*ordered_visible, *ordered_hidden]

    @Slot()
    def remove_selected_word_task(self) -> None:
        current_item = self.word_task_queue_list.currentItem()
        if current_item is None:
            return

        task_id = int(current_item.data(Qt.UserRole))
        if self._active_task is not None and self._active_task.task_type == "word_meaning" and self._active_task.task_id == task_id:
            self.statusBar().showMessage(f"任务 #{task_id} 正在执行，无法移除")
            return
        self._word_task_queue = [task for task in self._word_task_queue if task.task_id != task_id]
        self._refresh_task_queue_view()
        self.statusBar().showMessage(f"已移除队列任务 #{task_id}")

    @Slot()
    def clear_word_tasks(self) -> None:
        cleared_count = len(self._word_task_queue)
        self._word_task_queue.clear()
        self._refresh_task_queue_view()
        self.statusBar().showMessage(f"已清空 {cleared_count} 个单词任务")

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

    @Slot(int)
    def _edit_word_prompt_by_index(self, record_index: int) -> None:
        if record_index < 0 or record_index >= len(self._records):
            return

        record = self._records[record_index]
        prompt_text, accepted = QInputDialog.getMultiLineText(
            self,
            "编辑单词提示词",
            f"请修改 {record.word or '-'} 的 word.image 内容:",
            record.image_hint,
        )
        if not accepted:
            return

        prompt_text = prompt_text.strip()
        try:
            self._update_record_word_image_in_json(record_index, prompt_text)
            record.image_hint = prompt_text
            self._sync_record_word_image_to_memory(record, prompt_text)
        except Exception as exc:
            QMessageBox.critical(self, "保存提示词失败", str(exc))
            return

        self._render_record_page()
        self._update_preview_for_current_selection()
        self.statusBar().showMessage(f"已保存 {record.word or '-'} 的 word.image 提示词")

    @Slot(int)
    def _rename_record_image_by_index(self, record_index: int) -> None:
        if record_index < 0 or record_index >= len(self._records):
            return

        record = self._records[record_index]
        if not record.supports_image_generation:
            QMessageBox.information(self, "无法修改图片名字", "当前记录不是单词，不能修改图片名字。")
            return

        if (
            self._active_task is not None
            and self._active_task.task_type == "word_meaning"
            and self._task_targets_record(self._active_task, record)
        ):
            QMessageBox.warning(self, "任务执行中", "当前单词图片正在生成中，请等待完成后再修改图片名字。")
            return

        new_word, accepted = QInputDialog.getText(
            self,
            "修改图片名字",
            "请输入新的单词名称:",
            QLineEdit.Normal,
            record.word,
        )
        if not accepted:
            return

        new_word = new_word.strip()
        if not new_word:
            QMessageBox.warning(self, "输入无效", "单词不能为空。")
            return

        old_stem = self._resolve_meaning_file_stem(record)
        try:
            new_word_id = self._resolve_word_id_for_word_name(record, new_word)
        except Exception as exc:
            QMessageBox.critical(self, "修改图片名字失败", str(exc))
            return
        new_stem = _build_meaning_file_stem_from_parts(new_word_id, record.list_index, new_word)

        try:
            renamed_count = self._rename_meaning_output_files(
                record,
                old_stem,
                new_stem,
                new_word_id,
                new_word,
            )
            self._sync_word_tasks_after_rename(record, new_stem)
        except Exception as exc:
            QMessageBox.critical(self, "修改图片名字失败", str(exc))
            return

        self._render_record_page()
        self._refresh_task_queue_view()
        self._update_preview_for_current_selection()
        self._update_selection_details()
        self.statusBar().showMessage(
            f"已将 {old_stem} 更新为 {new_stem}，共处理 {renamed_count} 个文件"
        )

    def _resolve_word_id_for_word_name(self, record: WordRecord, new_word: str) -> int | None:
        normalized_word = new_word.strip().casefold()
        if not normalized_word:
            raise ValueError("单词不能为空。")

        if record.word.strip().casefold() == normalized_word:
            return record.word_id

        matched_records = [
            candidate
            for candidate in self._records
            if candidate.word.strip().casefold() == normalized_word
        ]
        if not matched_records:
            raise ValueError(f"未找到单词 {new_word} 对应的记录，无法确定新的 word_id。")

        unique_word_ids = sorted({candidate.word_id for candidate in matched_records}, key=lambda value: (value is None, value))
        if len(unique_word_ids) > 1:
            raise ValueError(
                f"单词 {new_word} 对应多个不同的 word_id: {', '.join(str(value) for value in unique_word_ids)}，请先消除歧义。"
            )
        return unique_word_ids[0]

    def _update_generate_buttons(self) -> None:
        has_record = self.current_record() is not None
        active_target = self._prompt_generation_target
        current_record_index = self._current_record_index
        meaning_busy = active_target is not None and active_target == (current_record_index, -1)
        prompt_busy = self._prompt_worker_thread is not None
        self.generate_meaning_button.setEnabled(has_record and not meaning_busy and not prompt_busy)
        self.generate_selected_meaning_button.setEnabled(bool(self._batch_selected_record_indices) and not prompt_busy)
        self.clear_selected_meaning_button.setEnabled(bool(self._batch_selected_record_indices) and not prompt_busy)
        self.start_queue_button.setEnabled(self._pending_task_count() > 0 and not self._queue_running)
        self.stop_queue_button.setEnabled(self._queue_running)
        self.remove_word_task_button.setEnabled(bool(self._word_task_queue))
        self.clear_word_queue_button.setEnabled(bool(self._word_task_queue))
        self._update_editor_action_buttons()

    def _resolve_api_key(self) -> str:
        return str(os.environ.get("ZHIPU_API_KEY") or DEFAULT_API_KEY).strip()

    def _build_meaning_file_stem(self, record: WordRecord) -> str:
        return self._resolve_meaning_file_stem(record)

    def _resolve_meaning_file_stem(self, record: WordRecord) -> str:
        return _resolve_meaning_file_stem(record, DEFAULT_WORDS_OUTPUT_DIR)

    def _build_example_file_stem(self, record: WordRecord, example: ExampleRecord) -> str:
        return _build_example_file_stem(record, example)

    def _format_selected_record_text(self, record: WordRecord) -> str:
        meaning_lines = _build_meaning_lines(record)
        lines = [
            f"序号: {record.list_index}",
            f"Word ID: {record.word_id if record.word_id is not None else '-'}",
            f"单词: {record.word or '-'}",
            f"音标: {record.phonetic or '-'}",
            f"分类: {record.word_tag or '-'}",
            f"释义数量: {len(record.meanings)}",
            f"例句数量: {len(record.examples)}",
        ]
        if meaning_lines:
            lines.append("释义列表:")
            lines.extend(meaning_lines)
        if record.examples:
            lines.append("")
            lines.append("例句列表:")
            for example in record.examples:
                difficulty_text = str(example.difficulty) if example.difficulty is not None else "-"
                lines.append(
                    f"- 例句{example.list_index}: {example.example_en or '-'} | {example.example_zh or '-'} | 难度 {difficulty_text}"
                )
        if record.image_hint:
            lines.append(f"现有图像提示: {record.image_hint}")
        lines.append("")
        lines.append(f"来源记录索引: {', '.join(str(index) for index in record.source_record_indices)}")
        lines.append("")
        lines.append("完整字段:")
        lines.append(self._format_json_payload(record.raw))
        return "\n".join(lines)

    def _update_selection_details(self) -> None:
        return

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
        if not record.supports_image_generation:
            raise ValueError("当前记录不是单词，不生成图片。")
        return GenerationTask(
            task_id=self._allocate_task_id(),
            task_type="word_meaning",
            display_text=f"单词词卡 | word_id={record.word_id if record.word_id is not None else '-'} | {record.word}",
            record=record,
            row_code=-1,
            prompt=prompt,
            output_dir=DEFAULT_WORDS_OUTPUT_DIR,
            file_stem=self._resolve_meaning_file_stem(record),
            metadata={"generation_type": "word_meaning", "example": None},
            status_text=f"正在生成 {record.word} 的释义图片...",
        )

    def _build_example_task(self, record: WordRecord, example: ExampleRecord, prompt: str) -> GenerationTask:
        del record, example, prompt
        raise ValueError("例句不生成图片。")

    def _calculate_record_items_per_page(self) -> int:
        return RECORDS_PER_PAGE

    def _record_list_height(self) -> int:
        if not hasattr(self, "record_list"):
            row_height = RECORD_LIST_ITEM_HEIGHT + 12
            spacing = RECORD_LIST_ITEM_SPACING
            return row_height * RECORDS_PER_PAGE + spacing * max(0, RECORDS_PER_PAGE - 1) + 4
        row_height = RECORD_LIST_ITEM_HEIGHT + 12
        spacing = self.record_list.spacing()
        frame = self.record_list.frameWidth() * 2
        return row_height * RECORDS_PER_PAGE + spacing * max(0, RECORDS_PER_PAGE - 1) + frame + 4

    def _current_prompt_elapsed_seconds(self) -> int | None:
        if self._prompt_generation_started_at is None:
            return None
        return max(0, int(time.monotonic() - self._prompt_generation_started_at))

    @Slot()
    def _refresh_prompt_generation_ui(self) -> None:
        active_target = self._prompt_generation_target
        elapsed_seconds = self._current_prompt_elapsed_seconds()

        meaning_text = "生成当前单词释义提示词"
        batch_text = "开始生成选中单词提示词"
        if active_target is not None and elapsed_seconds is not None and self._current_record_index is not None:
            if active_target == (self._current_record_index, -1):
                meaning_text = f"生成中 {elapsed_seconds}s"
            if self._prompt_generation_total > 1:
                batch_text = f"批量生成中 {self._prompt_generation_completed + 1}/{self._prompt_generation_total}"

        self.generate_meaning_button.setText(meaning_text)
        self.generate_selected_meaning_button.setText(batch_text)

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

    def _rebuild_filtered_record_indices(self) -> None:
        self._filtered_record_indices = [
            index
            for index, record in enumerate(self._records)
            if self._show_hidden_records or not record.hidden
        ]

        if not self._filtered_record_indices:
            self._current_record_index = None
            self._record_page = 0
            return

        if self._current_record_index in self._filtered_record_indices:
            return

        current_index = -1 if self._current_record_index is None else self._current_record_index
        next_visible_index = next(
            (index for index in self._filtered_record_indices if index > current_index),
            None,
        )
        if next_visible_index is None:
            next_visible_index = self._filtered_record_indices[-1]
        self._current_record_index = next_visible_index
        self._current_row_code = -1

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
            queued_row_codes = self._queued_row_codes_for_record(record_index)
            item_widget = RecordListItemWidget(
                record_index,
                record,
                selected_row_code,
                queued_row_codes,
                record_index in self._batch_selected_record_indices,
                self.record_list,
            )
            item_widget.selected.connect(self._select_record_by_index)
            item_widget.rowSelected.connect(self._on_record_row_selected)
            item_widget.queueRequested.connect(self._queue_record_row_by_index)
            item_widget.deletePromptRequested.connect(self._delete_record_prompt_by_index)
            item_widget.promptGenerateRequested.connect(self._generate_record_prompt_by_index)
            item_widget.editWordPromptRequested.connect(self._edit_word_prompt_by_index)
            item_widget.renameWordImageRequested.connect(self._rename_record_image_by_index)
            item_widget.confirmRequested.connect(self._confirm_record_by_index)
            item_widget.deleteAllPromptsRequested.connect(self._delete_all_record_prompts_by_index)
            item_widget.batchSelectionChanged.connect(self._on_batch_selection_changed)
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
            widget.set_queued_row_codes(self._queued_row_codes_for_record(record_index))
            widget.set_batch_selected(record_index in self._batch_selected_record_indices)

    def _task_targets_record(self, task: GenerationTask, record: WordRecord) -> bool:
        if task.record is record:
            return True
        if task.record.word_id is not None and record.word_id is not None:
            return task.record.word_id == record.word_id
        return task.record.list_index == record.list_index and task.record.word == record.word

    def _queued_row_codes_for_record(self, record_index: int) -> set[int]:
        if record_index < 0 or record_index >= len(self._records):
            return set()

        record = self._records[record_index]
        queued_row_codes: set[int] = set()
        task_candidates = [*self._word_task_queue]
        if self._active_task is not None:
            task_candidates.append(self._active_task)

        for task in task_candidates:
            if self._task_targets_record(task, record):
                queued_row_codes.add(task.row_code)

        return queued_row_codes

    def _update_record_pagination_controls(self) -> None:
        total_pages = self._max_record_page() + 1 if self._filtered_record_indices else 0
        current_page = self._record_page + 1 if total_pages else 0
        self.record_summary_label.setText(
            f"第 {current_page} / {total_pages} 页 | 显示 {len(self._filtered_record_indices)} / 共 {len(self._records)} 条 | 本页 {self.record_list.count()} 条 | 已勾选 {len(self._batch_selected_record_indices)} 条"
        )
        self.record_prev_page_button.setEnabled(self._record_page > 0)
        self.record_next_page_button.setEnabled(self._record_page < self._max_record_page())
        self.record_page_edit.setEnabled(total_pages > 0)
        self.record_jump_page_button.setEnabled(total_pages > 0)
        self.record_page_edit.setText(str(current_page) if current_page else "")
        self.record_page_edit.setPlaceholderText("1" if total_pages else "0")

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
        self._update_selection_details()
        self._update_generate_buttons()

    def _queue_record_meaning_by_index(self, record_index: int) -> None:
        self._on_record_row_selected(record_index, -1)
        record = self._records[record_index]
        if not record.supports_image_generation:
            self.statusBar().showMessage(f"{record.word or '-'} 不是单词，不生成图片")
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

    def _queue_record_example_by_index(self, record_index: int, example_offset: int) -> None:
        del record_index, example_offset
        self.statusBar().showMessage("例句不生成图片")

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
                self._sync_record_prompt_to_memory(record, "")
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

    @Slot(int)
    def _confirm_record_by_index(self, record_index: int) -> None:
        record = self._records[record_index]
        new_hidden = not record.hidden

        try:
            self._update_record_hidden_in_json(record_index, new_hidden)
            record.hidden = new_hidden
            record.raw["hidden"] = new_hidden
            for source_record in record.source_records:
                if isinstance(source_record, dict):
                    source_record["hidden"] = new_hidden
        except Exception as exc:
            QMessageBox.critical(self, "确认记录失败", str(exc))
            return

        self._rebuild_filtered_record_indices()
        self._ensure_record_page_valid()
        self._render_record_page()
        self._update_preview_for_current_selection()
        self._update_selection_details()
        if new_hidden:
            self.statusBar().showMessage(f"已确认并隐藏记录: {record.word or '-'}")
        else:
            self.statusBar().showMessage(f"已取消确认记录: {record.word or '-'}")

    @Slot(int)
    def _delete_all_record_prompts_by_index(self, record_index: int) -> None:
        record = self._records[record_index]
        cleared_count = 0

        try:
            if record.image_hint.strip():
                cleared_count += 1
            for example in record.examples:
                if example.image_prompt.strip():
                    cleared_count += 1

            if cleared_count == 0:
                self.statusBar().showMessage(f"{record.word or '-'} 当前没有可删除的提示词")
                return

            self._update_all_record_prompts_in_json(record_index, "")
            record.image_hint = ""
            self._sync_record_prompt_to_memory(record, "")

            for example in record.examples:
                example.image_prompt = ""
                example.raw["image"] = ""
        except Exception as exc:
            QMessageBox.critical(self, "删除所有提示词失败", str(exc))
            return

        self._render_record_page()
        self._update_preview_for_current_selection()
        self.statusBar().showMessage(f"已删除 {record.word or '-'} 的 {cleared_count} 条提示词")

    def _sync_record_word_image_to_memory(self, record: WordRecord, prompt_text: str) -> None:
        for source_record in record.source_records:
            word_payload = source_record.get("word")
            if isinstance(word_payload, dict):
                word_payload["image"] = prompt_text

    def _sync_record_word_to_memory(self, record: WordRecord, word_text: str) -> None:
        for source_record in record.source_records:
            word_payload = source_record.get("word")
            if isinstance(word_payload, dict):
                word_payload["word"] = word_text

    def _sync_record_prompt_to_memory(self, record: WordRecord, prompt_text: str) -> None:
        self._sync_record_word_image_to_memory(record, prompt_text)

    def _update_record_word_image_in_json(self, record_index: int, prompt_text: str) -> None:
        json_path = Path(self.json_path_edit.text().strip())
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        record = self._records[record_index]
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("当前 JSON 记录索引无效，无法更新 word.image 字段")

        for source_record_index in record.source_record_indices:
            if source_record_index >= len(records):
                continue
            record_payload = records[source_record_index]
            if not isinstance(record_payload, dict):
                continue
            word_payload = record_payload.get("word")
            if isinstance(word_payload, dict):
                word_payload["image"] = prompt_text

        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _update_record_word_in_json(self, record_index: int, word_text: str) -> None:
        json_path = Path(self.json_path_edit.text().strip())
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        record = self._records[record_index]
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("当前 JSON 记录索引无效，无法更新 word.word 字段")

        for source_record_index in record.source_record_indices:
            if source_record_index >= len(records):
                continue
            record_payload = records[source_record_index]
            if not isinstance(record_payload, dict):
                continue
            word_payload = record_payload.get("word")
            if isinstance(word_payload, dict):
                word_payload["word"] = word_text

        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _rename_meaning_output_files(
        self,
        record: WordRecord,
        old_stem: str,
        new_stem: str,
        new_word_id: int | None,
        new_word: str,
    ) -> int:
        output_dir = DEFAULT_WORDS_OUTPUT_DIR
        rename_pairs = [
            (output_dir / f"{old_stem}.png", output_dir / f"{new_stem}.png"),
        ]
        existing_pairs = [(source, target) for source, target in rename_pairs if source.exists()]
        if not existing_pairs:
            raise FileNotFoundError(f"未找到 {old_stem} 对应的图片文件，无法修改图片名字。")

        conflicts = [
            target.name
            for source, target in existing_pairs
            if source != target and target.exists()
        ]
        if conflicts:
            raise FileExistsError(f"目标文件已存在，无法覆盖: {', '.join(conflicts)}")

        renamed_paths: list[tuple[Path, Path]] = []
        try:
            for source, target in existing_pairs:
                if source == target:
                    continue
                source.rename(target)
                renamed_paths.append((target, source))
        except Exception:
            for target, source in reversed(renamed_paths):
                if target.exists() and not source.exists():
                    target.rename(source)
            raise

        return len(existing_pairs)

    def _sync_word_tasks_after_rename(self, record: WordRecord, new_stem: str) -> None:
        task_candidates = [*self._word_task_queue]
        if self._active_task is not None:
            task_candidates.append(self._active_task)

        for task in task_candidates:
            if task.task_type != "word_meaning":
                continue
            if not self._task_targets_record(task, record):
                continue
            task.file_stem = new_stem
            task.status_text = f"正在生成 {record.word} 的释义图片..."

    def _update_record_prompt_in_json(self, record_index: int, row_code: int, prompt_text: str) -> None:
        json_path = Path(self.json_path_edit.text().strip())
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        record = self._records[record_index]
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("当前 JSON 记录索引无效，无法更新提示词字段")

        if row_code == -1:
            for source_record_index in record.source_record_indices:
                if source_record_index >= len(records):
                    continue
                record_payload = records[source_record_index]
                if not isinstance(record_payload, dict):
                    continue
                word_payload = record_payload.get("word")
                if isinstance(word_payload, dict):
                    word_payload["image"] = prompt_text
                meaning_payload = record_payload.get("word_meaning")
                if isinstance(meaning_payload, dict) and "image" in meaning_payload:
                    meaning_payload["image"] = prompt_text
        else:
            if row_code >= len(record.examples):
                raise ValueError("当前记录缺少对应例句，无法更新提示词字段")
            example = record.examples[row_code]
            if example.source_record_index is None or example.source_example_index is None:
                raise ValueError("当前例句缺少来源索引，无法更新提示词字段")
            if example.source_record_index >= len(records):
                raise ValueError("当前例句来源记录无效，无法更新提示词字段")
            record_payload = records[example.source_record_index]
            if not isinstance(record_payload, dict):
                raise ValueError("当前记录格式无效，无法更新提示词字段")
            example_payloads = record_payload.get("word_example")
            if not isinstance(example_payloads, list) or example.source_example_index >= len(example_payloads):
                raise ValueError("当前记录缺少对应例句，无法更新提示词字段")
            example_payload = example_payloads[example.source_example_index]
            if not isinstance(example_payload, dict):
                raise ValueError("当前例句格式无效，无法更新提示词字段")
            example_payload["image"] = prompt_text

        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _update_all_record_prompts_in_json(self, record_index: int, prompt_text: str) -> None:
        json_path = Path(self.json_path_edit.text().strip())
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        record = self._records[record_index]
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("当前 JSON 记录索引无效，无法更新提示词字段")

        for source_record_index in record.source_record_indices:
            if source_record_index >= len(records):
                continue
            record_payload = records[source_record_index]
            if not isinstance(record_payload, dict):
                continue

            word_payload = record_payload.get("word")
            if isinstance(word_payload, dict):
                word_payload["image"] = prompt_text

            meaning_payload = record_payload.get("word_meaning")
            if isinstance(meaning_payload, dict) and "image" in meaning_payload:
                meaning_payload["image"] = prompt_text

            example_payloads = record_payload.get("word_example")
            if isinstance(example_payloads, list):
                for example_payload in example_payloads:
                    if isinstance(example_payload, dict):
                        example_payload["image"] = prompt_text

        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _update_record_hidden_in_json(self, record_index: int, hidden: bool) -> None:
        json_path = Path(self.json_path_edit.text().strip())
        if not json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {json_path}")

        record = self._records[record_index]
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        records = payload.get("records") if isinstance(payload, dict) else payload
        if not isinstance(records, list):
            raise ValueError("当前 JSON 记录索引无效，无法更新 hidden 字段")

        for source_record_index in record.source_record_indices:
            if source_record_index >= len(records):
                continue
            record_payload = records[source_record_index]
            if isinstance(record_payload, dict):
                record_payload["hidden"] = hidden
        json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _start_prompt_generation(self, record_index: int | None, row_code: int) -> None:
        if record_index is None:
            return
        self._start_prompt_generation_batch([(record_index, row_code)])

    def _start_prompt_generation_batch(self, targets: list[tuple[int, int]]) -> None:
        if not targets:
            return
        if self._prompt_worker_thread is not None:
            QMessageBox.information(self, "提示词生成中", "当前已有提示词生成任务，请等待完成后再试。")
            return

        self._prompt_generation_queue = list(targets[1:])
        self._prompt_generation_total = len(targets)
        self._prompt_generation_completed = 0
        first_record_index, first_row_code = targets[0]
        self._start_single_prompt_generation(first_record_index, first_row_code)

    def _start_single_prompt_generation(self, record_index: int, row_code: int) -> None:
        if self._prompt_worker_thread is not None:
            return

        record = self._records[record_index]
        if row_code >= 0 and row_code >= len(record.examples):
            QMessageBox.warning(self, "无可用例句", f"当前 record 没有例句{row_code + 1}。")
            return

        system_prompt, user_prompt = self._build_prompt_generation_messages(record, row_code)
        target_text = f"{record.word or '-'} 释义" if row_code == -1 else f"{record.word or '-'} 例句{row_code + 1}"
        self._set_info_text(f"正在用 GLM-5 生成 {target_text} 的提示词，请稍候...")
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
            "如果给了现有提示词，你必须生成一个明显不同的新版本，不能复用原句，也不能只做近义改写，要更换主要场景、构图或主体动作。"
        )
        if row_code == -1:
            existing_prompt_instruction = ""
            if record.image_hint.strip():
                existing_prompt_instruction = (
                    f"现有提示词: {record.image_hint}\n"
                    "要求: 新提示词必须和现有提示词明显不同，避免相同场景、相同主体组合和相同表达。\n"
                )
            user_prompt = (
                "请为下面这个英语单词释义生成图片提示词变量部分。\n"
                f"单词: {record.word or '-'}\n"
                f"释义列表:\n{_build_meaning_prompt_context(record)}\n"
                f"{existing_prompt_instruction}"
                "要求: 直接输出 1 到 2 句中文提示词，重点描述最适合表达该释义的场景。"
            )
            return system_prompt, user_prompt

        example = record.examples[row_code]
        existing_prompt_instruction = ""
        if example.image_prompt.strip():
            existing_prompt_instruction = (
                f"现有提示词: {example.image_prompt}\n"
                "要求: 新提示词必须与现有提示词明显不同，不能只换几个词，要换场景或主体动作。\n"
            )
        user_prompt = (
            "请为下面这个英语单词例句生成图片提示词变量部分。\n"
            f"单词: {record.word or '-'}\n"
            f"释义列表:\n{_build_meaning_prompt_context(record)}\n"
            f"英文例句: {example.example_en or '-'}\n"
            f"中文例句: {example.example_zh or '-'}\n"
            f"{existing_prompt_instruction}"
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
                self._sync_record_prompt_to_memory(record, prompt_text)
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
        self._set_info_text(
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
        self._prompt_generation_completed += 1
        next_target = self._prompt_generation_queue.pop(0) if self._prompt_generation_queue else None
        self._cleanup_prompt_worker()
        if next_target is not None:
            self._start_single_prompt_generation(*next_target)
            return
        self._prompt_generation_total = 0
        self._prompt_generation_completed = 0

    @Slot(str)
    def on_prompt_generation_failed(self, error_text: str) -> None:
        self._set_info_text(f"生成提示词失败:\n{error_text}")
        QMessageBox.critical(self, "生成提示词失败", error_text)
        self.statusBar().showMessage("生成提示词失败")
        self._prompt_generation_queue.clear()
        self._prompt_generation_total = 0
        self._prompt_generation_completed = 0
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
            if record.supports_image_generation:
                preview_path = _resolve_existing_meaning_preview_path(
                    DEFAULT_WORDS_OUTPUT_DIR,
                    record,
                )
                placeholder = f"{record.word or '-'} 释义\n暂无已生成图片"
            else:
                preview_path = None
                placeholder = f"{record.word or '-'}\n当前记录不是单词，不生成图片"
        elif 0 <= self._current_row_code < len(record.examples):
            preview_path = None
            placeholder = f"{record.word or '-'} 例句{self._current_row_code + 1}\n例句不生成图片"
        else:
            self._clear_preview("暂无图片")
            return

        if preview_path is None:
            self._clear_preview(placeholder)
            return

        if not self._load_image_into_editor(preview_path, prompt_on_dirty=True):
            self._clear_preview(placeholder)
            return

    def _clear_preview(self, text: str) -> None:
        if not self._can_replace_editor_image(None, prompt_on_dirty=True):
            return
        self.image_editor.clear_image(text)
        self._update_editor_action_buttons()

    def _load_image_into_editor(self, image_path: Path, *, prompt_on_dirty: bool) -> bool:
        current_path = self.image_editor.current_image_path()
        if self.image_editor.has_image() and current_path == image_path:
            self._update_editor_action_buttons()
            return True
        if not self._can_replace_editor_image(image_path, prompt_on_dirty=prompt_on_dirty):
            return False
        loaded = self.image_editor.load_image(image_path)
        self._update_editor_action_buttons()
        return loaded

    def _can_replace_editor_image(self, target_path: Path | None, *, prompt_on_dirty: bool) -> bool:
        if not prompt_on_dirty or not self.image_editor.is_modified():
            return True
        current_path = self.image_editor.current_image_path()
        if current_path == target_path:
            return True
        answer = QMessageBox.question(
            self,
            "未保存修改",
            "当前图片有未保存修改，是否放弃这些修改并切换图片？",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        return answer == QMessageBox.Yes

    @Slot()
    def open_image_for_editing(self) -> None:
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "打开 PNG 图片",
            str(DEFAULT_WORDS_OUTPUT_DIR),
            "PNG Files (*.png)",
        )
        if not file_path:
            return
        self._load_image_into_editor(Path(file_path), prompt_on_dirty=True)

    @Slot()
    def save_current_image(self) -> None:
        try:
            saved_path = self.image_editor.save_image()
        except Exception as exc:
            QMessageBox.critical(self, "保存图片失败", str(exc))
            return
        if saved_path is not None:
            self._update_editor_action_buttons()

    @Slot()
    def undo_current_image_edit(self) -> None:
        self.image_editor.undo_last_action()
        self._update_editor_action_buttons()

    @Slot(int)
    def _on_line_width_changed(self, value: int) -> None:
        self.image_editor.set_line_width(value)

    @Slot(int)
    def _on_eraser_size_changed(self, value: int) -> None:
        self.image_editor.set_eraser_size(value)

    def set_editor_tool(self, tool_name: str) -> None:
        self.image_editor.set_tool(tool_name)
        for current_tool_name, button in self._editor_tool_buttons.items():
            button.blockSignals(True)
            button.setChecked(current_tool_name == tool_name)
            button.blockSignals(False)

    @Slot(str)
    def _on_editor_status_changed(self, message: str) -> None:
        self.statusBar().showMessage(message)

    @Slot(bool)
    def _on_editor_dirty_changed(self, dirty: bool) -> None:
        del dirty
        self._update_editor_action_buttons()

    def _update_editor_action_buttons(self) -> None:
        if hasattr(self, "save_image_button"):
            self.save_image_button.setEnabled(self.image_editor.has_image() and self.image_editor.is_modified())
        if hasattr(self, "cancel_image_edit_button"):
            self.cancel_image_edit_button.setEnabled(self.image_editor.has_image() and self.image_editor.can_undo())

    def _set_info_text(self, text: str) -> None:
        self._last_info_text = text

    def _append_info_text(self, text: str) -> None:
        existing_text = self._last_info_text.strip()
        if existing_text:
            self._last_info_text = f"{existing_text}\n\n{text}"
        else:
            self._last_info_text = text

    @Slot()
    def jump_to_record_page(self) -> None:
        if not self._filtered_record_indices:
            return

        page_text = self.record_page_edit.text().strip()
        if not page_text:
            return

        try:
            requested_page = int(page_text)
        except ValueError:
            QMessageBox.warning(self, "页码无效", "请输入有效的页码数字。")
            self.record_page_edit.setText(str(self._record_page + 1))
            self.record_page_edit.selectAll()
            return

        total_pages = self._max_record_page() + 1
        if requested_page < 1 or requested_page > total_pages:
            QMessageBox.warning(self, "页码超出范围", f"请输入 1 到 {total_pages} 之间的页码。")
            self.record_page_edit.setText(str(self._record_page + 1))
            self.record_page_edit.selectAll()
            return

        target_page = requested_page - 1
        if target_page == self._record_page:
            return

        self._record_page = target_page
        page_indices = self._current_page_record_indices()
        if page_indices and self._current_record_index not in page_indices:
            self._current_record_index = page_indices[0]
            self._current_row_code = -1
        self._render_record_page()

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

    @Slot(bool)
    def _on_show_hidden_records_toggled(self, checked: bool) -> None:
        self._show_hidden_records = checked
        self._rebuild_filtered_record_indices()
        self._ensure_record_page_valid()
        self._render_record_page()

    def _refresh_task_queue_view(self) -> None:
        self.word_task_queue_list.clear()
        if self._active_task is not None and self._active_task.task_type == "word_meaning":
            active_item = QListWidgetItem(f"#{self._active_task.task_id} [执行中] {self._active_task.display_text}")
            active_item.setData(Qt.UserRole, self._active_task.task_id)
            active_item.setForeground(QColor("#1d70b8"))
            active_item.setBackground(QColor("#fff4cc"))
            self.word_task_queue_list.addItem(active_item)
        for task in self._word_task_queue:
            item = QListWidgetItem(f"#{task.task_id} [等待] {task.display_text}")
            item.setData(Qt.UserRole, task.task_id)
            item.setBackground(QColor("#fff4cc"))
            self.word_task_queue_list.addItem(item)

        state_text = "队列已停止"
        if self._queue_running:
            state_text = "队列运行中"
        elif self._active_task is not None:
            state_text = "当前任务执行中，后续队列已停止"

        active_text = "当前无执行中的任务"
        if self._active_task is not None:
            active_text = f"执行中: #{self._active_task.task_id} {self._active_task.display_text}"

        pending_text = f"单词队列: {len(self._word_task_queue)} 项"
        hint_text = "点击“开始任务队列”后执行"
        if self._queue_running:
            hint_text = "队列会按顺序持续执行"
        elif self._active_task is None and self._pending_task_count() == 0:
            hint_text = "队列为空"
        self.queue_status_label.setText(f"{state_text}\n{active_text}\n{pending_text}\n{hint_text}")

        if self.word_task_queue_list.count() > 0:
            if self._active_task is not None and self._active_task.task_type == "word_meaning" and self.word_task_queue_list.count() > 1:
                self.word_task_queue_list.setCurrentRow(1)
            else:
                self.word_task_queue_list.setCurrentRow(0)
        self._refresh_visible_record_item_states()
        self._update_generate_buttons()

    def _pending_task_count(self) -> int:
        return len(self._word_task_queue)

    def _pop_next_task(self) -> GenerationTask | None:
        if not self._word_task_queue:
            return None
        return self._word_task_queue.pop(0)

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


def _record_supports_image_generation(record: WordRecord) -> bool:
    return record.word_id is not None and bool(record.word.strip())


def _build_meaning_summary_text(record: WordRecord) -> str:
    parts: list[str] = []
    for meaning in record.meanings:
        meaning_id_text = str(meaning.meaning_id) if meaning.meaning_id is not None else "?"
        meaning_text = meaning.meaning_zh or meaning.meaning_en or meaning.pos or "无释义"
        parts.append(f"[{meaning_id_text}] {meaning_text}")
    return "; ".join(parts) if parts else (record.meaning_zh or record.meaning_en or "无释义")


def _build_meaning_lines(record: WordRecord) -> list[str]:
    lines: list[str] = []
    for meaning in record.meanings:
        meaning_id_text = str(meaning.meaning_id) if meaning.meaning_id is not None else "?"
        pos_text = meaning.pos or "未标注词性"
        zh_text = meaning.meaning_zh or "-"
        en_text = meaning.meaning_en or "-"
        lines.append(f"- [{meaning_id_text}] {pos_text} | {zh_text} | {en_text}")
    return lines


def _build_meaning_prompt_context(record: WordRecord) -> str:
    lines = _build_meaning_lines(record)
    if lines:
        return "\n".join(lines)
    meaning_id_text = str(record.meaning_id) if record.meaning_id is not None else "?"
    return f"- [{meaning_id_text}] {record.pos or '-'} | {record.meaning_zh or '-'} | {record.meaning_en or '-'}"


def _build_image_subject_prompt(record: WordRecord) -> str:
    meaning_text = record.meaning_zh or record.meaning_en or _build_meaning_summary_text(record) or "-"
    pos_text = record.pos or record.word_type or "-"
    word_type_text = record.word_type or "-"
    return "\n".join(
        [
            f"主题单词: {record.word or '-'}",
            f"中文释义: {meaning_text}",
            f"词性: {pos_text}",
            f"单词类型: {word_type_text}",
            "要求: 画面主体必须突出表现这个单词本身对应的对象、人物、动作或核心概念，不要偏离该单词含义。",
        ]
    ).strip()


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


def _build_meaning_file_stem_from_parts(word_id: int | None, list_index: int, word: str) -> str:
    word_id_text = str(word_id) if word_id is not None else f"idx{list_index}"
    return "_".join([word_id_text, _sanitize_stem_part(word)])


def _build_meaning_file_stem(record: WordRecord) -> str:
    return _build_meaning_file_stem_from_parts(record.word_id, record.list_index, record.word)


def _find_existing_meaning_file_stem(output_dir: Path, record: WordRecord) -> str | None:
    # Strict matching: only accept an existing file whose stem exactly equals the
    # expected stem built from `word_id` and `word`. Do not perform any fuzzy or
    # substring fallback matching to avoid incorrect matches like "no" -> "nose".
    expected_stem = _build_meaning_file_stem(record)
    candidate = output_dir / f"{expected_stem}.png"
    if candidate.exists():
        return expected_stem
    return None


def _resolve_meaning_file_stem(record: WordRecord, output_dir: Path) -> str:
    existing_stem = _find_existing_meaning_file_stem(output_dir, record)
    if existing_stem:
        return existing_stem
    return _build_meaning_file_stem(record)


def _build_example_file_stem(record: WordRecord, example: ExampleRecord) -> str:
    meaning_id_text = str(record.meaning_id) if record.meaning_id is not None else f"idx{record.list_index}"
    return "_".join([example.file_id_text, meaning_id_text, _sanitize_stem_part(record.word)])


def _resolve_existing_meaning_preview_path(output_dir: Path, record: WordRecord) -> Path | None:
    return _resolve_existing_preview_path(output_dir, _resolve_meaning_file_stem(record, output_dir))


def _resolve_existing_preview_path(output_dir: Path, file_stem: str) -> Path | None:
    preview_candidates = [output_dir / f"{file_stem}.png"]
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