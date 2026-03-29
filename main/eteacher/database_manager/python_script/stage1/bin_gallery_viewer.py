from __future__ import annotations

import importlib
import math
import subprocess
import sys
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

from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QAction, QImage, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)


SUPPORTED_SUFFIX = ".bin"
DEFAULT_ITEMS_PER_PAGE = 9
MIN_ITEMS_PER_PAGE = 1
MAX_ITEMS_PER_PAGE = 100
THUMBNAIL_MAX_SIZE = QSize(280, 210)


@dataclass(slots=True)
class BinImage:
    path: Path
    width: int
    height: int
    image: QImage


class BinImageError(Exception):
    pass


def read_bin_image(path: Path) -> BinImage:
    data = path.read_bytes()
    if len(data) < 4:
        raise BinImageError(f"文件过小，无法读取头部: {path.name}")

    width = data[0] | (data[1] << 8)
    height = data[2] | (data[3] << 8)
    if width <= 0 or height <= 0:
        raise BinImageError(f"宽高非法: {path.name} -> {width}x{height}")

    stride = (width + 7) // 8
    expected_size = 4 + stride * height
    if len(data) < expected_size:
        raise BinImageError(
            f"数据长度不足: {path.name} 需要 {expected_size} 字节，实际 {len(data)} 字节"
        )

    image = QImage(width, height, QImage.Format.Format_RGB32)
    image.fill(0xFFFFFFFF)

    bitmap = data[4:expected_size]
    for y in range(height):
        row_offset = y * stride
        for x in range(width):
            byte_value = bitmap[row_offset + (x >> 3)]
            bit_mask = 0x80 >> (x & 7)
            is_black = (byte_value & bit_mask) != 0
            image.setPixel(x, y, 0xFF000000 if is_black else 0xFFFFFFFF)

    return BinImage(path=path, width=width, height=height, image=image)


class BinCard(QFrame):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self.setStyleSheet(
            "QFrame { border: 1px solid #c8c8c8; border-radius: 8px; background: #fafafa; }"
            "QLabel { color: #202020; }"
        )

        self.name_label = QLabel()
        self.name_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.name_label.setWordWrap(True)
        self.name_label.setStyleSheet("font-weight: 600;")

        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_label.setMinimumSize(220, 160)
        self.image_label.setStyleSheet("background: white; border: 1px solid #e0e0e0; border-radius: 4px;")

        self.info_label = QLabel()
        self.info_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.info_label.setStyleSheet("color: #666666;")

        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(8)
        layout.addWidget(self.name_label)
        layout.addWidget(self.image_label, 1)
        layout.addWidget(self.info_label)

    def set_bin_image(self, bin_image: BinImage) -> None:
        pixmap = QPixmap.fromImage(bin_image.image)
        scaled = pixmap.scaled(
            THUMBNAIL_MAX_SIZE,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )
        self.name_label.setText(bin_image.path.name)
        self.image_label.setPixmap(scaled)
        self.info_label.setText(f"{bin_image.width} × {bin_image.height}    {bin_image.path.stat().st_size} bytes")


class BinGalleryWindow(QMainWindow):
    def __init__(self, initial_folder: Path) -> None:
        super().__init__()
        self.setWindowTitle("BIN 图片浏览器")
        self.resize(1400, 900)

        self.current_folder = initial_folder
        self.bin_paths: list[Path] = []
        self.page_index = 0
        self.image_cache: dict[Path, BinImage] = {}

        self.folder_edit = QLineEdit(str(initial_folder))
        self.folder_edit.setPlaceholderText("选择包含 .bin 图片的目录")

        self.browse_button = QPushButton("选择文件夹")
        self.refresh_button = QPushButton("刷新")

        self.items_per_page_spin = QSpinBox()
        self.items_per_page_spin.setRange(MIN_ITEMS_PER_PAGE, MAX_ITEMS_PER_PAGE)
        self.items_per_page_spin.setValue(DEFAULT_ITEMS_PER_PAGE)
        self.items_per_page_spin.setSuffix(" 张/页")

        self.prev_button = QPushButton("上一页")
        self.next_button = QPushButton("下一页")
        self.page_label = QLabel()
        self.page_label.setAlignment(Qt.AlignmentFlag.AlignCenter)

        toolbar_layout = QHBoxLayout()
        toolbar_layout.addWidget(QLabel("目录:"))
        toolbar_layout.addWidget(self.folder_edit, 1)
        toolbar_layout.addWidget(self.browse_button)
        toolbar_layout.addWidget(self.refresh_button)
        toolbar_layout.addSpacing(12)
        toolbar_layout.addWidget(QLabel("每页显示:"))
        toolbar_layout.addWidget(self.items_per_page_spin)
        toolbar_layout.addStretch(1)
        toolbar_layout.addWidget(self.prev_button)
        toolbar_layout.addWidget(self.page_label)
        toolbar_layout.addWidget(self.next_button)

        self.grid_widget = QWidget()
        self.grid_layout = QGridLayout(self.grid_widget)
        self.grid_layout.setContentsMargins(12, 12, 12, 12)
        self.grid_layout.setSpacing(12)

        self.empty_label = QLabel("当前目录没有可显示的 .bin 图片")
        self.empty_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.empty_label.setStyleSheet("font-size: 18px; color: #666666; padding: 40px;")

        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.scroll_area.setWidget(self.grid_widget)

        central = QWidget(self)
        central_layout = QVBoxLayout(central)
        central_layout.setContentsMargins(12, 12, 12, 12)
        central_layout.setSpacing(10)
        central_layout.addLayout(toolbar_layout)
        central_layout.addWidget(self.scroll_area, 1)
        self.setCentralWidget(central)

        self.setStatusBar(QStatusBar(self))

        open_action = QAction("打开目录", self)
        open_action.triggered.connect(self.choose_folder)
        self.menuBar().addAction(open_action)

        self.browse_button.clicked.connect(self.choose_folder)
        self.refresh_button.clicked.connect(self.reload_current_folder)
        self.items_per_page_spin.valueChanged.connect(self.on_items_per_page_changed)
        self.prev_button.clicked.connect(self.show_previous_page)
        self.next_button.clicked.connect(self.show_next_page)
        self.folder_edit.returnPressed.connect(self.reload_current_folder)

        self.reload_current_folder()

    def choose_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "选择包含 .bin 图片的文件夹", str(self.current_folder))
        if not folder:
            return
        self.folder_edit.setText(folder)
        self.reload_current_folder()

    def reload_current_folder(self) -> None:
        folder = Path(self.folder_edit.text().strip() or ".").expanduser()
        if not folder.exists() or not folder.is_dir():
            QMessageBox.warning(self, "目录无效", f"目录不存在或不可读取:\n{folder}")
            return

        self.current_folder = folder
        self.folder_edit.setText(str(folder))
        self.bin_paths = sorted(folder.glob(f"*{SUPPORTED_SUFFIX}"), key=lambda item: item.name.lower())
        self.page_index = 0

        existing_paths = set(self.bin_paths)
        self.image_cache = {path: image for path, image in self.image_cache.items() if path in existing_paths}

        self.render_page()

    def on_items_per_page_changed(self) -> None:
        self.page_index = 0
        self.render_page()

    def show_previous_page(self) -> None:
        if self.page_index > 0:
            self.page_index -= 1
            self.render_page()

    def show_next_page(self) -> None:
        if self.page_index + 1 < self.total_pages:
            self.page_index += 1
            self.render_page()

    @property
    def items_per_page(self) -> int:
        return self.items_per_page_spin.value()

    @property
    def total_pages(self) -> int:
        if not self.bin_paths:
            return 1
        return math.ceil(len(self.bin_paths) / self.items_per_page)

    def render_page(self) -> None:
        self.clear_grid()

        if not self.bin_paths:
            self.grid_layout.addWidget(self.empty_label, 0, 0)
            self.page_label.setText("第 0 / 0 页")
            self.prev_button.setEnabled(False)
            self.next_button.setEnabled(False)
            self.statusBar().showMessage(f"目录 {self.current_folder} 中没有 .bin 图片")
            return

        self.page_index = max(0, min(self.page_index, self.total_pages - 1))

        start = self.page_index * self.items_per_page
        end = min(start + self.items_per_page, len(self.bin_paths))
        page_items = self.bin_paths[start:end]

        columns = max(1, math.ceil(math.sqrt(self.items_per_page)))
        for index, path in enumerate(page_items):
            row = index // columns
            column = index % columns
            card = self.build_card(path)
            self.grid_layout.addWidget(card, row, column)

        for column in range(columns):
            self.grid_layout.setColumnStretch(column, 1)
        self.grid_layout.setRowStretch((len(page_items) + columns - 1) // columns, 1)

        self.page_label.setText(f"第 {self.page_index + 1} / {self.total_pages} 页")
        self.prev_button.setEnabled(self.page_index > 0)
        self.next_button.setEnabled(self.page_index + 1 < self.total_pages)
        self.statusBar().showMessage(
            f"目录: {self.current_folder}    共 {len(self.bin_paths)} 张图片    当前显示 {start + 1}-{end}"
        )

    def clear_grid(self) -> None:
        while self.grid_layout.count():
            item = self.grid_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.setParent(None)

    def build_card(self, path: Path) -> QWidget:
        card = BinCard()
        try:
            card.set_bin_image(self.load_image(path))
        except Exception as exc:
            card.name_label.setText(path.name)
            card.image_label.setText("加载失败")
            card.info_label.setText(str(exc))
        return card

    def load_image(self, path: Path) -> BinImage:
        cached = self.image_cache.get(path)
        if cached is not None:
            return cached

        image = read_bin_image(path)
        self.image_cache[path] = image
        return image


def main() -> int:
    initial_folder = Path(__file__).resolve().parent
    app = QApplication(sys.argv)
    window = BinGalleryWindow(initial_folder)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
