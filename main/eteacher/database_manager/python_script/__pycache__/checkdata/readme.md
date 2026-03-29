# words.db 数据校验 UI

入口脚本：`word_data_checker_ui.py`

默认会读取上一级目录中的 `words.db`：

- `main/eteacher/database_manager/python_script/words.db`

## 启动方式

```powershell
python word_data_checker_ui.py
```

如果当前环境未安装 `PySide6`，脚本会自动尝试安装。

## 已实现校验

### 1. `word` 表

- `word` 为空
- 单词包含非字母字符
- 单词包含多余空格
- 单词包含大写字母
- 词组包含非法字符
- 词组空格数量不合理
- 词组包含大写字母
- 重复单词
- 重复词组
- 单词 `phonetic` 为空
- 词组 `phonetic` 应为空
- `phonetic` 包含非音标字符

### 2. `word_meaning` 表

- `meaning_zh` 为空
- `meaning_zh` 包含非法字符

## 界面说明

- 左侧按“问题原因”分类展示数量
- 右侧展示对应记录详情、原因和修复建议
- 底部日志显示运行状态