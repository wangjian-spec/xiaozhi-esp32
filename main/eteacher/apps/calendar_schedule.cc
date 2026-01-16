// Calendar & Todo app for E-Ink display
#include "eteacher/apps/calendar_schedule.h"

#include "display.h"
#include "settings.h"
#include "assets/lang_config.h"

#include "eteacher/epd_manager/epd_manager.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_service/app_service.h"

#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>

#include <wifi_manager.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

static constexpr const char *kTitle = "日历 & TODO";
static constexpr const char *kTodoNamespace = "calendar_schedule";
static constexpr int kMaxTodos = 12;
static constexpr int kHttpPort = 8081;

static bool CanOpenSettings(const char *ns, nvs_open_mode mode)
{
	nvs_handle_t handle = 0;
	esp_err_t err = nvs_open(ns, mode, &handle);
	if (err != ESP_OK)
	{
		ESP_LOGW("CalendarSchedule", "nvs_open failed: %s", esp_err_to_name(err));
		return false;
	}
	if (handle != 0)
	{
		nvs_close(handle);
	}
	return true;
}

static const char kIndexHtml[] = R"HTML(
<!doctype html>
<html lang="zh">
<head>
	<meta charset="utf-8" />
	<meta name="viewport" content="width=device-width,initial-scale=1" />
	<title>日历 & Todo</title>
	<style>
		body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial; margin:16px;}
		h2{margin:0 0 8px 0;}
		form{display:flex; gap:8px; margin:12px 0;}
		input{flex:1; padding:10px; font-size:16px;}
		button{padding:10px 14px; font-size:16px;}
		ul{list-style:none; padding:0;}
		li{padding:6px 0; border-bottom:1px solid #eee;}
		.done{color:#888; text-decoration:line-through;}
		.hint{color:#666; font-size:12px;}
	</style>
</head>
<body>
	<h2>日历 & Todo</h2>
	<div class="hint">通过手机添加事项，设备会自动刷新。</div>
	<form id="f">
		<input id="text" name="text" placeholder="输入待办事项" maxlength="32" />
		<input id="start" name="start" type="time" />
		<input id="end" name="end" type="time" />
		<button type="submit">添加</button>
	</form>
	<ul id="list"></ul>

	<script>
		const list = document.getElementById('list');
		const form = document.getElementById('f');
		const text = document.getElementById('text');
		const start = document.getElementById('start');
		const end = document.getElementById('end');

		async function loadTodos(){
			const res = await fetch('/todos');
			const data = await res.json();
			list.innerHTML = '';
			(data.todos || []).forEach(t => {
				const li = document.createElement('li');
				li.textContent = (t.date ? (t.date + ' ') : '') + t.text;
				if (t.done) li.classList.add('done');
				list.appendChild(li);
			});
		}

		form.addEventListener('submit', async (e) => {
			e.preventDefault();
			const val = text.value.trim();
			if (!val) return;
			const body = 'text=' + encodeURIComponent(val)
				+ '&start=' + encodeURIComponent(start.value || '')
				+ '&end=' + encodeURIComponent(end.value || '');
			await fetch('/add', {
				method: 'POST',
				headers: {'Content-Type':'application/x-www-form-urlencoded'},
				body
			});
			text.value = '';
			await loadTodos();
		});

		loadTodos();
	</script>
</body>
</html>
)HTML";

struct DateInfo {
	int year = 0;
	int month = 0;
	int day = 0;
};

struct TodoItem {
	std::string text;
	bool done = false;
	int year = 0;
	int month = 0;
	int day = 0;
	int start_min = 0; // minutes since 00:00
	int end_min = 0;   // minutes since 00:00
};

static int ClampMinute(int value)
{
	if (value < 0)
		return 0;
	if (value > 24 * 60)
		return 24 * 60;
	return value;
}

static std::string FormatMinute(int minutes)
{
	minutes = ClampMinute(minutes);
	int h = minutes / 60;
	int m = minutes % 60;
	std::ostringstream oss;
	oss << std::setw(2) << std::setfill('0') << h
		<< ":" << std::setw(2) << std::setfill('0') << m;
	return oss.str();
}

static std::string FormatCountdown(int seconds)
{
	if (seconds < 0)
	{
		seconds = 0;
	}
	int h = seconds / 3600;
	int m = (seconds % 3600) / 60;
	int s = seconds % 60;
	std::ostringstream oss;
	oss << std::setw(2) << std::setfill('0') << h
		<< ":" << std::setw(2) << std::setfill('0') << m
		<< ":" << std::setw(2) << std::setfill('0') << s;
	return oss.str();
}

static std::string JsonEscape(const std::string &s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s)
	{
		switch (c)
		{
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
			{
				char buf[7];
				snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
				out += buf;
			}
			else
			{
				out += c;
			}
			break;
		}
	}
	return out;
}

static std::string UrlDecode(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '+')
		{
			out.push_back(' ');
		}
		else if (s[i] == '%' && i + 2 < s.size())
		{
			char hex[3] = {s[i + 1], s[i + 2], 0};
			char *end = nullptr;
			long v = strtol(hex, &end, 16);
			if (end && *end == 0)
			{
				out.push_back(static_cast<char>(v));
				i += 2;
			}
			else
			{
				out.push_back(s[i]);
			}
		}
		else
		{
			out.push_back(s[i]);
		}
	}
	return out;
}

static DateInfo GetLocalDate()
{
	time_t now = time(nullptr);
	struct tm tm_now;
	localtime_r(&now, &tm_now);
	return DateInfo{tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday};
}

static bool IsLeapYear(int year)
{
	return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int DaysInMonth(int year, int month)
{
	static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month == 2 && IsLeapYear(year))
	{
		return 29;
	}
	return kDays[month - 1];
}

static void AddMonths(int base_year, int base_month, int offset, int &out_year, int &out_month)
{
	int total = base_year * 12 + (base_month - 1) + offset;
	if (total >= 0)
	{
		out_year = total / 12;
		out_month = (total % 12) + 1;
		return;
	}
	// Handle negative months
	int year = (total - 11) / 12;
	int month = total - year * 12;
	out_year = year;
	out_month = month + 1;
}

// Monday-first weekday index (Mon=0..Sun=6)
static int FirstWeekdayMonday(int year, int month)
{
	struct tm t = {};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = 1;
	mktime(&t);
	int wday = t.tm_wday; // Sun=0..Sat=6
	return (wday + 6) % 7;
}

static std::array<int, 42> BuildMonthGrid(int year, int month)
{
	std::array<int, 42> grid{};
	grid.fill(0);
	int first = FirstWeekdayMonday(year, month);
	int days = DaysInMonth(year, month);
	for (int d = 1; d <= days; ++d)
	{
		int idx = first + (d - 1);
		if (idx >= 0 && idx < static_cast<int>(grid.size()))
		{
			grid[idx] = d;
		}
	}
	return grid;
}

static int EncodeDate(int year, int month, int day)
{
	return year * 10000 + month * 100 + day;
}

static DateInfo DecodeDate(int encoded)
{
	DateInfo info;
	info.year = encoded / 10000;
	info.month = (encoded / 100) % 100;
	info.day = encoded % 100;
	return info;
}

struct CalendarDrawCtx {
	CustomEpdDisplay *epd = nullptr;
	int width = 0;
	int height = 0;
	int year = 0;
	int month = 0;
	DateInfo today;
	DateInfo selected;
	bool focus_calendar = true;
	bool edit_mode = false;
	int selected_todo = 0;
	std::string url;
	std::string countdown;
	std::vector<TodoItem> todos;
	std::array<int, 42> grid;
};

static void DeleteCalendarDrawCtx(void *ctx)
{
	delete static_cast<CalendarDrawCtx *>(ctx);
}

static void DrawCalendarCb(Adafruit_GFX &gfx, void *ctx)
{
	auto *d = static_cast<CalendarDrawCtx *>(ctx);
	if (!d || !d->epd)
	{
		return;
	}

	gfx.fillScreen(GxEPD_WHITE);

	const int16_t margin = 8;
	const int16_t calendar_w = 220;
	const int16_t calendar_x = margin;

	// Title
	d->epd->DrawUtf8(margin, 20, kTitle, "wenquanyi_11pt", GxEPD_BLACK);

	if (!d->url.empty())
	{
		std::string url_line = "访问: " + d->url;
		d->epd->DrawUtf8(margin, d->height - 10, url_line, "wenquanyi_9pt", GxEPD_BLACK);
	}

	// Month title
	std::string month_label = std::to_string(d->year) + "-" + (d->month < 10 ? "0" : "") + std::to_string(d->month);
	std::string month_row = d->focus_calendar ? ("> " + month_label) : ("  " + month_label);
	d->epd->DrawUtf8(calendar_x, 36, month_row, "wenquanyi_11pt", GxEPD_BLACK);

	// Weekday header
	d->epd->DrawUtf8(calendar_x, 54, "Mo Tu We Th Fr Sa Su", "wenquanyi_9pt", GxEPD_BLACK);

	const int16_t grid_x = calendar_x;
	const int16_t grid_y = 70;
	const int16_t cell_w = calendar_w / 7;
	const int16_t cell_h = 18;

	for (int i = 0; i < 42; ++i)
	{
		int day = d->grid[i];
		if (day == 0)
		{
			continue;
		}

		int row = i / 7;
		int col = i % 7;
		int16_t x = grid_x + col * cell_w;
		int16_t baseline_y = grid_y + row * cell_h;
		std::string day_str = (day < 10 ? " " : "") + std::to_string(day);
		const bool is_selected = (d->year == d->selected.year && d->month == d->selected.month && day == d->selected.day);
		d->epd->DrawUtf8(x, baseline_y, day_str, "wenquanyi_9pt", GxEPD_BLACK);

		if (is_selected)
		{
			gfx.drawRect(x - 3, baseline_y - 13, cell_w - 2, cell_h, GxEPD_BLACK);
			gfx.drawRect(x - 4, baseline_y - 14, cell_w, cell_h + 2, GxEPD_BLACK);
		}

		if (d->year == d->today.year && d->month == d->today.month && day == d->today.day)
		{
			gfx.drawRect(x - 2, baseline_y - 12, cell_w - 4, cell_h - 2, GxEPD_BLACK);
		}
	}

	// Todo section
	const int16_t todo_x = calendar_x + calendar_w + 8;
	int16_t todo_y = 20;
		std::string todo_title;
		if (d->focus_calendar)
		{
			todo_title = "  Todo";
		}
		else
		{
			todo_title = d->edit_mode ? "> Todo(编辑)" : "> Todo";
		}
	d->epd->DrawUtf8(todo_x, todo_y, todo_title, "wenquanyi_11pt", GxEPD_BLACK);
	todo_y += 20;

		if (!d->countdown.empty())
		{
			std::string line = "倒计时: " + d->countdown;
			d->epd->DrawUtf8(todo_x, todo_y, line, "wenquanyi_9pt", GxEPD_BLACK);
			todo_y += 18;
		}

	int max_rows = (d->height - todo_y - 12) / 18;
	if (max_rows < 1)
	{
		max_rows = 1;
	}

		for (int i = 0; i < static_cast<int>(d->todos.size()) && i < max_rows; ++i)
	{
		const auto &item = d->todos[i];
		std::string line;
		if (!d->focus_calendar && i == d->selected_todo)
		{
			line += ">";
		}
		else
		{
			line += " ";
		}
		line += item.done ? "[x] " : "[ ] ";

		std::string label = item.text;
		if (label.size() > 16)
		{
			label.resize(15);
			label += "…";
		}
			line += FormatMinute(item.start_min) + "-" + FormatMinute(item.end_min) + " ";
			line += label;

		d->epd->DrawUtf8(todo_x, todo_y, line, "wenquanyi_9pt", GxEPD_BLACK);
		todo_y += 18;
	}

		if (d->todos.empty())
	{
		d->epd->DrawUtf8(todo_x, todo_y, "(空)  B 新建", "wenquanyi_9pt", GxEPD_BLACK);
	}
}

} // namespace

class CalendarScheduleApp : public AppBase {
public:
	MenuMeta GetMenuMeta() const override
	{
		return MenuMeta{"calendar_schedule", "Calendar & Todo", "方向键/ABCD/Start"};
	}

	void OnEnter(AppContext &ctx) override
	{
		board_ = &ctx.board;
		active_ = true;
		auto today = GetLocalDate();
		selected_date_ = today;
		LoadTodos();
		EnsureSampleTodos();
		EnsureWifiMode();
		StartServer();
		Render(ctx);
	}

	void OnExit(AppContext &ctx) override
	{
		active_ = false;
		StopServer();
		SaveTodos();
		ctx.board.GetDisplay()->SetChatMessage("system", "");
	}

	void OnButton(AppContext &ctx, const ButtonEvent &event) override
	{
		switch (event.id)
		{
		case AppButton::Up:
			if (edit_mode_)
			{
				AdjustSelectedTodoStart(-15);
				SaveTodos();
			}
			else
			{
				focus_ = Focus::Calendar;
				MoveSelectedDay(-7);
			}
			Render(ctx);
			break;
		case AppButton::Down:
			if (edit_mode_)
			{
				AdjustSelectedTodoStart(15);
				SaveTodos();
			}
			else
			{
				focus_ = Focus::Calendar;
				MoveSelectedDay(7);
			}
			Render(ctx);
			break;
		case AppButton::Left:
			if (edit_mode_)
			{
				AdjustSelectedTodoEnd(-15);
				SaveTodos();
			}
			else
			{
				focus_ = Focus::Calendar;
				MoveSelectedDay(-1);
			}
			Render(ctx);
			break;
		case AppButton::Right:
			if (edit_mode_)
			{
				AdjustSelectedTodoEnd(15);
				SaveTodos();
			}
			else
			{
				focus_ = Focus::Calendar;
				MoveSelectedDay(1);
			}
			Render(ctx);
			break;
		case AppButton::A:
			focus_ = Focus::Todo;
			edit_mode_ = false;
			MoveSelection(-1);
			Render(ctx);
			break;
		case AppButton::C:
			focus_ = Focus::Todo;
			edit_mode_ = false;
			MoveSelection(1);
			Render(ctx);
			break;
		case AppButton::B:
			focus_ = Focus::Todo;
			edit_mode_ = false;
			AddQuickTodo();
			SaveTodos();
			Render(ctx);
			break;
		case AppButton::D:
			focus_ = Focus::Todo;
			edit_mode_ = false;
			DeleteSelectedTodo();
			SaveTodos();
			Render(ctx);
			break;
		case AppButton::Start:
			if (event.action == ButtonAction::LongPress)
			{
				if (focus_ == Focus::Todo)
				{
					ToggleTodoDone();
					SaveTodos();
					Render(ctx);
				}
				break;
			}
			if (focus_ != Focus::Todo)
			{
				focus_ = Focus::Todo;
				edit_mode_ = false;
				Render(ctx);
				break;
			}
			if (HasSelectedTodo())
			{
				edit_mode_ = !edit_mode_;
				Render(ctx);
			}
			break;
		default:
			break;
		}
	}

private:
	enum class Focus {
		Calendar,
		Todo,
	};

	static esp_err_t HandleIndex(httpd_req_t *req)
	{
		httpd_resp_set_type(req, "text/html; charset=utf-8");
		httpd_resp_set_hdr(req, "Connection", "close");
		httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
		return ESP_OK;
	}

	static esp_err_t HandleTodos(httpd_req_t *req)
	{
		auto *app = static_cast<CalendarScheduleApp *>(req->user_ctx);
		if (!app)
		{
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
			return ESP_FAIL;
		}
		auto selected = app->GetSelectedDate();
		auto todos = app->GetTodosForDate(selected);
		std::string body = "{\"todos\":[";
		for (size_t i = 0; i < todos.size(); ++i)
		{
			const auto &t = todos[i];
			std::string date;
			if (t.year > 0)
			{
				std::ostringstream ds;
				ds << std::setw(4) << std::setfill('0') << t.year
				   << "-" << std::setw(2) << std::setfill('0') << t.month
				   << "-" << std::setw(2) << std::setfill('0') << t.day;
				date = ds.str();
			}
			if (i > 0)
				body += ",";
			body += "{\"text\":\"" + JsonEscape(t.text) + "\",\"done\":" + (t.done ? "true" : "false");
			body += ",\"start\":\"" + FormatMinute(t.start_min) + "\"";
			body += ",\"end\":\"" + FormatMinute(t.end_min) + "\"";
			if (!date.empty())
			{
				body += ",\"date\":\"" + date + "\"";
			}
			body += "}";
		}
		std::ostringstream ds;
		ds << std::setw(4) << std::setfill('0') << selected.year
		   << "-" << std::setw(2) << std::setfill('0') << selected.month
		   << "-" << std::setw(2) << std::setfill('0') << selected.day;
		body += "],\"date\":\"" + ds.str() + "\"}";

		httpd_resp_set_type(req, "application/json");
		httpd_resp_set_hdr(req, "Connection", "close");
		httpd_resp_send(req, body.c_str(), body.size());
		return ESP_OK;
	}

	static esp_err_t HandleAdd(httpd_req_t *req)
	{
		auto *app = static_cast<CalendarScheduleApp *>(req->user_ctx);
		if (!app)
		{
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
			return ESP_FAIL;
		}

		if (req->content_len <= 0)
		{
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
			return ESP_FAIL;
		}

		std::string body;
		body.resize(req->content_len);
		int received = 0;
		while (received < req->content_len)
		{
			int r = httpd_req_recv(req, &body[received], req->content_len - received);
			if (r <= 0)
			{
				httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
				return ESP_FAIL;
			}
			received += r;
		}

		std::string text;
		std::string start_str;
		std::string end_str;
		auto pos = body.find("text=");
		if (pos != std::string::npos)
		{
			text = body.substr(pos + 5);
			auto amp = text.find('&');
			if (amp != std::string::npos)
			{
				text = text.substr(0, amp);
			}
			text = UrlDecode(text);
		}

		pos = body.find("start=");
		if (pos != std::string::npos)
		{
			start_str = body.substr(pos + 6);
			auto amp = start_str.find('&');
			if (amp != std::string::npos)
			{
				start_str = start_str.substr(0, amp);
			}
			start_str = UrlDecode(start_str);
		}

		pos = body.find("end=");
		if (pos != std::string::npos)
		{
			end_str = body.substr(pos + 4);
			auto amp = end_str.find('&');
			if (amp != std::string::npos)
			{
				end_str = end_str.substr(0, amp);
			}
			end_str = UrlDecode(end_str);
		}

		if (text.empty())
		{
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing text");
			return ESP_FAIL;
		}

		app->AddTodoFromHttp(text, start_str, end_str);

		httpd_resp_set_type(req, "application/json");
		httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
		return ESP_OK;
	}

	void Render(AppContext &ctx)
	{
		RenderBoard(ctx.board);
	}

	void RenderBoard(Board &board)
	{
		auto *display = board.GetDisplay();
		if (auto *epd = dynamic_cast<CustomEpdDisplay *>(display))
		{
			RenderEpd(epd);
			return;
		}
		RenderText(display);
	}

	static int ParseTimeToMinutes(const std::string &value)
	{
		if (value.size() < 4)
		{
			return 0;
		}
		int h = 0;
		int m = 0;
		if (sscanf(value.c_str(), "%d:%d", &h, &m) != 2)
		{
			return 0;
		}
		return ClampMinute(h * 60 + m);
	}

	std::string GetCountdownString(const DateInfo &today, const DateInfo &selected) const
	{
		if (today.year != selected.year || today.month != selected.month || today.day != selected.day)
		{
			return "--:--:--";
		}
		time_t now = time(nullptr);
		struct tm tm_now;
		localtime_r(&now, &tm_now);
		int now_min = tm_now.tm_hour * 60 + tm_now.tm_min;
		int now_sec = tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec;
		int best_end = -1;
		{
			std::lock_guard<std::mutex> lock(todo_mutex_);
			for (const auto &t : todos_)
			{
				if (t.done)
					continue;
				if (t.year != selected.year || t.month != selected.month || t.day != selected.day)
					continue;
				int end_min = ClampMinute(t.end_min);
				if (end_min < now_min)
					continue;
				if (best_end < 0 || end_min < best_end)
				{
					best_end = end_min;
				}
			}
		}
		if (best_end < 0)
		{
			return "--:--:--";
		}
		int target_sec = best_end * 60;
		return FormatCountdown(target_sec - now_sec);
	}

	void RenderEpd(CustomEpdDisplay *epd)
	{
		auto today = GetLocalDate();
		int year = selected_date_.year;
		int month = selected_date_.month;

		auto *ctx = new CalendarDrawCtx();
		ctx->epd = epd;
		ctx->width = epd->width();
		ctx->height = epd->height();
		ctx->year = year;
		ctx->month = month;
		ctx->today = today;
		ctx->selected = selected_date_;
		ctx->focus_calendar = (focus_ == Focus::Calendar);
		ctx->edit_mode = edit_mode_;
		ctx->selected_todo = selected_todo_;
		ctx->todos = GetTodosForDate(selected_date_);
		ctx->countdown = GetCountdownString(today, selected_date_);
		ctx->url = url_;
		ctx->grid = BuildMonthGrid(year, month);

		EpdManager::GetInstance().Schedule(
			EpdManager::TaskType::kPartial,
			&DrawCalendarCb,
			ctx,
			&DeleteCalendarDrawCtx,
			EpdManager::Rect(0, 0, epd->width(), epd->height()));
	}

	void RenderText(Display *display)
	{
		int year = selected_date_.year;
		int month = selected_date_.month;
		auto today = GetLocalDate();

		std::ostringstream oss;
		oss << "Calendar & Todo\n";
		oss << year << "-" << std::setw(2) << std::setfill('0') << month << "\n";
		oss << "Selected: " << std::setw(4) << std::setfill('0') << selected_date_.year
			<< "-" << std::setw(2) << std::setfill('0') << selected_date_.month
			<< "-" << std::setw(2) << std::setfill('0') << selected_date_.day << "\n";

		std::string countdown = GetCountdownString(today, selected_date_);
		if (!countdown.empty())
		{
			oss << "倒计时: " << countdown << "\n";
		}
		if (!url_.empty())
		{
			oss << "访问: " << url_ << "\n";
		}
		oss << "Mo Tu We Th Fr Sa Su\n";

		auto grid = BuildMonthGrid(year, month);
		for (int r = 0; r < 6; ++r)
		{
			for (int c = 0; c < 7; ++c)
			{
				int day = grid[r * 7 + c];
				if (day == 0)
				{
					oss << "   ";
				}
				else
				{
					oss << std::setw(2) << day << " ";
				}
			}
			oss << "\n";
		}

		oss << "Todo:\n";
		if (focus_ == Focus::Todo && edit_mode_)
		{
			oss << "  [编辑模式] 上下改开始时间，左右改结束时间\n";
		}
		auto todos = GetTodosForDate(selected_date_);
		if (todos.empty())
		{
			oss << "  (空) 按 B 新建\n";
		}
		else
		{
			for (size_t i = 0; i < todos.size(); ++i)
			{
				oss << ((focus_ == Focus::Todo && static_cast<int>(i) == selected_todo_) ? ">" : " ");
				oss << (todos[i].done ? "[x] " : "[ ] ")
					<< FormatMinute(todos[i].start_min) << "-" << FormatMinute(todos[i].end_min)
					<< " " << todos[i].text << "\n";
			}
		}

		display->SetChatMessage("system", oss.str().c_str());
	}

	void LoadTodos()
	{
		if (!CanOpenSettings(kTodoNamespace, NVS_READONLY))
		{
			return;
		}
		Settings settings(kTodoNamespace, false);
		int count = settings.GetInt("todo_count", 0);
		count = std::min(count, kMaxTodos);
		std::lock_guard<std::mutex> lock(todo_mutex_);
		todos_.clear();
		todos_.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			TodoItem item;
			item.text = settings.GetString("todo_" + std::to_string(i) + "_text");
			item.done = settings.GetBool("todo_" + std::to_string(i) + "_done", false);
			item.start_min = settings.GetInt("todo_" + std::to_string(i) + "_start", 0);
			item.end_min = settings.GetInt("todo_" + std::to_string(i) + "_end", 0);
			int encoded = settings.GetInt("todo_" + std::to_string(i) + "_date", 0);
			if (encoded > 0)
			{
				auto date = DecodeDate(encoded);
				item.year = date.year;
				item.month = date.month;
				item.day = date.day;
			}
			todos_.push_back(std::move(item));
		}
		selected_todo_ = 0;
	}

	void SaveTodos()
	{
		if (!CanOpenSettings(kTodoNamespace, NVS_READWRITE))
		{
			return;
		}
		Settings settings(kTodoNamespace, true);
		settings.EraseAll();
		int count = 0;
		std::vector<TodoItem> snapshot;
		{
			std::lock_guard<std::mutex> lock(todo_mutex_);
			count = std::min(static_cast<int>(todos_.size()), kMaxTodos);
			snapshot.assign(todos_.begin(), todos_.begin() + count);
		}
		settings.SetInt("todo_count", count);
		for (int i = 0; i < count; ++i)
		{
			settings.SetString("todo_" + std::to_string(i) + "_text", snapshot[i].text);
			settings.SetBool("todo_" + std::to_string(i) + "_done", snapshot[i].done);
			settings.SetInt("todo_" + std::to_string(i) + "_start", snapshot[i].start_min);
			settings.SetInt("todo_" + std::to_string(i) + "_end", snapshot[i].end_min);
			if (snapshot[i].year > 0)
			{
				settings.SetInt("todo_" + std::to_string(i) + "_date", EncodeDate(snapshot[i].year, snapshot[i].month, snapshot[i].day));
			}
		}
	}

	void EnsureSampleTodos()
	{
		if (!GetTodosSnapshot().empty())
		{
			return;
		}
		auto today = GetLocalDate();
		{
			std::lock_guard<std::mutex> lock(todo_mutex_);
			todos_.push_back(TodoItem{"复习英语单词", false, today.year, today.month, today.day, 8 * 60, 9 * 60});
			todos_.push_back(TodoItem{"阅读 20 分钟", false, today.year, today.month, today.day, 19 * 60, 19 * 60 + 20});
			todos_.push_back(TodoItem{"整理书包", false, today.year, today.month, today.day, 20 * 60, 20 * 60 + 15});
		}
	}

	void MoveSelection(int delta)
	{
		auto indices = GetTodoIndicesForDate(selected_date_);
		if (indices.empty())
		{
			selected_todo_ = 0;
			return;
		}
		int count = static_cast<int>(indices.size());
		selected_todo_ = (selected_todo_ + delta + count) % count;
	}

	void MoveSelectedDay(int delta)
	{
		struct tm tm_date = {};
		tm_date.tm_year = selected_date_.year - 1900;
		tm_date.tm_mon = selected_date_.month - 1;
		tm_date.tm_mday = selected_date_.day + delta;
		mktime(&tm_date);
		selected_date_.year = tm_date.tm_year + 1900;
		selected_date_.month = tm_date.tm_mon + 1;
		selected_date_.day = tm_date.tm_mday;
		selected_todo_ = 0;
	}

	bool HasSelectedTodo() const
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		auto indices = GetTodoIndicesForDateLocked(selected_date_);
		return !indices.empty() && selected_todo_ >= 0 && selected_todo_ < static_cast<int>(indices.size());
	}

	void DeleteSelectedTodo()
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		auto indices = GetTodoIndicesForDateLocked(selected_date_);
		if (indices.empty() || selected_todo_ < 0 || selected_todo_ >= static_cast<int>(indices.size()))
		{
			return;
		}
		int idx = indices[static_cast<size_t>(selected_todo_)];
		if (idx < 0 || idx >= static_cast<int>(todos_.size()))
		{
			return;
		}
		todos_.erase(todos_.begin() + idx);
		auto after = GetTodoIndicesForDateLocked(selected_date_);
		if (after.empty())
		{
			selected_todo_ = 0;
		}
		else if (selected_todo_ >= static_cast<int>(after.size()))
		{
			selected_todo_ = static_cast<int>(after.size()) - 1;
		}
		AppService::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
	}

	void AdjustSelectedTodoStart(int delta_min)
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		auto indices = GetTodoIndicesForDateLocked(selected_date_);
		if (indices.empty() || selected_todo_ < 0 || selected_todo_ >= static_cast<int>(indices.size()))
		{
			return;
		}
		auto &item = todos_[indices[static_cast<size_t>(selected_todo_)]];
		item.start_min = ClampMinute(item.start_min + delta_min);
		if (item.end_min < item.start_min)
		{
			item.end_min = item.start_min;
		}
	}

	void AdjustSelectedTodoEnd(int delta_min)
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		auto indices = GetTodoIndicesForDateLocked(selected_date_);
		if (indices.empty() || selected_todo_ < 0 || selected_todo_ >= static_cast<int>(indices.size()))
		{
			return;
		}
		auto &item = todos_[indices[static_cast<size_t>(selected_todo_)]];
		item.end_min = ClampMinute(item.end_min + delta_min);
		if (item.start_min > item.end_min)
		{
			item.start_min = item.end_min;
		}
	}

	void ToggleTodoDone()
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		auto indices = GetTodoIndicesForDateLocked(selected_date_);
		if (indices.empty() || selected_todo_ < 0 || selected_todo_ >= static_cast<int>(indices.size()))
		{
			return;
		}
		auto &item = todos_[indices[static_cast<size_t>(selected_todo_)]];
		item.done = !item.done;
		AppService::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
	}

	void AddQuickTodo()
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		if (static_cast<int>(todos_.size()) >= kMaxTodos)
		{
			return;
		}
		time_t now = time(nullptr);
		struct tm tm_now;
		localtime_r(&now, &tm_now);

		std::ostringstream oss;
		oss << "新任务 " << std::setw(2) << std::setfill('0') << tm_now.tm_hour
			<< ":" << std::setw(2) << std::setfill('0') << tm_now.tm_min;

		TodoItem item;
		item.text = oss.str();
		item.done = false;
		item.year = selected_date_.year;
		item.month = selected_date_.month;
		item.day = selected_date_.day;
		item.start_min = tm_now.tm_hour * 60 + tm_now.tm_min;
		item.end_min = ClampMinute(item.start_min + 30);
		todos_.push_back(std::move(item));
		selected_todo_ = static_cast<int>(GetTodoIndicesForDateLocked(selected_date_).size()) - 1;
		focus_ = Focus::Todo;
		AppService::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
	}

	std::vector<TodoItem> GetTodosSnapshot() const
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		return todos_;
	}

	std::vector<int> GetTodoIndicesForDate(const DateInfo &date) const
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		return GetTodoIndicesForDateLocked(date);
	}

	std::vector<int> GetTodoIndicesForDateLocked(const DateInfo &date) const
	{
		std::vector<int> indices;
		indices.reserve(todos_.size());
		for (size_t i = 0; i < todos_.size(); ++i)
		{
			const auto &t = todos_[i];
			if (t.year == date.year && t.month == date.month && t.day == date.day)
			{
				indices.push_back(static_cast<int>(i));
			}
		}
		return indices;
	}

	std::vector<TodoItem> GetTodosForDate(const DateInfo &date) const
	{
		std::lock_guard<std::mutex> lock(todo_mutex_);
		std::vector<TodoItem> list;
		for (const auto &t : todos_)
		{
			if (t.year == date.year && t.month == date.month && t.day == date.day)
			{
				list.push_back(t);
			}
		}
		return list;
	}

	DateInfo GetSelectedDate() const
	{
		return selected_date_;
	}


	void AddTodoFromHttp(std::string text, const std::string &start_str, const std::string &end_str)
	{
		if (text.size() > 32)
		{
			text.resize(32);
		}
		if (text.empty())
		{
			return;
		}
		int start_min = ParseTimeToMinutes(start_str);
		int end_min = ParseTimeToMinutes(end_str);
		if (end_min == 0 && start_min > 0)
		{
			end_min = ClampMinute(start_min + 30);
		}
		if (end_min > 0 && end_min < start_min)
		{
			end_min = ClampMinute(start_min + 30);
		}
		{
			std::lock_guard<std::mutex> lock(todo_mutex_);
			if (static_cast<int>(todos_.size()) >= kMaxTodos)
			{
				return;
			}
			TodoItem item{text, false, selected_date_.year, selected_date_.month, selected_date_.day};
			item.start_min = start_min;
			item.end_min = end_min;
			todos_.push_back(std::move(item));
			selected_todo_ = static_cast<int>(GetTodoIndicesForDateLocked(selected_date_).size()) - 1;
			focus_ = Focus::Todo;
		}
		SaveTodos();
		AppService::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
		ScheduleRender();
	}

	void ScheduleRender()
	{
		if (!active_ || !board_)
		{
			return;
		}
		AppService::GetInstance().Schedule([this]() {
			if (!active_ || !board_)
			{
				return;
			}
			RenderBoard(*board_);
		});
	}

	void EnsureWifiMode()
	{
		auto &wifi = WifiManager::GetInstance();
		(void)wifi.Initialize();

		if (wifi.IsConnected())
		{
			ssid_ = wifi.GetSsid();
			std::string ip = wifi.GetIpAddress();
			if (!ip.empty())
			{
				url_ = std::string("http://") + ip + ":" + std::to_string(kHttpPort);
			}
			else
			{
				url_.clear();
			}
			return;
		}

		wifi.StartConfigAp();
		ssid_ = wifi.GetApSsid();
		url_ = wifi.GetApWebUrl();
		if (!url_.empty())
		{
			url_ += ":" + std::to_string(kHttpPort);
		}
	}

	void StartServer()
	{
		if (server_)
		{
			return;
		}
		httpd_config_t config = HTTPD_DEFAULT_CONFIG();
		config.server_port = kHttpPort;
		config.max_uri_handlers = 8;
		config.lru_purge_enable = true;
		config.recv_wait_timeout = 15;
		config.send_wait_timeout = 15;

		esp_err_t err = httpd_start(&server_, &config);
		if (err != ESP_OK)
		{
			ESP_LOGW("CalendarSchedule", "httpd_start failed: %s", esp_err_to_name(err));
			server_ = nullptr;
			return;
		}

		httpd_uri_t index = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = &HandleIndex,
			.user_ctx = this,
		};
		httpd_register_uri_handler(server_, &index);

		httpd_uri_t todos = {
			.uri = "/todos",
			.method = HTTP_GET,
			.handler = &HandleTodos,
			.user_ctx = this,
		};
		httpd_register_uri_handler(server_, &todos);

		httpd_uri_t add = {
			.uri = "/add",
			.method = HTTP_POST,
			.handler = &HandleAdd,
			.user_ctx = this,
		};
		httpd_register_uri_handler(server_, &add);

		ESP_LOGI("CalendarSchedule", "Todo server started on port %d", kHttpPort);
	}

	void StopServer()
	{
		if (!server_)
		{
			return;
		}
		httpd_stop(server_);
		server_ = nullptr;
	}

	Focus focus_ = Focus::Calendar;
	bool edit_mode_ = false;
	DateInfo selected_date_{};
	int selected_todo_ = 0;
	std::vector<TodoItem> todos_;
	mutable std::mutex todo_mutex_;
	httpd_handle_t server_ = nullptr;
	std::string url_;
	std::string ssid_;
	Board *board_ = nullptr;
	bool active_ = false;
};

std::unique_ptr<AppBase> MakeCalendarScheduleApp()
{
	return std::make_unique<CalendarScheduleApp>();
}
