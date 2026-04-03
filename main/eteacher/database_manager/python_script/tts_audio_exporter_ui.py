from __future__ import annotations

import asyncio
import importlib
import math
import os
import struct
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
import re


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


_ensure_dependency("edge_tts", "edge-tts")
_ensure_dependency("PySide6.QtCore", "PySide6")

import edge_tts

from PySide6.QtCore import QObject, QThread, Qt, Signal, Slot
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
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
    QVBoxLayout,
    QWidget,
)


DEFAULT_OUTPUT_DIR = Path(r"D:\王健备份\个人\英语口语教师\图片和音频资源\audio\word")
DEFAULT_DB_PATH = Path(__file__).resolve().parent / "words.db"
DEFAULT_VOICE = "en-US-AriaNeural"
DEFAULT_RATE = "-10%"
DEFAULT_BUNDLE_NAME = "audio_bundle.bin"

BUNDLE_MAGIC = b"OGGBIN1\0"
BUNDLE_VERSION = 1
BUNDLE_HEADER_FORMAT = "<8sIIIQ"
BUNDLE_RECORD_FORMAT = "<128sQQIII"
BUNDLE_HEADER_SIZE = struct.calcsize(BUNDLE_HEADER_FORMAT)
BUNDLE_RECORD_SIZE = struct.calcsize(BUNDLE_RECORD_FORMAT)
BUNDLE_ALIGNMENT = 4

KIND_CODES = {
    "word": 1,
    "example": 2,
}

STAGE_LABELS = {
    1: "primary",
    2: "junior",
    3: "senior",
    4: "cet4",
    5: "cet6",
    6: "postgraduate",
    7: "tem4",
    8: "tem8",
    9: "toefl",
    10: "ielts",
    11: "gre",
}

INVALID_FILENAME_PATTERN = re.compile(r'[<>:"/\\|?*\x00-\x1F]')
WHITESPACE_PATTERN = re.compile(r"\s+")
WINDOWS_RESERVED_NAMES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    "COM1",
    "COM2",
    "COM3",
    "COM4",
    "COM5",
    "COM6",
    "COM7",
    "COM8",
    "COM9",
    "LPT1",
    "LPT2",
    "LPT3",
    "LPT4",
    "LPT5",
    "LPT6",
    "LPT7",
    "LPT8",
    "LPT9",
}


@dataclass(slots=True)
class ExportItem:
    kind: str
    source_id: int
    text: str
    output_path: Path
    display_name: str


@dataclass(slots=True)
class ExportConfig:
    db_path: Path
    output_dir: Path
    voice: str
    rate: str
    export_words: bool
    export_examples: bool
    overwrite: bool


@dataclass(slots=True)
class BundleConfig:
    input_dir: Path
    output_dir: Path
    bundle_name: str
    overwrite: bool


@dataclass(slots=True)
class BundleItem:
    name: str
    file_path: Path
    kind: str
    source_id: int


@dataclass(slots=True)
class BundleEntry:
    name: str
    offset: int
    size: int
    kind_code: int
    source_id: int


def sanitize_filename(name: str, fallback: str) -> str:
    cleaned = INVALID_FILENAME_PATTERN.sub("_", name.strip())
    cleaned = WHITESPACE_PATTERN.sub(" ", cleaned)
    cleaned = cleaned.rstrip(" .")
    if not cleaned:
        cleaned = fallback
    if cleaned.upper() in WINDOWS_RESERVED_NAMES:
        cleaned = f"_{cleaned}"
    return cleaned


def normalize_text(text: str) -> str:
    return WHITESPACE_PATTERN.sub(" ", text.strip())


def stage_token(stage: int | None) -> str:
    if stage is None:
        return "stage0_unknown"
    label = STAGE_LABELS.get(stage, f"unknown{stage}")
    return f"stage{stage}_{label}"


def normalize_bundle_name(name: str) -> str:
    cleaned = sanitize_filename(name.strip(), "audio_bundle")
    if not cleaned.lower().endswith(".bin"):
        cleaned = f"{cleaned}.bin"
    return cleaned


def bundle_path_from_config(config: BundleConfig) -> Path:
    return config.output_dir / normalize_bundle_name(config.bundle_name)


def _encode_bundle_name(name: str) -> bytes:
    encoded = name.encode("utf-8")
    if len(encoded) >= 128:
        raise ValueError(f"文件名过长，无法写入索引: {name}")
    return encoded.ljust(128, b"\0")


def _aligned_size(size: int, alignment: int = BUNDLE_ALIGNMENT) -> int:
    return int(math.ceil(size / alignment) * alignment) if size else 0


def build_bundle_items_from_directory(input_dir: Path, bundle_path: Path) -> list[BundleItem]:
    bundle_items: list[BundleItem] = []
    for file_path in sorted(input_dir.glob("*.ogg"), key=lambda path: path.name.lower()):
        if file_path.resolve() == bundle_path.resolve():
            continue
        bundle_items.append(
            BundleItem(
                name=file_path.name,
                file_path=file_path,
                kind="unknown",
                source_id=0,
            )
        )
    return bundle_items


def write_audio_bundle(bundle_path: Path, items: list[BundleItem], overwrite: bool) -> tuple[int, int]:
    if not items:
        raise ValueError("没有可打包的 OGG 文件。")
    if bundle_path.exists() and not overwrite:
        raise FileExistsError(f"打包文件已存在: {bundle_path}")

    bundle_path.parent.mkdir(parents=True, exist_ok=True)
    entries: list[BundleEntry] = []
    data_offset = BUNDLE_HEADER_SIZE + BUNDLE_RECORD_SIZE * len(items)
    current_offset = data_offset

    for item in items:
        if not item.file_path.exists():
            raise FileNotFoundError(f"音频文件不存在: {item.file_path}")
        size = item.file_path.stat().st_size
        entries.append(
            BundleEntry(
                name=item.name,
                offset=current_offset,
                size=size,
                kind_code=KIND_CODES.get(item.kind, 0),
                source_id=item.source_id,
            )
        )
        current_offset += _aligned_size(size)

    temp_bundle = bundle_path.with_suffix(bundle_path.suffix + ".tmp")
    with open(temp_bundle, "wb") as handle:
        handle.write(
            struct.pack(
                BUNDLE_HEADER_FORMAT,
                BUNDLE_MAGIC,
                BUNDLE_VERSION,
                len(entries),
                BUNDLE_RECORD_SIZE,
                data_offset,
            )
        )
        for entry in entries:
            handle.write(
                struct.pack(
                    BUNDLE_RECORD_FORMAT,
                    _encode_bundle_name(entry.name),
                    entry.offset,
                    entry.size,
                    entry.kind_code,
                    entry.source_id,
                    0,
                )
            )

        for entry, item in zip(entries, items):
            with open(item.file_path, "rb") as source:
                shutil.copyfileobj(source, handle)
            padding = _aligned_size(entry.size) - entry.size
            if padding > 0:
                handle.write(b"\0" * padding)

    os.replace(temp_bundle, bundle_path)
    return len(entries), bundle_path.stat().st_size


def build_export_items(config: ExportConfig) -> tuple[list[ExportItem], list[str]]:
    if not config.db_path.exists():
        raise FileNotFoundError(f"数据库不存在: {config.db_path}")

    planned_files: set[str] = set()
    warnings: list[str] = []
    items: list[ExportItem] = []

    def allocate_name(base_name: str, fallback: str, extension: str, source_id: int) -> str:
        sanitized = sanitize_filename(base_name, fallback)
        filename = f"{sanitized}{extension}"
        if filename not in planned_files:
            planned_files.add(filename)
            return filename
        alternate = f"{sanitized}_{source_id}{extension}"
        if alternate not in planned_files:
            planned_files.add(alternate)
            warnings.append(f"检测到文件名冲突，已改为: {alternate}")
            return alternate
        counter = 2
        while True:
            candidate = f"{sanitized}_{source_id}_{counter}{extension}"
            if candidate not in planned_files:
                planned_files.add(candidate)
                warnings.append(f"检测到文件名冲突，已改为: {candidate}")
                return candidate
            counter += 1

    with sqlite3.connect(config.db_path) as conn:
        conn.row_factory = sqlite3.Row

        if config.export_words:
            word_rows = conn.execute(
                """
                SELECT id, word
                FROM word
                WHERE TRIM(COALESCE(word, '')) <> ''
                ORDER BY id
                """
            ).fetchall()
            for row in word_rows:
                word_text = normalize_text(str(row["word"]))
                filename = allocate_name(word_text, f"word_{row['id']}", ".ogg", int(row["id"]))
                items.append(
                    ExportItem(
                        kind="word",
                        source_id=int(row["id"]),
                        text=word_text,
                        output_path=config.output_dir / filename,
                        display_name=word_text,
                    )
                )

        if config.export_examples:
            example_rows = conn.execute(
                """
                SELECT we.id, w.word, wm.stage AS stage, we.example_en
                FROM word_example AS we
                JOIN word_meaning AS wm ON wm.id = we.meaning_id
                JOIN word AS w ON w.id = wm.word_id
                WHERE TRIM(COALESCE(we.example_en, '')) <> ''
                ORDER BY w.word, wm.stage, we.id
                """
            ).fetchall()
            sequence_by_word_stage: dict[tuple[str, int | None], int] = defaultdict(int)
            for row in example_rows:
                word_text = normalize_text(str(row["word"]))
                stage_value = row["stage"]
                stage = int(stage_value) if stage_value is not None else None
                sequence_key = (word_text, stage)
                sequence_by_word_stage[sequence_key] += 1
                sequence = sequence_by_word_stage[sequence_key]
                base_name = f"{word_text}_{stage_token(stage)}_{sequence:03d}"
                filename = allocate_name(base_name, f"example_{row['id']}", ".ogg", int(row["id"]))
                items.append(
                    ExportItem(
                        kind="example",
                        source_id=int(row["id"]),
                        text=normalize_text(str(row["example_en"])),
                        output_path=config.output_dir / filename,
                        display_name=base_name,
                    )
                )

    return items, warnings


class ExportWorker(QObject):
    log = Signal(str)
    progress = Signal(int, int)
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(self, config: ExportConfig) -> None:
        super().__init__()
        self.config = config
        self._cancel_requested = False

    @Slot()
    def run(self) -> None:
        try:
            result = asyncio.run(self._run_async())
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.finished.emit(result)

    def cancel(self) -> None:
        self._cancel_requested = True

    async def _run_async(self) -> dict[str, int | bool | str]:
        ffmpeg_path = shutil.which("ffmpeg")
        if not ffmpeg_path:
            raise RuntimeError("未找到 ffmpeg，请先安装 ffmpeg 并确保其已加入 PATH。")

        items, warnings = build_export_items(self.config)
        total = len(items)
        if total == 0:
            return {
                "total": 0,
                "exported": 0,
                "skipped": 0,
                "failed": 0,
                "cancelled": False,
            }

        for message in warnings[:20]:
            self.log.emit(message)
        if len(warnings) > 20:
            self.log.emit(f"另有 {len(warnings) - 20} 条文件名冲突提示未展开。")

        self.log.emit(f"共加载 {total} 条任务，开始生成 OGG 音频。")

        exported = 0
        skipped = 0
        failed = 0

        with tempfile.TemporaryDirectory(prefix="edge_tts_export_") as temp_dir:
            temp_root = Path(temp_dir)
            for index, item in enumerate(items, start=1):
                if self._cancel_requested:
                    self.log.emit("检测到取消请求，已停止后续导出。")
                    return {
                        "total": total,
                        "exported": exported,
                        "skipped": skipped,
                        "failed": failed,
                        "cancelled": True,
                    }

                self.progress.emit(index, total)
                if item.output_path.exists() and not self.config.overwrite:
                    skipped += 1
                    self.log.emit(f"[{index}/{total}] 跳过已存在文件: {item.output_path.name}")
                    continue

                temp_audio = temp_root / f"{item.kind}_{item.source_id}.mp3"
                try:
                    communicate = edge_tts.Communicate(text=item.text, voice=self.config.voice, rate=self.config.rate)
                    await communicate.save(str(temp_audio))
                    item.output_path.parent.mkdir(parents=True, exist_ok=True)
                    result = subprocess.run(
                        [
                            ffmpeg_path,
                            "-y",
                            "-hide_banner",
                            "-loglevel",
                            "error",
                            "-i",
                            str(temp_audio),
                            "-c:a",
                            "libvorbis",
                            str(item.output_path),
                        ],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    if result.returncode != 0:
                        raise RuntimeError(result.stderr.strip() or "ffmpeg 转码失败")
                    exported += 1
                    self.log.emit(f"[{index}/{total}] 已生成: {item.output_path.name}")
                except Exception as exc:
                    failed += 1
                    self.log.emit(f"[{index}/{total}] 失败: {item.display_name} -> {exc}")
                finally:
                    if temp_audio.exists():
                        temp_audio.unlink(missing_ok=True)

        return {
            "total": total,
            "exported": exported,
            "skipped": skipped,
            "failed": failed,
            "cancelled": False,
        }


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("words.db 音频批量导出")
        self.resize(900, 680)

        self.worker_thread: QThread | None = None
        self.worker: ExportWorker | None = None

        central = QWidget(self)
        self.setCentralWidget(central)

        layout = QVBoxLayout(central)
        form_layout = QGridLayout()
        layout.addLayout(form_layout)

        self.db_path_edit = QLineEdit(str(DEFAULT_DB_PATH))
        self.output_dir_edit = QLineEdit(str(DEFAULT_OUTPUT_DIR))
        self.voice_edit = QLineEdit(DEFAULT_VOICE)
        self.rate_edit = QLineEdit(DEFAULT_RATE)
        self.bundle_input_dir_edit = QLineEdit(str(DEFAULT_OUTPUT_DIR))
        self.bundle_output_dir_edit = QLineEdit(str(DEFAULT_OUTPUT_DIR))
        self.bundle_name_edit = QLineEdit(DEFAULT_BUNDLE_NAME)

        db_browse_button = QPushButton("选择数据库")
        db_browse_button.clicked.connect(self.select_db_path)
        output_browse_button = QPushButton("选择输出目录")
        output_browse_button.clicked.connect(self.select_output_dir)
        bundle_input_browse_button = QPushButton("选择打包输入目录")
        bundle_input_browse_button.clicked.connect(self.select_bundle_input_dir)
        bundle_output_browse_button = QPushButton("选择打包输出目录")
        bundle_output_browse_button.clicked.connect(self.select_bundle_output_dir)

        export_section_label = QLabel("音频导出")
        export_section_label.setStyleSheet("font-weight: bold;")
        bundle_section_label = QLabel("BIN 打包")
        bundle_section_label.setStyleSheet("font-weight: bold;")

        form_layout.addWidget(export_section_label, 0, 0)
        form_layout.addWidget(QLabel("words.db 路径"), 1, 0)
        form_layout.addWidget(self.db_path_edit, 1, 1)
        form_layout.addWidget(db_browse_button, 1, 2)

        form_layout.addWidget(QLabel("音频导出目录"), 2, 0)
        form_layout.addWidget(self.output_dir_edit, 2, 1)
        form_layout.addWidget(output_browse_button, 2, 2)

        form_layout.addWidget(QLabel("Edge TTS 声音"), 3, 0)
        form_layout.addWidget(self.voice_edit, 3, 1, 1, 2)

        form_layout.addWidget(QLabel("语速"), 4, 0)
        form_layout.addWidget(self.rate_edit, 4, 1, 1, 2)

        form_layout.addWidget(bundle_section_label, 5, 0)
        form_layout.addWidget(QLabel("打包输入目录"), 6, 0)
        form_layout.addWidget(self.bundle_input_dir_edit, 6, 1)
        form_layout.addWidget(bundle_input_browse_button, 6, 2)

        form_layout.addWidget(QLabel("打包输出目录"), 7, 0)
        form_layout.addWidget(self.bundle_output_dir_edit, 7, 1)
        form_layout.addWidget(bundle_output_browse_button, 7, 2)

        form_layout.addWidget(QLabel("BIN 文件名"), 8, 0)
        form_layout.addWidget(self.bundle_name_edit, 8, 1, 1, 2)

        self.export_words_checkbox = QCheckBox("导出 word 表中的单词音频")
        self.export_words_checkbox.setChecked(True)
        self.export_examples_checkbox = QCheckBox("导出 word_example 表中的 example_en 例句音频")
        self.export_examples_checkbox.setChecked(True)
        self.overwrite_checkbox = QCheckBox("覆盖已存在的音频文件")
        self.overwrite_checkbox.setChecked(False)
        self.bundle_overwrite_checkbox = QCheckBox("覆盖已存在的 BIN 文件")
        self.bundle_overwrite_checkbox.setChecked(False)

        layout.addWidget(self.export_words_checkbox)
        layout.addWidget(self.export_examples_checkbox)
        layout.addWidget(self.overwrite_checkbox)
        layout.addWidget(self.bundle_overwrite_checkbox)

        hint_label = QLabel(
            "默认声音为 en-US-AriaNeural（女生，美音），默认语速 -10%。\n"
            "单词文件名格式：单词.ogg\n"
            "例句文件名格式：单词_stage序号标签_例句序号.ogg，例如 apple_stage2_junior_001.ogg\n"
            "打包功能独立于导出功能。BIN 包会写入固定长度索引，每条索引包含文件名、偏移、长度、类型和源 ID，便于 ESP32-S3 直接按索引定位读取。"
        )
        hint_label.setWordWrap(True)
        hint_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        layout.addWidget(hint_label)

        export_button_layout = QHBoxLayout()
        self.start_button = QPushButton("开始生成音频")
        self.start_button.clicked.connect(self.start_export)
        self.cancel_button = QPushButton("取消")
        self.cancel_button.clicked.connect(self.cancel_export)
        self.cancel_button.setEnabled(False)
        self.preview_button = QPushButton("预览任务数量")
        self.preview_button.clicked.connect(self.preview_export_counts)
        self.bundle_button = QPushButton("开始打包 BIN")
        self.bundle_button.clicked.connect(self.bundle_existing_audio)

        export_button_layout.addWidget(self.start_button)
        export_button_layout.addWidget(self.cancel_button)
        export_button_layout.addWidget(self.preview_button)
        layout.addLayout(export_button_layout)

        bundle_button_layout = QHBoxLayout()
        bundle_button_layout.addWidget(self.bundle_button)
        layout.addLayout(bundle_button_layout)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        layout.addWidget(self.progress_bar)

        self.status_label = QLabel("等待开始")
        layout.addWidget(self.status_label)

        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        layout.addWidget(self.log_edit)

    def build_config(self) -> ExportConfig:
        return ExportConfig(
            db_path=Path(self.db_path_edit.text().strip()),
            output_dir=Path(self.output_dir_edit.text().strip()),
            voice=self.voice_edit.text().strip() or DEFAULT_VOICE,
            rate=self.rate_edit.text().strip() or DEFAULT_RATE,
            export_words=self.export_words_checkbox.isChecked(),
            export_examples=self.export_examples_checkbox.isChecked(),
            overwrite=self.overwrite_checkbox.isChecked(),
        )

    def build_bundle_config(self) -> BundleConfig:
        return BundleConfig(
            input_dir=Path(self.bundle_input_dir_edit.text().strip()),
            output_dir=Path(self.bundle_output_dir_edit.text().strip()),
            bundle_name=normalize_bundle_name(self.bundle_name_edit.text().strip() or DEFAULT_BUNDLE_NAME),
            overwrite=self.bundle_overwrite_checkbox.isChecked(),
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
    def select_output_dir(self) -> None:
        folder = QFileDialog.getExistingDirectory(
            self,
            "选择输出目录",
            self.output_dir_edit.text().strip() or str(DEFAULT_OUTPUT_DIR),
        )
        if folder:
            self.output_dir_edit.setText(folder)

    @Slot()
    def select_bundle_input_dir(self) -> None:
        folder = QFileDialog.getExistingDirectory(
            self,
            "选择打包输入目录",
            self.bundle_input_dir_edit.text().strip() or str(DEFAULT_OUTPUT_DIR),
        )
        if folder:
            self.bundle_input_dir_edit.setText(folder)

    @Slot()
    def select_bundle_output_dir(self) -> None:
        folder = QFileDialog.getExistingDirectory(
            self,
            "选择打包输出目录",
            self.bundle_output_dir_edit.text().strip() or str(DEFAULT_OUTPUT_DIR),
        )
        if folder:
            self.bundle_output_dir_edit.setText(folder)

    @Slot()
    def preview_export_counts(self) -> None:
        try:
            config = self.build_config()
            if not config.export_words and not config.export_examples:
                raise ValueError("请至少勾选一种导出内容。")
            items, warnings = build_export_items(config)
            word_count = sum(1 for item in items if item.kind == "word")
            example_count = sum(1 for item in items if item.kind == "example")
            message = (
                f"预览完成：单词 {word_count} 条，例句 {example_count} 条，总计 {len(items)} 条。"
            )
            self.append_log(message)
            if warnings:
                self.append_log(f"另检测到 {len(warnings)} 条文件名冲突，导出时会自动处理。")
            self.status_label.setText(message)
        except Exception as exc:
            QMessageBox.critical(self, "预览失败", str(exc))

    @Slot()
    def bundle_existing_audio(self) -> None:
        try:
            config = self.build_bundle_config()
            if not config.input_dir.exists():
                raise FileNotFoundError(f"打包输入目录不存在: {config.input_dir}")
            config.output_dir.mkdir(parents=True, exist_ok=True)
            bundle_output_path = bundle_path_from_config(config)
            items = build_bundle_items_from_directory(config.input_dir, bundle_output_path)
            bundle_count, bundle_size = write_audio_bundle(
                bundle_output_path,
                items,
                overwrite=config.overwrite,
            )
        except Exception as exc:
            QMessageBox.critical(self, "打包失败", str(exc))
            return

        message = (
            f"BIN 打包完成：{bundle_output_path}，包含 {bundle_count} 个音频，"
            f"文件大小 {bundle_size} 字节。"
        )
        self.append_log(message)
        self.status_label.setText(message)
        QMessageBox.information(self, "打包完成", message)

    @Slot()
    def start_export(self) -> None:
        try:
            config = self.build_config()
            if not config.export_words and not config.export_examples:
                raise ValueError("请至少勾选一种导出内容。")
            if not config.db_path.exists():
                raise FileNotFoundError(f"数据库不存在: {config.db_path}")
            config.output_dir.mkdir(parents=True, exist_ok=True)
        except Exception as exc:
            QMessageBox.critical(self, "参数错误", str(exc))
            return

        self.log_edit.clear()
        self.progress_bar.setValue(0)
        self.status_label.setText("准备导出...")
        self.start_button.setEnabled(False)
        self.preview_button.setEnabled(False)
        self.bundle_button.setEnabled(False)
        self.cancel_button.setEnabled(True)

        self.worker_thread = QThread(self)
        self.worker = ExportWorker(config)
        self.worker.moveToThread(self.worker_thread)

        self.worker_thread.started.connect(self.worker.run)
        self.worker.log.connect(self.append_log)
        self.worker.progress.connect(self.update_progress)
        self.worker.finished.connect(self.on_export_finished)
        self.worker.failed.connect(self.on_export_failed)
        self.worker.finished.connect(self.worker_thread.quit)
        self.worker.failed.connect(self.worker_thread.quit)
        self.worker_thread.finished.connect(self.cleanup_worker)

        self.worker_thread.start()

    @Slot()
    def cancel_export(self) -> None:
        if self.worker is not None:
            self.worker.cancel()
            self.append_log("已请求取消，当前任务完成后停止。")
            self.status_label.setText("取消中...")
            self.cancel_button.setEnabled(False)

    @Slot(int, int)
    def update_progress(self, current: int, total: int) -> None:
        percent = 0 if total == 0 else int(current * 100 / total)
        self.progress_bar.setValue(percent)
        self.status_label.setText(f"导出进度: {current}/{total}")

    @Slot(dict)
    def on_export_finished(self, result: dict[str, int | bool]) -> None:
        self.start_button.setEnabled(True)
        self.preview_button.setEnabled(True)
        self.bundle_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        self.progress_bar.setValue(100 if result["total"] else 0)

        summary = (
            f"任务结束：总数 {result['total']}，成功 {result['exported']}，"
            f"跳过 {result['skipped']}，失败 {result['failed']}。"
        )
        if result["cancelled"]:
            summary = "任务已取消。" + summary
        self.append_log(summary)
        self.status_label.setText(summary)
        QMessageBox.information(self, "导出完成", summary)

    @Slot(str)
    def on_export_failed(self, message: str) -> None:
        self.start_button.setEnabled(True)
        self.preview_button.setEnabled(True)
        self.bundle_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        self.append_log(f"导出失败: {message}")
        self.status_label.setText("导出失败")
        QMessageBox.critical(self, "导出失败", message)

    @Slot()
    def cleanup_worker(self) -> None:
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