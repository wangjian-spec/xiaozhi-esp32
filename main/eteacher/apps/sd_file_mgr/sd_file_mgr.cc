#include "eteacher/apps/sd_file_mgr/sd_file_mgr.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "boards/EnglishTeacher/english-teacher.h"
#include "eteacher/app_manager/menu.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/font_manager/font_manager.h"

#include "display.h"

#include <SdFat.h>
#include <esp_err.h>
#include <esp_log.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>

namespace {

static constexpr const char *kTag = "SdFileMgr";
static constexpr const char *kTitle = "文件管理";
static constexpr const char *kTextFont = "wenquanyi_11pt";
static constexpr const char *kStatusFont = "wenquanyi_9pt";
static constexpr int kColGap = 8;

struct LayoutInfo {
	int padding = 8;
	int line_height = 16;
	int text_ascent = 12;
	int list_top = 0;
	int col_w = 0;
	int rows = 1;
	int items_per_page = 1;
};

int GetFontHeight(std::string_view name) {
	const auto *font = eteacher::font_manager::GetBuiltinFont(name);
	if (!font) {
		return 16;
	}
	return static_cast<int>(font->Header().ascent + font->Header().descent);
}

int GetFontAscent(std::string_view name) {
	const auto *font = eteacher::font_manager::GetBuiltinFont(name);
	if (!font) {
		return 12;
	}
	return static_cast<int>(font->Header().ascent);
}

LayoutInfo ComputeLayout(CustomEpdDisplay *epd, int columns) {
	LayoutInfo info;
	if (!epd) {
		return info;
	}

	const auto style = eteacher::app_menu::MenuStyle{};
	const int text_h = GetFontHeight(kTextFont);
	const int ascent = GetFontAscent(kTextFont);
	const int line_h = text_h + 4;

	info.padding = style.padding;
	info.line_height = line_h;
	info.text_ascent = ascent;
	info.list_top = style.padding + line_h * 2;

	const int available_h = epd->height() - info.list_top - style.bottom_height - style.padding;
	info.rows = std::max(1, available_h / line_h);
	const int total_gap = kColGap * std::max(0, columns - 1);
	info.col_w = (epd->width() - info.padding * 2 - total_gap) / std::max(1, columns);
	info.items_per_page = std::max(1, info.rows * std::max(1, columns));
	return info;
}

std::string EscapeJson(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (unsigned char c : s) {
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

std::string UrlDecode(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		char c = s[i];
		if (c == '%' && i + 2 < s.size()) {
			char h1 = s[i + 1];
			char h2 = s[i + 2];
			auto hex = [](char ch) -> int {
				if (ch >= '0' && ch <= '9') return ch - '0';
				if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
				if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
				return -1;
			};
			int hi = hex(h1);
			int lo = hex(h2);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		if (c == '+') {
			out.push_back(' ');
		} else {
			out.push_back(c);
		}
	}
	return out;
}

bool IsSafePath(const std::string &path) {
	if (path.empty() || path[0] != '/') {
		return false;
	}
	if (path.find("..") != std::string::npos) {
		return false;
	}
	return true;
}

std::string NormalizePath(const std::string &path) {
	if (path.empty()) {
		return "/";
	}
	std::string out;
	out.reserve(path.size());
	bool last_slash = false;
	for (char c : path) {
		if (c == '/') {
			if (!last_slash) {
				out.push_back('/');
				last_slash = true;
			}
		} else {
			out.push_back(c);
			last_slash = false;
		}
	}
	if (out.empty()) {
		out = "/";
	}
	while (out.size() > 1 && out.back() == '/') {
		out.pop_back();
	}
	return out;
}

std::string JoinPath(const std::string &base, const std::string &name) {
	if (base == "/") {
		return "/" + name;
	}
	return base + "/" + name;
}

std::string SanitizeFilename(const std::string &name) {
	if (name.empty()) return "";
	// If name contains non-ASCII bytes, convert to a safe ascii name using hex of the bytes.
	bool has_non_ascii = false;
	for (unsigned char c : name) {
		if (c >= 0x80) { has_non_ascii = true; break; }
	}
	// keep a safe ascii subset for simple names
	auto keep_char = [](unsigned char c) -> bool {
		if (c >= 'a' && c <= 'z') return true;
		if (c >= 'A' && c <= 'Z') return true;
		if (c >= '0' && c <= '9') return true;
		if (c == '-' || c == '_' || c == '.' ) return true;
		return false;
	};

	if (!has_non_ascii) {
		std::string out;
		out.reserve(name.size());
		for (unsigned char c : name) {
			if (c == '/' || c == '\\' || c < 0x20) continue;
			if (keep_char(c)) out.push_back(static_cast<char>(c));
			else out.push_back('_');
			if (out.size() >= 200) break;
		}
		if (out.empty()) return "unnamed";
		return out;
	}

	// Contains non-ascii: derive extension and create hex-based filename to avoid filesystem encoding issues.
	std::string ext;
	auto dot = name.find_last_of('.');
	if (dot != std::string::npos && dot + 1 < name.size()) {
		ext = name.substr(dot); // include dot
	}
	// hex the bytes (but limit length)
	std::ostringstream h;
	for (unsigned char c : name) {
		char buf[4];
		std::snprintf(buf, sizeof(buf), "%02x", c);
		h << buf;
		if (h.tellp() > 64) break; // limit
	}
	std::string hex = h.str();
	std::string out = "u_" + hex;
	if (out.size() > 120) out.resize(120);
	if (!ext.empty()) {
		// ensure extension is ascii-safe
		std::string safe_ext;
		for (unsigned char c : ext) {
			if (keep_char(c)) safe_ext.push_back(static_cast<char>(c));
		}
		if (safe_ext.empty()) safe_ext = ".bin";
		out += safe_ext;
	}
	ESP_LOGI(kTag, "SanitizeFilename: original='%s' -> '%s'", name.c_str(), out.c_str());
	return out;
}

std::string TrimToWidth(CustomEpdDisplay *epd, const std::string &text, int max_width, std::string_view font) {
	if (!epd || epd->MeasureUtf8Width(text, font) <= max_width) {
		return text;
	}
	std::string trimmed = text;
	const std::string prefix = "...";
	while (!trimmed.empty() && epd->MeasureUtf8Width(prefix + trimmed, font) > max_width) {
		trimmed.erase(trimmed.begin());
	}
	return prefix + trimmed;
}

CustomSdFat *GetSd(Board *board) {
	auto *et_board = dynamic_cast<EnglishTeacherBoard *>(board);
	if (!et_board) {
		return nullptr;
	}
	return et_board->GetSd();
}

bool GetQueryParam(httpd_req_t *req, const char *key, std::string *out) {
	if (!req || !key || !out) {
		return false;
	}
	const size_t len = httpd_req_get_url_query_len(req);
	if (len == 0) {
		return false;
	}
	std::string query;
	query.resize(len + 1);
	if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
		return false;
	}
	char buf[256] = {0};
	if (httpd_query_key_value(query.c_str(), key, buf, sizeof(buf)) != ESP_OK) {
		return false;
	}
	*out = UrlDecode(buf);
	return true;
}

static const char *kIndexHtml = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>文件管理</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial;max-width:980px;margin:24px auto;padding:0 16px;color:#1b1f24;background:#fafafa}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
h1{font-size:20px;margin:0}
#path{font-weight:600}
button{margin-right:8px}
.list{background:white;border:1px solid #e5e7eb;border-radius:8px;overflow:hidden}
.row{display:grid;grid-template-columns:40px 1fr 120px 120px;gap:8px;padding:10px 12px;border-bottom:1px solid #f0f2f5;align-items:center}
.row:hover{background:#f6f8fa}
.row.selected{background:#eef6ff}
.row:last-child{border-bottom:none}
.badge{font-size:12px;padding:2px 6px;border-radius:4px;background:#eef2ff;color:#3730a3}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:12px}
input[type=text]{min-width:240px;padding:6px 8px}
#uploadInput{display:none}
.btn{padding:6px 10px;border:1px solid #d0d7de;border-radius:6px;background:#fff;cursor:pointer}
.btn.primary{background:#1f6feb;color:#fff;border-color:#1f6feb}
</style>
</head>
<body>
<header>
<h1>文件管理</h1>
<div>当前路径：<span id="path">/</span></div>
</header>
<div class="toolbar">
<button class="btn" id="upBtn">上级目录</button>
<button class="btn" id="refreshBtn">刷新</button>
<button class="btn" id="mkdirBtn">新建文件夹</button>
<button class="btn" id="deleteBtn">删除</button>
<button class="btn" id="copyBtn">拷贝</button>
<button class="btn primary" id="uploadBtn">上传文件</button>
<input type="file" id="uploadInput" />
</div>
<div class="list" id="list"></div>
<script>
const listEl = document.getElementById('list');
const pathEl = document.getElementById('path');
let currentPath = '/';
let selected = null;

async function fetchList(path){
  const res = await fetch(`/list?path=${encodeURIComponent(path)}`);
  if(!res.ok){alert('读取失败');return;}
  const data = await res.json();
  currentPath = data.path || '/';
  pathEl.textContent = currentPath;
  selected = null;
  renderList(data.items||[]);
}

function renderList(items){
  listEl.innerHTML = '';
  if(items.length===0){
    listEl.innerHTML = '<div class="row"><div></div><div>空目录</div><div></div><div></div></div>';
    return;
  }
  items.forEach((item)=>{
    const row = document.createElement('div');
    row.className = 'row';
    row.onclick = ()=>{ document.querySelectorAll('.row').forEach(r=>r.classList.remove('selected')); row.classList.add('selected'); selected = item; };
    const type = document.createElement('div');
    type.innerHTML = item.dir ? '<span class="badge">DIR</span>' : '<span class="badge">FILE</span>';
    const name = document.createElement('div');
    name.textContent = item.name;
    name.ondblclick = ()=>{ if(item.dir){ enterDir(item.name); } else { downloadFile(item.name); } };
    const size = document.createElement('div');
    size.textContent = item.dir ? '-' : formatSize(item.size||0);
		const action = document.createElement('div');
		if(!item.dir){
			const link = document.createElement('a');
			link.textContent = '下载';
			link.href = `/download?path=${encodeURIComponent(joinPath(currentPath, item.name))}`;
			link.target = '_blank';
			action.appendChild(link);
		}
    row.appendChild(type);row.appendChild(name);row.appendChild(size);row.appendChild(action);
    listEl.appendChild(row);
  });
}

function formatSize(bytes){
  if(bytes<1024) return bytes+' B';
  if(bytes<1024*1024) return (bytes/1024).toFixed(1)+' KB';
  return (bytes/1024/1024).toFixed(1)+' MB';
}

function joinPath(base, name){
  if(base === '/') return '/' + name;
  return base + '/' + name;
}

function enterDir(name){
  fetchList(joinPath(currentPath, name));
}

function upDir(){
  if(currentPath === '/') return;
  const parts = currentPath.split('/').filter(Boolean);
  parts.pop();
  fetchList('/' + parts.join('/'));
}

async function downloadFile(name){
  const path = joinPath(currentPath, name);
  window.open(`/download?path=${encodeURIComponent(path)}`, '_blank');
}

async function mkdir(){
  const name = prompt('文件夹名称');
  if(!name) return;
  const path = joinPath(currentPath, name);
  const res = await fetch(`/mkdir?path=${encodeURIComponent(path)}`, {method:'POST'});
  if(!res.ok){alert('创建失败');return;}
  fetchList(currentPath);
}

async function deleteItem(){
  if(!selected){alert('请选择一个条目');return;}
  if(!confirm(`确认删除 ${selected.name}?`)) return;
  const path = joinPath(currentPath, selected.name);
  const res = await fetch(`/delete?path=${encodeURIComponent(path)}`, {method:'POST'});
  if(!res.ok){alert('删除失败');return;}
  fetchList(currentPath);
}

async function copyItem(){
  if(!selected || selected.dir){alert('请选择文件');return;}
  const dst = prompt('目标文件名(可带路径)', selected.name);
  if(!dst) return;
  const srcPath = joinPath(currentPath, selected.name);
  const dstPath = dst.startsWith('/') ? dst : joinPath(currentPath, dst);
  const res = await fetch(`/copy?src=${encodeURIComponent(srcPath)}&dst=${encodeURIComponent(dstPath)}`, {method:'POST'});
  if(!res.ok){alert('拷贝失败');return;}
  fetchList(currentPath);
}

async function uploadFile(file){
  if(!file) return;
  const path = joinPath(currentPath, file.name);
	const res = await fetch(`/upload?path=${encodeURIComponent(path)}`, {method:'POST', body: file});
	if(!res.ok){
		const msg = await res.text();
		alert('上传失败: ' + (msg || res.status));
		return;
	}
  fetchList(currentPath);
}

// bind

document.getElementById('refreshBtn').onclick = ()=>fetchList(currentPath);
document.getElementById('upBtn').onclick = ()=>upDir();
document.getElementById('mkdirBtn').onclick = ()=>mkdir();
document.getElementById('deleteBtn').onclick = ()=>deleteItem();
document.getElementById('copyBtn').onclick = ()=>copyItem();
document.getElementById('uploadBtn').onclick = ()=>document.getElementById('uploadInput').click();
document.getElementById('uploadInput').onchange = (e)=>uploadFile(e.target.files[0]);

fetchList(currentPath);
</script>
</body>
</html>
)HTML";

} // namespace

SdFileMgrApp::SdFileMgrApp() = default;

MenuMeta SdFileMgrApp::GetMenuMeta() const {
	return MenuMeta{"sd_file_mgr", "文件管理", "WiFi + 文件浏览"};
}

void SdFileMgrApp::OnEnter(AppContext &ctx) {
	active_ = true;
	board_ = &ctx.board;
	view_ = View::kConnecting;
	mode_ = NetMode::kSta;
	mode_selected_ = 1;
	current_path_ = "/";
	entries_.clear();
	status_line_.clear();
	url_.clear();
	ssid_.clear();
	selected_index_ = 0;
	page_offset_ = 0;
	tick_accum_ms_ = 0;
	EnterMode(ctx, NetMode::kSta);
}

void SdFileMgrApp::OnExit(AppContext &ctx) {
	(void)ctx;
	active_ = false;
	StopServer();
}

void SdFileMgrApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
	if (view_ == View::kModeSelect) {
		if (event.id == AppButton::Up || event.id == AppButton::Left) {
			mode_selected_ = (mode_selected_ - 1 + 2) % 2;
			RenderModeSelect(ctx);
			return;
		}
		if (event.id == AppButton::Down || event.id == AppButton::Right) {
			mode_selected_ = (mode_selected_ + 1) % 2;
			RenderModeSelect(ctx);
			return;
		}
		if (event.id == AppButton::Start) {
			EnterMode(ctx, mode_selected_ == 0 ? NetMode::kAp : NetMode::kSta);
			return;
		}
		return;
	}

	if (view_ == View::kConnecting) {
		if (event.id == AppButton::Left) {
			view_ = View::kModeSelect;
			Render(ctx);
			return;
		}
		if (event.id == AppButton::Start) {
			view_ = View::kBrowser;
			LoadEntries(ctx);
			RenderBrowser(ctx);
			return;
		}
		return;
	}

	if (view_ == View::kBrowser) {
		if (event.id == AppButton::Up) {
			MoveSelection(-1, 0);
			RenderBrowser(ctx);
			return;
		}
		if (event.id == AppButton::Down) {
			MoveSelection(1, 0);
			RenderBrowser(ctx);
			return;
		}
		if (event.id == AppButton::Left) {
			MoveSelection(0, -1);
			RenderBrowser(ctx);
			return;
		}
		if (event.id == AppButton::Right) {
			MoveSelection(0, 1);
			RenderBrowser(ctx);
			return;
		}
		if (event.id == AppButton::Start) {
			EnterFolder(ctx);
			RenderBrowser(ctx);
			return;
		}
		if (event.id == AppButton::B) {
			ExitFolder(ctx);
			RenderBrowser(ctx);
			return;
		}
		if (event.id == AppButton::Select) {
			ExitFolder(ctx);
			RenderBrowser(ctx);
			return;
		}
		return;
	}
}

void SdFileMgrApp::OnTick(AppContext &ctx, uint32_t delta_ms) {
	(void)ctx;
	if (!active_) {
		return;
	}
	tick_accum_ms_ += delta_ms;
	if (tick_accum_ms_ < 1000) {
		return;
	}
	tick_accum_ms_ = 0;

	if (view_ == View::kConnecting) {
		UpdateConnectionInfo();
		RenderConnecting(ctx);
	}
}

void SdFileMgrApp::Render(AppContext &ctx) {
	if (view_ == View::kModeSelect) {
		RenderModeSelect(ctx);
		return;
	}
	if (view_ == View::kConnecting) {
		RenderConnecting(ctx);
		return;
	}
	RenderBrowser(ctx);
}

void SdFileMgrApp::RenderModeSelect(AppContext &ctx) {
	auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
	if (!epd) {
		ctx.board.GetDisplay()->SetChatMessage("system", "SD File Manager: EPD unavailable");
		return;
	}

	struct DrawCtx {
		CustomEpdDisplay *epd;
		int selected;
	};

	auto *draw_ctx = new DrawCtx{epd, mode_selected_};

	auto cb = [](Adafruit_GFX &gfx, void *ctx) {
		auto *d = static_cast<DrawCtx *>(ctx);
		if (!d || !d->epd) {
			return;
		}
		gfx.fillScreen(GxEPD_WHITE);
		const auto layout = ComputeLayout(d->epd, 1);
		int y = layout.padding + layout.text_ascent;
		d->epd->DrawUtf8(layout.padding, y, kTitle, kTextFont, GxEPD_BLACK);
		y += layout.line_height;
		d->epd->DrawUtf8(layout.padding, y, "> STA 模式", kTextFont, GxEPD_BLACK);
		y += layout.line_height;
		d->epd->DrawUtf8(layout.padding, y + layout.line_height, "Start 进入文件管理", kStatusFont, GxEPD_BLACK);
	};

	EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, draw_ctx, [](void *ctx) {
		delete static_cast<DrawCtx *>(ctx);
	}, EpdManager::Rect(0, 0, draw_ctx->epd->width(), draw_ctx->epd->height()));
}

void SdFileMgrApp::RenderConnecting(AppContext &ctx) {
	auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
	if (!epd) {
		return;
	}
	struct DrawCtx {
		CustomEpdDisplay *epd;
		std::string mode;
		std::string ssid;
		std::string url;
		std::string status;
	};

	std::string mode_text = "STA 模式";
	auto *draw_ctx = new DrawCtx{epd, mode_text, ssid_, url_, status_line_};

	auto cb = [](Adafruit_GFX &gfx, void *ctx) {
		auto *d = static_cast<DrawCtx *>(ctx);
		if (!d || !d->epd) {
			return;
		}
		gfx.fillScreen(GxEPD_WHITE);
		const auto layout = ComputeLayout(d->epd, 1);
		int y = layout.padding + layout.text_ascent;
		d->epd->DrawUtf8(layout.padding, y, kTitle, kTextFont, GxEPD_BLACK);
		y += layout.line_height;
		d->epd->DrawUtf8(layout.padding, y, "网络: " + d->mode, kTextFont, GxEPD_BLACK);
		y += layout.line_height;
		if (!d->ssid.empty()) {
			d->epd->DrawUtf8(layout.padding, y, "SSID: " + d->ssid, kTextFont, GxEPD_BLACK);
			y += layout.line_height;
		}
		if (!d->url.empty()) {
			d->epd->DrawUtf8(layout.padding, y, "URL: " + d->url, kTextFont, GxEPD_BLACK);
			y += layout.line_height;
		}
		if (!d->status.empty()) {
			d->epd->DrawUtf8(layout.padding, y, d->status, kStatusFont, GxEPD_BLACK);
		}
		d->epd->DrawUtf8(layout.padding, d->epd->height() - 6, "Start 进入文件列表  左键返回", kStatusFont, GxEPD_BLACK);
	};

	EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, draw_ctx, [](void *ctx) {
		delete static_cast<DrawCtx *>(ctx);
	}, EpdManager::Rect(0, 0, draw_ctx->epd->width(), draw_ctx->epd->height()));
}

void SdFileMgrApp::RenderBrowser(AppContext &ctx) {
	auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
	if (!epd) {
		return;
	}

	EnsureSelectionVisible(epd);

	struct DrawCtx {
		CustomEpdDisplay *epd;
		std::string path;
		std::vector<Entry> entries;
		int selected;
		int page_offset;
		int columns;
		std::string status;
	};

	std::string footer = status_line_.empty() ? "Start 进入目录  B 返回上级" : status_line_;
	auto *draw_ctx = new DrawCtx{epd, current_path_, entries_, selected_index_, page_offset_, columns_, footer};

	auto cb = [](Adafruit_GFX &gfx, void *ctx) {
		auto *d = static_cast<DrawCtx *>(ctx);
		if (!d || !d->epd) {
			return;
		}
		gfx.fillScreen(GxEPD_WHITE);
		const auto layout = ComputeLayout(d->epd, d->columns);

		int y = layout.padding + layout.text_ascent;
		d->epd->DrawUtf8(layout.padding, y, kTitle, kTextFont, GxEPD_BLACK);
		y += layout.line_height;
		std::string path = TrimToWidth(d->epd, d->path, d->epd->width() - layout.padding * 2, kTextFont);
		d->epd->DrawUtf8(layout.padding, y, path, kTextFont, GxEPD_BLACK);

		const int start = std::max(0, d->page_offset);
		const int total = static_cast<int>(d->entries.size());
		const int end = std::min(total, start + layout.items_per_page);
		const int list_top = layout.list_top;

		if (total == 0) {
			d->epd->DrawUtf8(layout.padding, list_top + layout.text_ascent, "空目录", kTextFont, GxEPD_BLACK);
		} else {
		for (int i = start; i < end; ++i) {
			const int local = i - start;
			const int row = local / d->columns;
			const int col = local % d->columns;
			const int x = layout.padding + col * (layout.col_w + kColGap);
			const int y0 = list_top + row * layout.line_height;
			const bool selected = (i == d->selected);

			if (selected) {
				gfx.fillRect(x - 2, y0 - 2, layout.col_w + 4, layout.line_height, GxEPD_BLACK);
			}

			const auto &entry = d->entries[static_cast<size_t>(i)];
			std::string label = entry.is_dir ? "[DIR] " : "[FILE] ";
			label += entry.name;
			std::string text = TrimToWidth(d->epd, label, layout.col_w - 4, kTextFont);
			const auto color = selected ? GxEPD_WHITE : GxEPD_BLACK;
			d->epd->DrawUtf8(x, y0 + layout.text_ascent, text, kTextFont, color);
		}
		}

		if (!d->status.empty()) {
			d->epd->DrawUtf8(layout.padding, d->epd->height() - 6, d->status, kStatusFont, GxEPD_BLACK);
		}
	};

	EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, draw_ctx, [](void *ctx) {
		delete static_cast<DrawCtx *>(ctx);
	}, EpdManager::Rect(0, 0, draw_ctx->epd->width(), draw_ctx->epd->height()));
}

void SdFileMgrApp::EnterMode(AppContext &ctx, NetMode mode) {
	mode_ = NetMode::kSta;
	view_ = View::kConnecting;
	url_.clear();
	ssid_.clear();
	status_line_.clear();

	auto &wifi = WifiManager::GetInstance();
	wifi.Initialize();
	wifi.StartStation();
	StartServer();
	UpdateConnectionInfo();
	RenderConnecting(ctx);
}

void SdFileMgrApp::UpdateConnectionInfo() {
	auto &wifi = WifiManager::GetInstance();

	if (wifi.IsConnected()) {
		ssid_ = wifi.GetSsid();
		std::string ip = wifi.GetIpAddress();
		url_ = ip.empty() ? "" : ("http://" + ip + ":" + std::to_string(kHttpPort));
		status_line_ = "已连接，可通过电脑访问";
	} else {
		ssid_ = wifi.GetSsid();
		url_.clear();
		status_line_ = "连接中...";
	}
}

void SdFileMgrApp::StartServer() {
	if (server_) {
		return;
	}
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = kHttpPort;
	config.max_uri_handlers = 12;
	config.lru_purge_enable = true;
	config.recv_wait_timeout = 15;
	config.send_wait_timeout = 15;

	esp_err_t err = httpd_start(&server_, &config);
	if (err != ESP_OK) {
		ESP_LOGW(kTag, "httpd_start failed: %s", esp_err_to_name(err));
		server_ = nullptr;
		return;
	}

	httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = &SdFileMgrApp::HandleIndex, .user_ctx = this};
	httpd_uri_t list = {.uri = "/list", .method = HTTP_GET, .handler = &SdFileMgrApp::HandleList, .user_ctx = this};
	httpd_uri_t download = {.uri = "/download", .method = HTTP_GET, .handler = &SdFileMgrApp::HandleDownload, .user_ctx = this};
	httpd_uri_t upload = {.uri = "/upload", .method = HTTP_POST, .handler = &SdFileMgrApp::HandleUpload, .user_ctx = this};
	httpd_uri_t mkdir = {.uri = "/mkdir", .method = HTTP_POST, .handler = &SdFileMgrApp::HandleMkdir, .user_ctx = this};
	httpd_uri_t del = {.uri = "/delete", .method = HTTP_POST, .handler = &SdFileMgrApp::HandleDelete, .user_ctx = this};
	httpd_uri_t copy = {.uri = "/copy", .method = HTTP_POST, .handler = &SdFileMgrApp::HandleCopy, .user_ctx = this};

	httpd_register_uri_handler(server_, &index);
	httpd_register_uri_handler(server_, &list);
	httpd_register_uri_handler(server_, &download);
	httpd_register_uri_handler(server_, &upload);
	httpd_register_uri_handler(server_, &mkdir);
	httpd_register_uri_handler(server_, &del);
	httpd_register_uri_handler(server_, &copy);

	ESP_LOGI(kTag, "SD file server started on port %d", kHttpPort);
}

void SdFileMgrApp::StopServer() {
	if (!server_) {
		return;
	}
	httpd_stop(server_);
	server_ = nullptr;
}

bool SdFileMgrApp::LoadEntries(AppContext &ctx) {
	std::string err;
	std::vector<Entry> out;
	bool ok = ListEntries(current_path_, &out, &err);
	if (!ok) {
		status_line_ = err;
		entries_.clear();
		ResetSelection();
		return false;
	}
	entries_ = std::move(out);
	status_line_.clear();
	ResetSelection();
	return true;
}

bool SdFileMgrApp::ListEntries(const std::string &path, std::vector<Entry> *out, std::string *err) {
	if (!out || !board_) {
		if (err) *err = "SD 未就绪";
		return false;
	}
	auto *sd = GetSd(board_);
	if (!sd) {
		if (err) *err = "SD 未初始化";
		return false;
	}
	std::lock_guard<std::mutex> lock(sd_mutex_);

	SdFile dir;
	if (!dir.open(path.c_str(), O_RDONLY)) {
		if (err) *err = "打开目录失败";
		return false;
	}

	out->clear();
	SdFile entry;
	while (entry.openNext(&dir, O_RDONLY)) {
		char name[256] = {0};
		if (!entry.getName(name, sizeof(name))) {
			entry.close();
			continue;
		}
		std::string n(name);
		if (n == "." || n == "..") {
			entry.close();
			continue;
		}
		Entry e;
		e.name = std::move(n);
		e.is_dir = entry.isDir();
		e.size = entry.fileSize();
		out->push_back(std::move(e));
		entry.close();
	}
	dir.close();

	std::sort(out->begin(), out->end(), [](const Entry &a, const Entry &b) {
		if (a.is_dir != b.is_dir) {
			return a.is_dir > b.is_dir;
		}
		return a.name < b.name;
	});
	return true;
}

bool SdFileMgrApp::DeletePath(const std::string &path, std::string *err) {
	if (!board_) {
		if (err) *err = "SD 未就绪";
		return false;
	}
	if (path == "/") {
		if (err) *err = "禁止删除根目录";
		return false;
	}
	auto *sd = GetSd(board_);
	if (!sd) {
		if (err) *err = "SD 未初始化";
		return false;
	}
	std::lock_guard<std::mutex> lock(sd_mutex_);

	SdFile target;
	if (!target.open(path.c_str(), O_RDONLY)) {
		if (err) *err = "打开失败";
		return false;
	}
	const bool is_dir = target.isDir();
	target.close();

	if (!is_dir) {
		if (!sd->remove(path.c_str())) {
			if (err) *err = "删除失败";
			return false;
		}
		return true;
	}

	std::function<bool(const std::string &)> delete_dir = [&](const std::string &dir_path) -> bool {
		SdFile dir;
		if (!dir.open(dir_path.c_str(), O_RDONLY)) {
			if (err) *err = "打开目录失败";
			return false;
		}
		SdFile entry;
		while (entry.openNext(&dir, O_RDONLY)) {
			char name[256] = {0};
			if (!entry.getName(name, sizeof(name))) {
				entry.close();
				continue;
			}
			std::string n(name);
			if (n == "." || n == "..") {
				entry.close();
				continue;
			}
			std::string child = JoinPath(dir_path, n);
			const bool child_dir = entry.isDir();
			entry.close();
			if (child_dir) {
				if (!delete_dir(child)) {
					dir.close();
					return false;
				}
			} else {
				if (!sd->remove(child.c_str())) {
					if (err) *err = "删除文件失败";
					dir.close();
					return false;
				}
			}
		}
		dir.close();
		if (!sd->rmdir(dir_path.c_str())) {
			if (err) *err = "删除目录失败";
			return false;
		}
		return true;
	};

	return delete_dir(path);
}

bool SdFileMgrApp::CopyFile(const std::string &src, const std::string &dst, std::string *err) {
	if (!board_) {
		if (err) *err = "SD 未就绪";
		return false;
	}
	auto *sd = GetSd(board_);
	if (!sd) {
		if (err) *err = "SD 未初始化";
		return false;
	}
	std::lock_guard<std::mutex> lock(sd_mutex_);

	SdFile src_file;
	if (!src_file.open(src.c_str(), O_RDONLY)) {
		if (err) *err = "源文件打开失败";
		return false;
	}
	if (src_file.isDir()) {
		src_file.close();
		if (err) *err = "不支持拷贝目录";
		return false;
	}

	SdFile dst_file;
	if (!dst_file.open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) {
		src_file.close();
		if (err) *err = "目标文件打开失败";
		return false;
	}

	uint8_t buf[1024];
	int n = 0;
	while ((n = src_file.read(buf, sizeof(buf))) > 0) {
		int w = dst_file.write(buf, n);
		if (w != n) {
			src_file.close();
			dst_file.close();
			if (err) *err = "写入失败";
			return false;
		}
	}

	src_file.close();
	dst_file.close();
	return true;
}

bool SdFileMgrApp::WriteFileFromRequest(httpd_req_t *req, const std::string &raw_path, std::string *err) {
	if (!req || !board_) {
		if (err) *err = "SD 未就绪";
		return false;
	}

	auto *sd = GetSd(board_);
	if (!sd) {
		if (err) *err = "SD 未初始化";
		return false;
	}

	// ===== 1. 拆分目录和文件名 =====
	std::string dir = "/";
	std::string filename;

	auto pos = raw_path.find_last_of('/');
	if (pos != std::string::npos) {
		dir = (pos == 0) ? "/" : raw_path.substr(0, pos);
		filename = raw_path.substr(pos + 1);
	} else {
		filename = raw_path;
	}

	// ===== 2. 文件名 sanitize（关键！） =====
	std::string safe_name = SanitizeFilename(filename);
	std::string path = (dir == "/") ? ("/" + safe_name) : (dir + "/" + safe_name);

	ESP_LOGI(kTag, "Upload path: '%s' -> '%s'", raw_path.c_str(), path.c_str());

	std::lock_guard<std::mutex> lock(sd_mutex_);

	// ===== 3. 创建目录 =====
	if (!sd->exists(dir.c_str())) {
		// create intermediate directories
		std::string sub;
		if (dir.size() > 1) {
			for (size_t i = 1; i < dir.size(); ++i) {
				sub.push_back(dir[i]);
				if (dir[i] == '/' || i + 1 == dir.size()) {
					std::string to_create = (dir[0] == '/') ? std::string("/") + sub : sub;
					// normalize trailing slash
					if (!to_create.empty() && to_create.back() == '/') to_create.pop_back();
					if (!sd->exists(to_create.c_str())) {
						if (!sd->mkdir(to_create.c_str())) {
							if (err) *err = "创建目录失败";
							ESP_LOGW(kTag, "mkdir failed: %s", to_create.c_str());
							return false;
						}
					}
				}
			}
		} else {
			if (!sd->mkdir(dir.c_str())) {
				if (err) *err = "创建目录失败";
				ESP_LOGW(kTag, "mkdir failed: %s", dir.c_str());
				return false;
			}
		}
	}

	SdFile file;
	if (!file.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) {
		if (err) *err = "打开文件失败";
		ESP_LOGW(kTag, "open failed: %s", path.c_str());
		return false;
	}

	// ===== 4. 写入内容 =====
	int remaining = static_cast<int>(req->content_len);
	uint8_t buf[1024];

	while (remaining > 0) {
		int to_read = std::min(remaining, static_cast<int>(sizeof(buf)));
		int r = httpd_req_recv(req, reinterpret_cast<char *>(buf), to_read);
		if (r <= 0) {
			file.close();
			if (err) *err = "接收失败";
			ESP_LOGW(kTag, "recv failed: %s", path.c_str());
			return false;
		}
		int w = file.write(buf, r);
		if (w != r) {
			file.close();
			if (err) *err = "写入失败";
			ESP_LOGW(kTag, "write failed: %s", path.c_str());
			return false;
		}
		remaining -= r;
	}
	file.close();
	return true;
}

bool SdFileMgrApp::ReadFileToResponse(httpd_req_t *req, const std::string &path, std::string *err) {
	if (!req || !board_) {
		if (err) *err = "SD 未就绪";
		return false;
	}
	auto *sd = GetSd(board_);
	if (!sd) {
		if (err) *err = "SD 未初始化";
		return false;
	}
	std::lock_guard<std::mutex> lock(sd_mutex_);

	SdFile file;
	if (!file.open(path.c_str(), O_RDONLY)) {
		if (err) *err = "打开文件失败";
		return false;
	}
	if (file.isDir()) {
		file.close();
		if (err) *err = "不支持目录下载";
		return false;
	}

	httpd_resp_set_type(req, "application/octet-stream");
	httpd_resp_set_hdr(req, "Connection", "close");

	uint8_t buf[1024];
	int n = 0;
	while ((n = file.read(buf, sizeof(buf))) > 0) {
		if (httpd_resp_send_chunk(req, reinterpret_cast<const char *>(buf), n) != ESP_OK) {
			file.close();
			if (err) *err = "发送失败";
			return false;
		}
	}
	file.close();
	httpd_resp_send_chunk(req, nullptr, 0);
	return true;
}

void SdFileMgrApp::EnterFolder(AppContext &ctx) {
	if (entries_.empty() || selected_index_ < 0 || selected_index_ >= static_cast<int>(entries_.size())) {
		return;
	}
	const auto &entry = entries_[static_cast<size_t>(selected_index_)];
	if (!entry.is_dir) {
		return;
	}
	current_path_ = NormalizePath(JoinPath(current_path_, entry.name));
	LoadEntries(ctx);
}

void SdFileMgrApp::ExitFolder(AppContext &ctx) {
	if (current_path_ == "/") {
		view_ = View::kConnecting;
		RenderConnecting(ctx);
		return;
	}
	std::string path = current_path_;
	if (path.back() == '/') {
		path.pop_back();
	}
	auto pos = path.find_last_of('/');
	if (pos == std::string::npos || pos == 0) {
		current_path_ = "/";
	} else {
		current_path_ = path.substr(0, pos);
	}
	LoadEntries(ctx);
}

void SdFileMgrApp::MoveSelection(int delta_row, int delta_col) {
	if (entries_.empty()) {
		selected_index_ = 0;
		page_offset_ = 0;
		return;
	}
	int index = selected_index_ < 0 ? 0 : selected_index_;
	int col = index % columns_;
	int row = index / columns_;
	col = std::max(0, std::min(columns_ - 1, col + delta_col));
	row = std::max(0, row + delta_row);
	int new_index = row * columns_ + col;
	int max_index = static_cast<int>(entries_.size()) - 1;
	if (new_index > max_index) {
		new_index = max_index;
	}
	selected_index_ = new_index;
}

void SdFileMgrApp::EnsureSelectionVisible(CustomEpdDisplay *epd) {
	if (!epd) {
		return;
	}
	const auto layout = ComputeLayout(epd, columns_);
	const int total = static_cast<int>(entries_.size());
	if (total <= 0) {
		selected_index_ = 0;
		page_offset_ = 0;
		return;
	}
	if (selected_index_ < 0) {
		selected_index_ = 0;
	}
	if (selected_index_ >= total) {
		selected_index_ = total - 1;
	}
	const int page = selected_index_ / layout.items_per_page;
	page_offset_ = page * layout.items_per_page;
}

void SdFileMgrApp::ResetSelection() {
	selected_index_ = 0;
	page_offset_ = 0;
}

SdFileMgrApp *SdFileMgrApp::GetApp(httpd_req_t *req) {
	if (!req) {
		return nullptr;
	}
	return static_cast<SdFileMgrApp *>(req->user_ctx);
}

esp_err_t SdFileMgrApp::HandleIndex(httpd_req_t *req) {
	httpd_resp_set_type(req, "text/html; charset=utf-8");
	httpd_resp_set_hdr(req, "Connection", "close");
	httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

esp_err_t SdFileMgrApp::HandleList(httpd_req_t *req) {
	auto *app = GetApp(req);
	if (!app) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
	}
	std::string path = "/";
	GetQueryParam(req, "path", &path);
	path = NormalizePath(path);
	if (!IsSafePath(path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
	}

	std::vector<Entry> items;
	std::string err;
	if (!app->ListEntries(path, &items, &err)) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err.c_str());
	}

	std::ostringstream oss;
	oss << "{\"path\":\"" << EscapeJson(path) << "\",\"items\":[";
	for (size_t i = 0; i < items.size(); ++i) {
		const auto &it = items[i];
		if (i > 0) {
			oss << ',';
		}
		oss << "{\"name\":\"" << EscapeJson(it.name) << "\",\"dir\":" << (it.is_dir ? "true" : "false")
			<< ",\"size\":" << it.size << "}";
	}
	oss << "]}";
	const auto body = oss.str();

	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Connection", "close");
	httpd_resp_send(req, body.c_str(), body.size());
	return ESP_OK;
}

esp_err_t SdFileMgrApp::HandleDownload(httpd_req_t *req) {
	auto *app = GetApp(req);
	if (!app) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
	}
	std::string path;
	if (!GetQueryParam(req, "path", &path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing path");
	}
	path = NormalizePath(path);

	// 拆目录 + 文件名，清理文件名非法字节
	{
		auto pos = path.find_last_of('/');
		if (pos != std::string::npos) {
			std::string dir = path.substr(0, pos);
			std::string name = path.substr(pos + 1);
			name = SanitizeFilename(name);
			if (dir.empty() || dir == "/") {
				path = "/" + name;
			} else {
				path = dir + "/" + name;
			}
		}
	}

	// 拆目录 + 文件名，清理文件名非法字节
	{
		auto pos = path.find_last_of('/');
		if (pos != std::string::npos) {
			std::string dir = path.substr(0, pos);
			std::string name = path.substr(pos + 1);
			name = SanitizeFilename(name);
			if (dir.empty() || dir == "/") {
				path = "/" + name;
			} else {
				path = dir + "/" + name;
			}
		}
	}

	// 拆目录 + 文件名，清理文件名非法字节
	{
		auto pos = path.find_last_of('/');
		if (pos != std::string::npos) {
			std::string dir = path.substr(0, pos);
			std::string name = path.substr(pos + 1);
			name = SanitizeFilename(name);
			if (dir.empty() || dir == "/") {
				path = "/" + name;
			} else {
				path = dir + "/" + name;
			}
		}
	}

	// 拆目录 + 文件名，清理文件名非法字节
	{
		auto pos = path.find_last_of('/');
		if (pos != std::string::npos) {
			std::string dir = path.substr(0, pos);
			std::string name = path.substr(pos + 1);
			name = SanitizeFilename(name);
			if (dir.empty() || dir == "/") {
				path = "/" + name;
			} else {
				path = dir + "/" + name;
			}
		}
	}
	if (!IsSafePath(path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
	}
	std::string err;
	if (!app->ReadFileToResponse(req, path, &err)) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err.c_str());
	}
	return ESP_OK;
}

esp_err_t SdFileMgrApp::HandleUpload(httpd_req_t *req) {
	auto *app = GetApp(req);
	if (!app) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
	}
	std::string path;
	if (!GetQueryParam(req, "path", &path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing path");
	}
	path = NormalizePath(path);
	if (!IsSafePath(path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
	}
	std::string err;
	if (!app->WriteFileFromRequest(req, path, &err)) {
		ESP_LOGW(kTag, "upload failed: %s (%s)", path.c_str(), err.c_str());
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err.c_str());
	}
	httpd_resp_set_type(req, "application/json");
	httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

esp_err_t SdFileMgrApp::HandleMkdir(httpd_req_t *req) {
	auto *app = GetApp(req);
	if (!app) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
	}
	std::string path;
	if (!GetQueryParam(req, "path", &path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing path");
	}
	path = NormalizePath(path);
	if (!IsSafePath(path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
	}
	if (!app->board_) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd not ready");
	}
	auto *sd = GetSd(app->board_);
	if (!sd) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd not init");
	}
	{
		std::lock_guard<std::mutex> lock(app->sd_mutex_);
		if (!sd->mkdir(path.c_str())) {
			return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mkdir failed");
		}
	}
	httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

esp_err_t SdFileMgrApp::HandleDelete(httpd_req_t *req) {
	auto *app = GetApp(req);
	if (!app) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
	}
	std::string path;
	if (!GetQueryParam(req, "path", &path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing path");
	}
	path = NormalizePath(path);
	if (!IsSafePath(path)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
	}
	std::string err;
	if (!app->DeletePath(path, &err)) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err.c_str());
	}
	httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

esp_err_t SdFileMgrApp::HandleCopy(httpd_req_t *req) {
	auto *app = GetApp(req);
	if (!app) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
	}
	std::string src;
	std::string dst;
	if (!GetQueryParam(req, "src", &src) || !GetQueryParam(req, "dst", &dst)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing param");
	}
	src = NormalizePath(src);
	dst = NormalizePath(dst);
	if (!IsSafePath(src) || !IsSafePath(dst)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
	}
	std::string err;
	if (!app->CopyFile(src, dst, &err)) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err.c_str());
	}
	httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

std::unique_ptr<AppBase> MakeSdFileMgrApp() {
	return std::make_unique<SdFileMgrApp>();
}
