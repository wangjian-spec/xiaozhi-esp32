import json
import os
import sys
import uuid
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Dict, List, Optional

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import (
    QAction,
    QBrush,
    QColor,
    QCursor,
    QKeySequence,
    QPainter,
    QPen,
    QUndoCommand,
    QUndoStack,
)
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QDockWidget,
    QFileDialog,
    QFormLayout,
    QGraphicsItem,
    QGraphicsRectItem,
    QGraphicsScene,
    QGraphicsTextItem,
    QGraphicsView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSizePolicy,
    QTabWidget,
    QSpinBox,
    QToolBar,
    QVBoxLayout,
    QWidget,
)

CANVAS_WIDTH = 400
CANVAS_HEIGHT = 300
GRID_SIZE = 10
EDITOR_STATE_FILE = os.path.join(os.path.dirname(__file__), "ui_editor_state.json")
DEFAULT_UI_VERSION = "1.0"
DEFAULT_TARGET = "esp32-s3"
DEFAULT_DISPLAY_TYPE = "eink"
DEFAULT_DISPLAY_COLOR = "mono"
DEFAULT_GENERATOR_TOOL = "ui_editor"
DEFAULT_GENERATOR_VERSION = "0.3.1"

WIDGET_TYPES = [
    "Label",
    "Image",
    "Progress",
    "Canvas",
    "LineEdit",
    "TextArea",
    "Button",
    "Checkbox",
    "Radio",
    "Switch",
    "Dropdown",
    "ComboBox",
    "ListView",
    "TableView",
    "GridView",
    "Slider",
    "ScrollBar",
    "ScrollArea",
    "ScrollView",
    "Panel",
    "Frame",
    "GroupBox",
    "TabView",
    "TabWidget",
    "HBox",
    "VBox",
    "GridLayout",
    "DatePicker",
    "TimePicker",
    "Calendar",
    "Spinner",
    "Stepper",
    "Tooltip",
    "LabelTip",
    "CustomWidget",

    # Menu system controls
    "MenuBar",
    "Menu",
    "SubMenu",
    "MenuItem",
    "CheckMenuItem",
    "RadioMenuItem",
    "MenuSeparator",

    # Context menu
    "ContextMenu",
    "ContextMenuItem",
    "ContextSubMenu",

    # Navigation
    "NavBar",
    "SideMenu",
    "Breadcrumb",
    "Drawer",

    # Tabs
    "TabView",
    "TabPage",
    "TabItem",
    "TabHeader",

    # Overlays / dialogs
    "Dialog",
    "ConfirmDialog",
    "Alert",
    "Toast",
    "Popover",
    "Modal",
    "Overlay",
]

# Categorize controls for the palette UI
CONTROL_CATEGORIES = {
    "Basics": [
        "Label",
        "Image",
        "Button",
        "Checkbox",
        "Radio",
        "LineEdit",
        "TextArea",
    ],
    "Layout": ["Panel", "Frame", "GroupBox", "HBox", "VBox", "GridLayout"],
    "Menus": [
        "MenuBar",
        "Menu",
        "SubMenu",
        "MenuItem",
        "CheckMenuItem",
        "RadioMenuItem",
        "MenuSeparator",
    ],
    "Context Menus": ["ContextMenu", "ContextMenuItem", "ContextSubMenu"],
    "Navigation": ["NavBar", "SideMenu", "Drawer", "Breadcrumb"],
    "Tabs": ["TabView", "TabPage", "TabItem", "TabHeader"],
    "Overlays": ["Dialog", "ConfirmDialog", "Alert", "Toast", "Popover", "Modal", "Overlay"],
    "Advanced": [
        "Progress",
        "Canvas",
        "ListView",
        "TableView",
        "GridView",
        "Slider",
        "ScrollBar",
        "ScrollArea",
        "ScrollView",
        "DatePicker",
        "TimePicker",
        "Calendar",
        "Spinner",
        "Stepper",
        "Tooltip",
        "LabelTip",
        "CustomWidget",
    ],
}


@dataclass
class WidgetData:
    id: str
    type: str
    x: int
    y: int
    w: int
    h: int
    text: str = ""
    style: str = ""
    z_order: int = 0
    events: Dict[str, str] = field(default_factory=dict)


@dataclass
class SceneData:
    id: str
    name: str
    widgets: List[WidgetData] = field(default_factory=list)


@dataclass
class ProjectData:
    version: str = "3.0"
    scenes: List[SceneData] = field(default_factory=list)
    styles: List[Dict[str, object]] = field(default_factory=list)
    fonts: List[Dict[str, object]] = field(default_factory=list)
    images: List[Dict[str, object]] = field(default_factory=list)
    favorites: List[str] = field(default_factory=list)
    control_defaults: Dict[str, Dict[str, object]] = field(default_factory=dict)


class ResizeHandle(QGraphicsRectItem):
    def __init__(self, owner: "WidgetItem", corner: str):
        super().__init__(-4, -4, 8, 8, owner)
        self.owner = owner
        self.corner = corner
        self.setBrush(QBrush(QColor("#ffffff")))
        self.setPen(QPen(QColor("#3b82f6"), 1))
        self.setFlag(QGraphicsItem.ItemIsMovable, True)
        self.setFlag(QGraphicsItem.ItemSendsGeometryChanges, True)
        self.setFlag(QGraphicsItem.ItemIgnoresParentOpacity, True)
        self.setCursor(Qt.SizeFDiagCursor)

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionChange:
            self.owner.resize_from_handle(self.corner, value)
            return QPointF(0, 0)
        return super().itemChange(change, value)


class WidgetItem(QGraphicsRectItem):
    def __init__(self, data: WidgetData, editor: "EditorWindow"):
        super().__init__(0, 0, data.w, data.h)
        self.data = data
        self.editor = editor
        self.text_item = QGraphicsTextItem(self)
        self.text_item.setDefaultTextColor(QColor("#111827"))
        self.setFlags(
            QGraphicsItem.ItemIsMovable
            | QGraphicsItem.ItemIsSelectable
            | QGraphicsItem.ItemSendsGeometryChanges
        )
        self.setBrush(QBrush(QColor("#e5e7eb")))
        self.setPen(QPen(QColor("#374151"), 1))
        self.handles: Dict[str, ResizeHandle] = {}
        self._handles_visible = False
        self._press_pos = QPointF()
        self._start_rect = QRectF()
        self.sync_from_data()

    def sync_from_data(self):
        self.setRect(0, 0, self.data.w, self.data.h)
        self.setPos(self.data.x, self.data.y)
        self.setZValue(self.data.z_order)
        self.text_item.setPlainText(self.data.text or self.data.type)
        self.text_item.setPos(4, 4)
        self.update_handles()

    def update_handles(self):
        selected = self.isSelected()
        if selected and not self._handles_visible:
            if not self.handles:
                self.handles["br"] = ResizeHandle(self, "br")
            self._handles_visible = True
        if not selected and self._handles_visible:
            for handle in self.handles.values():
                handle.setParentItem(None)
                if self.scene():
                    self.scene().removeItem(handle)
            self.handles.clear()
            self._handles_visible = False
        if self._handles_visible and "br" in self.handles:
            self.handles["br"].setPos(self.rect().width(), self.rect().height())

    def resize_from_handle(self, corner: str, pos: QPointF):
        new_w = max(1, int(pos.x()))
        new_h = max(1, int(pos.y()))
        rect = QRectF(0, 0, new_w, new_h)
        self.setRect(rect)
        self.text_item.setPos(4, 4)
        self.editor.update_data_from_item(self, update_panel=False)

    def mousePressEvent(self, event):
        self._press_pos = self.pos()
        self._start_rect = self.rect()
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        super().mouseReleaseEvent(event)
        if self._press_pos != self.pos() or self._start_rect != self.rect():
            self.editor.commit_move_resize(self, self._press_pos, self._start_rect)

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemSelectedHasChanged:
            self.update_handles()
        if change == QGraphicsItem.ItemPositionChange:
            # Allow arbitrary positions (integers) without snapping
            new_pos: QPointF = value
            try:
                return QPointF(int(new_pos.x()), int(new_pos.y()))
            except Exception:
                return new_pos
        return super().itemChange(change, value)


class CanvasScene(QGraphicsScene):
    selection_changed = Signal()

    def __init__(self, editor: "EditorWindow"):
        super().__init__(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT)
        self.editor = editor
        self.setBackgroundBrush(QBrush(QColor("#f8fafc")))
        self.selectionChanged.connect(self.selection_changed.emit)

    def drawBackground(self, painter: QPainter, rect: QRectF) -> None:
        super().drawBackground(painter, rect)
        painter.save()
        # Grid drawing removed; canvas is intended to be clean for free placement

        # Draw top/bottom guide lines: 20px from top, 16px from bottom, solid black
        guide_pen = QPen(QColor("#000000"), 1, Qt.SolidLine)
        painter.setPen(guide_pen)
        top_y = 20
        bottom_y = CANVAS_HEIGHT - 16
        painter.drawLine(0, top_y, CANVAS_WIDTH, top_y)
        painter.drawLine(0, bottom_y, CANVAS_WIDTH, bottom_y)
        # Draw 1px canvas border (inset by 0.5 for crisp stroke)
        border_pen = QPen(QColor("#000000"), 1, Qt.SolidLine)
        painter.setPen(border_pen)
        painter.drawRect(0.5, 0.5, CANVAS_WIDTH - 1, CANVAS_HEIGHT - 1)
        painter.restore()


class MoveResizeCommand(QUndoCommand):
    def __init__(self, item: WidgetItem, old_pos: QPointF, old_rect: QRectF):
        super().__init__("Move/Resize Widget")
        self.item = item
        self.old_pos = old_pos
        self.old_rect = old_rect
        self.new_pos = item.pos()
        self.new_rect = item.rect()

    def undo(self):
        self.item.setPos(self.old_pos)
        self.item.setRect(self.old_rect)
        self.item.editor.update_data_from_item(self.item, update_panel=True)

    def redo(self):
        self.item.setPos(self.new_pos)
        self.item.setRect(self.new_rect)
        self.item.editor.update_data_from_item(self.item, update_panel=True)


class PropertyChangeCommand(QUndoCommand):
    def __init__(self, item: WidgetItem, before: WidgetData, after: WidgetData):
        super().__init__("Edit Widget Properties")
        self.item = item
        self.before = before
        self.after = after

    def undo(self):
        self.item.data = WidgetData(**asdict(self.before))
        self.item.editor.replace_widget_data(self.item, self.item.data)

    def redo(self):
        self.item.data = WidgetData(**asdict(self.after))
        self.item.editor.replace_widget_data(self.item, self.item.data)


class AddWidgetCommand(QUndoCommand):
    def __init__(self, editor: "EditorWindow", widget: WidgetData):
        super().__init__("Add Widget")
        self.editor = editor
        self.widget = widget
        self.item: Optional[WidgetItem] = None

    def redo(self):
        self.item = self.editor.add_widget_item(self.widget)

    def undo(self):
        if self.item:
            self.editor.remove_widget_item(self.item)


class RemoveWidgetCommand(QUndoCommand):
    def __init__(self, editor: "EditorWindow", item: WidgetItem):
        super().__init__("Remove Widget")
        self.editor = editor
        self.item = item
        self.widget = item.data

    def redo(self):
        self.editor.remove_widget_item(self.item)

    def undo(self):
        self.editor.add_widget_item(self.widget)


class EditorWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP32 UI Editor")
        self.resize(1000, 600)

        # No grid and no snapping by default so controls can be placed freely
        self.grid_visible = False
        self.snap_to_grid = False

        self.project = ProjectData(scenes=[SceneData(id="page_main", name="MainPage")])
        self.current_scene = self.project.scenes[0]
        self.load_editor_state()

        self.undo_stack = QUndoStack(self)
        self._updating_properties = False
        self._clipboard = None

        self.scene = CanvasScene(self)
        self.scene.selection_changed.connect(self.on_selection_changed)
        self.view = QGraphicsView(self.scene)
        self.view.setRenderHint(QPainter.Antialiasing)
        self.view.setDragMode(QGraphicsView.RubberBandDrag)
        # Add a 1px viewport margin so the scene's 1px border isn't clipped
        self.view.setViewportMargins(1, 1, 1, 1)
        # Let the view expand/contract with the central widget; we'll scale the scene to fit
        self.view.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        # Prevent panning via middle-button; install event filter on viewport
        self.view.viewport().installEventFilter(self)
        self.view.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.view.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.scene.setSceneRect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT)

        central = QWidget()
        layout = QHBoxLayout(central)

        # Canvas area with alignment buttons below
        canvas_container = QWidget()
        canvas_layout = QVBoxLayout(canvas_container)
        canvas_layout.setContentsMargins(0, 0, 0, 0)
        canvas_layout.setSpacing(4)
        # Center the canvas view in the container
        canvas_layout.addWidget(self.view, alignment=Qt.AlignCenter)

        align_bar = QWidget()
        align_layout = QHBoxLayout(align_bar)
        align_layout.setContentsMargins(0, 0, 0, 0)
        align_layout.setSpacing(4)

        # Alignment: Left/Right/Top/Bottom + Distribute H/V
        btn_align_left = QPushButton("Left")
        btn_align_left.clicked.connect(self.align_left)
        align_layout.addWidget(btn_align_left)

        btn_align_right = QPushButton("Right")
        btn_align_right.clicked.connect(self.align_right)
        align_layout.addWidget(btn_align_right)

        btn_align_top = QPushButton("Top")
        btn_align_top.clicked.connect(self.align_top)
        align_layout.addWidget(btn_align_top)

        btn_align_bottom = QPushButton("Bottom")
        btn_align_bottom.clicked.connect(self.align_bottom)
        align_layout.addWidget(btn_align_bottom)

        btn_distribute_h = QPushButton("Distribute H")
        btn_distribute_h.clicked.connect(self.distribute_h)
        align_layout.addWidget(btn_distribute_h)

        btn_distribute_v = QPushButton("Distribute V")
        btn_distribute_v.clicked.connect(self.distribute_v)
        align_layout.addWidget(btn_distribute_v)

        canvas_layout.addWidget(align_bar)
        layout.addWidget(canvas_container)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.setCentralWidget(central)

        self.property_panel = self.build_property_panel()
        self.scene_list = self.build_scene_list()
        self.widget_palette = self.build_widget_palette()
        self.resources_panel = self.build_resources_panel()
        self.build_toolbar()

        dock_left = QDockWidget("Pages", self)
        dock_left.setWidget(self.scene_list)
        dock_left.setAllowedAreas(Qt.LeftDockWidgetArea)
        self.addDockWidget(Qt.LeftDockWidgetArea, dock_left)

        dock_right = QDockWidget("Properties", self)
        dock_right.setWidget(self.property_panel)
        dock_right.setAllowedAreas(Qt.RightDockWidgetArea)
        self.addDockWidget(Qt.RightDockWidgetArea, dock_right)

        dock_palette = QDockWidget("Widgets", self)
        dock_palette.setWidget(self.widget_palette)
        dock_palette.setAllowedAreas(Qt.LeftDockWidgetArea)
        self.addDockWidget(Qt.LeftDockWidgetArea, dock_palette)
        self.splitDockWidget(dock_left, dock_palette, Qt.Vertical)

        dock_resources = QDockWidget("Styles & Resources", self)
        dock_resources.setWidget(self.resources_panel)
        dock_resources.setAllowedAreas(Qt.RightDockWidgetArea)
        self.addDockWidget(Qt.RightDockWidgetArea, dock_resources)

        self.refresh_scene()
        # Ensure the view initially fits the entire canvas
        self.view.resetTransform()
        self.view.fitInView(self.scene.sceneRect(), Qt.KeepAspectRatio)

    def resizeEvent(self, event):
        # Keep the canvas fitted to the available view area when the window resizes
        try:
            self.view.resetTransform()
            self.view.fitInView(self.scene.sceneRect(), Qt.KeepAspectRatio)
        except Exception:
            pass
        return super().resizeEvent(event)

    def eventFilter(self, obj, event):
        # Block middle-button pan and touchpad two-finger drag that may move the view
        from PySide6.QtCore import QEvent
        if obj is self.view.viewport():
            if event.type() == QEvent.MouseButtonPress:
                if event.button() == Qt.MiddleButton:
                    return True
            if event.type() == QEvent.MouseMove:
                buttons = event.buttons()
                if buttons & Qt.MiddleButton:
                    return True
        return super().eventFilter(obj, event)

    def build_toolbar(self):
        toolbar = QToolBar("Tools")
        self.addToolBar(toolbar)
        toolbar.addAction(self.undo_stack.createUndoAction(self, "Undo"))
        toolbar.addAction(self.undo_stack.createRedoAction(self, "Redo"))

        toolbar.addSeparator()
        export_action = QAction("Export JSON", self)
        export_action.triggered.connect(self.export_json)
        toolbar.addAction(export_action)

        open_action = QAction("Open JSON", self)
        open_action.triggered.connect(self.open_json)
        toolbar.addAction(open_action)

        save_action = QAction("Save JSON", self)
        save_action.triggered.connect(self.save_json)
        toolbar.addAction(save_action)

        delete_action = QAction("Delete Selected", self)
        delete_action.triggered.connect(self.delete_selected_widgets)
        toolbar.addAction(delete_action)

        copy_action = QAction("Copy", self)
        copy_action.setShortcut("Ctrl+C")
        copy_action.triggered.connect(self.copy_selected_widgets)
        toolbar.addAction(copy_action)

        paste_action = QAction("Paste", self)
        paste_action.setShortcut("Ctrl+V")
        paste_action.triggered.connect(self.paste_widgets)
        toolbar.addAction(paste_action)

        new_action = QAction("New File", self)
        new_action.triggered.connect(self.new_file)
        toolbar.addAction(new_action)

        close_action = QAction("Close File", self)
        close_action.triggered.connect(self.close_current_file)
        toolbar.addAction(close_action)

        toolbar.addSeparator()
        grid_action = QAction("Grid", self)
        grid_action.setCheckable(True)
        grid_action.setChecked(False)
        grid_action.triggered.connect(self.toggle_grid)
        toolbar.addAction(grid_action)

        snap_action = QAction("Snap", self)
        snap_action.setCheckable(True)
        snap_action.setChecked(False)
        snap_action.triggered.connect(self.toggle_snap)
        toolbar.addAction(snap_action)

        z_up = QAction("Bring Front", self)
        z_up.triggered.connect(self.bring_front)
        toolbar.addAction(z_up)

        z_down = QAction("Send Back", self)
        z_down.triggered.connect(self.send_back)
        toolbar.addAction(z_down)

    def keyPressEvent(self, event):
        # Delete key removes selected widgets
        if event.key() == Qt.Key_Delete:
            self.delete_selected_widgets()
            return
        # Copy / Paste
        if event.modifiers() & Qt.ControlModifier:
            if event.key() == Qt.Key_C:
                self.copy_selected_widgets()
                return
            if event.key() == Qt.Key_V:
                self.paste_widgets()
                return
        return super().keyPressEvent(event)

    # Alignment helpers
    def _selection_bbox(self):
        items = self.selected_items()
        if not items:
            return None
        min_x = min(int(item.pos().x()) for item in items)
        min_y = min(int(item.pos().y()) for item in items)
        max_x = max(int(item.pos().x() + item.rect().width()) for item in items)
        max_y = max(int(item.pos().y() + item.rect().height()) for item in items)
        return (min_x, min_y, max_x, max_y)

    def align_left(self):
        items = self.selected_items()
        if not items:
            return
        bbox = self._selection_bbox()
        if not bbox:
            return
        min_x = bbox[0]
        for item in items:
            old_pos = QPointF(item.pos())
            old_rect = QRectF(item.rect())
            item.setPos(min_x, item.pos().y())
            self.update_data_from_item(item, update_panel=False)
            self.commit_move_resize(item, old_pos, old_rect)

    def align_right(self):
        items = self.selected_items()
        if not items:
            return
        bbox = self._selection_bbox()
        if not bbox:
            return
        _, _, max_x, _ = bbox
        for item in items:
            old_pos = QPointF(item.pos())
            old_rect = QRectF(item.rect())
            w = item.rect().width()
            item.setPos(int(max_x - w), item.pos().y())
            self.update_data_from_item(item, update_panel=False)
            self.commit_move_resize(item, old_pos, old_rect)

    def align_top(self):
        items = self.selected_items()
        if not items:
            return
        bbox = self._selection_bbox()
        if not bbox:
            return
        _, min_y, _, _ = bbox
        for item in items:
            old_pos = QPointF(item.pos())
            old_rect = QRectF(item.rect())
            item.setPos(item.pos().x(), min_y)
            self.update_data_from_item(item, update_panel=False)
            self.commit_move_resize(item, old_pos, old_rect)

    def distribute_h(self):
        items = self.selected_items()
        if not items or len(items) < 2:
            return
        # sort by current x (center)
        items_sorted = sorted(items, key=lambda it: it.pos().x() + it.rect().width() / 2)
        centers = [it.pos().x() + it.rect().width() / 2 for it in items_sorted]
        min_c = min(centers)
        max_c = max(centers)
        n = len(items_sorted)
        for i, item in enumerate(items_sorted):
            old_pos = QPointF(item.pos())
            old_rect = QRectF(item.rect())
            target_c = min_c + (max_c - min_c) * (i / (n - 1)) if n > 1 else centers[i]
            w = item.rect().width()
            item.setPos(int(target_c - w / 2), int(item.pos().y()))
            self.update_data_from_item(item, update_panel=False)
            self.commit_move_resize(item, old_pos, old_rect)

    def distribute_v(self):
        items = self.selected_items()
        if not items or len(items) < 2:
            return
        items_sorted = sorted(items, key=lambda it: it.pos().y() + it.rect().height() / 2)
        centers = [it.pos().y() + it.rect().height() / 2 for it in items_sorted]
        min_c = min(centers)
        max_c = max(centers)
        n = len(items_sorted)
        for i, item in enumerate(items_sorted):
            old_pos = QPointF(item.pos())
            old_rect = QRectF(item.rect())
            target_c = min_c + (max_c - min_c) * (i / (n - 1)) if n > 1 else centers[i]
            h = item.rect().height()
            item.setPos(int(item.pos().x()), int(target_c - h / 2))
            self.update_data_from_item(item, update_panel=False)
            self.commit_move_resize(item, old_pos, old_rect)

    def align_bottom(self):
        items = self.selected_items()
        if not items:
            return
        bbox = self._selection_bbox()
        if not bbox:
            return
        _, _, _, max_y = bbox
        for item in items:
            old_pos = QPointF(item.pos())
            old_rect = QRectF(item.rect())
            h = item.rect().height()
            item.setPos(item.pos().x(), int(max_y - h))
            self.update_data_from_item(item, update_panel=False)
            self.commit_move_resize(item, old_pos, old_rect)

    def build_widget_palette(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)

        # Favorites column
        fav_layout = QVBoxLayout()
        fav_layout.addWidget(QLabel("Favorites"))
        self.favorites_list = QListWidget()
        self.favorites_list.itemDoubleClicked.connect(self.add_widget_from_palette)
        fav_layout.addWidget(self.favorites_list)
        remove_fav_btn = QPushButton("Remove Favorite")
        remove_fav_btn.clicked.connect(self.remove_favorite)
        fav_layout.addWidget(remove_fav_btn)

        # Controls categorized in tabs
        all_layout = QVBoxLayout()
        all_layout.addWidget(QLabel("All Controls"))
        self.controls_tabs = QTabWidget()
        self.category_lists: Dict[str, QListWidget] = {}
        for cat, types in CONTROL_CATEGORIES.items():
            lst = QListWidget()
            lst.itemDoubleClicked.connect(self.add_widget_from_palette)
            self.category_lists[cat] = lst
            self.controls_tabs.addTab(lst, cat)
        all_layout.addWidget(self.controls_tabs)

        btns = QHBoxLayout()
        add_to_fav_btn = QPushButton("Add to Favorites")
        add_to_fav_btn.clicked.connect(self.add_to_favorites)
        add_selected_btn = QPushButton("Add Selected")
        add_selected_btn.clicked.connect(self.add_widget_from_palette_button)
        btns.addWidget(add_to_fav_btn)
        btns.addWidget(add_selected_btn)
        all_layout.addLayout(btns)

        top = QHBoxLayout()
        top.addLayout(fav_layout)
        top.addLayout(all_layout)

        layout.addLayout(top)
        layout.addStretch(1)

        self.refresh_palette_lists()
        return widget

    def refresh_palette_lists(self):
        # populate favorites and all controls lists
        self.favorites_list.clear()
        favs = getattr(self.project, 'favorites', []) or []
        for f in favs:
            QListWidgetItem(f, self.favorites_list)
        # populate category lists
        for cat, lst in getattr(self, 'category_lists', {}).items():
            lst.clear()
            types = CONTROL_CATEGORIES.get(cat, [])
            for t in types:
                if t not in favs:
                    QListWidgetItem(t, lst)

    def add_to_favorites(self):
        name = self.get_selected_control()
        if not name:
            return
        if name not in self.project.favorites:
            self.project.favorites.append(name)
            self.save_editor_state()
        self.refresh_palette_lists()

    def get_selected_control(self) -> Optional[str]:
        # check current tab list
        if hasattr(self, 'controls_tabs'):
            w = self.controls_tabs.currentWidget()
            if isinstance(w, QListWidget):
                it = w.currentItem()
                if it:
                    return it.text()
        # fallback to favorites selection
        if hasattr(self, 'favorites_list'):
            it = self.favorites_list.currentItem()
            if it:
                return it.text()
        return None

    def remove_favorite(self):
        item = self.favorites_list.currentItem()
        if not item:
            return
        name = item.text()
        if name in self.project.favorites:
            self.project.favorites.remove(name)
            self.save_editor_state()
        self.refresh_palette_lists()

    def build_scene_list(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        self.scene_list_widget = QListWidget()
        layout.addWidget(self.scene_list_widget)
        for scene in self.project.scenes:
            item = QListWidgetItem(scene.name)
            item.setData(Qt.UserRole, scene)
            self.scene_list_widget.addItem(item)
        self.scene_list_widget.currentRowChanged.connect(self.switch_scene)

        self.scene_name_edit = QLineEdit()
        # Apply rename when editing finished (press Enter or focus lost)
        self.scene_name_edit.editingFinished.connect(self.rename_scene)
        layout.addWidget(QLabel("Page Name"))
        layout.addWidget(self.scene_name_edit)

        add_btn = QPushButton("Add Page")
        add_btn.clicked.connect(self.add_scene)
        layout.addWidget(add_btn)

        remove_btn = QPushButton("Remove Current Page")
        remove_btn.clicked.connect(self.remove_scene)
        layout.addWidget(remove_btn)

        save_btn = QPushButton("Save All Pages")
        save_btn.clicked.connect(self.save_json)
        layout.addWidget(save_btn)

        open_btn = QPushButton("Open Pages")
        open_btn.clicked.connect(self.open_json)
        layout.addWidget(open_btn)

        return widget

    def build_property_panel(self) -> QWidget:
        widget = QWidget()
        layout = QFormLayout(widget)

        self.widget_list = QListWidget()
        self.widget_list.currentRowChanged.connect(self.select_widget_from_list)
        layout.addRow("Widgets", self.widget_list)

        self.prop_id = QLineEdit()
        self.prop_type = QLineEdit()
        self.prop_x = QSpinBox()
        self.prop_y = QSpinBox()
        self.prop_w = QSpinBox()
        self.prop_h = QSpinBox()
        self.prop_text = QLineEdit()
        self.prop_style = QLineEdit()
        self.prop_z = QSpinBox()
        self.prop_onclick = QLineEdit()

        # Allow arbitrary positive integers for coords and sizes
        for spin in [self.prop_x, self.prop_y]:
            spin.setRange(0, 100000)
            spin.setSingleStep(1)
        for spin in [self.prop_w, self.prop_h]:
            spin.setRange(1, 100000)
            spin.setSingleStep(1)
        # z-order can be negative if desired
        self.prop_z.setRange(-10000, 10000)

        layout.addRow("ID", self.prop_id)
        layout.addRow("Type", self.prop_type)
        layout.addRow("X", self.prop_x)
        layout.addRow("Y", self.prop_y)
        layout.addRow("W", self.prop_w)
        layout.addRow("H", self.prop_h)
        layout.addRow("Text", self.prop_text)
        layout.addRow("Style", self.prop_style)
        layout.addRow("Z", self.prop_z)
        layout.addRow("onClick", self.prop_onclick)

        self.prop_id.editingFinished.connect(self.apply_properties)
        self.prop_type.editingFinished.connect(self.apply_properties)
        self.prop_text.editingFinished.connect(self.apply_properties)
        self.prop_style.editingFinished.connect(self.apply_properties)
        self.prop_onclick.editingFinished.connect(self.apply_properties)
        for spin in [self.prop_x, self.prop_y, self.prop_w, self.prop_h, self.prop_z]:
            spin.valueChanged.connect(self.apply_properties)

        # 'Save As Default' button removed; defaults are now auto-saved on size changes

        return widget

    def save_selected_as_default(self):
        selected = self.selected_items()
        if not selected:
            QMessageBox.information(self, "No selection", "Select a widget first.")
            return
        item = selected[0]
        defaults = {
            "w": item.data.w,
            "h": item.data.h,
            "text": item.data.text,
            "style": item.data.style,
            "z_order": item.data.z_order,
            "events": item.data.events.copy(),
        }
        self.project.control_defaults[item.data.type] = defaults
        self.save_editor_state()
        QMessageBox.information(self, "Saved", f"Defaults saved for {item.data.type}")

    def load_editor_state(self):
        if not os.path.exists(EDITOR_STATE_FILE):
            return
        try:
            with open(EDITOR_STATE_FILE, "r", encoding="utf-8") as f:
                raw = json.load(f)
            self.project.favorites = list(raw.get("favorites", []))
            self.project.control_defaults = dict(raw.get("control_defaults", {}))
        except Exception:
            return

    def save_editor_state(self):
        data = {
            "favorites": getattr(self.project, "favorites", []),
            "control_defaults": getattr(self.project, "control_defaults", {}),
        }
        try:
            with open(EDITOR_STATE_FILE, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
        except Exception:
            return

    def build_resources_panel(self) -> QWidget:
        tabs = QTabWidget()

        tabs.addTab(self._build_style_tab(), "Styles")
        tabs.addTab(self._build_font_tab(), "Fonts")
        tabs.addTab(self._build_image_tab(), "Images")
        return tabs

    def _build_style_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        self.style_list = QListWidget()
        layout.addWidget(self.style_list)

        form = QFormLayout()
        self.style_id = QLineEdit()
        self.style_bg = QLineEdit()
        self.style_text = QLineEdit()
        self.style_font = QLineEdit()
        self.style_radius = QSpinBox()
        self.style_radius.setRange(0, 1000)
        form.addRow("ID", self.style_id)
        form.addRow("bgColor", self.style_bg)
        form.addRow("textColor", self.style_text)
        form.addRow("font", self.style_font)
        form.addRow("borderRadius", self.style_radius)
        layout.addLayout(form)

        btns = QHBoxLayout()
        add_btn = QPushButton("Add/Update")
        remove_btn = QPushButton("Remove")
        add_btn.clicked.connect(self.add_or_update_style)
        remove_btn.clicked.connect(self.remove_style)
        btns.addWidget(add_btn)
        btns.addWidget(remove_btn)
        layout.addLayout(btns)

        self.style_list.currentRowChanged.connect(self.load_style)
        self.refresh_styles()
        return widget

    def _build_font_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        self.font_list = QListWidget()
        layout.addWidget(self.font_list)

        form = QFormLayout()
        self.font_id = QLineEdit()
        self.font_path = QLineEdit()
        self.font_size = QSpinBox()
        self.font_size.setRange(1, 256)
        form.addRow("ID", self.font_id)
        form.addRow("path", self.font_path)
        form.addRow("size", self.font_size)
        layout.addLayout(form)

        btns = QHBoxLayout()
        add_btn = QPushButton("Add/Update")
        remove_btn = QPushButton("Remove")
        add_btn.clicked.connect(self.add_or_update_font)
        remove_btn.clicked.connect(self.remove_font)
        btns.addWidget(add_btn)
        btns.addWidget(remove_btn)
        layout.addLayout(btns)

        self.font_list.currentRowChanged.connect(self.load_font)
        self.refresh_fonts()
        return widget

    def _build_image_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        self.image_list = QListWidget()
        layout.addWidget(self.image_list)

        form = QFormLayout()
        self.image_id = QLineEdit()
        self.image_path = QLineEdit()
        self.image_w = QSpinBox()
        self.image_h = QSpinBox()
        self.image_w.setRange(1, 5000)
        self.image_h.setRange(1, 5000)
        form.addRow("ID", self.image_id)
        form.addRow("path", self.image_path)
        form.addRow("w", self.image_w)
        form.addRow("h", self.image_h)
        layout.addLayout(form)

        btns = QHBoxLayout()
        add_btn = QPushButton("Add/Update")
        remove_btn = QPushButton("Remove")
        add_btn.clicked.connect(self.add_or_update_image)
        remove_btn.clicked.connect(self.remove_image)
        btns.addWidget(add_btn)
        btns.addWidget(remove_btn)
        layout.addLayout(btns)

        self.image_list.currentRowChanged.connect(self.load_image)
        self.refresh_images()
        return widget

    def refresh_scene(self):
        self.scene.clear()
        self.refresh_widget_list()
        for widget in self.current_scene.widgets:
            self.add_widget_item(widget)

    def add_widget_item(self, widget: WidgetData) -> WidgetItem:
        item = WidgetItem(widget, self)
        self.scene.addItem(item)
        return item

    def remove_widget_item(self, item: WidgetItem):
        if item.data in self.current_scene.widgets:
            self.current_scene.widgets.remove(item.data)
        self.scene.removeItem(item)

    def add_widget(self, widget_type: str):
        # apply control defaults if available
        defs = getattr(self.project, 'control_defaults', {}) or {}
        cd = defs.get(widget_type, {})
        w = int(cd.get('w', 80))
        h = int(cd.get('h', 40))
        x = int(cd.get('x', 10))
        y = int(cd.get('y', 10))
        text = str(cd.get('text', widget_type))
        z = int(cd.get('z_order', 0))
        events = dict(cd.get('events', {}))

        widget = WidgetData(
            id=f"{widget_type.lower()}_{uuid.uuid4().hex[:6]}",
            type=widget_type,
            x=x,
            y=y,
            w=w,
            h=h,
            text=text,
            z_order=z,
            events=events,
        )
        self.current_scene.widgets.append(widget)
        self.undo_stack.push(AddWidgetCommand(self, widget))

    def add_widget_from_palette(self, item: QListWidgetItem):
        if not item:
            return
        self.add_widget(item.text())

    def add_widget_from_palette_button(self):
        name = self.get_selected_control()
        if not name:
            return
        self.add_widget(name)

    def is_unique_widget_id(self, widget_id: str, current: Optional[WidgetData] = None) -> bool:
        for widget in self.current_scene.widgets:
            if widget is current:
                continue
            if widget.id == widget_id:
                return False
        return True

    def on_selection_changed(self):
        selected = self.selected_items()
        if not selected:
            return
        item = selected[0]
        self._updating_properties = True
        self.sync_widget_list_selection(item)
        self.prop_id.setText(item.data.id)
        self.prop_type.setText(item.data.type)
        self.prop_x.setValue(item.data.x)
        self.prop_y.setValue(item.data.y)
        self.prop_w.setValue(item.data.w)
        self.prop_h.setValue(item.data.h)
        self.prop_text.setText(item.data.text)
        self.prop_style.setText(item.data.style)
        self.prop_z.setValue(item.data.z_order)
        self.prop_onclick.setText(item.data.events.get("onClick", ""))
        self._updating_properties = False

    def selected_items(self) -> List[WidgetItem]:
        return [item for item in self.scene.selectedItems() if isinstance(item, WidgetItem)]

    def update_data_from_item(self, item: WidgetItem, update_panel: bool = True):
        pos = item.pos()
        rect = item.rect()
        item.data.x = int(pos.x())
        item.data.y = int(pos.y())
        item.data.w = int(rect.width())
        item.data.h = int(rect.height())
        item.data.z_order = int(item.zValue())
        if update_panel:
            self.on_selection_changed()

    def replace_widget_data(self, item: WidgetItem, data: WidgetData):
        if item.data in self.current_scene.widgets:
            idx = self.current_scene.widgets.index(item.data)
            self.current_scene.widgets[idx] = data
        item.data = data
        item.sync_from_data()
        self.on_selection_changed()

    def apply_properties(self):
        if self._updating_properties:
            return
        selected = self.selected_items()
        if not selected:
            return
        item = selected[0]
        new_id = self.prop_id.text().strip() or item.data.id
        if not self.is_unique_widget_id(new_id, current=item.data):
            QMessageBox.warning(self, "Duplicate ID", f"Widget ID '{new_id}' already exists.")
            self._updating_properties = True
            self.prop_id.setText(item.data.id)
            self._updating_properties = False
            return
        before = WidgetData(**asdict(item.data))
        item.data.id = new_id
        item.data.type = self.prop_type.text().strip() or item.data.type
        item.data.x = int(self.prop_x.value())
        item.data.y = int(self.prop_y.value())
        item.data.w = max(1, int(self.prop_w.value()))
        item.data.h = max(1, int(self.prop_h.value()))
        item.data.text = self.prop_text.text()
        item.data.style = self.prop_style.text()
        item.data.z_order = self.prop_z.value()
        onclick = self.prop_onclick.text().strip()
        if onclick:
            item.data.events["onClick"] = onclick
        elif "onClick" in item.data.events:
            item.data.events.pop("onClick")
        after = WidgetData(**asdict(item.data))
        if asdict(before) != asdict(after):
            self.undo_stack.push(PropertyChangeCommand(item, before, after))
            # Auto-save updated width/height defaults when user edits properties
            try:
                if not hasattr(self.project, 'control_defaults') or self.project.control_defaults is None:
                    self.project.control_defaults = {}
                defs = dict(self.project.control_defaults.get(item.data.type, {}))
                defs['w'] = int(item.data.w)
                defs['h'] = int(item.data.h)
                self.project.control_defaults[item.data.type] = defs
                self.save_editor_state()
            except Exception:
                pass

    def commit_move_resize(self, item: WidgetItem, old_pos: QPointF, old_rect: QRectF):
        self.undo_stack.push(MoveResizeCommand(item, old_pos, old_rect))
        try:
            old_w = int(old_rect.width())
            old_h = int(old_rect.height())
            new_w = int(item.rect().width())
            new_h = int(item.rect().height())
            if new_w != old_w or new_h != old_h:
                if not hasattr(self.project, 'control_defaults') or self.project.control_defaults is None:
                    self.project.control_defaults = {}
                defs = dict(self.project.control_defaults.get(item.data.type, {}))
                defs['w'] = new_w
                defs['h'] = new_h
                self.project.control_defaults[item.data.type] = defs
                self.save_editor_state()
        except Exception:
            pass

    def snap_value(self, value: int) -> int:
        return int(round(value / GRID_SIZE) * GRID_SIZE)

    def toggle_grid(self, checked: bool):
        self.grid_visible = checked
        self.scene.update()

    def toggle_snap(self, checked: bool):
        self.snap_to_grid = checked

    def bring_front(self):
        current = [w.z_order for w in self.current_scene.widgets]
        max_z = max(current) if current else 0
        for item in self.selected_items():
            max_z += 1
            item.setZValue(max_z)
            item.data.z_order = int(max_z)

    def send_back(self):
        current = [w.z_order for w in self.current_scene.widgets]
        min_z = min(current) if current else 0
        for item in self.selected_items():
            min_z -= 1
            item.setZValue(min_z)
            item.data.z_order = int(min_z)

    def delete_selected_widgets(self):
        selected = list(self.selected_items())
        if not selected:
            return
        for item in selected:
            self.undo_stack.push(RemoveWidgetCommand(self, item))

    def copy_selected_widgets(self):
        selected = list(self.selected_items())
        if not selected:
            return
        data = [asdict(item.data) for item in selected]
        try:
            self._clipboard = json.dumps(data)
        except Exception:
            self._clipboard = None

    def paste_widgets(self):
        if not self._clipboard:
            return
        try:
            data = json.loads(self._clipboard)
        except Exception:
            return
        # Paste each widget as a new widget with new id and offset
        for d in data:
            d_copy = dict(d)
            d_copy['id'] = f"{d_copy.get('type','widget').lower()}_{uuid.uuid4().hex[:6]}"
            d_copy['x'] = int(d_copy.get('x', 0)) + 10
            d_copy['y'] = int(d_copy.get('y', 0)) + 10
            widget = WidgetData(**d_copy)
            self.current_scene.widgets.append(widget)
            self.undo_stack.push(AddWidgetCommand(self, widget))

    def export_json(self):
        path, _ = QFileDialog.getSaveFileName(self, "Export JSON", "ui.json", "JSON (*.json)")
        if not path:
            return
        project_stem = Path(path).stem
        if project_stem.startswith("ui_"):
            project_name = project_stem
        else:
            project_name = f"ui_{project_stem}" if project_stem else "ui_project"

        texts: Dict[str, str] = {}
        text_id_by_value: Dict[str, str] = {}
        widgets: Dict[str, Dict[str, object]] = {}
        pages: Dict[str, Dict[str, object]] = {}

        for scene in self.project.scenes:
            root_id = f"root_{scene.id}"
            widgets[root_id] = {
                "type": "Container",
                "rect": {"x": 0, "y": 0, "w": CANVAS_WIDTH, "h": CANVAS_HEIGHT},
                "children": [],
            }
            scene_widget_ids: List[str] = []
            for w in scene.widgets:
                props: Dict[str, object] = {}
                if w.text:
                    existing = text_id_by_value.get(w.text)
                    if existing:
                        text_id = existing
                    else:
                        text_id = f"TEXT_{w.id}".upper().replace("-", "_")
                        text_id_by_value[w.text] = text_id
                        texts[text_id] = w.text
                    props["textId"] = text_id

                widgets[w.id] = {
                    "type": w.type,
                    "rect": {"x": w.x, "y": w.y, "w": w.w, "h": w.h},
                    "style": w.style or None,
                    "properties": props,
                }
                scene_widget_ids.append(w.id)
                widgets[root_id]["children"].append(w.id)

            pages[scene.id] = {
                "name": scene.name,
                "root": root_id,
            }

        styles: Dict[str, Dict[str, object]] = {}
        for style in self.project.styles:
            style_id = str(style.get("id", "")).strip()
            if not style_id:
                continue
            style_props = dict(style)
            style_props.pop("id", None)
            styles[style_id] = style_props

        fonts: Dict[str, Dict[str, object]] = {}
        for font in self.project.fonts:
            font_id = str(font.get("id", "")).strip()
            if not font_id:
                continue
            fonts[font_id] = {
                "file": str(font.get("path", "")),
                "height": int(font.get("size", 0)),
            }

        images: Dict[str, Dict[str, object]] = {}
        for image in self.project.images:
            image_id = str(image.get("id", "")).strip()
            if not image_id:
                continue
            images[image_id] = {
                "file": str(image.get("path", "")),
                "width": int(image.get("w", 0)),
                "height": int(image.get("h", 0)),
            }

        data = {
            "meta": {
                "project": project_name,
                "ui_version": DEFAULT_UI_VERSION,
                "target": DEFAULT_TARGET,
                "display": {
                    "type": DEFAULT_DISPLAY_TYPE,
                    "width": CANVAS_WIDTH,
                    "height": CANVAS_HEIGHT,
                    "color": DEFAULT_DISPLAY_COLOR,
                },
                "generator": {
                    "tool": DEFAULT_GENERATOR_TOOL,
                    "version": DEFAULT_GENERATOR_VERSION,
                },
            },
            "resources": {
                "texts": texts,
                "images": images,
                "fonts": fonts,
            },
            "styles": styles,
            "themes": {},
            "data": {},
            "pages": pages,
            "widgets": widgets,
            "events": [],
            "navigation": {"focus": {}, "scene_flow": {}},
            "states": {},
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

    def open_json(self):
        path, _ = QFileDialog.getOpenFileName(self, "Open JSON", "", "JSON (*.json)")
        if not path:
            return
        with open(path, "r", encoding="utf-8") as f:
            raw = json.load(f)
        if "meta" in raw:
            scenes: List[SceneData] = []
            widgets_raw = raw.get("widgets", {})
            texts = raw.get("resources", {}).get("texts", {})

            def resolve_text_from_props(props: Dict[str, object]) -> str:
                text_id = (
                    props.get("textId")
                    or props.get("text")
                    or props.get("TextID")
                    or props.get("Text")
                )
                if not text_id:
                    return ""
                return str(texts.get(text_id, text_id))

            def build_scene_widgets(scene_root: str, widget_map: Dict[str, WidgetData]) -> List[WidgetData]:
                result: List[WidgetData] = []
                visited: set[str] = set()

                def dfs(wid: str):
                    if wid in visited:
                        return
                    visited.add(wid)
                    widget = widget_map.get(wid)
                    if not widget:
                        return
                    if not wid.startswith("root_"):
                        result.append(WidgetData(**asdict(widget)))
                    raw = widgets_raw.get(wid, {}) if isinstance(widgets_raw, dict) else {}
                    children = raw.get("children", []) if isinstance(raw, dict) else []
                    if isinstance(children, list):
                        for child in children:
                            dfs(str(child))

                dfs(scene_root)
                return result

            if isinstance(widgets_raw, dict):
                widget_map: Dict[str, WidgetData] = {}
                for wid, w in widgets_raw.items():
                    rect = w.get("rect", {}) if isinstance(w, dict) else {}
                    props = w.get("properties", {}) if isinstance(w, dict) else {}
                    widget_map[wid] = WidgetData(
                        id=str(wid),
                        type=str(w.get("type", "")) if isinstance(w, dict) else "",
                        x=int(rect.get("x", 0)),
                        y=int(rect.get("y", 0)),
                        w=int(rect.get("w", 0)),
                        h=int(rect.get("h", 0)),
                        text=resolve_text_from_props(props if isinstance(props, dict) else {}),
                        style=str(w.get("style", "")) if isinstance(w, dict) and w.get("style") else "",
                        z_order=0,
                        events={},
                    )

                page_map = raw.get("pages", {})
                if not page_map:
                    page_map = raw.get("scenes", {})
                for page_id, page in page_map.items():
                    page_root = page.get("root", "") if isinstance(page, dict) else ""
                    page_widgets = build_scene_widgets(str(page_root), widget_map) if page_root else []
                    scenes.append(
                        SceneData(id=str(page_id), name=page.get("name", "") if isinstance(page, dict) else "", widgets=page_widgets)
                    )
            else:
                properties = raw.get("properties", [])

                def resolve_text(widget_raw: Dict[str, object]) -> str:
                    start = int(widget_raw.get("prop_start", 0))
                    count = int(widget_raw.get("prop_count", 0))
                    if count <= 0:
                        return ""
                    end = start + count
                    if start < 0 or end > len(properties):
                        return ""
                    for i in range(start, end):
                        prop = properties[i]
                        if prop.get("key") == "TextID":
                            text_id = prop.get("value", "")
                            return str(texts.get(text_id, text_id))
                    return ""

                page_map = raw.get("pages", {})
                if not page_map:
                    page_map = raw.get("scenes", {})
                for scene_id, scene in page_map.items():
                    scene_widgets: List[WidgetData] = []
                    for w in widgets_raw:
                        if w.get("scene") != scene_id:
                            continue
                        rect = w.get("rect", {})
                        scene_widgets.append(
                            WidgetData(
                                id=w.get("id", ""),
                                type=w.get("type", ""),
                                x=int(rect.get("x", 0)),
                                y=int(rect.get("y", 0)),
                                w=int(rect.get("w", 0)),
                                h=int(rect.get("h", 0)),
                                text=resolve_text(w),
                                style=w.get("style", "") or "",
                                z_order=0,
                                events={},
                            )
                        )
                    scenes.append(SceneData(id=str(scene_id), name=scene.get("name", ""), widgets=scene_widgets))

            styles_raw = raw.get("styles", {})
            styles = []
            for style_id, props in styles_raw.items():
                entry = {"id": style_id}
                if isinstance(props, dict):
                    entry.update(props)
                styles.append(entry)

            fonts_raw = raw.get("resources", {}).get("fonts", {})
            fonts = []
            for font_id, props in fonts_raw.items():
                fonts.append(
                    {
                        "id": font_id,
                        "path": str(props.get("file", "")) if isinstance(props, dict) else "",
                        "size": int(props.get("height", 0)) if isinstance(props, dict) else 0,
                    }
                )

            images_raw = raw.get("resources", {}).get("images", {})
            images = []
            for image_id, props in images_raw.items():
                images.append(
                    {
                        "id": image_id,
                        "path": str(props.get("file", "")) if isinstance(props, dict) else "",
                        "w": int(props.get("width", 0)) if isinstance(props, dict) else 0,
                        "h": int(props.get("height", 0)) if isinstance(props, dict) else 0,
                    }
                )

            self.project = ProjectData(
                version=raw.get("meta", {}).get("ui_version", "4.0"),
                scenes=scenes,
                styles=styles,
                fonts=fonts,
                images=images,
                favorites=[],
                control_defaults={},
            )
        else:
            scenes = []
            for scene in raw.get("scenes", []):
                widgets: List[WidgetData] = []
                for w in scene.get("widgets", []):
                    if "pos" in w or "size" in w:
                        pos = w.get("pos", {})
                        size = w.get("size", {})
                        widget = WidgetData(
                            id=w.get("id", ""),
                            type=w.get("type", ""),
                            x=int(pos.get("x", 0)),
                            y=int(pos.get("y", 0)),
                            w=int(size.get("w", 0)),
                            h=int(size.get("h", 0)),
                            text=w.get("text", ""),
                            style=w.get("style", ""),
                            z_order=int(w.get("z_order", 0)),
                            events=dict(w.get("events", {})),
                        )
                    else:
                        widget = WidgetData(**w)
                    widgets.append(widget)
                scenes.append(SceneData(id=scene.get("id", ""), name=scene.get("name", ""), widgets=widgets))

            self.project = ProjectData(
                version=raw.get("version", "3.0"),
                scenes=scenes,
                styles=raw.get("styles", []),
                fonts=raw.get("fonts", []),
                images=raw.get("images", []),
                favorites=raw.get("favorites", []),
                control_defaults=raw.get("control_defaults", {}),
            )
        self.current_scene = self.project.scenes[0] if self.project.scenes else SceneData("scene", "Scene")
        self.scene_list_widget.clear()
        for scene in self.project.scenes:
            item = QListWidgetItem(scene.name)
            item.setData(Qt.UserRole, scene)
            self.scene_list_widget.addItem(item)
        if self.project.scenes:
            self.scene_list_widget.setCurrentRow(0)
            self.scene_name_edit.setText(self.current_scene.name)
        else:
            self.scene_name_edit.setText("")
        self.refresh_scene()
        self.refresh_styles()
        self.refresh_fonts()
        self.refresh_images()
        if hasattr(self, 'refresh_palette_lists'):
            self.refresh_palette_lists()

    def save_json(self):
        self.export_json()

    def new_file(self):
        favs = list(getattr(self.project, "favorites", []))
        defaults = dict(getattr(self.project, "control_defaults", {}))
        self.project = ProjectData(
            scenes=[SceneData(id="page_main", name="MainPage")],
            favorites=favs,
            control_defaults=defaults,
        )
        self.current_scene = self.project.scenes[0]
        if hasattr(self, 'scene_list_widget'):
            self.scene_list_widget.clear()
            item = QListWidgetItem(self.current_scene.name)
            item.setData(Qt.UserRole, self.current_scene)
            self.scene_list_widget.addItem(item)
            self.scene_list_widget.setCurrentRow(0)
        if hasattr(self, 'scene_name_edit'):
            self.scene_name_edit.setText(self.current_scene.name)
        self.refresh_scene()
        if hasattr(self, 'refresh_palette_lists'):
            self.refresh_palette_lists()

    def close_current_file(self):
        reply = QMessageBox.question(self, "Close", "Close current project? Unsaved changes will be lost.", QMessageBox.Yes | QMessageBox.No)
        if reply != QMessageBox.Yes:
            return
        favs = list(getattr(self.project, "favorites", []))
        defaults = dict(getattr(self.project, "control_defaults", {}))
        self.project = ProjectData(favorites=favs, control_defaults=defaults)
        self.current_scene = SceneData(id="page", name="Page")
        if hasattr(self, 'scene_list_widget'):
            self.scene_list_widget.clear()
        if hasattr(self, 'scene_name_edit'):
            self.scene_name_edit.setText("")
        self.refresh_scene()
        self.refresh_styles()
        self.refresh_fonts()
        self.refresh_images()

    def resizeEvent(self, event):
        super().resizeEvent(event)

    def closeEvent(self, event):
        self.save_editor_state()
        super().closeEvent(event)

    def add_scene(self):
        scene = SceneData(id=f"page_{uuid.uuid4().hex[:4]}", name="NewPage")
        self.project.scenes.append(scene)
        item = QListWidgetItem(scene.name)
        item.setData(Qt.UserRole, scene)
        self.scene_list_widget.addItem(item)
        self.scene_list_widget.setCurrentItem(item)
        self.scene_name_edit.setText(scene.name)

    def remove_scene(self):
        row = self.scene_list_widget.currentRow()
        if row < 0:
            return
        self.project.scenes.pop(row)
        self.scene_list_widget.takeItem(row)
        if self.project.scenes:
            self.current_scene = self.project.scenes[0]
            self.scene_list_widget.setCurrentRow(0)
            self.refresh_scene()
            self.scene_name_edit.setText(self.current_scene.name)
        else:
            self.current_scene = SceneData("page", "Page")
            self.scene_name_edit.setText("")

    def switch_scene(self, row: int):
        if row < 0 or row >= len(self.project.scenes):
            return
        self.current_scene = self.project.scenes[row]
        self.refresh_scene()
        self.scene_name_edit.setText(self.current_scene.name)

    def rename_scene(self):
        row = self.scene_list_widget.currentRow()
        if row < 0 or row >= len(self.project.scenes):
            return
        new_name = self.scene_name_edit.text().strip()
        if not new_name:
            return
        self.project.scenes[row].name = new_name
        item = self.scene_list_widget.item(row)
        if item:
            item.setText(new_name)

    def refresh_widget_list(self):
        if not hasattr(self, "widget_list"):
            return
        self.widget_list.blockSignals(True)
        self.widget_list.clear()
        for widget in self.current_scene.widgets:
            self.widget_list.addItem(widget.id)
        self.widget_list.blockSignals(False)

    def select_widget_from_list(self, row: int):
        if self._updating_properties:
            return
        if row < 0 or row >= len(self.current_scene.widgets):
            return
        target = self.current_scene.widgets[row]
        for item in self.scene.items():
            if isinstance(item, WidgetItem):
                item.setSelected(item.data is target)

    def sync_widget_list_selection(self, selected_item: WidgetItem):
        if not hasattr(self, "widget_list"):
            return
        try:
            index = self.current_scene.widgets.index(selected_item.data)
        except ValueError:
            return
        self.widget_list.blockSignals(True)
        self.widget_list.setCurrentRow(index)
        self.widget_list.blockSignals(False)

    def refresh_styles(self):
        self.style_list.clear()
        for style in self.project.styles:
            self.style_list.addItem(style.get("id", ""))

    def load_style(self, row: int):
        if row < 0 or row >= len(self.project.styles):
            return
        style = self.project.styles[row]
        self.style_id.setText(str(style.get("id", "")))
        self.style_bg.setText(str(style.get("bgColor", "")))
        self.style_text.setText(str(style.get("textColor", "")))
        self.style_font.setText(str(style.get("font", "")))
        self.style_radius.setValue(int(style.get("borderRadius", 0)))

    def add_or_update_style(self):
        style_id = self.style_id.text().strip()
        if not style_id:
            return
        style = {
            "id": style_id,
            "bgColor": self.style_bg.text().strip(),
            "textColor": self.style_text.text().strip(),
            "font": self.style_font.text().strip(),
            "borderRadius": self.style_radius.value(),
        }
        for i, existing in enumerate(self.project.styles):
            if existing.get("id") == style_id:
                self.project.styles[i] = style
                self.refresh_styles()
                return
        self.project.styles.append(style)
        self.refresh_styles()

    def remove_style(self):
        row = self.style_list.currentRow()
        if row < 0:
            return
        self.project.styles.pop(row)
        self.refresh_styles()

    def refresh_fonts(self):
        self.font_list.clear()
        for font in self.project.fonts:
            self.font_list.addItem(font.get("id", ""))

    def load_font(self, row: int):
        if row < 0 or row >= len(self.project.fonts):
            return
        font = self.project.fonts[row]
        self.font_id.setText(str(font.get("id", "")))
        self.font_path.setText(str(font.get("path", "")))
        self.font_size.setValue(int(font.get("size", 0)))

    def add_or_update_font(self):
        font_id = self.font_id.text().strip()
        if not font_id:
            return
        font = {
            "id": font_id,
            "path": self.font_path.text().strip(),
            "size": self.font_size.value(),
        }
        for i, existing in enumerate(self.project.fonts):
            if existing.get("id") == font_id:
                self.project.fonts[i] = font
                self.refresh_fonts()
                return
        self.project.fonts.append(font)
        self.refresh_fonts()

    def remove_font(self):
        row = self.font_list.currentRow()
        if row < 0:
            return
        self.project.fonts.pop(row)
        self.refresh_fonts()

    def refresh_images(self):
        self.image_list.clear()
        for image in self.project.images:
            self.image_list.addItem(image.get("id", ""))

    def load_image(self, row: int):
        if row < 0 or row >= len(self.project.images):
            return
        image = self.project.images[row]
        self.image_id.setText(str(image.get("id", "")))
        self.image_path.setText(str(image.get("path", "")))
        self.image_w.setValue(int(image.get("w", 0)))
        self.image_h.setValue(int(image.get("h", 0)))

    def add_or_update_image(self):
        image_id = self.image_id.text().strip()
        if not image_id:
            return
        image = {
            "id": image_id,
            "path": self.image_path.text().strip(),
            "w": self.image_w.value(),
            "h": self.image_h.value(),
        }
        for i, existing in enumerate(self.project.images):
            if existing.get("id") == image_id:
                self.project.images[i] = image
                self.refresh_images()
                return
        self.project.images.append(image)
        self.refresh_images()

    def remove_image(self):
        row = self.image_list.currentRow()
        if row < 0:
            return
        self.project.images.pop(row)
        self.refresh_images()


def main() -> int:
    app = QApplication(sys.argv)
    window = EditorWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
