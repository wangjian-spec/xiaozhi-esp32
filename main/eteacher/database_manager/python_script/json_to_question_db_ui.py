from __future__ import annotations

import json
import sqlite3
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext


class QuestionDbImporterApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("question.db 题库导入工具")
        self.root.geometry("760x520")

        self.base_dir = Path(__file__).resolve().parent
        self.db_path = self.base_dir / "question.db"

        self.selected_file = tk.StringVar(value="未选择 JSON 文件")
        self.count_text = tk.StringVar(value="当前题目数：-")

        self._build_ui()
        self.ensure_schema()
        self.refresh_count()

    def _build_ui(self) -> None:
        frame = tk.Frame(self.root, padx=14, pady=14)
        frame.pack(fill=tk.BOTH, expand=True)

        title = tk.Label(frame, text="题库 JSON 导入 / 清空 工具", font=("Microsoft YaHei UI", 14, "bold"))
        title.pack(anchor="w")

        db_label = tk.Label(frame, text=f"数据库路径：{self.db_path}")
        db_label.pack(anchor="w", pady=(6, 10))

        file_row = tk.Frame(frame)
        file_row.pack(fill=tk.X)

        tk.Button(file_row, text="选择 JSON 文件", width=16, command=self.pick_json_file).pack(side=tk.LEFT)
        tk.Label(file_row, textvariable=self.selected_file, anchor="w").pack(side=tk.LEFT, padx=(10, 0), fill=tk.X, expand=True)

        action_row = tk.Frame(frame)
        action_row.pack(fill=tk.X, pady=(12, 0))

        tk.Button(action_row, text="导入到 question.db", width=18, command=self.import_selected_file).pack(side=tk.LEFT)
        tk.Button(action_row, text="清空 question_bank", width=18, command=self.clear_questions).pack(side=tk.LEFT, padx=8)
        tk.Button(action_row, text="刷新题目数量", width=14, command=self.refresh_count).pack(side=tk.LEFT)

        info_row = tk.Frame(frame)
        info_row.pack(fill=tk.X, pady=(10, 8))
        tk.Label(info_row, textvariable=self.count_text, fg="#0b6b2f").pack(side=tk.LEFT)

        self.log_text = scrolledtext.ScrolledText(frame, wrap=tk.WORD, height=22)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        self.log("工具已启动。")

    def log(self, message: str) -> None:
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)

    def get_connection(self) -> sqlite3.Connection:
        return sqlite3.connect(self.db_path)

    def ensure_schema(self) -> None:
        sql = """
        CREATE TABLE IF NOT EXISTS question_bank (
            id INTEGER PRIMARY KEY,
            question_type TEXT,
            stage TEXT,
            difficulty INTEGER,
            content_json TEXT,
            answer TEXT
        )
        """
        with self.get_connection() as conn:
            conn.execute(sql)
            conn.commit()

    def pick_json_file(self) -> None:
        picked = filedialog.askopenfilename(
            title="选择题库 JSON 文件",
            filetypes=[("JSON 文件", "*.json"), ("所有文件", "*.*")],
            initialdir=str(self.base_dir),
        )
        if not picked:
            return
        self.selected_file.set(picked)
        self.log(f"已选择文件：{picked}")

    def _parse_questions(self, file_path: Path) -> list[dict]:
        text = file_path.read_text(encoding="utf-8").strip()
        if not text:
            raise ValueError("JSON 文件为空")

        text_no_comments = self._strip_json_line_comments(text)

        try:
            data = json.loads(text_no_comments)
            return self._extract_questions(data)
        except json.JSONDecodeError as full_err:
            merged: list[dict] = []
            block_count = 0

            for block in self._split_top_level_blocks(text_no_comments):
                block_count += 1
                try:
                    data = json.loads(block)
                    merged.extend(self._extract_questions(data))
                    continue
                except json.JSONDecodeError:
                    pass

                for item_text in self._extract_question_item_blocks(block):
                    try:
                        obj = json.loads(item_text)
                    except json.JSONDecodeError:
                        continue
                    if isinstance(obj, dict):
                        merged.append(obj)

            if not merged:
                raise ValueError(
                    f"无法解析 JSON，请检查文件格式。首个错误位置：第 {full_err.lineno} 行，第 {full_err.colno} 列"
                ) from full_err

            self.log(f"检测到 JSON 含格式问题，已容错导入可解析题目。解析块数：{block_count}")
            return merged

    @staticmethod
    def _split_top_level_blocks(text: str) -> list[str]:
        blocks: list[str] = []
        text_len = len(text)
        i = 0

        while i < text_len:
            while i < text_len and text[i].isspace():
                i += 1
            if i >= text_len:
                break

            if text[i] not in "[{":
                i += 1
                continue

            start = i
            start_char = text[i]
            end_char = "]" if start_char == "[" else "}"
            depth = 0
            in_string = False
            escaped = False

            while i < text_len:
                ch = text[i]

                if in_string:
                    if escaped:
                        escaped = False
                    elif ch == "\\":
                        escaped = True
                    elif ch == '"':
                        in_string = False
                    i += 1
                    continue

                if ch == '"':
                    in_string = True
                    i += 1
                    continue

                if ch == start_char:
                    depth += 1
                elif ch == end_char:
                    depth -= 1
                    if depth == 0:
                        i += 1
                        blocks.append(text[start:i])
                        break
                i += 1
            else:
                break

        return blocks

    @staticmethod
    def _strip_json_line_comments(text: str) -> str:
        out_chars: list[str] = []
        in_string = False
        escaped = False
        i = 0
        text_len = len(text)

        while i < text_len:
            ch = text[i]

            if in_string:
                out_chars.append(ch)
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == '"':
                    in_string = False
                i += 1
                continue

            if ch == '"':
                in_string = True
                out_chars.append(ch)
                i += 1
                continue

            if ch == "/" and i + 1 < text_len and text[i + 1] == "/":
                i += 2
                while i < text_len and text[i] not in "\r\n":
                    i += 1
                continue

            out_chars.append(ch)
            i += 1

        return "".join(out_chars)

    @staticmethod
    def _extract_question_item_blocks(block_text: str) -> list[str]:
        results: list[str] = []
        marker = '"questions"'
        search_pos = 0

        while True:
            marker_idx = block_text.find(marker, search_pos)
            if marker_idx == -1:
                break

            bracket_idx = block_text.find("[", marker_idx)
            if bracket_idx == -1:
                break

            end_idx = QuestionDbImporterApp._find_matching_bracket(block_text, bracket_idx)
            if end_idx == -1:
                search_pos = marker_idx + len(marker)
                continue

            inner = block_text[bracket_idx + 1 : end_idx]
            results.extend(QuestionDbImporterApp._split_object_items(inner))
            search_pos = end_idx + 1

        return results

    @staticmethod
    def _find_matching_bracket(text: str, start_idx: int) -> int:
        depth = 0
        in_string = False
        escaped = False

        for i in range(start_idx, len(text)):
            ch = text[i]

            if in_string:
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == '"':
                    in_string = False
                continue

            if ch == '"':
                in_string = True
                continue

            if ch == "[":
                depth += 1
            elif ch == "]":
                depth -= 1
                if depth == 0:
                    return i

        return -1

    @staticmethod
    def _split_object_items(text: str) -> list[str]:
        items: list[str] = []
        i = 0
        text_len = len(text)

        while i < text_len:
            while i < text_len and text[i] != "{":
                i += 1
            if i >= text_len:
                break

            start = i
            depth = 0
            in_string = False
            escaped = False

            while i < text_len:
                ch = text[i]

                if in_string:
                    if escaped:
                        escaped = False
                    elif ch == "\\":
                        escaped = True
                    elif ch == '"':
                        in_string = False
                    i += 1
                    continue

                if ch == '"':
                    in_string = True
                    i += 1
                    continue

                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        items.append(text[start:i])
                        break
                i += 1
            else:
                break

        return items

    @staticmethod
    def _extract_questions(data: object) -> list[dict]:
        if isinstance(data, dict):
            if "questions" in data:
                questions = data.get("questions")
                if not isinstance(questions, list):
                    raise ValueError("questions 字段必须是数组")
                return [q for q in questions if isinstance(q, dict)]
            return [data]
        if isinstance(data, list):
            return [q for q in data if isinstance(q, dict)]
        raise ValueError("JSON 根节点必须是对象或数组")

    @staticmethod
    def _normalize_row(question: dict) -> tuple:
        qid = question.get("id")
        if qid is None:
            raise ValueError("题目缺少 id 字段")

        question_type = str(question.get("question_type", ""))
        stage = str(question.get("stage", ""))

        difficulty_value = question.get("difficulty", 1)
        try:
            difficulty = int(difficulty_value)
        except (TypeError, ValueError):
            difficulty = 1

        content = question.get("content_json", {})
        content_obj: dict | None = None
        if isinstance(content, dict):
            content_obj = dict(content)
        elif isinstance(content, str):
            try:
                parsed = json.loads(content)
            except (TypeError, ValueError, json.JSONDecodeError):
                parsed = None
            if isinstance(parsed, dict):
                content_obj = parsed

        audio_value = question.get("audio", "")
        if not isinstance(audio_value, str):
            audio_value = str(audio_value)
        if not audio_value:
            legacy_audio = question.get("audio_path", "")
            if isinstance(legacy_audio, str):
                audio_value = legacy_audio
            elif legacy_audio is not None:
                audio_value = str(legacy_audio)
        if not audio_value:
            audio_value = f"audio/question_{int(qid)}.ogg"

        if content_obj is not None:
            if "audio" not in content_obj:
                content_obj["audio"] = audio_value
            content_json = json.dumps(content_obj, ensure_ascii=False)
        elif isinstance(content, str):
            content_json = content
        else:
            content_json = json.dumps({"audio": audio_value}, ensure_ascii=False)

        answer = question.get("answer", "")
        if not isinstance(answer, str):
            answer = json.dumps(answer, ensure_ascii=False)

        return (
            int(qid),
            question_type,
            stage,
            difficulty,
            content_json,
            answer,
        )

    def import_selected_file(self) -> None:
        selected = self.selected_file.get()
        if selected == "未选择 JSON 文件":
            messagebox.showwarning("提示", "请先选择 JSON 文件")
            return

        file_path = Path(selected)
        if not file_path.exists():
            messagebox.showerror("错误", "选中的文件不存在")
            return

        try:
            questions = self._parse_questions(file_path)
            rows = [self._normalize_row(q) for q in questions]
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("解析失败", str(exc))
            self.log(f"解析失败：{exc}")
            return

        sql = """
        INSERT OR REPLACE INTO question_bank (
            id, question_type, stage, difficulty,
            content_json, answer
        )
        VALUES (?, ?, ?, ?, ?, ?)
        """

        try:
            with self.get_connection() as conn:
                conn.executemany(sql, rows)
                conn.commit()
            self.log(f"导入完成：{len(rows)} 条（重复 id 会覆盖）")
            messagebox.showinfo("成功", f"成功导入 {len(rows)} 条题目")
            self.refresh_count()
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("导入失败", str(exc))
            self.log(f"导入失败：{exc}")

    def clear_questions(self) -> None:
        ok = messagebox.askyesno("确认", "确定要删除 question_bank 里的全部题目吗？")
        if not ok:
            return

        try:
            with self.get_connection() as conn:
                conn.execute("DELETE FROM question_bank")
                conn.commit()
            self.log("已清空 question_bank 表。")
            messagebox.showinfo("完成", "题目数据已清空")
            self.refresh_count()
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("操作失败", str(exc))
            self.log(f"清空失败：{exc}")

    def refresh_count(self) -> None:
        try:
            with self.get_connection() as conn:
                cursor = conn.execute("SELECT COUNT(*) FROM question_bank")
                count = cursor.fetchone()[0]
            self.count_text.set(f"当前题目数：{count}")
        except Exception as exc:  # noqa: BLE001
            self.count_text.set("当前题目数：读取失败")
            self.log(f"读取数量失败：{exc}")


if __name__ == "__main__":
    app_root = tk.Tk()
    app = QuestionDbImporterApp(app_root)
    app_root.mainloop()
