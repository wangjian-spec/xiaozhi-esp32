from __future__ import annotations

import argparse
import importlib
import json
import subprocess
import sys
from collections import Counter
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


DEFAULT_JSON_PATH = Path(__file__).resolve().parent / "issue_word.json"
ALL_CATEGORY_KEY = "__all__"


@dataclass(slots=True)
class IssueEntry:
    category: str
    record_index: int | None
    word_text: str
    reason: str
    payload: dict[str, Any]

    @property
    def display_text(self) -> str:
        prefix = f"#{self.record_index}" if self.record_index is not None else "#?"
        word_text = self.word_text or "<无单词>"
        return f"{prefix}  {word_text}"

    def matches(self, keyword: str) -> bool:
        if not keyword:
            return True
        lowered = keyword.casefold()
        candidates = (
            self.category,
            self.reason,
            self.word_text,
            str(self.record_index) if self.record_index is not None else "",
            json.dumps(self.payload, ensure_ascii=False),
        )
        return any(lowered in value.casefold() for value in candidates)


class IssueRepository:
    def __init__(self, json_path: Path) -> None:
        self.json_path = json_path
        self.metadata: dict[str, Any] = {}
        self.entries: list[IssueEntry] = []
        self.category_counts: Counter[str] = Counter()

    def load(self) -> None:
        if not self.json_path.exists():
            raise FileNotFoundError(f"JSON 文件不存在: {self.json_path}")

        payload = json.loads(self.json_path.read_text(encoding="utf-8"))
        if isinstance(payload, list):
            payload = {"metadata": {}, "issues": payload}
        if not isinstance(payload, dict):
            raise ValueError("issue_word.json 格式不正确，根节点必须是对象或数组")

        issues = payload.get("issues")
        if issues is None:
            issues = payload.get("records")
        if not isinstance(issues, list):
            raise ValueError("JSON 中缺少 issues 数组")

        metadata = payload.get("metadata")
        self.metadata = metadata if isinstance(metadata, dict) else {}

        entries: list[IssueEntry] = []
        for item in issues:
            if not isinstance(item, dict):
                continue
            category = str(item.get("issue_type") or "uncategorized")
            record_index = item.get("record_index")
            if not isinstance(record_index, int):
                record_index = None
            word_payload = item.get("word")
            word_text = ""
            if isinstance(word_payload, dict):
                word_text = str(word_payload.get("word") or "")
            reason = str(item.get("reason") or "")
            entries.append(
                IssueEntry(
                    category=category,
                    record_index=record_index,
                    word_text=word_text,
                    reason=reason,
                    payload=item,
                )
            )

        entries.sort(key=lambda entry: (entry.category.casefold(), entry.record_index is None, entry.record_index or 0))
        self.entries = entries
        self.category_counts = Counter(entry.category for entry in entries)

    def categories(self) -> list[tuple[str, int]]:
        ordered = sorted(self.category_counts.items(), key=lambda item: (-item[1], item[0].casefold()))
        return [(ALL_CATEGORY_KEY, len(self.entries)), *ordered]

    def filter_entries(self, category: str, keyword: str) -> list[IssueEntry]:
        return [
            entry
            for entry in self.entries
            if (category == ALL_CATEGORY_KEY or entry.category == category) and entry.matches(keyword)
        ]


def print_summary(json_path: Path) -> int:
    repository = IssueRepository(json_path)
    repository.load()

    print(f"文件: {json_path}")
    print(f"问题总数: {len(repository.entries)}")
    if repository.metadata:
        source_file = repository.metadata.get("source_file")
        checked_range = repository.metadata.get("checked_range")
        checked_at = repository.metadata.get("checked_at")
        if source_file:
            print(f"来源文件: {source_file}")
        if checked_range:
            print(f"检查范围: {checked_range}")
        if checked_at:
            print(f"检查时间: {checked_at}")

    print("\n分类统计:")
    for category, count in repository.categories()[1:]:
        print(f"- {category}: {count}")
    return 0


_ensure_dependency("PySide6.QtCore", "PySide6")

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
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


class IssueWordCategoryViewer(QMainWindow):
    def __init__(self, json_path: Path) -> None:
        super().__init__()
        self.setWindowTitle("Issue Word 分类浏览器")
        self.resize(1500, 920)

        self.repository = IssueRepository(json_path)
        self.filtered_entries: list[IssueEntry] = []

        self.path_edit = QLineEdit(str(json_path))
        self.path_edit.setPlaceholderText("选择 issue_word.json 文件")

        self.open_button = QPushButton("打开 JSON")
        self.reload_button = QPushButton("重新加载")
        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("搜索单词、原因、分类、记录号")

        self.summary_label = QLabel()
        self.summary_label.setWordWrap(True)
        self.summary_label.setStyleSheet("font-size: 13px; color: #444444;")

        self.category_list = QListWidget()
        self.record_list = QListWidget()
        self.detail_title = QLabel("请选择一条记录")
        self.detail_title.setStyleSheet("font-size: 16px; font-weight: 600;")
        self.detail_reason = QLabel()
        self.detail_reason.setWordWrap(True)
        self.detail_reason.setStyleSheet("color: #555555;")
        self.detail_view = QPlainTextEdit()
        self.detail_view.setReadOnly(True)

        top_layout = QHBoxLayout()
        top_layout.addWidget(QLabel("文件:"))
        top_layout.addWidget(self.path_edit, 1)
        top_layout.addWidget(self.open_button)
        top_layout.addWidget(self.reload_button)
        top_layout.addSpacing(12)
        top_layout.addWidget(QLabel("搜索:"))
        top_layout.addWidget(self.search_edit, 1)

        category_group = QGroupBox("问题分类")
        category_layout = QVBoxLayout(category_group)
        category_layout.addWidget(self.category_list)

        record_group = QGroupBox("问题记录")
        record_layout = QVBoxLayout(record_group)
        record_layout.addWidget(self.record_list)

        detail_group = QGroupBox("详情")
        detail_layout = QVBoxLayout(detail_group)
        detail_layout.addWidget(self.detail_title)
        detail_layout.addWidget(self.detail_reason)
        detail_layout.addWidget(self.detail_view, 1)

        splitter = QSplitter()
        splitter.addWidget(category_group)
        splitter.addWidget(record_group)
        splitter.addWidget(detail_group)
        splitter.setStretchFactor(0, 2)
        splitter.setStretchFactor(1, 3)
        splitter.setStretchFactor(2, 5)

        central = QWidget(self)
        central_layout = QVBoxLayout(central)
        central_layout.setContentsMargins(12, 12, 12, 12)
        central_layout.setSpacing(10)
        central_layout.addLayout(top_layout)
        central_layout.addWidget(self.summary_label)
        central_layout.addWidget(splitter, 1)
        self.setCentralWidget(central)

        self.setStatusBar(QStatusBar(self))

        open_action = QAction("打开 JSON", self)
        open_action.triggered.connect(self.choose_json_file)
        self.menuBar().addAction(open_action)

        self.open_button.clicked.connect(self.choose_json_file)
        self.reload_button.clicked.connect(self.reload_from_path)
        self.path_edit.returnPressed.connect(self.reload_from_path)
        self.search_edit.textChanged.connect(self.refresh_records)
        self.category_list.currentItemChanged.connect(self.refresh_records)
        self.record_list.currentItemChanged.connect(self.show_selected_record)

        self.reload_from_path()

    def choose_json_file(self) -> None:
        selected_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 issue_word.json",
            str(Path(self.path_edit.text()).resolve().parent if self.path_edit.text().strip() else DEFAULT_JSON_PATH.parent),
            "JSON Files (*.json);;All Files (*)",
        )
        if not selected_path:
            return
        self.path_edit.setText(selected_path)
        self.reload_from_path()

    def reload_from_path(self) -> None:
        json_path = Path(self.path_edit.text().strip() or DEFAULT_JSON_PATH).expanduser()
        try:
            self.repository = IssueRepository(json_path)
            self.repository.load()
        except Exception as exc:
            QMessageBox.critical(self, "加载失败", str(exc))
            self.statusBar().showMessage(f"加载失败: {exc}", 6000)
            return

        self.path_edit.setText(str(json_path))
        self.populate_categories()
        self.update_summary()
        self.statusBar().showMessage(f"已加载 {len(self.repository.entries)} 条问题记录", 5000)

    def populate_categories(self) -> None:
        self.category_list.blockSignals(True)
        self.category_list.clear()
        for category, count in self.repository.categories():
            label = f"全部问题 ({count})" if category == ALL_CATEGORY_KEY else f"{category} ({count})"
            item = QListWidgetItem(label)
            item.setData(Qt.ItemDataRole.UserRole, category)
            self.category_list.addItem(item)
        self.category_list.blockSignals(False)
        if self.category_list.count() > 0:
            self.category_list.setCurrentRow(0)
        else:
            self.refresh_records()

    def update_summary(self) -> None:
        metadata = self.repository.metadata
        parts = [f"问题总数: {len(self.repository.entries)}", f"分类数: {len(self.repository.category_counts)}"]
        if metadata.get("source_file"):
            parts.append(f"来源: {metadata['source_file']}")
        if metadata.get("checked_range"):
            parts.append(f"范围: {metadata['checked_range']}")
        if metadata.get("checked_at"):
            parts.append(f"检查时间: {metadata['checked_at']}")
        if metadata.get("note"):
            parts.append(f"备注: {metadata['note']}")
        self.summary_label.setText(" | ".join(str(part) for part in parts))

    def current_category(self) -> str:
        item = self.category_list.currentItem()
        if item is None:
            return ALL_CATEGORY_KEY
        return str(item.data(Qt.ItemDataRole.UserRole) or ALL_CATEGORY_KEY)

    def refresh_records(self) -> None:
        self.record_list.blockSignals(True)
        self.record_list.clear()

        keyword = self.search_edit.text().strip()
        self.filtered_entries = self.repository.filter_entries(self.current_category(), keyword)

        for entry in self.filtered_entries:
            subtitle = entry.reason or entry.category
            item = QListWidgetItem(f"{entry.display_text}    {subtitle}")
            item.setData(Qt.ItemDataRole.UserRole, entry)
            self.record_list.addItem(item)

        self.record_list.blockSignals(False)
        if self.record_list.count() > 0:
            self.record_list.setCurrentRow(0)
        else:
            self.show_record(None)

        self.statusBar().showMessage(f"当前显示 {len(self.filtered_entries)} 条记录", 3000)

    def show_selected_record(self) -> None:
        item = self.record_list.currentItem()
        entry = item.data(Qt.ItemDataRole.UserRole) if item is not None else None
        if isinstance(entry, IssueEntry):
            self.show_record(entry)
        else:
            self.show_record(None)

    def show_record(self, entry: IssueEntry | None) -> None:
        if entry is None:
            self.detail_title.setText("没有匹配的记录")
            self.detail_reason.setText("")
            self.detail_view.clear()
            return

        prefix = f"记录 #{entry.record_index}" if entry.record_index is not None else "记录号缺失"
        word_text = entry.word_text or "<无单词>"
        self.detail_title.setText(f"{prefix} | {word_text} | {entry.category}")
        self.detail_reason.setText(entry.reason or "无原因说明")
        self.detail_view.setPlainText(json.dumps(entry.payload, ensure_ascii=False, indent=2))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="浏览 issue_word.json 的问题分类和记录详情")
    parser.add_argument("json_path", nargs="?", default=str(DEFAULT_JSON_PATH), help="issue_word.json 路径")
    parser.add_argument("--summary", action="store_true", help="仅输出分类统计，不打开界面")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    json_path = Path(args.json_path).expanduser()

    if args.summary:
        return print_summary(json_path)

    app = QApplication(sys.argv)
    window = IssueWordCategoryViewer(json_path)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())