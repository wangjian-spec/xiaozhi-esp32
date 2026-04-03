from __future__ import annotations

import importlib
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


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


_ensure_dependency("PIL", "Pillow")
_ensure_dependency("PySide6.QtCore", "PySide6")

from PIL import Image, ImageOps, ImageStat
from PySide6.QtCore import QObject, QThread, Qt, Signal, Slot
from PySide6.QtWidgets import (
	QApplication,
	QCheckBox,
	QComboBox,
	QFileDialog,
	QGridLayout,
	QGroupBox,
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


ROOT_DIR = Path(__file__).resolve().parent
TARGET_SIZE = (80, 80)
DEFAULT_OUTPUT_FORMAT = "bin"
PACKAGE_MAGIC = b"IPK1"
PACKAGE_VERSION = 1
PACKAGE_HEADER_STRUCT = struct.Struct("<4sH H I I I")
PACKAGE_ENTRY_STRUCT = struct.Struct("<H I I H H")
SUPPORTED_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}
OUTPUT_FORMAT_EXTENSIONS = {
	"bin": ".bin",
	"png": ".png",
	"bmp": ".bmp",
	"jpg": ".jpg",
	"jpeg": ".jpg",
	"webp": ".webp",
}
FORMAT_LABELS = {
	"bin": "BIN 1-bit 位图",
	"png": "PNG",
	"bmp": "BMP",
	"jpg": "JPG",
	"webp": "WEBP",
}


@dataclass(slots=True)
class ConvertConfig:
	input_dir: Path
	output_dir: Path
	width: int
	height: int
	output_format: str
	recursive: bool
	overwrite: bool


@dataclass(slots=True)
class PackConfig:
	input_dir: Path
	output_file: Path
	recursive: bool
	overwrite: bool


def iter_images(folder: Path, recursive: bool = False) -> Iterable[Path]:
	iterator = folder.rglob("*") if recursive else folder.iterdir()
	for path in iterator:
		if path.is_file() and path.suffix.lower() in SUPPORTED_EXTS:
			yield path


def iter_bin_files(folder: Path, recursive: bool = False) -> Iterable[Path]:
	iterator = folder.rglob("*.bin") if recursive else folder.glob("*.bin")
	for path in iterator:
		if path.is_file():
			yield path


def _pack_1bit(img: Image.Image) -> bytes:
	if img.mode != "1":
		img = img.convert("1")
	width, height = img.size
	stride = (width + 7) // 8
	packed = bytearray(stride * height)
	px = img.load()
	for y in range(height):
		row_offset = y * stride
		for x in range(width):
			if px[x, y] == 0:
				byte_index = row_offset + (x >> 3)
				bit = 0x80 >> (x & 7)
				packed[byte_index] |= bit
	return bytes(packed)


def prepare_image(image_path: Path, target_size: tuple[int, int]) -> Image.Image:
	with Image.open(image_path) as img:
		img = img.convert("RGBA")
		if img.size != target_size:
			img = img.resize(target_size, Image.NEAREST)

		alpha = img.getchannel("A")
		if alpha.getextrema()[0] < 255:
			mask = alpha.point(lambda value: 255 if value > 0 else 0)
			base = Image.new("L", img.size, 255)
			base.paste(0, mask=mask)
			img = base
		else:
			bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
			img = Image.alpha_composite(bg, img).convert("L")

		folder_name = image_path.parent.name
		if folder_name not in {"top", "bottom"}:
			try:
				width, height = img.size
				border = max(1, min(width, height) // 10)
				top = img.crop((0, 0, width, border))
				bottom = img.crop((0, height - border, width, height))
				left = img.crop((0, 0, border, height))
				right = img.crop((width - border, 0, width, height))
				mean_vals = (
					ImageStat.Stat(top).mean[0]
					+ ImageStat.Stat(bottom).mean[0]
					+ ImageStat.Stat(left).mean[0]
					+ ImageStat.Stat(right).mean[0]
				) / 4.0
				if mean_vals < 128:
					img = ImageOps.invert(img)
			except Exception:
				pass

		return img.convert("1", dither=Image.FLOYDSTEINBERG)


def build_output_path(
	image_path: Path,
	output_dir: Path,
	output_format: str,
	source_root: Path | None = None,
) -> Path:
	format_key = output_format.lower()
	if format_key not in OUTPUT_FORMAT_EXTENSIONS:
		raise ValueError(f"不支持的输出格式: {output_format}")
	try:
		relative_parent = image_path.parent.relative_to(source_root) if source_root else Path()
	except ValueError:
		relative_parent = Path()
	return output_dir / relative_parent / f"{image_path.stem}{OUTPUT_FORMAT_EXTENSIONS[format_key]}"


def save_image(
	img: Image.Image,
	image_path: Path,
	output_dir: Path,
	output_format: str,
	source_root: Path | None = None,
) -> Path:
	format_key = output_format.lower()
	out_path = build_output_path(image_path, output_dir, format_key, source_root)
	out_path.parent.mkdir(parents=True, exist_ok=True)

	if format_key == "bin":
		width, height = img.size
		data = _pack_1bit(img)
		header = width.to_bytes(2, "little") + height.to_bytes(2, "little")
		out_path.write_bytes(header + data)
		return out_path

	image_to_save = img
	if format_key in {"jpg", "jpeg", "webp"}:
		image_to_save = img.convert("L")

	if format_key in {"jpg", "jpeg"}:
		image_to_save.save(out_path, format="JPEG")
		return out_path

	image_to_save.save(out_path, format=format_key.upper())
	return out_path


def convert_image(
	image_path: Path,
	target_size: tuple[int, int],
	output_dir: Path,
	output_format: str,
	source_root: Path,
) -> Path:
	img = prepare_image(image_path, target_size)
	return save_image(img, image_path, output_dir, output_format, source_root)


def read_bin_dimensions(bin_path: Path) -> tuple[int, int]:
	header = bin_path.read_bytes()[:4]
	if len(header) < 4:
		raise ValueError(f"BIN 文件头长度不足: {bin_path}")
	width = int.from_bytes(header[:2], "little")
	height = int.from_bytes(header[2:4], "little")
	return width, height


def build_bin_package(
	pack_config: PackConfig,
	log_callback: Callable[[str], None] | None = None,
	progress_callback: Callable[[int, int], None] | None = None,
	should_cancel: Callable[[], bool] | None = None,
) -> dict[str, int | str | bool]:
	input_root = pack_config.input_dir.resolve()
	output_file = pack_config.output_file.resolve()
	bin_files = []
	for path in sorted(iter_bin_files(pack_config.input_dir, recursive=pack_config.recursive)):
		resolved_path = path.resolve()
		if resolved_path == output_file:
			continue
		bin_files.append(path)

	if not bin_files:
		raise ValueError("未找到可打包的 .bin 文件")

	entries: list[dict[str, object]] = []
	index_size = 0
	for bin_path in bin_files:
		if should_cancel is not None and should_cancel():
			return {
				"total": len(entries),
				"output": str(output_file),
				"package_size": 0,
				"cancelled": True,
			}
		relative_path = bin_path.resolve().relative_to(input_root).as_posix()
		name_bytes = relative_path.encode("utf-8")
		width, height = read_bin_dimensions(bin_path)
		file_size = bin_path.stat().st_size
		entries.append(
			{
				"path": bin_path,
				"name": relative_path,
				"name_bytes": name_bytes,
				"size": file_size,
				"width": width,
				"height": height,
			}
		)
		index_size += PACKAGE_ENTRY_STRUCT.size + len(name_bytes)

	data_offset = PACKAGE_HEADER_STRUCT.size + index_size
	current_offset = data_offset
	for entry in entries:
		entry["offset"] = current_offset
		current_offset += int(entry["size"])

	output_file.parent.mkdir(parents=True, exist_ok=True)
	with output_file.open("wb") as fp:
		fp.write(
			PACKAGE_HEADER_STRUCT.pack(
				PACKAGE_MAGIC,
				PACKAGE_VERSION,
				0,
				len(entries),
				index_size,
				data_offset,
			)
		)
		for entry in entries:
			if should_cancel is not None and should_cancel():
				fp.close()
				output_file.unlink(missing_ok=True)
				return {
					"total": len(entries),
					"output": str(output_file),
					"package_size": 0,
					"cancelled": True,
				}
			name_bytes = entry["name_bytes"]
			fp.write(
				PACKAGE_ENTRY_STRUCT.pack(
					len(name_bytes),
					int(entry["offset"]),
					int(entry["size"]),
					int(entry["width"]),
					int(entry["height"]),
				)
			)
			fp.write(name_bytes)

		for index, entry in enumerate(entries, start=1):
			if should_cancel is not None and should_cancel():
				fp.close()
				output_file.unlink(missing_ok=True)
				return {
					"total": len(entries),
					"output": str(output_file),
					"package_size": 0,
					"cancelled": True,
				}
			bin_path = entry["path"]
			with Path(bin_path).open("rb") as source_fp:
				fp.write(source_fp.read())
			if progress_callback is not None:
				progress_callback(index, len(entries))
			if log_callback is not None:
				log_callback(
					f"[{index}/{len(entries)}] 已写入: {entry['name']} -> 偏移 {entry['offset']}"
				)

	return {
		"total": len(entries),
		"output": str(output_file),
		"package_size": output_file.stat().st_size,
			"cancelled": False,
	}


class ConvertWorker(QObject):
	log = Signal(str)
	progress = Signal(int, int)
	finished = Signal(dict)
	failed = Signal(str)

	def __init__(self, config: ConvertConfig) -> None:
		super().__init__()
		self.config = config
		self._cancel_requested = False

	@Slot()
	def run(self) -> None:
		try:
			result = self._run_impl()
		except Exception as exc:
			self.failed.emit(str(exc))
			return
		self.finished.emit(result)

	def cancel(self) -> None:
		self._cancel_requested = True

	def _run_impl(self) -> dict[str, int | bool]:
		images = sorted(iter_images(self.config.input_dir, recursive=self.config.recursive))
		total = len(images)
		if total == 0:
			return {
				"total": 0,
				"converted": 0,
				"skipped": 0,
				"failed": 0,
				"cancelled": False,
			}

		converted = 0
		skipped = 0
		failed = 0
		target_size = (self.config.width, self.config.height)

		self.log.emit(f"共发现 {total} 张图片，开始批量转换。")
		for index, image_path in enumerate(images, start=1):
			if self._cancel_requested:
				self.log.emit("检测到取消请求，已停止后续转换。")
				return {
					"total": total,
					"converted": converted,
					"skipped": skipped,
					"failed": failed,
					"cancelled": True,
				}

			self.progress.emit(index, total)
			out_path = build_output_path(
				image_path,
				self.config.output_dir,
				self.config.output_format,
				self.config.input_dir,
			)
			if out_path.exists() and not self.config.overwrite:
				skipped += 1
				self.log.emit(f"[{index}/{total}] 跳过已存在文件: {out_path}")
				continue

			try:
				convert_image(
					image_path=image_path,
					target_size=target_size,
					output_dir=self.config.output_dir,
					output_format=self.config.output_format,
					source_root=self.config.input_dir,
				)
				converted += 1
				self.log.emit(f"[{index}/{total}] 已生成: {out_path}")
			except Exception as exc:
				failed += 1
				self.log.emit(f"[{index}/{total}] 失败: {image_path.name} -> {exc}")

		return {
			"total": total,
			"converted": converted,
			"skipped": skipped,
			"failed": failed,
			"cancelled": False,
		}


class PackWorker(QObject):
	log = Signal(str)
	progress = Signal(int, int)
	finished = Signal(dict)
	failed = Signal(str)

	def __init__(self, config: PackConfig) -> None:
		super().__init__()
		self.config = config
		self._cancel_requested = False

	@Slot()
	def run(self) -> None:
		try:
			result = self._run_impl()
		except Exception as exc:
			self.failed.emit(str(exc))
			return
		self.finished.emit(result)

	def cancel(self) -> None:
		self._cancel_requested = True

	def _run_impl(self) -> dict[str, int | bool | str]:
		if self.config.output_file.exists() and not self.config.overwrite:
			raise FileExistsError(f"输出文件已存在: {self.config.output_file}")

		bin_files = []
		for path in sorted(iter_bin_files(self.config.input_dir, recursive=self.config.recursive)):
			if path.resolve() == self.config.output_file.resolve():
				continue
			bin_files.append(path)

		total = len(bin_files)
		if total == 0:
			return {
				"mode": "pack",
				"total": 0,
				"cancelled": False,
				"output": str(self.config.output_file),
			}

		self.log.emit(f"共发现 {total} 个 .bin 文件，开始打包。")
		result = build_bin_package(
			self.config,
			log_callback=self.log.emit,
			progress_callback=self.progress.emit,
			should_cancel=lambda: self._cancel_requested,
		)
		result["mode"] = "pack"
		if result.get("cancelled"):
			self.log.emit("检测到取消请求，已停止打包。")
		return result


class MainWindow(QMainWindow):
	def __init__(self) -> None:
		super().__init__()
		self.setWindowTitle("图片批量转换")
		self.resize(980, 900)

		self.worker_thread: QThread | None = None
		self.worker: QObject | None = None

		central = QWidget(self)
		self.setCentralWidget(central)

		layout = QVBoxLayout(central)
		convert_group = QGroupBox("图片转换")
		convert_layout = QVBoxLayout(convert_group)
		form_layout = QGridLayout()
		convert_layout.addLayout(form_layout)

		self.input_dir_edit = QLineEdit(str(ROOT_DIR))
		self.output_dir_edit = QLineEdit(str(ROOT_DIR / "converted_output"))

		input_browse_button = QPushButton("选择输入文件夹")
		input_browse_button.clicked.connect(self.select_input_dir)
		output_browse_button = QPushButton("选择输出文件夹")
		output_browse_button.clicked.connect(self.select_output_dir)

		form_layout.addWidget(QLabel("输入图片文件夹"), 0, 0)
		form_layout.addWidget(self.input_dir_edit, 0, 1)
		form_layout.addWidget(input_browse_button, 0, 2)

		form_layout.addWidget(QLabel("输出目录"), 1, 0)
		form_layout.addWidget(self.output_dir_edit, 1, 1)
		form_layout.addWidget(output_browse_button, 1, 2)

		self.width_spin = QSpinBox()
		self.width_spin.setRange(1, 4096)
		self.width_spin.setValue(TARGET_SIZE[0])
		self.height_spin = QSpinBox()
		self.height_spin.setRange(1, 4096)
		self.height_spin.setValue(TARGET_SIZE[1])

		form_layout.addWidget(QLabel("目标宽度"), 2, 0)
		form_layout.addWidget(self.width_spin, 2, 1)
		form_layout.addWidget(QLabel("像素"), 2, 2)

		form_layout.addWidget(QLabel("目标高度"), 3, 0)
		form_layout.addWidget(self.height_spin, 3, 1)
		form_layout.addWidget(QLabel("像素"), 3, 2)

		self.format_combo = QComboBox()
		for format_key, label in FORMAT_LABELS.items():
			self.format_combo.addItem(label, userData=format_key)
		default_index = self.format_combo.findData(DEFAULT_OUTPUT_FORMAT)
		if default_index >= 0:
			self.format_combo.setCurrentIndex(default_index)

		form_layout.addWidget(QLabel("输出格式"), 4, 0)
		form_layout.addWidget(self.format_combo, 4, 1, 1, 2)

		self.recursive_checkbox = QCheckBox("递归扫描子目录")
		self.recursive_checkbox.setChecked(True)
		self.overwrite_checkbox = QCheckBox("覆盖已存在的输出文件")
		self.overwrite_checkbox.setChecked(False)

		convert_layout.addWidget(self.recursive_checkbox)
		convert_layout.addWidget(self.overwrite_checkbox)

		hint_label = QLabel(
			"支持 PNG/JPG/JPEG/BMP/WEBP 输入。\n"
			"可选择输入目录、输出目录、输出尺寸和输出格式。\n"
			"输出为 BIN 时会生成带宽高头的 1-bit 点阵文件；其他格式会输出缩放后的黑白图。"
		)
		hint_label.setWordWrap(True)
		hint_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
		convert_layout.addWidget(hint_label)

		button_layout = QHBoxLayout()
		self.start_button = QPushButton("开始转换")
		self.start_button.clicked.connect(self.start_convert)
		self.cancel_button = QPushButton("取消")
		self.cancel_button.clicked.connect(self.cancel_convert)
		self.cancel_button.setEnabled(False)
		self.preview_button = QPushButton("预览图片数量")
		self.preview_button.clicked.connect(self.preview_inputs)

		button_layout.addWidget(self.start_button)
		button_layout.addWidget(self.cancel_button)
		button_layout.addWidget(self.preview_button)
		convert_layout.addLayout(button_layout)
		layout.addWidget(convert_group)

		pack_group = QGroupBox("BIN 打包")
		pack_layout = QVBoxLayout(pack_group)
		pack_form_layout = QGridLayout()
		pack_layout.addLayout(pack_form_layout)

		self.pack_input_dir_edit = QLineEdit(str(ROOT_DIR / "converted_output"))
		self.pack_output_file_edit = QLineEdit(str(ROOT_DIR / "packed_images.bin"))

		pack_input_browse_button = QPushButton("选择 BIN 输入目录")
		pack_input_browse_button.clicked.connect(self.select_pack_input_dir)
		pack_output_browse_button = QPushButton("选择打包输出文件")
		pack_output_browse_button.clicked.connect(self.select_pack_output_file)

		pack_form_layout.addWidget(QLabel("BIN 输入目录"), 0, 0)
		pack_form_layout.addWidget(self.pack_input_dir_edit, 0, 1)
		pack_form_layout.addWidget(pack_input_browse_button, 0, 2)

		pack_form_layout.addWidget(QLabel("打包输出文件"), 1, 0)
		pack_form_layout.addWidget(self.pack_output_file_edit, 1, 1)
		pack_form_layout.addWidget(pack_output_browse_button, 1, 2)

		self.pack_recursive_checkbox = QCheckBox("递归扫描子目录中的 .bin 文件")
		self.pack_recursive_checkbox.setChecked(True)
		self.pack_overwrite_checkbox = QCheckBox("覆盖已存在的打包文件")
		self.pack_overwrite_checkbox.setChecked(False)

		pack_layout.addWidget(self.pack_recursive_checkbox)
		pack_layout.addWidget(self.pack_overwrite_checkbox)

		pack_hint_label = QLabel(
			"将多个单独的图片 .bin 合并成一个带索引的 .bin 包。\n"
			"包格式包含 magic、版本号、文件数量、索引区、文件偏移、尺寸和相对路径，便于 ESP32-S3 按名称查找。"
		)
		pack_hint_label.setWordWrap(True)
		pack_hint_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
		pack_layout.addWidget(pack_hint_label)

		pack_button_layout = QHBoxLayout()
		self.pack_start_button = QPushButton("开始打包 BIN")
		self.pack_start_button.clicked.connect(self.start_pack)
		self.pack_preview_button = QPushButton("预览 BIN 数量")
		self.pack_preview_button.clicked.connect(self.preview_pack_inputs)
		pack_button_layout.addWidget(self.pack_start_button)
		pack_button_layout.addWidget(self.pack_preview_button)
		pack_layout.addLayout(pack_button_layout)

		layout.addWidget(pack_group)

		self.progress_bar = QProgressBar()
		self.progress_bar.setRange(0, 100)
		self.progress_bar.setValue(0)
		layout.addWidget(self.progress_bar)

		self.status_label = QLabel("等待开始")
		layout.addWidget(self.status_label)

		self.log_edit = QPlainTextEdit()
		self.log_edit.setReadOnly(True)
		layout.addWidget(self.log_edit)

	def build_config(self) -> ConvertConfig:
		input_dir_text = self.input_dir_edit.text().strip()
		output_dir_text = self.output_dir_edit.text().strip()
		input_dir = Path(input_dir_text)
		output_dir = Path(output_dir_text)
		if not input_dir.exists() or not input_dir.is_dir():
			raise FileNotFoundError(f"输入目录不存在: {input_dir}")
		if not output_dir_text:
			raise ValueError("输出目录不能为空")
		return ConvertConfig(
			input_dir=input_dir,
			output_dir=output_dir,
			width=self.width_spin.value(),
			height=self.height_spin.value(),
			output_format=str(self.format_combo.currentData()),
			recursive=self.recursive_checkbox.isChecked(),
			overwrite=self.overwrite_checkbox.isChecked(),
		)

	def append_log(self, message: str) -> None:
		self.log_edit.appendPlainText(message)

	def set_busy_state(self, busy: bool) -> None:
		self.start_button.setEnabled(not busy)
		self.preview_button.setEnabled(not busy)
		self.pack_start_button.setEnabled(not busy)
		self.pack_preview_button.setEnabled(not busy)
		self.cancel_button.setEnabled(busy)

	@Slot()
	def select_input_dir(self) -> None:
		folder = QFileDialog.getExistingDirectory(
			self,
			"选择输入图片文件夹",
			self.input_dir_edit.text().strip() or str(ROOT_DIR),
		)
		if folder:
			self.input_dir_edit.setText(folder)

	@Slot()
	def select_output_dir(self) -> None:
		folder = QFileDialog.getExistingDirectory(
			self,
			"选择输出目录",
			self.output_dir_edit.text().strip() or str(ROOT_DIR),
		)
		if folder:
			self.output_dir_edit.setText(folder)

	@Slot()
	def select_pack_input_dir(self) -> None:
		folder = QFileDialog.getExistingDirectory(
			self,
			"选择 BIN 输入目录",
			self.pack_input_dir_edit.text().strip() or str(ROOT_DIR),
		)
		if folder:
			self.pack_input_dir_edit.setText(folder)

	@Slot()
	def select_pack_output_file(self) -> None:
		file_path, _ = QFileDialog.getSaveFileName(
			self,
			"选择打包输出文件",
			self.pack_output_file_edit.text().strip() or str(ROOT_DIR / "packed_images.bin"),
			"BIN Package (*.bin);;All Files (*.*)",
		)
		if file_path:
			self.pack_output_file_edit.setText(file_path)

	@Slot()
	def preview_inputs(self) -> None:
		try:
			config = self.build_config()
			count = sum(1 for _ in iter_images(config.input_dir, recursive=config.recursive))
			message = (
				f"预览完成：发现 {count} 张图片，输出格式为 {FORMAT_LABELS[config.output_format]}，"
				f"目标尺寸 {config.width}x{config.height}。"
			)
			self.append_log(message)
			self.status_label.setText(message)
		except Exception as exc:
			QMessageBox.critical(self, "预览失败", str(exc))

	def build_pack_config(self) -> PackConfig:
		input_dir_text = self.pack_input_dir_edit.text().strip()
		output_file_text = self.pack_output_file_edit.text().strip()
		input_dir = Path(input_dir_text)
		output_file = Path(output_file_text)
		if not input_dir.exists() or not input_dir.is_dir():
			raise FileNotFoundError(f"BIN 输入目录不存在: {input_dir}")
		if not output_file_text:
			raise ValueError("打包输出文件不能为空")
		if output_file.suffix.lower() != ".bin":
			output_file = output_file.with_suffix(".bin")
			self.pack_output_file_edit.setText(str(output_file))
		return PackConfig(
			input_dir=input_dir,
			output_file=output_file,
			recursive=self.pack_recursive_checkbox.isChecked(),
			overwrite=self.pack_overwrite_checkbox.isChecked(),
		)

	@Slot()
	def preview_pack_inputs(self) -> None:
		try:
			config = self.build_pack_config()
			count = sum(
				1
				for path in iter_bin_files(config.input_dir, recursive=config.recursive)
				if path.resolve() != config.output_file.resolve()
			)
			message = (
				f"预览完成：发现 {count} 个 .bin 文件，输出包为 {config.output_file.name}。"
			)
			self.append_log(message)
			self.status_label.setText(message)
		except Exception as exc:
			QMessageBox.critical(self, "预览失败", str(exc))

	@Slot()
	def start_convert(self) -> None:
		try:
			config = self.build_config()
			config.output_dir.mkdir(parents=True, exist_ok=True)
		except Exception as exc:
			QMessageBox.critical(self, "参数错误", str(exc))
			return

		self.log_edit.clear()
		self.progress_bar.setValue(0)
		self.status_label.setText("准备转换...")
		self.set_busy_state(True)

		self.worker_thread = QThread(self)
		self.worker = ConvertWorker(config)
		self.worker.moveToThread(self.worker_thread)

		self.worker_thread.started.connect(self.worker.run)
		self.worker.log.connect(self.append_log)
		self.worker.progress.connect(self.update_progress)
		self.worker.finished.connect(self.on_operation_finished)
		self.worker.failed.connect(self.on_operation_failed)
		self.worker.finished.connect(self.worker_thread.quit)
		self.worker.failed.connect(self.worker_thread.quit)
		self.worker_thread.finished.connect(self.cleanup_worker)

		self.worker_thread.start()

	@Slot()
	def cancel_convert(self) -> None:
		if self.worker is not None and hasattr(self.worker, "cancel"):
			self.worker.cancel()
			self.append_log("已请求取消，当前图片处理完成后停止。")
			self.status_label.setText("取消中...")
			self.cancel_button.setEnabled(False)

	@Slot()
	def start_pack(self) -> None:
		try:
			config = self.build_pack_config()
			config.output_file.parent.mkdir(parents=True, exist_ok=True)
		except Exception as exc:
			QMessageBox.critical(self, "参数错误", str(exc))
			return

		self.log_edit.clear()
		self.progress_bar.setValue(0)
		self.status_label.setText("准备打包 BIN...")
		self.set_busy_state(True)

		self.worker_thread = QThread(self)
		self.worker = PackWorker(config)
		self.worker.moveToThread(self.worker_thread)

		self.worker_thread.started.connect(self.worker.run)
		self.worker.log.connect(self.append_log)
		self.worker.progress.connect(self.update_progress)
		self.worker.finished.connect(self.on_operation_finished)
		self.worker.failed.connect(self.on_operation_failed)
		self.worker.finished.connect(self.worker_thread.quit)
		self.worker.failed.connect(self.worker_thread.quit)
		self.worker_thread.finished.connect(self.cleanup_worker)

		self.worker_thread.start()

	@Slot(int, int)
	def update_progress(self, current: int, total: int) -> None:
		percent = 0 if total == 0 else int(current * 100 / total)
		self.progress_bar.setValue(percent)
		self.status_label.setText(f"当前进度: {current}/{total}")

	@Slot(dict)
	def on_operation_finished(self, result: dict[str, int | bool | str]) -> None:
		self.set_busy_state(False)
		self.progress_bar.setValue(100 if result["total"] else 0)

		mode = str(result.get("mode", "convert"))
		if mode == "pack":
			summary = f"BIN 打包完成：共 {result['total']} 个文件，输出 {result['output']}。"
		else:
			summary = (
				f"任务结束：总数 {result['total']}，成功 {result['converted']}，"
				f"跳过 {result['skipped']}，失败 {result['failed']}。"
			)
		if result["cancelled"]:
			summary = "任务已取消。" + summary
		self.append_log(summary)
		self.status_label.setText(summary)
		QMessageBox.information(self, "操作完成", summary)

	@Slot(str)
	def on_operation_failed(self, message: str) -> None:
		self.set_busy_state(False)
		self.append_log(f"操作失败: {message}")
		self.status_label.setText("操作失败")
		QMessageBox.critical(self, "操作失败", message)

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