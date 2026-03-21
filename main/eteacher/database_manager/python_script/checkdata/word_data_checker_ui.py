from __future__ import annotations

import importlib
import re
import sqlite3
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass
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
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
    QGridLayout,
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)


DEFAULT_DB_PATH = Path(__file__).resolve().parents[1] / "words.db"
WHITESPACE_PATTERN = re.compile(r"\s+")
WORD_PATTERN = re.compile(r"[A-Za-z]+")
PHRASE_PATTERN = re.compile(r"[A-Za-z]+(?: [A-Za-z]+)+")
PHONETIC_ALLOWED_PATTERN = re.compile(
    r"^[A-Za-z\u00C0-\u024F\u0250-\u02AF\u02B0-\u02FF\u0300-\u036F\u0370-\u03FF\u0400-\u04FF\u1D00-\u1D7F"
    r"\[\]/() '‘’`\-.,;:ːˈˌ]+$"
)
MEANING_ALLOWED_PATTERN = re.compile(
    r"^[A-Za-z0-9\u4E00-\u9FFF\u3400-\u4DBF\s"
    r"，。；：！？、,.!?;:/\\()（）\[\]【】{}<>《》\-+&%'\"“”‘’~·=…]+$"
)

CATEGORY_TITLES = {
    "word_empty": "word 字段为空",
    "word_invalid_word_chars": "单词包含非字母字符",
    "word_word_spacing": "单词空格不规范",
    "word_word_uppercase": "单词包含大写字母",
    "word_invalid_phrase_chars": "词组包含非法字符",
    "word_phrase_spacing": "词组空格数量不合理",
    "word_phrase_uppercase": "词组包含大写字母",
    "word_duplicate_word": "重复单词",
    "word_duplicate_phrase": "重复词组",
    "phonetic_phrase_should_be_empty": "词组 phonetic 应为空",
    "phonetic_word_empty": "单词 phonetic 为空",
    "phonetic_invalid_chars": "phonetic 包含非音标字符",
    "meaning_zh_empty": "meaning_zh 为空",
    "meaning_zh_invalid_chars": "meaning_zh 包含非法字符",
}


@dataclass(slots=True)
class ValidationIssue:
    category_key: str
    category_title: str
    table_name: str
    record_id: int
    word_id: int | None
    word_text: str
    field_name: str
    raw_value: str
    normalized_value: str
    reason: str
    suggestion: str


@dataclass(slots=True)
class ValidationSummary:
    db_path: Path
    total_words: int
    total_meanings: int
    issues: list[ValidationIssue]


def normalize_spaces(text: object) -> str:
    if text is None:
        return ""
    return WHITESPACE_PATTERN.sub(" ", str(text).strip())


def contains_uppercase(text: str) -> bool:
    return any(character.isalpha() and character.isupper() for character in text)


def is_phrase(raw_word: str, normalized_word: str) -> bool:
    return " " in raw_word.strip() or " " in normalized_word


def find_invalid_characters(text: str, allowed_pattern: re.Pattern[str]) -> str:
    invalid = []
    for character in text:
        if not allowed_pattern.fullmatch(character):
            invalid.append(character)
    seen: list[str] = []
    for character in invalid:
        if character not in seen:
            seen.append(character)
    return " ".join(repr(character) for character in seen)


class DatabaseValidator:
    def __init__(self, db_path: Path) -> None:
        self.db_path = db_path

    def validate(self) -> ValidationSummary:
        if not self.db_path.exists():
            raise FileNotFoundError(f"数据库不存在: {self.db_path}")

        issues: list[ValidationIssue] = []
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            word_rows = conn.execute(
                """
                SELECT id, COALESCE(word, '') AS word, COALESCE(phonetic, '') AS phonetic, COALESCE(word_type, '') AS word_type
                FROM word
                ORDER BY id
                """
            ).fetchall()
            meaning_rows = conn.execute(
                """
                SELECT wm.id, wm.word_id, COALESCE(w.word, '') AS word, COALESCE(wm.meaning_zh, '') AS meaning_zh
                FROM word_meaning AS wm
                JOIN word AS w ON w.id = wm.word_id
                ORDER BY wm.id
                """
            ).fetchall()

        duplicate_words: dict[str, list[sqlite3.Row]] = defaultdict(list)
        duplicate_phrases: dict[str, list[sqlite3.Row]] = defaultdict(list)

        for row in word_rows:
            raw_word = str(row["word"])
            normalized_word = normalize_spaces(raw_word)
            raw_phonetic = str(row["phonetic"])
            normalized_phonetic = normalize_spaces(raw_phonetic)
            phrase = is_phrase(raw_word, normalized_word)

            if not normalized_word:
                issues.append(
                    ValidationIssue(
                        category_key="word_empty",
                        category_title=CATEGORY_TITLES["word_empty"],
                        table_name="word",
                        record_id=int(row["id"]),
                        word_id=int(row["id"]),
                        word_text=raw_word,
                        field_name="word",
                        raw_value=raw_word,
                        normalized_value=normalized_word,
                        reason="word 字段去除空白后为空。",
                        suggestion="填写有效英文单词或词组。",
                    )
                )
            else:
                lowered_key = normalized_word.casefold()
                if phrase:
                    duplicate_phrases[lowered_key].append(row)
                    if raw_word != normalized_word or "  " in raw_word:
                        issues.append(
                            ValidationIssue(
                                category_key="word_phrase_spacing",
                                category_title=CATEGORY_TITLES["word_phrase_spacing"],
                                table_name="word",
                                record_id=int(row["id"]),
                                word_id=int(row["id"]),
                                word_text=raw_word,
                                field_name="word",
                                raw_value=raw_word,
                                normalized_value=normalized_word,
                                reason="词组存在首尾空格或连续空格。",
                                suggestion=f"建议改为: {normalized_word}",
                            )
                        )
                    if contains_uppercase(raw_word):
                        issues.append(
                            ValidationIssue(
                                category_key="word_phrase_uppercase",
                                category_title=CATEGORY_TITLES["word_phrase_uppercase"],
                                table_name="word",
                                record_id=int(row["id"]),
                                word_id=int(row["id"]),
                                word_text=raw_word,
                                field_name="word",
                                raw_value=raw_word,
                                normalized_value=normalized_word.lower(),
                                reason="词组中包含大写英文字母。",
                                suggestion=f"建议统一为小写: {normalized_word.lower()}",
                            )
                        )
                    if not PHRASE_PATTERN.fullmatch(normalized_word):
                        invalid_chars = find_invalid_characters(normalized_word, re.compile(r"[A-Za-z ]"))
                        issues.append(
                            ValidationIssue(
                                category_key="word_invalid_phrase_chars",
                                category_title=CATEGORY_TITLES["word_invalid_phrase_chars"],
                                table_name="word",
                                record_id=int(row["id"]),
                                word_id=int(row["id"]),
                                word_text=raw_word,
                                field_name="word",
                                raw_value=raw_word,
                                normalized_value=normalized_word,
                                reason=f"词组只能由英文单词和单个空格组成，检测到非法字符: {invalid_chars or '未知'}。",
                                suggestion="删除非英文字母字符，并确保单词之间只保留一个空格。",
                            )
                        )
                else:
                    duplicate_words[lowered_key].append(row)
                    if raw_word != normalized_word or " " in raw_word:
                        issues.append(
                            ValidationIssue(
                                category_key="word_word_spacing",
                                category_title=CATEGORY_TITLES["word_word_spacing"],
                                table_name="word",
                                record_id=int(row["id"]),
                                word_id=int(row["id"]),
                                word_text=raw_word,
                                field_name="word",
                                raw_value=raw_word,
                                normalized_value=normalized_word,
                                reason="单词不应包含首尾空格或内部空格。",
                                suggestion=f"建议改为: {normalized_word.replace(' ', '')}",
                            )
                        )
                    if contains_uppercase(raw_word):
                        issues.append(
                            ValidationIssue(
                                category_key="word_word_uppercase",
                                category_title=CATEGORY_TITLES["word_word_uppercase"],
                                table_name="word",
                                record_id=int(row["id"]),
                                word_id=int(row["id"]),
                                word_text=raw_word,
                                field_name="word",
                                raw_value=raw_word,
                                normalized_value=normalized_word.lower(),
                                reason="单词中包含大写英文字母。",
                                suggestion=f"建议统一为小写: {normalized_word.lower()}",
                            )
                        )
                    if not WORD_PATTERN.fullmatch(normalized_word):
                        invalid_chars = find_invalid_characters(normalized_word, re.compile(r"[A-Za-z]"))
                        issues.append(
                            ValidationIssue(
                                category_key="word_invalid_word_chars",
                                category_title=CATEGORY_TITLES["word_invalid_word_chars"],
                                table_name="word",
                                record_id=int(row["id"]),
                                word_id=int(row["id"]),
                                word_text=raw_word,
                                field_name="word",
                                raw_value=raw_word,
                                normalized_value=normalized_word,
                                reason=f"单词只能包含英文字母，检测到非法字符: {invalid_chars or '未知'}。",
                                suggestion="删除非字母字符。",
                            )
                        )

            if phrase:
                if normalized_phonetic:
                    issues.append(
                        ValidationIssue(
                            category_key="phonetic_phrase_should_be_empty",
                            category_title=CATEGORY_TITLES["phonetic_phrase_should_be_empty"],
                            table_name="word",
                            record_id=int(row["id"]),
                            word_id=int(row["id"]),
                            word_text=normalized_word,
                            field_name="phonetic",
                            raw_value=raw_phonetic,
                            normalized_value=normalized_phonetic,
                            reason="词组的 phonetic 按当前规则应为空。",
                            suggestion="清空该词组的 phonetic 字段。",
                        )
                    )
            else:
                if normalized_word and not normalized_phonetic:
                    issues.append(
                        ValidationIssue(
                            category_key="phonetic_word_empty",
                            category_title=CATEGORY_TITLES["phonetic_word_empty"],
                            table_name="word",
                            record_id=int(row["id"]),
                            word_id=int(row["id"]),
                            word_text=normalized_word,
                            field_name="phonetic",
                            raw_value=raw_phonetic,
                            normalized_value=normalized_phonetic,
                            reason="单词缺少 phonetic。",
                            suggestion="补充该单词的音标。",
                        )
                    )
                elif normalized_phonetic and not PHONETIC_ALLOWED_PATTERN.fullmatch(normalized_phonetic):
                    invalid_chars = find_invalid_characters(normalized_phonetic, PHONETIC_ALLOWED_PATTERN)
                    issues.append(
                        ValidationIssue(
                            category_key="phonetic_invalid_chars",
                            category_title=CATEGORY_TITLES["phonetic_invalid_chars"],
                            table_name="word",
                            record_id=int(row["id"]),
                            word_id=int(row["id"]),
                            word_text=normalized_word,
                            field_name="phonetic",
                            raw_value=raw_phonetic,
                            normalized_value=normalized_phonetic,
                            reason=f"phonetic 中含有非音标字符: {invalid_chars or '未知'}。",
                            suggestion="仅保留方括号、音标字符、重音符号和必要空格。",
                        )
                    )

        for normalized_key, rows in duplicate_words.items():
            if len(rows) <= 1:
                continue
            duplicate_ids = ", ".join(str(int(row["id"])) for row in rows)
            for row in rows:
                issues.append(
                    ValidationIssue(
                        category_key="word_duplicate_word",
                        category_title=CATEGORY_TITLES["word_duplicate_word"],
                        table_name="word",
                        record_id=int(row["id"]),
                        word_id=int(row["id"]),
                        word_text=str(row["word"]),
                        field_name="word",
                        raw_value=str(row["word"]),
                        normalized_value=normalized_key,
                        reason=f"标准化后与其他单词重复，重复记录 ID: {duplicate_ids}。",
                        suggestion="保留一条标准记录，其余记录删除或合并。",
                    )
                )

        for normalized_key, rows in duplicate_phrases.items():
            if len(rows) <= 1:
                continue
            duplicate_ids = ", ".join(str(int(row["id"])) for row in rows)
            for row in rows:
                issues.append(
                    ValidationIssue(
                        category_key="word_duplicate_phrase",
                        category_title=CATEGORY_TITLES["word_duplicate_phrase"],
                        table_name="word",
                        record_id=int(row["id"]),
                        word_id=int(row["id"]),
                        word_text=str(row["word"]),
                        field_name="word",
                        raw_value=str(row["word"]),
                        normalized_value=normalized_key,
                        reason=f"标准化后与其他词组重复，重复记录 ID: {duplicate_ids}。",
                        suggestion="保留一条标准记录，其余记录删除或合并。",
                    )
                )

        for row in meaning_rows:
            raw_meaning = str(row["meaning_zh"])
            normalized_meaning = normalize_spaces(raw_meaning)
            if not normalized_meaning:
                issues.append(
                    ValidationIssue(
                        category_key="meaning_zh_empty",
                        category_title=CATEGORY_TITLES["meaning_zh_empty"],
                        table_name="word_meaning",
                        record_id=int(row["id"]),
                        word_id=int(row["word_id"]),
                        word_text=str(row["word"]),
                        field_name="meaning_zh",
                        raw_value=raw_meaning,
                        normalized_value=normalized_meaning,
                        reason="meaning_zh 去除空白后为空。",
                        suggestion="补充有效中文释义。",
                    )
                )
                continue

            if not MEANING_ALLOWED_PATTERN.fullmatch(normalized_meaning):
                invalid_chars = find_invalid_characters(normalized_meaning, MEANING_ALLOWED_PATTERN)
                issues.append(
                    ValidationIssue(
                        category_key="meaning_zh_invalid_chars",
                        category_title=CATEGORY_TITLES["meaning_zh_invalid_chars"],
                        table_name="word_meaning",
                        record_id=int(row["id"]),
                        word_id=int(row["word_id"]),
                        word_text=str(row["word"]),
                        field_name="meaning_zh",
                        raw_value=raw_meaning,
                        normalized_value=normalized_meaning,
                        reason=f"meaning_zh 包含非法字符: {invalid_chars or '未知'}。",
                        suggestion="删除异常字符，保留中文释义、词性缩写和常见标点。",
                    )
                )

        return ValidationSummary(
            db_path=self.db_path,
            total_words=len(word_rows),
            total_meanings=len(meaning_rows),
            issues=issues,
        )


class ValidationWorker(QObject):
    finished = Signal(object)
    failed = Signal(str)
    log = Signal(str)

    def __init__(self, db_path: Path) -> None:
        super().__init__()
        self.db_path = db_path

    @Slot()
    def run(self) -> None:
        try:
            self.log.emit(f"开始校验数据库: {self.db_path}")
            summary = DatabaseValidator(self.db_path).validate()
            self.log.emit(f"校验完成，共发现 {len(summary.issues)} 条问题。")
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.finished.emit(summary)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("words.db 数据校验工具")
        self.resize(1360, 860)

        self.current_summary: ValidationSummary | None = None
        self.current_issues_by_category: dict[str, list[ValidationIssue]] = {}
        self.worker_thread: QThread | None = None
        self.worker: ValidationWorker | None = None

        self.db_path_edit = QLineEdit(str(DEFAULT_DB_PATH))
        self.browse_button = QPushButton("选择数据库")
        self.run_button = QPushButton("开始校验")
        self.summary_label = QLabel("等待开始")
        self.summary_label.setWordWrap(True)

        self.category_tree = QTreeWidget()
        self.category_tree.setColumnCount(2)
        self.category_tree.setHeaderLabels(["问题分类", "数量"])

        self.issue_table = QTableWidget(0, 8)
        self.issue_table.setHorizontalHeaderLabels(
            ["表", "记录 ID", "word_id", "单词/词组", "字段", "原始值", "原因", "建议"]
        )
        self.issue_table.setAlternatingRowColors(True)
        self.issue_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.issue_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.issue_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        self.issue_table.horizontalHeader().setStretchLastSection(True)
        self.issue_table.verticalHeader().setVisible(False)

        self.log_output = QPlainTextEdit()
        self.log_output.setReadOnly(True)
        self.log_output.setPlaceholderText("校验日志会显示在这里")

        self._build_layout()
        self._connect_signals()

    def _build_layout(self) -> None:
        controls_layout = QGridLayout()
        controls_layout.addWidget(QLabel("数据库路径"), 0, 0)
        controls_layout.addWidget(self.db_path_edit, 0, 1)
        controls_layout.addWidget(self.browse_button, 0, 2)
        controls_layout.addWidget(self.run_button, 0, 3)
        controls_layout.addWidget(QLabel("校验结果"), 1, 0)
        controls_layout.addWidget(self.summary_label, 1, 1, 1, 3)
        controls_layout.setColumnStretch(1, 1)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self.category_tree)
        splitter.addWidget(self.issue_table)
        splitter.setStretchFactor(0, 2)
        splitter.setStretchFactor(1, 5)

        central_widget = QWidget()
        root_layout = QVBoxLayout(central_widget)
        root_layout.addLayout(controls_layout)
        root_layout.addWidget(splitter, stretch=1)
        root_layout.addWidget(QLabel("运行日志"))
        root_layout.addWidget(self.log_output, stretch=1)

        self.setCentralWidget(central_widget)

    def _connect_signals(self) -> None:
        self.browse_button.clicked.connect(self.select_db_path)
        self.run_button.clicked.connect(self.start_validation)
        self.category_tree.currentItemChanged.connect(self.on_category_changed)

    @Slot()
    def select_db_path(self) -> None:
        selected, _ = QFileDialog.getOpenFileName(
            self,
            "选择 words.db",
            str(Path(self.db_path_edit.text()).parent),
            "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*)",
        )
        if selected:
            self.db_path_edit.setText(selected)

    @Slot()
    def start_validation(self) -> None:
        db_path = Path(self.db_path_edit.text().strip())
        if not db_path.exists():
            QMessageBox.warning(self, "路径错误", f"数据库不存在:\n{db_path}")
            return

        if self.worker_thread is not None:
            QMessageBox.information(self, "提示", "校验任务正在运行，请等待完成。")
            return

        self.current_summary = None
        self.current_issues_by_category = {}
        self.category_tree.clear()
        self.issue_table.setRowCount(0)
        self.summary_label.setText("校验中，请稍候...")
        self.run_button.setEnabled(False)
        self.log_output.clear()

        self.worker_thread = QThread(self)
        self.worker = ValidationWorker(db_path)
        self.worker.moveToThread(self.worker_thread)
        self.worker_thread.started.connect(self.worker.run)
        self.worker.finished.connect(self.on_validation_finished)
        self.worker.failed.connect(self.on_validation_failed)
        self.worker.log.connect(self.append_log)
        self.worker.finished.connect(self.worker_thread.quit)
        self.worker.failed.connect(self.worker_thread.quit)
        self.worker_thread.finished.connect(self.cleanup_worker)
        self.worker_thread.start()

    @Slot(object)
    def on_validation_finished(self, summary: ValidationSummary) -> None:
        self.current_summary = summary
        issues_by_category: dict[str, list[ValidationIssue]] = defaultdict(list)
        for issue in summary.issues:
            issues_by_category[issue.category_key].append(issue)
        self.current_issues_by_category = dict(sorted(issues_by_category.items(), key=lambda item: (-len(item[1]), item[0])))

        self.category_tree.clear()
        root_item = QTreeWidgetItem(["全部问题", str(len(summary.issues))])
        root_item.setData(0, Qt.ItemDataRole.UserRole, "__all__")
        self.category_tree.addTopLevelItem(root_item)

        for category_key, issues in self.current_issues_by_category.items():
            item = QTreeWidgetItem([CATEGORY_TITLES.get(category_key, category_key), str(len(issues))])
            item.setData(0, Qt.ItemDataRole.UserRole, category_key)
            root_item.addChild(item)

        root_item.setExpanded(True)
        self.category_tree.setCurrentItem(root_item)

        issue_count = len(summary.issues)
        category_count = len(self.current_issues_by_category)
        if issue_count == 0:
            self.summary_label.setText(
                f"校验完成。word 表 {summary.total_words} 条，word_meaning 表 {summary.total_meanings} 条，未发现问题。"
            )
        else:
            self.summary_label.setText(
                f"校验完成。word 表 {summary.total_words} 条，word_meaning 表 {summary.total_meanings} 条，"
                f"共发现 {issue_count} 条问题，分布在 {category_count} 类原因中。"
            )

    @Slot(str)
    def on_validation_failed(self, message: str) -> None:
        self.summary_label.setText("校验失败")
        self.append_log(f"校验失败: {message}")
        QMessageBox.critical(self, "校验失败", message)

    @Slot()
    def cleanup_worker(self) -> None:
        if self.worker is not None:
            self.worker.deleteLater()
        if self.worker_thread is not None:
            self.worker_thread.deleteLater()
        self.worker = None
        self.worker_thread = None
        self.run_button.setEnabled(True)

    @Slot(str)
    def append_log(self, message: str) -> None:
        self.log_output.appendPlainText(message)

    @Slot(QTreeWidgetItem, QTreeWidgetItem)
    def on_category_changed(self, current: QTreeWidgetItem | None, _: QTreeWidgetItem | None) -> None:
        if current is None or self.current_summary is None:
            self.populate_issue_table([])
            return

        category_key = current.data(0, Qt.ItemDataRole.UserRole)
        if category_key == "__all__":
            self.populate_issue_table(self.current_summary.issues)
            return

        self.populate_issue_table(self.current_issues_by_category.get(str(category_key), []))

    def populate_issue_table(self, issues: list[ValidationIssue]) -> None:
        self.issue_table.setRowCount(len(issues))
        for row_index, issue in enumerate(issues):
            values = [
                issue.table_name,
                str(issue.record_id),
                "" if issue.word_id is None else str(issue.word_id),
                issue.word_text,
                issue.field_name,
                issue.raw_value,
                issue.reason,
                issue.suggestion,
            ]
            for column_index, value in enumerate(values):
                item = QTableWidgetItem(value)
                if column_index in {1, 2}:
                    item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self.issue_table.setItem(row_index, column_index, item)

            if issue.category_key.startswith("phonetic"):
                color = QColor("#FFF4E5")
            elif issue.category_key.startswith("meaning"):
                color = QColor("#FDECEC")
            else:
                color = QColor("#EEF6FF")

            for column_index in range(self.issue_table.columnCount()):
                cell = self.issue_table.item(row_index, column_index)
                if cell is not None:
                    cell.setBackground(color)

        self.issue_table.resizeColumnsToContents()


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())