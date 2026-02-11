#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
from typing import Dict, List, Optional, Tuple

TYPE_MAP = {
    "Label": "Label",
    "Image": "Image",
    "Progress": "Progress",
    "TextArea": "TextArea",
    "Button": "Button",
    "Checkbox": "Checkbox",
    "Switch": "Switch",
    "Radio": "Radio",
    "ListView": "ListView",
    "TabView": "TabView",
    "Container": "Container",
    "Frame": "Frame",
    "Menu": "Menu",
    "Dialog": "Dialog",
    "SoftKeyboard": "SoftKeyboard",
    "TopBar": "TopBar",
    "BottomBar": "BottomBar",
}

TEXT_TYPES = {
    "Label",
    "Image",
    "TextArea",
    "Button",
    "ListView",
    "TabView",
    "Frame",
    "Menu",
    "Dialog",
    "TopBar",
    "BottomBar",
}

CHECKABLE_TYPES = {"Checkbox", "Radio", "Switch"}

DEFAULT_FOCUSABLE = {"Button", "Checkbox", "Radio", "Switch", "TextArea", "ListView"}


def fnv1a(text: str) -> int:
    h = 0x811C9DC5
    for ch in text.encode("utf-8"):
        h ^= ch
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def escape_cpp_string(value: str) -> str:
    out = []
    for ch in value:
        code = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif 0x20 <= code <= 0x7E:
            out.append(ch)
        elif code <= 0xFFFF:
            out.append("\\u%04X" % code)
        else:
            out.append("\\U%08X" % code)
    return "".join(out)


def safe_ident(value: str) -> str:
    out = []
    for ch in value:
        if ch.isalnum():
            out.append(ch)
        else:
            out.append("_")
    if not out or not out[0].isalpha():
        out.insert(0, "w")
    return "".join(out)


def get_bool(obj: Dict, key: str, default: bool) -> bool:
    val = obj.get(key, None)
    if isinstance(val, bool):
        return val
    return default


def get_int(obj: Dict, key: str, default: int) -> int:
    val = obj.get(key, None)
    if isinstance(val, int):
        return val
    return default


def get_widget_z(widget: Dict) -> int:
    if not isinstance(widget, dict):
        return 0
    val = widget.get("z", None)
    if isinstance(val, int):
        return val
    val = widget.get("z_order", None)
    if isinstance(val, int):
        return val
    return 0


def extract_text(widget: Dict, texts: Dict[str, str]) -> Tuple[str, int]:
    direct_text = widget.get("text", "")
    if isinstance(direct_text, str) and direct_text:
        return direct_text, 0

    props = widget.get("properties")
    if not isinstance(props, dict):
        return "", 0

    for key in ("textId", "text", "TextID", "Text"):
        value = props.get(key)
        if isinstance(value, str) and value:
            resolved = texts.get(value, value)
            return resolved, fnv1a(value)

    return "", 0


def walk_widgets(root_id: str, widgets: Dict[str, Dict]) -> List[Tuple[str, str, Dict]]:
    ordered: List[Tuple[str, str, Dict]] = []
    stack = set()

    children_by_parent: Dict[str, List[str]] = {}
    for wid, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        if wid != root_id and isinstance(wid, str) and wid.startswith("root_"):
            continue
        if wid == root_id:
            continue
        parent = widget.get("parent")
        parent_id = parent if isinstance(parent, str) else ""
        if not parent_id or parent_id not in widgets:
            parent_id = root_id
        children_by_parent.setdefault(parent_id, []).append(wid)

    def dfs(widget_id: str, parent_id: str) -> None:
        if widget_id in stack:
            return
        widget = widgets.get(widget_id)
        if not isinstance(widget, dict):
            return
        stack.add(widget_id)
        ordered.append((widget_id, parent_id, widget))
        for child_id in children_by_parent.get(widget_id, []):
            if isinstance(child_id, str):
                dfs(child_id, widget_id)
        stack.remove(widget_id)

    dfs(root_id, "")
    return ordered


def build_widget_desc(scene_id: str,
                      widget_id: str,
                      parent_id: str,
                      widget: Dict,
                      texts: Dict[str, str],
                      out_lines: List[str]) -> Tuple[Optional[str], str]:
    widget_type = widget.get("type", "")
    if widget_type not in TYPE_MAP:
        return None, ""

    rect = widget.get("rect") or {}
    x = int(rect.get("x", 0))
    y = int(rect.get("y", 0))
    w = int(rect.get("w", 0))
    h = int(rect.get("h", 0))

    visible = get_bool(widget, "visible", True)
    enabled = get_bool(widget, "enabled", True)
    focusable = get_bool(widget, "focusable", widget_type in DEFAULT_FOCUSABLE)

    flags = []
    if visible:
        flags.append("app_ui::desc::kWidgetFlagVisible")
    if enabled:
        flags.append("app_ui::desc::kWidgetFlagEnabled")
    if focusable:
        flags.append("app_ui::desc::kWidgetFlagFocusable")
    flags_expr = " | ".join(flags) if flags else "0"

    style = widget.get("style")
    style_id = fnv1a(style) if isinstance(style, str) and style else None

    text, text_id = extract_text(widget, texts)
    specific_name = "nullptr"
    specific_type = None

    name_prefix = safe_ident(scene_id + "_" + widget_id)

    if widget_type in TEXT_TYPES:
        specific_type = "TextDesc"
        specific_name = f"kText_{name_prefix}"
        out_lines.append(f"static const app_ui::desc::TextDesc {specific_name} = {{\n"
                        f"    \"{escape_cpp_string(text)}\",\n"
                        f"    0x{text_id:08X}u\n"
                        f"}};\n")
        specific_name = f"&kText_{name_prefix}"
    elif widget_type in CHECKABLE_TYPES:
        specific_type = "CheckableDesc"
        specific_name = f"kCheckable_{name_prefix}"
        checked = get_bool(widget, "checked", False)
        out_lines.append(f"static const app_ui::desc::CheckableDesc {specific_name} = {{\n"
                        f"    \"{escape_cpp_string(text)}\",\n"
                        f"    0x{text_id:08X}u,\n"
                        f"    {'true' if checked else 'false'}\n"
                        f"}};\n")
        specific_name = f"&kCheckable_{name_prefix}"
    elif widget_type == "Progress":
        specific_type = "ProgressDesc"
        specific_name = f"kProgress_{name_prefix}"
        value = get_int(widget, "value", 0)
        out_lines.append(f"static const app_ui::desc::ProgressDesc {specific_name} = {{\n"
                        f"    \"{escape_cpp_string(text)}\",\n"
                        f"    0x{text_id:08X}u,\n"
                        f"    {value}\n"
                        f"}};\n")
        specific_name = f"&kProgress_{name_prefix}"

    parent_hash = fnv1a(parent_id) if parent_id else 0
    widget_hash = fnv1a(widget_id)
    z_order = get_widget_z(widget)

    style_expr = f"0x{style_id:08X}u" if style_id is not None else "app_ui::kInvalidStyleId"
    desc_line = (
        "{ "
        f"0x{widget_hash:08X}u, "
        f"0x{parent_hash:08X}u, "
        f"app_ui::WidgetType::{TYPE_MAP[widget_type]}, "
        f"{{{x}, {y}, {w}, {h}}}, "
        f"{style_expr}, "
        f"{flags_expr}, "
        f"{z_order}, "
        f"{specific_name} "
        "}"
    )

    return specific_type, desc_line


def emit_scene(namespace: str,
               scene_id: str,
               scene: Dict,
               widgets: Dict[str, Dict],
               texts: Dict[str, str],
               out_lines: List[str]) -> str:
    root_id = scene.get("root")
    if not isinstance(root_id, str) or not root_id:
        return ""

    widget_list = walk_widgets(root_id, widgets)

    out_lines.append(f"// Scene {scene_id}\n")
    widget_desc_lines: List[str] = []
    for widget_id, parent_id, widget in widget_list:
        _, desc_line = build_widget_desc(scene_id, widget_id, parent_id, widget, texts, out_lines)
        if desc_line:
            widget_desc_lines.append(desc_line)

    array_name = f"kScene_{safe_ident(scene_id)}_widgets"
    out_lines.append(f"static const app_ui::desc::WidgetDesc {array_name}[] = {{")
    for line in widget_desc_lines:
        out_lines.append(f"    {line},")
    out_lines.append("};\n")

    root_hash = fnv1a(root_id)
    scene_desc_name = f"kScene_{safe_ident(scene_id)}"
    out_lines.append(
        f"static const app_ui::desc::SceneDesc {scene_desc_name} = {{\n"
        f"    \"{escape_cpp_string(scene_id)}\",\n"
        f"    0x{root_hash:08X}u,\n"
        f"    {array_name},\n"
        f"    sizeof({array_name}) / sizeof({array_name}[0])\n"
        f"}};\n"
    )
    return scene_desc_name


def emit_header(namespace: str, output_path: Path) -> None:
    out_lines: List[str] = []
    out_lines.append("#pragma once\n")
    out_lines.append("#include \"eteacher/app_ui/ui_desc.h\"\n\n")
    out_lines.append(f"namespace app_ui::generated::{namespace} {{\n")
    out_lines.append("extern const app_ui::desc::UiDesc kUi;\n")
    out_lines.append("} // namespace app_ui::generated\n")
    output_path.write_text("\n".join(out_lines), encoding="utf-8")


def emit_source(json_path: Path, header_path: Path, output_path: Path) -> None:
    data = json.loads(json_path.read_text(encoding="utf-8"))

    texts = {}
    resources = data.get("resources")
    if isinstance(resources, dict):
        text_map = resources.get("texts")
        if isinstance(text_map, dict):
            for key, value in text_map.items():
                if isinstance(key, str) and isinstance(value, str):
                    texts[key] = value

    widgets = data.get("widgets")
    if not isinstance(widgets, dict):
        return

    pages = data.get("pages")
    if not isinstance(pages, dict):
        return

    namespace = json_path.stem

    out_lines: List[str] = []
    out_lines.append(f"#include \"{header_path.name}\"\n\n")
    out_lines.append(f"namespace app_ui::generated::{namespace} {{\n")

    scene_desc_names: List[str] = []
    for scene_id, scene in pages.items():
        if isinstance(scene_id, str) and isinstance(scene, dict):
            scene_desc = emit_scene(namespace, scene_id, scene, widgets, texts, out_lines)
            if scene_desc:
                scene_desc_names.append(scene_desc)

    public_scene_name = "nullptr"
    public_section = data.get("public")
    if isinstance(public_section, dict):
        public_page = public_section.get("page")
        public_widgets = public_section.get("widgets")
        if isinstance(public_page, dict) and isinstance(public_widgets, dict):
            public_scene_name = emit_scene("public", "public", public_page, public_widgets, texts, out_lines)

    scenes_array = "kScenes"
    out_lines.append(f"static const app_ui::desc::SceneDesc {scenes_array}[] = {{")
    for name in scene_desc_names:
        out_lines.append(f"    {name},")
    out_lines.append("};\n")

    if public_scene_name == "nullptr":
        out_lines.append("const app_ui::desc::UiDesc kUi = {\n"
                        f"    {scenes_array},\n"
                        f"    sizeof({scenes_array}) / sizeof({scenes_array}[0]),\n"
                        "    nullptr\n"
                        "};\n")
    else:
        out_lines.append("const app_ui::desc::UiDesc kUi = {\n"
                        f"    {scenes_array},\n"
                        f"    sizeof({scenes_array}) / sizeof({scenes_array}[0]),\n"
                        f"    &{public_scene_name}\n"
                        "};\n")

    out_lines.append("} // namespace app_ui::generated\n")

    output_path.write_text("\n".join(out_lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate UI descriptor headers from JSON")
    parser.add_argument("--input", default=str(Path.cwd()))
    parser.add_argument("--output-root", default=str(Path(__file__).resolve().parents[2] / "apps"))
    args = parser.parse_args()

    input_dir = Path(args.input)
    output_root = Path(args.output_root)

    if not input_dir.exists():
        print(f"[ui_gen] input dir not found: {input_dir}")
        return 1

    json_files = sorted(input_dir.glob("*.json"))
    if not json_files:
        print(f"[ui_gen] no json files in: {input_dir}")
        return 0

    for json_path in json_files:
        app_name = json_path.stem
        app_dir = output_root / app_name
        if not app_dir.exists():
            print(f"[ui_gen] app dir missing for {json_path.name}: {app_dir}")
            continue
        header_path = app_dir / f"{app_name}_ui.h"
        source_path = app_dir / f"{app_name}_ui.cc"
        emit_header(app_name, header_path)
        emit_source(json_path, header_path, source_path)
        print(f"[ui_gen] generated: {header_path.name}, {source_path.name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
