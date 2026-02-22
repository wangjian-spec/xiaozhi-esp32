#include "eteacher/apps/calendar_schedule/calendar_schedule.h"

#include <algorithm>
#include <array>
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <ctime>
#include <string>
#include <vector>

#include <SD.h>
#include <SPI.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <qrcode.h>
#include <sqlite3.h>
#include <wifi_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "boards/EnglishTeacher/config.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/input.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/apps/calendar_schedule/calendar_schedule_ui.h"

#include "eteacher/apps/calendar_schedule/solar_term_day_table.inc"

namespace {
constexpr const char *kTag = "CalendarScheduleApp";
constexpr const app_ui::desc::UiDesc *kUiDesc = &app_ui::generated::calendar_schedule::kUi;

constexpr uint32_t kWidgetTabView = 0xA8EC2EDCu;
constexpr uint32_t kWidgetBottomBar = 0x80DAA8ADu;
constexpr uint32_t kWidgetDialogAlert = 0xAE9E9A20u;
constexpr uint32_t kWidgetLabelAlert = 0x2C695914u;
constexpr uint32_t kWidgetButtonSubmit = 0xD17B7AB2u;
constexpr uint32_t kWidgetButtonCancel = 0x087A890Eu;

constexpr uint32_t kWidgetFrameCalendar = 0xD20A71A6u;
constexpr uint32_t kWidgetListTask = 0xBCABB678u;
constexpr uint32_t kWidgetRadioToday = 0x5B96521Au;
constexpr uint32_t kWidgetRadioThisWeek = 0x7EC7C59Du;
constexpr uint32_t kWidgetRadioAll = 0xB144D128u;
constexpr uint32_t kWidgetCheckboxTodo = 0x0C430A7Du;
constexpr uint32_t kWidgetCheckboxDone = 0x00DD8371u;
constexpr uint32_t kWidgetCheckboxDelete = 0x542B7F4Au;
constexpr uint32_t kWidgetButtonPhoneConnect = 0x86570FC8u;
constexpr uint32_t kWidgetImageScheduleQr = 0xA1D5D681u;
constexpr uint32_t kWidgetLabelScheduleUrl = 0xF31EF511u;
constexpr uint32_t kWidgetCalendarGrid = 0xC85B2001u;

constexpr int kScheduleHttpPort = 8090;
constexpr int kScheduleHttpStackSize = 16384;
static httpd_handle_t g_schedule_http_server = nullptr;

struct QrCaptureContext {
	int size = 0;
	std::vector<uint8_t> modules{};
};

static QrCaptureContext *g_qr_capture_context = nullptr;
static std::mutex g_qr_generate_mutex;
static std::recursive_mutex g_task_db_mutex;

constexpr const char *kDataDbPathPrimary = "/sdcard/Data.db";
constexpr const char *kDataDbPathSecondary = "/sd/Data.db";
constexpr std::array<const char *, 8> kTaskColumns = {
	"content", "done", "delete", "date", "starttime", "endtime", "priority", "period"};

constexpr const char *kAlertNoNetwork = "当前未连接网络，请在系统设置中连接WIFI！";

bool FileExists(const char *path) {
	if (!path || !path[0]) {
		return false;
	}
	struct stat st {};
	return ::stat(path, &st) == 0;
}

std::string DiscoverDataDbPath() {
	if (FileExists(kDataDbPathPrimary)) {
		return std::string(kDataDbPathPrimary);
	}
	if (FileExists(kDataDbPathSecondary)) {
		return std::string(kDataDbPathSecondary);
	}
	return {};
}

bool EnsureSqliteSdMounted() {
	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);

	static bool mounted = false;
	static bool attempted = false;
	if (mounted) {
		return true;
	}
	if (attempted) {
		return false;
	}
	attempted = true;

	if (SD.begin((int)SD_PIN_NUM_CS, SPI, 20000000, "/sdcard")) {
		mounted = true;
		return true;
	}
	if (SD.begin((int)SD_PIN_NUM_CS, SPI, 20000000, "/sd")) {
		mounted = true;
		return true;
	}
	return false;
}

bool EnsureSqliteRuntimeReady() {
	static bool initialized = false;
	if (initialized) {
		return true;
	}
	const int rc = sqlite3_initialize();
	if (rc != SQLITE_OK) {
		ESP_LOGE(kTag, "sqlite3_initialize failed rc=%d", rc);
		return false;
	}
	initialized = true;
	return true;
}

void LogHttpdRuntime(const char *endpoint, const char *phase) {
	const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
	const char *task_name = pcTaskGetName(nullptr);
	const size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
	const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
	ESP_LOGW(kTag,
		"httpd[%s] %s task=%s stack_hwm=%u words(%u bytes) free_heap=%u free_internal=%u",
		endpoint ? endpoint : "?", phase ? phase : "?", task_name ? task_name : "?",
		static_cast<unsigned>(hwm_words), static_cast<unsigned>(hwm_words * sizeof(StackType_t)),
		static_cast<unsigned>(free_heap), static_cast<unsigned>(free_internal));
}

std::string JsonEscape(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
			case '\\':
				out += "\\\\";
				break;
			case '"':
				out += "\\\"";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				out.push_back(c);
				break;
		}
	}
	return out;
}

std::string NormalizeTaskDisplayText(std::string text) {
	std::string out;
	out.reserve(text.size());

	auto append_utf8 = [&out](const char *p, size_t len) {
		out.append(p, len);
	};

	for (size_t i = 0; i < text.size();) {
		const unsigned char c0 = static_cast<unsigned char>(text[i]);
		if ((c0 & 0x80u) == 0) {
			out.push_back(static_cast<char>(c0));
			++i;
			continue;
		}

		uint32_t cp = 0;
		size_t len = 0;
		if ((c0 & 0xE0u) == 0xC0u && i + 1 < text.size()) {
			const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
			if ((c1 & 0xC0u) == 0x80u) {
				cp = (static_cast<uint32_t>(c0 & 0x1Fu) << 6) | static_cast<uint32_t>(c1 & 0x3Fu);
				len = 2;
			}
		} else if ((c0 & 0xF0u) == 0xE0u && i + 2 < text.size()) {
			const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
			const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
			if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u) {
				cp = (static_cast<uint32_t>(c0 & 0x0Fu) << 12) |
					 (static_cast<uint32_t>(c1 & 0x3Fu) << 6) |
					 static_cast<uint32_t>(c2 & 0x3Fu);
				len = 3;
			}
		} else if ((c0 & 0xF8u) == 0xF0u && i + 3 < text.size()) {
			const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
			const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
			const unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
			if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u && (c3 & 0xC0u) == 0x80u) {
				cp = (static_cast<uint32_t>(c0 & 0x07u) << 18) |
					 (static_cast<uint32_t>(c1 & 0x3Fu) << 12) |
					 (static_cast<uint32_t>(c2 & 0x3Fu) << 6) |
					 static_cast<uint32_t>(c3 & 0x3Fu);
				len = 4;
			}
		}

		if (len == 0) {
			out.push_back(static_cast<char>(c0));
			++i;
			continue;
		}

		const bool is_unicode_space =
			cp == 0x00A0 || cp == 0x1680 || cp == 0x180E || cp == 0x202F || cp == 0x205F || cp == 0x3000 ||
			(cp >= 0x2000 && cp <= 0x200A);
		const bool is_zero_width = cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF;

		if (is_zero_width) {
			// skip
		} else if (is_unicode_space) {
			out.push_back(' ');
		} else {
			append_utf8(text.data() + i, len);
		}
		i += len;
	}

	return out;
}

std::string GetScheduleUrl() {
	auto &wifi = WifiManager::GetInstance();
	wifi.Initialize();
	if (!wifi.IsConnected()) {
		return {};
	}
	std::string ip = wifi.GetIpAddress();
	if (ip.empty()) {
		return {};
	}
	return std::string("http://") + ip + ":" + std::to_string(kScheduleHttpPort);
}

void CaptureQrModules(esp_qrcode_handle_t qrcode) {
	if (!g_qr_capture_context || !qrcode) {
		return;
	}
	const int size = esp_qrcode_get_size(qrcode);
	if (size <= 0 || size > 177) {
		return;
	}
	const size_t module_count = static_cast<size_t>(size) * static_cast<size_t>(size);
	if (module_count == 0 || module_count > 177u * 177u) {
		return;
	}
	g_qr_capture_context->size = size;
	g_qr_capture_context->modules.assign(module_count, 0);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const bool black = esp_qrcode_get_module(qrcode, x, y);
			g_qr_capture_context->modules[static_cast<size_t>(y * size + x)] = black ? 1 : 0;
		}
	}
}

bool GenerateScheduleQr(const std::string &text, int &size, std::vector<uint8_t> &modules) {
	std::lock_guard<std::mutex> lock(g_qr_generate_mutex);

	size = 0;
	modules.clear();
	if (text.empty()) {
		return false;
	}

	QrCaptureContext capture{};
	g_qr_capture_context = &capture;
	esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
	cfg.display_func = CaptureQrModules;
	cfg.max_qrcode_version = 10;
	cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
	const esp_err_t err = esp_qrcode_generate(&cfg, text.c_str());
	g_qr_capture_context = nullptr;

	if (err != ESP_OK || capture.size <= 0 || capture.modules.empty()) {
		return false;
	}
	if (static_cast<size_t>(capture.size) * static_cast<size_t>(capture.size) != capture.modules.size()) {
		return false;
	}
	size = capture.size;
	modules = std::move(capture.modules);
	return true;
}

esp_err_t SendJson(httpd_req_t *req, const std::string &json) {
	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	return httpd_resp_send(req, json.c_str(), json.size());
}

bool ReadRequestBody(httpd_req_t *req, std::string &body) {
	body.clear();
	int remaining = req->content_len;
	if (remaining <= 0) {
		return true;
	}
	body.resize(static_cast<size_t>(remaining));
	int offset = 0;
	while (remaining > 0) {
		int read = httpd_req_recv(req, body.data() + offset, remaining);
		if (read <= 0) {
			body.clear();
			return false;
		}
		offset += read;
		remaining -= read;
	}
	return true;
}

bool EnsureTaskTable(sqlite3 *db) {
	if (!db) {
		return false;
	}
	const char *sql =
		"CREATE TABLE IF NOT EXISTS task ("
		"content TEXT,"
		"done INTEGER DEFAULT 0,"
		"\"delete\" INTEGER DEFAULT 0,"
		"date TEXT,"
		"starttime TEXT,"
		"endtime TEXT,"
		"priority TEXT,"
		"period TEXT"
		");";
	char *errmsg = nullptr;
	const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
	if (errmsg) {
		sqlite3_free(errmsg);
	}
	return rc == SQLITE_OK;
}

bool UpdateTaskFlagByRowId(int rowid, const char *column, int value) {
	if (rowid <= 0 || !column || !column[0]) {
		return false;
	}

	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);
	if (!EnsureSqliteRuntimeReady() || !EnsureSqliteSdMounted()) {
		return false;
	}

	const std::string db_path = DiscoverDataDbPath();
	if (db_path.empty()) {
		ESP_LOGW(kTag, "Data.db not found");
		return false;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}

	if (!EnsureTaskTable(db)) {
		sqlite3_close(db);
		return false;
	}

	std::string sql = "UPDATE task SET \"";
	sql += column;
	sql += "\"=? WHERE rowid=?;";

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
		return false;
	}

	sqlite3_bind_int(stmt, 1, value ? 1 : 0);
	sqlite3_bind_int(stmt, 2, rowid);
	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return rc == SQLITE_DONE;
}

void BindJson(sqlite3_stmt *stmt, int idx, cJSON *item) {
	if (!stmt) {
		return;
	}
	if (!item || cJSON_IsNull(item)) {
		sqlite3_bind_null(stmt, idx);
		return;
	}
	if (cJSON_IsBool(item)) {
		sqlite3_bind_int(stmt, idx, cJSON_IsTrue(item) ? 1 : 0);
		return;
	}
	if (cJSON_IsNumber(item)) {
		sqlite3_bind_double(stmt, idx, item->valuedouble);
		return;
	}
	if (cJSON_IsString(item) && item->valuestring) {
		sqlite3_bind_text(stmt, idx, item->valuestring, -1, SQLITE_TRANSIENT);
		return;
	}
	sqlite3_bind_null(stmt, idx);
}

esp_err_t HandleScheduleIndex(httpd_req_t *req) {
	static const char *kHtml = R"HTML(
<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>日程设置</title><style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#f7f8fa;margin:0;padding:16px;color:#1f2937}
.card{background:#fff;border:1px solid #e5e7eb;border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 2px 8px rgba(0,0,0,.04)}
h2{margin:0 0 10px 0}.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1 1 120px}
input,select,button{font-size:14px;padding:8px;border-radius:8px;border:1px solid #d1d5db}
button{background:#111827;color:#fff;border:none;cursor:pointer}button.alt{background:#6b7280}
table{width:100%;border-collapse:collapse;font-size:13px}th,td{border-bottom:1px solid #e5e7eb;padding:6px;text-align:left;white-space:nowrap}
tr:hover{background:#f9fafb}
.field{display:flex;align-items:center;gap:8px;flex:1 1 220px}
.field label{min-width:84px;color:#4b5563;font-size:12px}
.field input,.field select{flex:1}
</style></head><body>
<div class='card'><h2>待办事项设置</h2><div class='row'><button id='btn_reload'>刷新</button><button id='btn_new'>新建任务</button><button id='btn_save' class='alt'>确定添加</button><button id='btn_delete' class='alt'>标记删除</button></div><div id='status' style='margin-top:8px;color:#374151;font-size:12px'>就绪</div></div>
<div class='card'><h2 style='font-size:16px;margin-bottom:8px'>任务字段设置</h2><div id='form' class='row'></div></div>
<div class='card'><table id='tbl'><thead></thead><tbody></tbody></table></div>
<script>
const defaultCols=['content','done','delete','date','starttime','endtime','priority','period'];
let cols=[],rows=[],current={};
let isNewMode=true;
function setStatus(t,e=false){const s=document.getElementById('status');if(!s)return;s.textContent=t;s.style.color=e?'#b91c1c':'#374151';}
async function api(u,m='GET',d){const o={method:m,headers:{'Content-Type':'application/json'}};if(d)o.body=JSON.stringify(d);const r=await fetch(u,o);if(!r.ok)throw new Error(await r.text());return r.json();}
function fieldLabel(c){if(c==='content')return '待办内容';if(c==='done')return '完成状态';if(c==='delete')return '删除状态';if(c==='date')return '日期';if(c==='starttime')return '开始时间';if(c==='endtime')return '结束时间';if(c==='priority')return '优先级';if(c==='period')return '重复周期';return c;}
function buildForm(){const f=document.getElementById('form');f.innerHTML='';cols.forEach(c=>{const wrap=document.createElement('div');wrap.className='field';const l=document.createElement('label');l.htmlFor='f_'+c;l.textContent=fieldLabel(c);wrap.appendChild(l);let el;if(c==='done'){el=document.createElement('select');el.innerHTML='<option value="0">未完成</option><option value="1">完成</option>';}else if(c==='delete'){el=document.createElement('select');el.innerHTML='<option value="0">未删除</option><option value="1">已删除</option>';}else if(c==='priority'){el=document.createElement('select');el.innerHTML='<option value="">(空)</option><option value="紧急">紧急</option><option value="非常重要">非常重要</option><option value="重要">重要</option><option value="一般">一般</option>';}else if(c==='period'){el=document.createElement('select');el.innerHTML='<option value="">(空)</option><option value="每天">每天</option><option value="每周一">每周一</option><option value="每周二">每周二</option><option value="每周三">每周三</option><option value="每周四">每周四</option><option value="每周五">每周五</option><option value="每周六">每周六</option><option value="每周日">每周日</option>';}else if(c==='date'){el=document.createElement('input');el.type='date';}else if(c==='starttime'||c==='endtime'){el=document.createElement('input');el.type='time';}else{el=document.createElement('input');el.type='text';el.placeholder='请输入 '+fieldLabel(c);}el.id='f_'+c;el.oninput=()=>{current[c]=el.value;};el.onchange=()=>{current[c]=el.value;};wrap.appendChild(el);f.appendChild(wrap);});}
function fillForm(){cols.forEach(c=>{const el=document.getElementById('f_'+c);if(el)el.value=(current[c]??'');});}
function updateSaveButton(){const btn=document.getElementById('btn_save');if(!btn)return;btn.textContent=isNewMode?'确定添加':'保存修改';}
function render(){const th=document.querySelector('#tbl thead');const tb=document.querySelector('#tbl tbody');th.innerHTML='<tr><th>rowid</th>'+cols.map(c=>`<th>${c}</th>`).join('')+'</tr>';tb.innerHTML='';rows.forEach(r=>{const tr=document.createElement('tr');tr.onclick=()=>{current=JSON.parse(JSON.stringify(r));isNewMode=false;updateSaveButton();fillForm();setStatus('已选择 rowid='+String(r.__rowid__||'')+'，可修改后保存');};tr.innerHTML='<td>'+r.__rowid__+'</td>'+cols.map(c=>'<td>'+String(r[c]??'')+'</td>').join('');tb.appendChild(tr);});}
function collectForm(){const data={};cols.forEach(c=>{const el=document.getElementById('f_'+c);if(!el)return;const v=el.value;data[c]=v===''?null:v;});if(!isNewMode&&current&&current.__rowid__)data.__rowid__=current.__rowid__;return data;}
async function reloadData(){setStatus('加载中...');const schema=await api('/api/schema');cols=(schema.columns&&schema.columns.length)?schema.columns:defaultCols;buildForm();rows=(await api('/api/tasks')).items||[];render();setStatus('已加载 '+rows.length+' 条');}
function newTask(){current={};isNewMode=true;updateSaveButton();fillForm();setStatus('新建模式：请填写各字段，点击“确定添加”');}
async function saveTask(){const payload=collectForm();setStatus(isNewMode?'添加中...':'保存中...');await api('/api/task/save','POST',payload);await reloadData();if(isNewMode){newTask();setStatus('添加成功');}else{setStatus('保存成功');}}
async function softDelete(){if(!current.__rowid__){setStatus('请先选择一条记录',true);return;}setStatus('删除中...');await api('/api/task/delete','POST',{rowid:current.__rowid__});await reloadData();setStatus('删除成功');}
document.getElementById('btn_reload').addEventListener('click',()=>{reloadData().catch(e=>setStatus('刷新失败: '+e.message,true));});
document.getElementById('btn_new').addEventListener('click',newTask);
document.getElementById('btn_save').addEventListener('click',()=>{saveTask().catch(e=>setStatus('保存失败: '+e.message,true));});
document.getElementById('btn_delete').addEventListener('click',()=>{softDelete().catch(e=>setStatus('删除失败: '+e.message,true));});
reloadData().then(()=>newTask()).catch(e=>setStatus('加载失败: '+e.message,true));
</script></body></html>)HTML";
	httpd_resp_set_type(req, "text/html; charset=utf-8");
	return httpd_resp_send(req, kHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HandleScheduleSchema(httpd_req_t *req) {
	LogHttpdRuntime("/api/schema", "enter");
	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);

	if (!EnsureSqliteRuntimeReady() || !EnsureSqliteSdMounted()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sqlite not ready");
	}
	const std::string db_path = DiscoverDataDbPath();
	if (db_path.empty()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "db not found");
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open db failed");
	}
	if (!EnsureTaskTable(db)) {
		sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ensure task table failed");
	}
	std::vector<std::string> cols;
	cols.reserve(kTaskColumns.size());
	for (const char *c : kTaskColumns) {
		cols.emplace_back(c);
	}
	LogHttpdRuntime("/api/schema", "after-read-columns");
	sqlite3_close(db);
	std::string json = "{\"columns\":[";
	for (size_t i = 0; i < cols.size(); ++i) {
		if (i) json += ",";
		json += "\"" + JsonEscape(cols[i]) + "\"";
	}
	json += "]}";
	LogHttpdRuntime("/api/schema", "before-send");
	return SendJson(req, json);
}

esp_err_t HandleScheduleTasks(httpd_req_t *req) {
	LogHttpdRuntime("/api/tasks", "enter");
	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);

	if (!EnsureSqliteRuntimeReady() || !EnsureSqliteSdMounted()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sqlite not ready");
	}
	const std::string db_path = DiscoverDataDbPath();
	if (db_path.empty()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "db not found");
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open db failed");
	}
	if (!EnsureTaskTable(db)) {
		sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ensure task table failed");
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql =
		"SELECT rowid AS __rowid__, content, IFNULL(done,0) AS done, IFNULL(\"delete\",0) AS \"delete\", "
		"date, starttime, endtime, priority, period "
		"FROM task ORDER BY date ASC, starttime ASC;";
	LogHttpdRuntime("/api/tasks", "before-prepare");
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		if (stmt) sqlite3_finalize(stmt);
		sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "query failed");
	}
	LogHttpdRuntime("/api/tasks", "after-prepare");

	std::string json = "{\"items\":[";
	bool first_row = true;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (!first_row) json += ",";
		first_row = false;
		json += "{";
		for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
			if (i) json += ",";
			const char *col = sqlite3_column_name(stmt, i);
			json += "\"" + JsonEscape(col ? col : "") + "\":";
			int t = sqlite3_column_type(stmt, i);
			if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) {
				const char *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
				json += txt ? txt : "0";
			} else if (t == SQLITE_TEXT) {
				const char *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
				json += "\"" + JsonEscape(txt ? txt : "") + "\"";
			} else {
				json += "null";
			}
		}
		json += "}";
	}
	json += "]}";

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	LogHttpdRuntime("/api/tasks", "before-send");
	return SendJson(req, json);
}

esp_err_t HandleScheduleTaskSave(httpd_req_t *req) {
	LogHttpdRuntime("/api/task/save", "enter");
	std::string body;
	if (!ReadRequestBody(req, body)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read body failed");
	}

	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);
	if (!EnsureSqliteRuntimeReady() || !EnsureSqliteSdMounted()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sqlite not ready");
	}

	cJSON *root = cJSON_Parse(body.c_str());
	if (!cJSON_IsObject(root)) {
		if (root) cJSON_Delete(root);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
	}

	const std::string db_path = DiscoverDataDbPath();
	if (db_path.empty()) {
		cJSON_Delete(root);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "db not found");
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) sqlite3_close(db);
		cJSON_Delete(root);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open db failed");
	}
	if (!EnsureTaskTable(db)) {
		sqlite3_close(db);
		cJSON_Delete(root);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ensure task table failed");
	}

	std::vector<std::string> cols;
	cols.reserve(kTaskColumns.size());
	for (const char *c : kTaskColumns) {
		cols.emplace_back(c);
	}
	cJSON *rowid_item = cJSON_GetObjectItem(root, "__rowid__");
	const int rowid = cJSON_IsNumber(rowid_item) ? rowid_item->valueint : 0;

	std::vector<std::string> used_cols;
	for (const auto &c : cols) {
		if (cJSON_GetObjectItem(root, c.c_str()) != nullptr) {
			used_cols.push_back(c);
		}
	}
	if (used_cols.empty()) {
		sqlite3_close(db);
		cJSON_Delete(root);
		return SendJson(req, "{\"ok\":true}");
	}

	std::string sql;
	if (rowid > 0) {
		sql = "UPDATE task SET ";
		for (size_t i = 0; i < used_cols.size(); ++i) {
			if (i) sql += ",";
			sql += "\"" + used_cols[i] + "\"=?";
		}
		sql += " WHERE rowid=?;";
	} else {
		sql = "INSERT INTO task (";
		for (size_t i = 0; i < used_cols.size(); ++i) {
			if (i) sql += ",";
			sql += "\"" + used_cols[i] + "\"";
		}
		sql += ") VALUES (";
		for (size_t i = 0; i < used_cols.size(); ++i) {
			if (i) sql += ",";
			sql += "?";
		}
		sql += ");";
	}

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		if (stmt) sqlite3_finalize(stmt);
		sqlite3_close(db);
		cJSON_Delete(root);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "prepare failed");
	}

	int bind_idx = 1;
	for (const auto &c : used_cols) {
		BindJson(stmt, bind_idx++, cJSON_GetObjectItem(root, c.c_str()));
	}
	if (rowid > 0) {
		sqlite3_bind_int(stmt, bind_idx++, rowid);
	}
	int rc = sqlite3_step(stmt);
	LogHttpdRuntime("/api/task/save", "after-step");
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	cJSON_Delete(root);
	if (rc != SQLITE_DONE) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
	}
	LogHttpdRuntime("/api/task/save", "before-send");
	return SendJson(req, "{\"ok\":true}");
}

esp_err_t HandleScheduleTaskDelete(httpd_req_t *req) {
	LogHttpdRuntime("/api/task/delete", "enter");
	std::string body;
	if (!ReadRequestBody(req, body)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read body failed");
	}
	cJSON *root = cJSON_Parse(body.c_str());
	if (!cJSON_IsObject(root)) {
		if (root) cJSON_Delete(root);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
	}
	cJSON *rowid_item = cJSON_GetObjectItem(root, "rowid");
	const int rowid = cJSON_IsNumber(rowid_item) ? rowid_item->valueint : 0;
	cJSON_Delete(root);
	if (rowid <= 0) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid rowid");
	}

	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);
	if (!EnsureSqliteRuntimeReady() || !EnsureSqliteSdMounted()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sqlite not ready");
	}

	const std::string db_path = DiscoverDataDbPath();
	if (db_path.empty()) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "db not found");
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open db failed");
	}
	if (!EnsureTaskTable(db)) {
		sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ensure task table failed");
	}
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, "UPDATE task SET \"delete\"=1 WHERE rowid=?;", -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		if (stmt) sqlite3_finalize(stmt);
		sqlite3_close(db);
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "prepare failed");
	}
	sqlite3_bind_int(stmt, 1, rowid);
	int rc = sqlite3_step(stmt);
	LogHttpdRuntime("/api/task/delete", "after-step");
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	if (rc != SQLITE_DONE) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "delete failed");
	}
	LogHttpdRuntime("/api/task/delete", "before-send");
	return SendJson(req, "{\"ok\":true}");
}

void StartScheduleWebServerIfNeeded() {
	if (g_schedule_http_server) {
		return;
	}
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = kScheduleHttpPort;
	config.stack_size = kScheduleHttpStackSize;
	config.max_uri_handlers = 16;
	config.lru_purge_enable = true;
	ESP_LOGW(kTag, "starting schedule httpd: port=%d stack=%d", config.server_port, config.stack_size);
	if (httpd_start(&g_schedule_http_server, &config) != ESP_OK) {
		g_schedule_http_server = nullptr;
		ESP_LOGW(kTag, "schedule http server start failed");
		return;
	}
	httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = HandleScheduleIndex, .user_ctx = nullptr};
	httpd_uri_t schema = {.uri = "/api/schema", .method = HTTP_GET, .handler = HandleScheduleSchema, .user_ctx = nullptr};
	httpd_uri_t tasks = {.uri = "/api/tasks", .method = HTTP_GET, .handler = HandleScheduleTasks, .user_ctx = nullptr};
	httpd_uri_t save = {.uri = "/api/task/save", .method = HTTP_POST, .handler = HandleScheduleTaskSave, .user_ctx = nullptr};
	httpd_uri_t del = {.uri = "/api/task/delete", .method = HTTP_POST, .handler = HandleScheduleTaskDelete, .user_ctx = nullptr};
	httpd_register_uri_handler(g_schedule_http_server, &index);
	httpd_register_uri_handler(g_schedule_http_server, &schema);
	httpd_register_uri_handler(g_schedule_http_server, &tasks);
	httpd_register_uri_handler(g_schedule_http_server, &save);
	httpd_register_uri_handler(g_schedule_http_server, &del);
}

struct LunarDate {
	int year = 0;
	int month = 0;
	int day = 0;
	bool is_leap = false;
};

constexpr uint32_t kLunarInfo[] = {
	0x04BD8, 0x04AE0, 0x0A570, 0x054D5, 0x0D260, 0x0D950, 0x16554, 0x056A0, 0x09AD0, 0x055D2,
	0x04AE0, 0x0A5B6, 0x0A4D0, 0x0D250, 0x1D255, 0x0B540, 0x0D6A0, 0x0ADA2, 0x095B0, 0x14977,
	0x04970, 0x0A4B0, 0x0B4B5, 0x06A50, 0x06D40, 0x1AB54, 0x02B60, 0x09570, 0x052F2, 0x04970,
	0x06566, 0x0D4A0, 0x0EA50, 0x06E95, 0x05AD0, 0x02B60, 0x186E3, 0x092E0, 0x1C8D7, 0x0C950,
	0x0D4A0, 0x1D8A6, 0x0B550, 0x056A0, 0x1A5B4, 0x025D0, 0x092D0, 0x0D2B2, 0x0A950, 0x0B557,
	0x06CA0, 0x0B550, 0x15355, 0x04DA0, 0x0A5D0, 0x14573, 0x052D0, 0x0A9A8, 0x0E950, 0x06AA0,
	0x0AEA6, 0x0AB50, 0x04B60, 0x0AAE4, 0x0A570, 0x05260, 0x0F263, 0x0D950, 0x05B57, 0x056A0,
	0x096D0, 0x04DD5, 0x04AD0, 0x0A4D0, 0x0D4D4, 0x0D250, 0x0D558, 0x0B540, 0x0B5A0, 0x195A6,
	0x095B0, 0x049B0, 0x0A974, 0x0A4B0, 0x0B27A, 0x06A50, 0x06D40, 0x0AF46, 0x0AB60, 0x09570,
	0x04AF5, 0x04970, 0x064B0, 0x074A3, 0x0EA50, 0x06B58, 0x05AC0, 0x0AB60, 0x096D5, 0x092E0,
	0x0C960, 0x0D954, 0x0D4A0, 0x0DA50, 0x07552, 0x056A0, 0x0ABB7, 0x025D0, 0x092D0, 0x0CAB5,
	0x0A950, 0x0B4A0, 0x0BAA4, 0x0AD50, 0x055D9, 0x04BA0, 0x0A5B0, 0x15176, 0x052B0, 0x0A930,
	0x07954, 0x06AA0, 0x0AD50, 0x05B52, 0x04B60, 0x0A6E6, 0x0A4E0, 0x0D260, 0x0EA65, 0x0D530,
	0x05AA0, 0x076A3, 0x096D0, 0x04BD7, 0x04AD0, 0x0A4D0, 0x1D0B6, 0x0D250, 0x0D520, 0x0DD45,
	0x0B5A0, 0x056D0, 0x055B2, 0x049B0, 0x0A577, 0x0A4B0, 0x0AA50, 0x1B255, 0x06D20, 0x0ADA0,
	0x14B63, 0x09370, 0x049F8, 0x04970, 0x064B0, 0x168A6, 0x0EA50, 0x06B20, 0x1A6C4, 0x0AAE0,
	0x0A2E0, 0x0D2E3, 0x0C960, 0x0D557, 0x0D4A0, 0x0DA50, 0x05D55, 0x056A0, 0x0A6D0, 0x055D4,
	0x052D0, 0x0A9B8, 0x0A950, 0x0B4A0, 0x0B6A6, 0x0AD50, 0x055A0, 0x0ABA4, 0x0A5B0, 0x052B0,
	0x0B273, 0x06930, 0x07337, 0x06AA0, 0x0AD50, 0x14B55, 0x04B60, 0x0A570, 0x054E4, 0x0D160,
	0x0E968, 0x0D520, 0x0DAA0, 0x16AA6, 0x056D0, 0x04AE0, 0x0A9D4, 0x0A2D0, 0x0D150, 0x0F252,
	0x0D520,
};

int LeapMonth(int lunar_year) {
	if (lunar_year < 1900 || lunar_year > 2100) {
		return 0;
	}
	return static_cast<int>(kLunarInfo[lunar_year - 1900] & 0xF);
}

int LeapDays(int lunar_year) {
	const int leap_month = LeapMonth(lunar_year);
	if (leap_month == 0) {
		return 0;
	}
	return (kLunarInfo[lunar_year - 1900] & 0x10000) ? 30 : 29;
}

int LunarMonthDays(int lunar_year, int lunar_month) {
	return (kLunarInfo[lunar_year - 1900] & (0x10000 >> lunar_month)) ? 30 : 29;
}

int LunarYearDays(int lunar_year) {
	int total = 348;
	uint32_t info = kLunarInfo[lunar_year - 1900];
	for (uint32_t bit = 0x8000; bit > 0x8; bit >>= 1) {
		total += (info & bit) ? 1 : 0;
	}
	return total + LeapDays(lunar_year);
}

LunarDate SolarToLunar(int year, int month, int day) {
	LunarDate lunar{};
	if (year < 1900 || year > 2100) {
		return lunar;
	}

	auto days_from_civil = [](int y, unsigned m, unsigned d) -> int {
		y -= m <= 2;
		const int era = (y >= 0 ? y : y - 399) / 400;
		const unsigned yoe = static_cast<unsigned>(y - era * 400);
		const unsigned mp = (m > 2) ? (m - 3) : (m + 9);
		const unsigned doy = (153 * mp + 2) / 5 + d - 1;
		const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + static_cast<int>(doe);
	};

	const int base_days = days_from_civil(1900, 1, 31);
	const int target_days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
	if (target_days < base_days) {
		return lunar;
	}

	int offset = target_days - base_days;
	int lunar_year = 1900;
	int days_of_year = LunarYearDays(lunar_year);
	while (lunar_year < 2100 && offset >= days_of_year) {
		offset -= days_of_year;
		++lunar_year;
		days_of_year = LunarYearDays(lunar_year);
	}

	int leap_month = LeapMonth(lunar_year);
	bool is_leap = false;
	int lunar_month = 1;
	int days_of_month = 0;
	while (lunar_month <= 12) {
		days_of_month = is_leap ? LeapDays(lunar_year) : LunarMonthDays(lunar_year, lunar_month);

		if (offset < days_of_month) {
			break;
		}
		offset -= days_of_month;

		if (leap_month > 0 && lunar_month == leap_month && !is_leap) {
			is_leap = true;
		} else {
			if (is_leap) {
				is_leap = false;
			}
			++lunar_month;
		}
	}

	lunar.year = lunar_year;
	lunar.month = lunar_month;
	lunar.day = offset + 1;
	lunar.is_leap = is_leap;
	return lunar;
}

std::string LunarDayText(int lunar_month, int lunar_day) {
	static const char *kDayNames[] = {
		"",     "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
		"十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十", "廿一",
		"廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
	};
	static const char *kMonthNames[] = {
		"", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊",
	};

	if (lunar_month < 1 || lunar_month > 12 || lunar_day < 1 || lunar_day > 30) {
		return "";
	}
	if (lunar_day == 1) {
		return std::string(kMonthNames[lunar_month]) + "月";
	}
	return kDayNames[lunar_day];
}

std::string SolarFestivalName(int month, int day) {
	const int key = month * 100 + day;
	switch (key) {
		case 101:
			return "元旦";
		case 214:
			return "情人节";
		case 308:
			return "妇女节";
		case 401:
			return "愚人节";
		case 501:
			return "劳动节";
		case 601:
			return "儿童节";
		case 1001:
			return "国庆节";
		case 1225:
			return "圣诞节";
		default:
			return "";
	}
}

std::string LunarFestivalName(const LunarDate &lunar) {
	if (lunar.is_leap) {
		return "";
	}
	const int key = lunar.month * 100 + lunar.day;
	switch (key) {
		case 101:
			return "春节";
		case 115:
			return "元宵";
		case 505:
			return "端午";
		case 707:
			return "七夕";
		case 815:
			return "中秋";
		case 909:
			return "重阳";
		case 1208:
			return "腊八";
		case 1223:
			return "小年";
		default:
			return "";
	}
}

constexpr const char *kSolarTermName[24] = {
	"小寒", "大寒", "立春", "雨水", "惊蛰", "春分",
	"清明", "谷雨", "立夏", "小满", "芒种", "夏至",
	"小暑", "大暑", "立秋", "处暑", "白露", "秋分",
	"寒露", "霜降", "立冬", "小雪", "大雪", "冬至"
};

int GetSolarTermDay(int year, int term_index) {
	if (year < kSolarTermStartYear || year > kSolarTermEndYear || term_index < 0 || term_index >= 24) {
		return -1;
	}
	return static_cast<int>(kSolarTermDay[year - kSolarTermStartYear][term_index]);
}

bool IsSolarTerm(int year, int month, int day, int &term_index) {
	if (month < 1 || month > 12) {
		return false;
	}
	const int first = (month - 1) * 2;
	for (int i = first; i <= first + 1; ++i) {
		if (GetSolarTermDay(year, i) == day) {
			term_index = i;
			return true;
		}
	}
	return false;
}

const char *GetSolarTermName(int year, int month, int day) {
	int index = -1;
	if (IsSolarTerm(year, month, day, index)) {
		return kSolarTermName[index];
	}
	return nullptr;
}

std::string SolarTermName(int year, int month, int day) {
	const char *name = GetSolarTermName(year, month, day);
	return name ? std::string(name) : std::string();
}

int DaysInMonth(int year, int month);

int DaysInMonth(int year, int month) {
	static const int kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month < 1 || month > 12) {
		return 30;
	}
	if (month != 2) {
		return kMonthDays[month - 1];
	}
	const bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
	return leap ? 29 : 28;
}

int WeekdayMondayFirst(int year, int month, int day) {
	std::tm t{};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = 12;
	if (std::mktime(&t) == static_cast<std::time_t>(-1)) {
		return 0;
	}
	return (t.tm_wday + 6) % 7;
}

struct DateInfoValue {
	int year = 0;
	int month = 0;
	int day = 0;
};

DateInfoValue GetTodayDateInfo() {
	std::time_t now = std::time(nullptr);
	std::tm local_tm{};
	localtime_r(&now, &local_tm);
	DateInfoValue info{};
	info.year = local_tm.tm_year + 1900;
	info.month = local_tm.tm_mon + 1;
	info.day = local_tm.tm_mday;
	return info;
}

DateInfoValue GetTodayUtcDateInfo() {
	std::time_t now = std::time(nullptr);
	std::tm utc_tm{};
	gmtime_r(&now, &utc_tm);
	DateInfoValue info{};
	info.year = utc_tm.tm_year + 1900;
	info.month = utc_tm.tm_mon + 1;
	info.day = utc_tm.tm_mday;
	return info;
}

int ToYmdInt(const DateInfoValue &date) {
	return date.year * 10000 + date.month * 100 + date.day;
}

bool ParseDateYmd(const std::string &text, DateInfoValue &out) {
	if (text.size() < 10) {
		return false;
	}
	auto digit = [](char c) { return c >= '0' && c <= '9'; };
	if (!digit(text[0]) || !digit(text[1]) || !digit(text[2]) || !digit(text[3])) {
		return false;
	}
	if (!(text[4] == '-' || text[4] == '/')) {
		return false;
	}
	if (!digit(text[5]) || !digit(text[6])) {
		return false;
	}
	if (!(text[7] == '-' || text[7] == '/')) {
		return false;
	}
	if (!digit(text[8]) || !digit(text[9])) {
		return false;
	}

	out.year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
	out.month = (text[5] - '0') * 10 + (text[6] - '0');
	out.day = (text[8] - '0') * 10 + (text[9] - '0');
	return out.month >= 1 && out.month <= 12 && out.day >= 1 && out.day <= 31;
}

DateInfoValue ShiftDays(const DateInfoValue &date, int delta_days) {
	std::tm t{};
	t.tm_year = date.year - 1900;
	t.tm_mon = date.month - 1;
	t.tm_mday = date.day + delta_days;
	t.tm_hour = 12;
	std::mktime(&t);
	DateInfoValue out{};
	out.year = t.tm_year + 1900;
	out.month = t.tm_mon + 1;
	out.day = t.tm_mday;
	return out;
}

DateInfoValue ShiftMonth(const DateInfoValue &date, int delta_months) {
	const int total_month = (date.year * 12 + (date.month - 1)) + delta_months;
	DateInfoValue out{};
	out.year = total_month / 12;
	out.month = (total_month % 12) + 1;
	out.day = std::min(date.day, DaysInMonth(out.year, out.month));
	return out;
}

bool IsDoneValue(const char *value_text, int value_int) {
	if (value_int != 0) {
		return true;
	}
	if (!value_text) {
		return false;
	}
	std::string text(value_text);
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
		if (c >= 'A' && c <= 'Z') {
			return static_cast<char>(c - 'A' + 'a');
		}
		return static_cast<char>(c);
	});
	return text == "1" || text == "true" || text == "done" || text == "yes" || text == "完成";
}

bool IsDeletedValue(const char *value_text, int value_int) {
	if (value_int != 0) {
		return true;
	}
	if (!value_text) {
		return false;
	}
	std::string text(value_text);
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
		if (c >= 'A' && c <= 'Z') {
			return static_cast<char>(c - 'A' + 'a');
		}
		return static_cast<char>(c);
	});
	return text == "1" || text == "true" || text == "deleted" || text == "delete" || text == "yes";
}

void AppendIfValid(const app_ui::Widget *widget, std::vector<uint32_t> &out) {
	if (!widget) {
		return;
	}
	const uint32_t id = widget->Id();
	if (id != 0) {
		out.push_back(id);
	}
}

class CalendarGridWidget : public app_ui::BasicWidget {
public:
	struct Cell {
		bool valid = false;
		bool is_today = false;
		int day = 0;
		std::string second_line;
	};

	struct Header {
		int year = 0;
		int month = 0;
	};

	CalendarGridWidget() {
		SetFontName("wenquanyi_11pt");
	}

	void SetHeader(const Header &header) {
		header_ = header;
		MarkDirty();
	}

	void SetCells(std::array<Cell, 42> cells) {
		cells_ = std::move(cells);
		MarkDirty();
	}

protected:
	void OnDraw(app_ui::Painter &p) override {
		const app_ui::Rect rect = LocalRect();
		if (rect.w <= 0 || rect.h <= 0) {
			return;
		}

		static const char *kWeekNames[7] = {
			"星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日",
		};

		const int rows = 8;
		const int cols = 7;
		const int cell_w = rect.w / cols;
		constexpr int kHeaderRowHeight = 21;
		constexpr int kWeekRowHeight = 21;
		const int remaining_h = std::max(0, rect.h - kHeaderRowHeight - kWeekRowHeight);
		const int day_row_h = remaining_h / 6;
		std::array<int, 8> row_h{};
		row_h[0] = kHeaderRowHeight;
		row_h[1] = kWeekRowHeight;
		for (int i = 2; i < rows; ++i) {
			row_h[i] = day_row_h;
		}
		std::array<int, 8> row_y{};
		int acc_y = 0;
		for (int i = 0; i < rows; ++i) {
			row_y[i] = acc_y;
			acc_y += row_h[i];
		}
		app_ui::Font font_11{"wenquanyi_11pt"};
		app_ui::Font font_9{"wenquanyi_9pt"};

		auto draw_centered = [&](int x, int y, int w, int h, const std::string &text, app_ui::Color text_color,
			app_ui::Font *font) {
			if (text.empty()) {
				return;
			}
			p.SetFont(font);
			p.SetTextColor(text_color);
			const app_ui::Size text_size = p.MeasureText(text.c_str(), font);
			int16_t tx = static_cast<int16_t>(x + (w - text_size.w) / 2);
			int16_t ty = static_cast<int16_t>(y + (h - text_size.h) / 2);
			if (tx < x + 1) tx = static_cast<int16_t>(x + 1);
			if (ty < y + 1) ty = static_cast<int16_t>(y + 1);
			p.DrawText({tx, ty}, text.c_str());
		};

		p.SetDrawColor(app_ui::Color::White);
		p.FillRect(rect);

		for (int r = 0; r < rows; ++r) {
			for (int c = 0; c < cols; ++c) {
				const int x = c * cell_w;
				const int y = row_y[r];
				const int w = (c == cols - 1) ? (rect.w - x) : cell_w;
				const int h = (r == rows - 1) ? (rect.h - y) : row_h[r];

				if (r == 0) {
					if (c == 0) {
						char ym[24];
						std::snprintf(ym, sizeof(ym), "%04d年%02d月", header_.year, header_.month);
						draw_centered(0, y, rect.w, h, ym, app_ui::Color::Black, &font_11);
					}
					continue;
				}

				if (r == 1) {
					draw_centered(x, y, w, h, kWeekNames[c], app_ui::Color::Black, &font_11);
					continue;
				}

				const int idx = (r - 2) * cols + c;
				if (idx < 0 || idx >= static_cast<int>(cells_.size())) {
					continue;
				}
				const Cell &cell = cells_[idx];
				if (!cell.valid) {
					continue;
				}

				const bool selected = cell.is_today;
				p.SetDrawColor(selected ? app_ui::Color::Black : app_ui::Color::White);
				p.FillRect({static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1), static_cast<int16_t>(w - 2), static_cast<int16_t>(h - 2)});

				const app_ui::Color text_color = selected ? app_ui::Color::White : app_ui::Color::Black;
				draw_centered(x + 1, y + 2, w - 2, (h - 2) / 2, std::to_string(cell.day), text_color, &font_9);
				draw_centered(x + 1, y + (h / 2), w - 2, (h - 2) / 2, cell.second_line, text_color, &font_9);
			}
		}

		p.SetDrawColor(app_ui::Color::Black);
		p.DrawHLine({0, static_cast<int16_t>(kHeaderRowHeight + kWeekRowHeight)}, rect.w);
	}

private:
	Header header_{};
	std::array<Cell, 42> cells_{};
};
}

MenuMeta CalendarScheduleApp::GetMenuMeta() const {
	return MenuMeta{"calendar_schedule", "智能日历", "Left/Right切换页"};
}

void CalendarScheduleApp::OnEnter(AppContext &ctx) {
	ui_ready_ = false;
	alert_dialog_visible_ = false;
	is_calendar_scene_ = false;
	calendar_view_year_ = 0;
	calendar_view_month_ = 0;
	todo_filter_ = TodoFilter::All;
	bottom_bar_hint_.clear();
	focus_cycle_ids_.clear();
	focus_cycle_index_ = 0;

	root_ = nullptr;
	tabview_ = nullptr;
	bottom_bar_ = nullptr;
	dialog_alert_ = nullptr;
	label_alert_ = nullptr;
	button_submit_ = nullptr;
	button_cancel_ = nullptr;
	frame_calendar_ = nullptr;
	calendar_grid_ = nullptr;
	listview_task_ = nullptr;
	radio_today_ = nullptr;
	radio_this_week_ = nullptr;
	radio_all_ = nullptr;
	checkbox_todo_ = nullptr;
	checkbox_done_ = nullptr;
	checkbox_delete_ = nullptr;
	button_phone_connect_ = nullptr;
	image_schedule_qr_ = nullptr;
	label_schedule_url_ = nullptr;

	router_.Reset();
	scene_load_id_ = 0;
	epd_ = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
	StartScheduleWebServerIfNeeded();

	if (!LoadUi(ctx)) {
		return;
	}

	InitUiEngine();
	router_.SetActivateFn([this](AppContext &context, size_t /*index*/, const std::string &scene_id) {
		return LoadScene(context, scene_id, ++scene_load_id_);
	});

	if (router_.Activate(ctx, 0)) {
		Render(ctx);
	}
}

void CalendarScheduleApp::OnExit(AppContext &ctx) {
	(void)ctx;
	ui_ready_ = false;
	alert_dialog_visible_ = false;
	is_calendar_scene_ = false;
	calendar_view_year_ = 0;
	calendar_view_month_ = 0;
	root_ = nullptr;
	tabview_ = nullptr;
	bottom_bar_ = nullptr;
	dialog_alert_ = nullptr;
	label_alert_ = nullptr;
	button_submit_ = nullptr;
	button_cancel_ = nullptr;
	frame_calendar_ = nullptr;
	calendar_grid_ = nullptr;
	listview_task_ = nullptr;
	radio_today_ = nullptr;
	radio_this_week_ = nullptr;
	radio_all_ = nullptr;
	checkbox_todo_ = nullptr;
	checkbox_done_ = nullptr;
	checkbox_delete_ = nullptr;
	button_phone_connect_ = nullptr;
	image_schedule_qr_ = nullptr;
	label_schedule_url_ = nullptr;
	epd_ = nullptr;
	if (g_schedule_http_server) {
		httpd_stop(g_schedule_http_server);
		g_schedule_http_server = nullptr;
	}
	router_.Reset();
}

void CalendarScheduleApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
	if (!ui_ready_ || !IsClickLike(event)) {
		return;
	}

	if (is_calendar_scene_ && !alert_dialog_visible_ && event.id == AppButton::Down) {
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (HandleAlertDialogButtons(event)) {
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (HandleTabViewNav(ctx, event)) {
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (button_phone_connect_ && button_phone_connect_->Focused() &&
		(event.id == AppButton::Start || event.id == AppButton::C)) {
		EnterScheduleSettingsScene(ctx);
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (!alert_dialog_visible_ && listview_task_ && listview_task_->Focused() && event.id == AppButton::D) {
		ShowDeleteConfirmAlertForSelectedTask();
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (!alert_dialog_visible_ && listview_task_ && listview_task_->Focused() && event.id == AppButton::B) {
		if (ToggleSelectedTaskDone()) {
			UpdateBottomBarHintByFocus();
			Render(ctx);
			return;
		}
	}

	if (is_calendar_scene_ && !alert_dialog_visible_ && (event.id == AppButton::B || event.id == AppButton::D)) {
		DateInfoValue current{};
		current.year = calendar_view_year_;
		current.month = calendar_view_month_;
		current.day = 1;
		const DateInfoValue shifted = ShiftMonth(current, event.id == AppButton::B ? -1 : 1);
		calendar_view_year_ = shifted.year;
		calendar_view_month_ = shifted.month;
		RefreshCalendarData();
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (HandleFocusCycle(event)) {
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (HandleTodoFilterAction(event)) {
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	SendInputToUi(event);
	UpdateBottomBarHintByFocus();
	Render(ctx);
}

bool CalendarScheduleApp::ShouldInterceptSelectExit() const {
	return alert_dialog_visible_;
}

bool CalendarScheduleApp::LoadUi(AppContext &ctx) {
	auto scene_ids = app_ui::CollectSceneIds(*kUiDesc);
	if (scene_ids.empty()) {
		ctx.board.GetDisplay()->SetChatMessage("system", "Calendar Schedule UI: no scenes");
		return false;
	}
	router_.SetScenes(std::move(scene_ids));
	ui_ready_ = true;
	return true;
}

bool CalendarScheduleApp::LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index) {
	if (!scene_runtime_.LoadFromDesc(*kUiDesc, scene_id.c_str(), scene_index)) {
		ctx.board.GetDisplay()->SetChatMessage("system", "Calendar Schedule UI: failed to load scene");
		return false;
	}
	auto new_root = scene_runtime_.TakeRoot();
	if (!new_root) {
		ctx.board.GetDisplay()->SetChatMessage("system", "Calendar Schedule UI: failed to take scene root");
		return false;
	}

	root_ = new_root.get();
	BindWidgets(root_);
	ui_engine_.SetRoot(std::move(new_root));

	UpdateTabSelection();
	BuildFocusCycle(scene_id);
	SyncFocusCycleIndex();
	if (tabview_) {
		ui_engine_.RequestFocus(tabview_->Id());
	}
	if (scene_id == "page_main") {
		EnterCalendarScene(ctx);
	} else if (scene_id == "page_9b37") {
		EnterScheduleSettingsScene(ctx);
	} else {
		HideCalendarAlert();
	}

	if (scene_id == "page_d3f0") {
		SetTodoFilter(todo_filter_);
		RefreshTodoLists();
	}

	UpdateBottomBarHintByFocus();
	return true;
}

void CalendarScheduleApp::InitUiEngine() {
	ui_engine_.Reset();
	ui_engine_.SetEpd(epd_);
}

void CalendarScheduleApp::Render(AppContext &ctx) {
	if (!ui_ready_) {
		return;
	}
	ui_engine_.RequestRender();
	if (!epd_) {
		std::string msg = "Calendar Schedule UI\n";
		msg += "Scene: #";
		msg += std::to_string(scene_runtime_.SceneId());
		ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
	}
}

void CalendarScheduleApp::PrevScene(AppContext &ctx) {
	if (!router_.HasScenes()) {
		return;
	}
	if (router_.Prev(ctx)) {
		UpdateTabSelection();
	}
}

void CalendarScheduleApp::NextScene(AppContext &ctx) {
	if (!router_.HasScenes()) {
		return;
	}
	if (router_.Next(ctx)) {
		UpdateTabSelection();
	}
}

void CalendarScheduleApp::BindWidgets(app_ui::Widget *root) {
	if (!root) {
		tabview_ = nullptr;
		bottom_bar_ = nullptr;
		dialog_alert_ = nullptr;
		label_alert_ = nullptr;
		button_submit_ = nullptr;
		button_cancel_ = nullptr;
		frame_calendar_ = nullptr;
		calendar_grid_ = nullptr;
		listview_task_ = nullptr;
		radio_today_ = nullptr;
		radio_this_week_ = nullptr;
		radio_all_ = nullptr;
		checkbox_todo_ = nullptr;
		checkbox_done_ = nullptr;
		checkbox_delete_ = nullptr;
		button_phone_connect_ = nullptr;
		image_schedule_qr_ = nullptr;
		label_schedule_url_ = nullptr;
		return;
	}

	tabview_ = dynamic_cast<app_ui::TabViewWidget *>(root->FindById(kWidgetTabView));
	bottom_bar_ = dynamic_cast<app_ui::BottomBarWidget *>(root->FindById(kWidgetBottomBar));
	dialog_alert_ = dynamic_cast<app_ui::DialogWidget *>(root->FindById(kWidgetDialogAlert));
	label_alert_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelAlert));
	button_submit_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonSubmit));
	button_cancel_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonCancel));

	frame_calendar_ = dynamic_cast<app_ui::FrameWidget *>(root->FindById(kWidgetFrameCalendar));
	listview_task_ = dynamic_cast<app_ui::ListViewWidget *>(root->FindById(kWidgetListTask));
	radio_today_ = dynamic_cast<app_ui::RadioWidget *>(root->FindById(kWidgetRadioToday));
	radio_this_week_ = dynamic_cast<app_ui::RadioWidget *>(root->FindById(kWidgetRadioThisWeek));
	radio_all_ = dynamic_cast<app_ui::RadioWidget *>(root->FindById(kWidgetRadioAll));
	checkbox_todo_ = dynamic_cast<app_ui::CheckboxWidget *>(root->FindById(kWidgetCheckboxTodo));
	checkbox_done_ = dynamic_cast<app_ui::CheckboxWidget *>(root->FindById(kWidgetCheckboxDone));
	checkbox_delete_ = dynamic_cast<app_ui::CheckboxWidget *>(root->FindById(kWidgetCheckboxDelete));
	button_phone_connect_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonPhoneConnect));
	image_schedule_qr_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageScheduleQr));
	label_schedule_url_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelScheduleUrl));

	if (listview_task_) {
		listview_task_->SetFontName("wenquanyi_11pt");
		const int list_h = listview_task_->DeclaredRect().h;
		const int rows = std::max(1, list_h / 40);
		listview_task_->SetRows(rows);
	}
	if (checkbox_todo_) {
		checkbox_todo_->SetFontName("wenquanyi_11pt");
		checkbox_todo_->SetText("待办");
		checkbox_todo_->SetChecked(true);
	}
	if (checkbox_done_) {
		checkbox_done_->SetFontName("wenquanyi_11pt");
		checkbox_done_->SetText("完成");
	}
	if (checkbox_delete_) {
		checkbox_delete_->SetFontName("wenquanyi_11pt");
		checkbox_delete_->SetText("删除");
	}
	if (radio_all_) {
		radio_all_->SetFontName("wenquanyi_11pt");
		radio_all_->SetChecked(true);
	}
	if (radio_today_) {
		radio_today_->SetFontName("wenquanyi_11pt");
	}
	if (radio_this_week_) {
		radio_this_week_->SetFontName("wenquanyi_11pt");
	}

	if (tabview_) {
		tabview_->SetFocusable(true);
	}
	if (dialog_alert_) {
		dialog_alert_->SetVisible(false);
		dialog_alert_->SetZOrder(25);
	}
	if (label_alert_) {
		label_alert_->SetVisible(false);
		label_alert_->SetZOrder(30);
	}

	if (frame_calendar_) {
		frame_calendar_->SetVisible(false);
		calendar_grid_ = root->FindById(kWidgetCalendarGrid);
		if (!calendar_grid_) {
			auto grid = std::make_unique<CalendarGridWidget>();
			grid->SetId(kWidgetCalendarGrid);
			grid->SetRectInParent(frame_calendar_->DeclaredRect());
			grid->SetZOrder(frame_calendar_->ZOrder());
			calendar_grid_ = root->AddChild(std::move(grid));
		}
	}
}

void CalendarScheduleApp::BuildFocusCycle(const std::string &scene_id) {
	is_calendar_scene_ = (scene_id == "page_main");
	focus_cycle_ids_.clear();
	focus_cycle_index_ = 0;

	AppendIfValid(tabview_, focus_cycle_ids_);

	if (scene_id == "page_d3f0") {
		AppendIfValid(checkbox_todo_, focus_cycle_ids_);
		AppendIfValid(checkbox_done_, focus_cycle_ids_);
		AppendIfValid(checkbox_delete_, focus_cycle_ids_);
		AppendIfValid(radio_all_, focus_cycle_ids_);
		AppendIfValid(radio_today_, focus_cycle_ids_);
		AppendIfValid(radio_this_week_, focus_cycle_ids_);
		AppendIfValid(listview_task_, focus_cycle_ids_);
	}
}

void CalendarScheduleApp::SyncFocusCycleIndex() {
	if (focus_cycle_ids_.empty()) {
		focus_cycle_index_ = 0;
		return;
	}
	for (size_t i = 0; i < focus_cycle_ids_.size(); ++i) {
		const uint32_t id = focus_cycle_ids_[i];
		app_ui::Widget *widget = root_ ? root_->FindById(id) : nullptr;
		if (widget && widget->Focused()) {
			focus_cycle_index_ = static_cast<int>(i);
			return;
		}
	}
	focus_cycle_index_ = 0;
}

bool CalendarScheduleApp::HandleFocusCycle(const ButtonEvent &event) {
	if (focus_cycle_ids_.empty()) {
		return false;
	}

	const bool tab_focused = tabview_ && tabview_->Focused();
	if (tab_focused) {
		if (event.id != AppButton::Down) {
			return false;
		}
	} else {
		if (event.id == AppButton::Up) {
			ui_engine_.RequestFocus(tabview_ ? tabview_->Id() : focus_cycle_ids_[0]);
			return true;
		}
		if (listview_task_ && listview_task_->Focused() && (event.id == AppButton::Left || event.id == AppButton::Right)) {
			return false;
		}
		if (event.id != AppButton::Left && event.id != AppButton::Right) {
			return false;
		}
	}

	SyncFocusCycleIndex();
	const int count = static_cast<int>(focus_cycle_ids_.size());
	if (count <= 1) {
		return false;
	}

	if (event.id == AppButton::Left) {
		focus_cycle_index_ = (focus_cycle_index_ + count - 1) % count;
	} else {
		focus_cycle_index_ = (focus_cycle_index_ + 1) % count;
	}

	if (focus_cycle_index_ == 0 && count > 1) {
		focus_cycle_index_ = 1;
	}

	ui_engine_.RequestFocus(focus_cycle_ids_[focus_cycle_index_]);
	return true;
}

bool CalendarScheduleApp::HandleTabViewNav(AppContext &ctx, const ButtonEvent &event) {
	if (!tabview_ || !tabview_->Focused()) {
		return false;
	}
	if (event.id == AppButton::Left) {
		PrevScene(ctx);
		return true;
	}
	if (event.id == AppButton::Right) {
		NextScene(ctx);
		return true;
	}
	return false;
}

bool CalendarScheduleApp::HandleAlertDialogButtons(const ButtonEvent &event) {
	if (!alert_dialog_visible_) {
		return false;
	}

	if (event.id == AppButton::B || event.id == AppButton::Select) {
		delete_confirm_mode_ = false;
		pending_delete_rowid_ = 0;
		HideCalendarAlert();
		return true;
	}

	if (event.id == AppButton::Up || event.id == AppButton::Down || event.id == AppButton::Left ||
		event.id == AppButton::Right) {
		if (button_submit_ && button_cancel_) {
			const bool submit_focused = button_submit_->Focused();
			ui_engine_.RequestFocus(submit_focused ? button_cancel_->Id() : button_submit_->Id());
		}
		return true;
	}

	if (event.id == AppButton::Start || event.id == AppButton::C) {
		if (delete_confirm_mode_) {
			const bool submit_focused = button_submit_ && button_submit_->Focused();
			if (submit_focused && pending_delete_rowid_ > 0) {
				(void)ToggleTaskDeletedByRowId(pending_delete_rowid_);
			}
			delete_confirm_mode_ = false;
			pending_delete_rowid_ = 0;
		}
		HideCalendarAlert();
		return true;
	}
	return true;
}

bool CalendarScheduleApp::HandleTodoFilterAction(const ButtonEvent &event) {
	if (!(event.id == AppButton::Start || event.id == AppButton::C)) {
		return false;
	}

	if (checkbox_todo_ && checkbox_todo_->Focused()) {
		checkbox_todo_->SetChecked(!checkbox_todo_->Checked());
		RefreshTodoLists();
		return true;
	}
	if (checkbox_done_ && checkbox_done_->Focused()) {
		checkbox_done_->SetChecked(!checkbox_done_->Checked());
		RefreshTodoLists();
		return true;
	}
	if (checkbox_delete_ && checkbox_delete_->Focused()) {
		checkbox_delete_->SetChecked(!checkbox_delete_->Checked());
		RefreshTodoLists();
		return true;
	}

	if (radio_all_ && radio_all_->Focused()) {
		SetTodoFilter(TodoFilter::All);
		RefreshTodoLists();
		return true;
	}
	if (radio_today_ && radio_today_->Focused()) {
		SetTodoFilter(TodoFilter::Today);
		RefreshTodoLists();
		return true;
	}
	if (radio_this_week_ && radio_this_week_->Focused()) {
		SetTodoFilter(TodoFilter::ThisWeek);
		RefreshTodoLists();
		return true;
	}
	return false;
}

void CalendarScheduleApp::SendInputToUi(const ButtonEvent &event) {
	app_ui::KeyCode key{};
	if (!MapButtonToKey(event.id, key)) {
		return;
	}
	app_ui::InputEvent e;
	e.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat : app_ui::InputType::KeyDown;
	e.key = static_cast<int>(key);
	e.timestamp = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
	ui_engine_.OnInput(e);
}

void CalendarScheduleApp::UpdateTabSelection() {
	if (!tabview_) {
		return;
	}
	tabview_->SetSelectedIndex(static_cast<int>(router_.Index()));
}

void CalendarScheduleApp::SetBottomBarHint(const std::string &text) {
	bottom_bar_hint_ = text;
	if (bottom_bar_) {
		bottom_bar_->SetText(bottom_bar_hint_);
	}
}

void CalendarScheduleApp::UpdateBottomBarHintByFocus() {
	if (alert_dialog_visible_) {
		SetBottomBarHint("上下切换 Start/C确认 B取消");
		return;
	}
	if (tabview_ && tabview_->Focused()) {
		if (is_calendar_scene_) {
			SetBottomBarHint("左右切换页面 B上月 D下月");
		} else {
			SetBottomBarHint("左右切换页面 下切换焦点");
		}
		return;
	}
	if ((checkbox_todo_ && checkbox_todo_->Focused()) || (checkbox_done_ && checkbox_done_->Focused()) ||
		(checkbox_delete_ && checkbox_delete_->Focused()) || (radio_all_ && radio_all_->Focused()) ||
		(radio_today_ && radio_today_->Focused()) || (radio_this_week_ && radio_this_week_->Focused())) {
		SetBottomBarHint("Start/C选择筛选 上回到Tab");
		return;
	}
	if (listview_task_ && listview_task_->Focused()) {
		SetBottomBarHint("上下滚动 B完成切换 D删除切换");
		return;
	}
	SetBottomBarHint("");
}

void CalendarScheduleApp::EnterCalendarScene(AppContext &ctx) {
	const DateInfoValue today = GetTodayUtcDateInfo();
	calendar_view_year_ = today.year;
	calendar_view_month_ = today.month;
	RefreshCalendarData();
	if (!IsNetworkConnected()) {
		ShowCalendarAlert(kAlertNoNetwork);
	} else {
		HideCalendarAlert();
	}
	(void)ctx;
}

void CalendarScheduleApp::EnterScheduleSettingsScene(AppContext &ctx) {
	StartScheduleWebServerIfNeeded();
	std::string url = GetScheduleUrl();
	if (button_phone_connect_) {
		button_phone_connect_->SetText("手机连接设置");
	}
	if (label_schedule_url_) {
		if (url.empty()) {
			label_schedule_url_->SetText("网址: 请先连接WIFI");
		} else {
			label_schedule_url_->SetText("网址: " + url);
		}
	}
	if (image_schedule_qr_) {
		if (url.empty()) {
			image_schedule_qr_->ClearQrCode();
			image_schedule_qr_->SetText("未连接网络");
		} else {
			int qr_size = 0;
			std::vector<uint8_t> qr_modules;
			if (GenerateScheduleQr(url, qr_size, qr_modules)) {
				image_schedule_qr_->SetText("");
				image_schedule_qr_->SetQrCode(qr_size, qr_modules);
			} else {
				image_schedule_qr_->ClearQrCode();
				image_schedule_qr_->SetText("二维码生成失败");
			}
		}
	}
	(void)ctx;
}

void CalendarScheduleApp::RefreshCalendarData() {
	if (!calendar_grid_) {
		return;
	}

	auto *grid = dynamic_cast<CalendarGridWidget *>(calendar_grid_);
	if (!grid) {
		return;
	}

	const DateInfoValue today = GetTodayUtcDateInfo();
	if (calendar_view_year_ <= 0 || calendar_view_month_ <= 0) {
		calendar_view_year_ = today.year;
		calendar_view_month_ = today.month;
	}

	grid->SetHeader({calendar_view_year_, calendar_view_month_});

	const int first_weekday = WeekdayMondayFirst(calendar_view_year_, calendar_view_month_, 1);
	const int month_days = DaysInMonth(calendar_view_year_, calendar_view_month_);

	std::array<CalendarGridWidget::Cell, 42> cells{};
	for (int idx = 0; idx < 42; ++idx) {
		const int day = idx - first_weekday + 1;
		if (day < 1 || day > month_days) {
			continue;
		}

		CalendarGridWidget::Cell cell{};
		cell.valid = true;
		cell.day = day;
		cell.is_today = (calendar_view_year_ == today.year && calendar_view_month_ == today.month && day == today.day);

		const LunarDate lunar = SolarToLunar(calendar_view_year_, calendar_view_month_, day);
		std::string festival = SolarFestivalName(calendar_view_month_, day);
		if (festival.empty()) {
			festival = LunarFestivalName(lunar);
		}
		if (festival.empty()) {
			festival = SolarTermName(calendar_view_year_, calendar_view_month_, day);
		}
		if (!festival.empty()) {
			cell.second_line = festival;
		} else {
			cell.second_line = LunarDayText(lunar.month, lunar.day);
		}

		cells[idx] = std::move(cell);
	}
	grid->SetCells(std::move(cells));
}

bool CalendarScheduleApp::IsNetworkConnected() const {
	auto &wifi = WifiManager::GetInstance();
	wifi.Initialize();
	return wifi.IsConnected();
}

void CalendarScheduleApp::ShowCalendarAlert(const std::string &text) {
	alert_dialog_visible_ = true;
	if (dialog_alert_) {
		dialog_alert_->SetVisible(true);
	}
	if (label_alert_) {
		label_alert_->SetText(text);
		label_alert_->SetVisible(true);
	}
	if (button_submit_) {
		button_submit_->SetVisible(true);
	}
	if (button_cancel_) {
		button_cancel_->SetVisible(true);
	}
	if (button_submit_) {
		ui_engine_.RequestFocus(button_submit_->Id());
	}
}

void CalendarScheduleApp::HideCalendarAlert() {
	const bool restore_list_focus = delete_confirm_mode_;
	alert_dialog_visible_ = false;
	delete_confirm_mode_ = false;
	pending_delete_rowid_ = 0;
	if (dialog_alert_) {
		dialog_alert_->SetVisible(false);
	}
	if (label_alert_) {
		label_alert_->SetVisible(false);
	}
	if (button_submit_) {
		button_submit_->SetVisible(false);
	}
	if (button_cancel_) {
		button_cancel_->SetVisible(false);
	}
	if (restore_list_focus && listview_task_) {
		ui_engine_.RequestFocus(listview_task_->Id());
	} else if (tabview_) {
		ui_engine_.RequestFocus(tabview_->Id());
	}
}

void CalendarScheduleApp::ShowDeleteConfirmAlertForSelectedTask() {
	if (!listview_task_) {
		return;
	}
	const int idx = listview_task_->SelectedIndex();
	if (idx < 0 || idx >= static_cast<int>(visible_tasks_.size())) {
		return;
	}
	const TaskEntry &task = visible_tasks_[idx];
	if (task.rowid <= 0) {
		return;
	}
	pending_delete_rowid_ = task.rowid;
	delete_confirm_mode_ = true;
	ShowCalendarAlert("确认删除该待办事项？");
}

bool CalendarScheduleApp::ToggleSelectedTaskDone() {
	if (!listview_task_) {
		return false;
	}
	const int idx = listview_task_->SelectedIndex();
	if (idx < 0 || idx >= static_cast<int>(visible_tasks_.size())) {
		return false;
	}
	const TaskEntry &task = visible_tasks_[idx];
	if (task.rowid <= 0) {
		return false;
	}
	const bool next_done = !task.done;
	if (!UpdateTaskFlagByRowId(task.rowid, "done", next_done ? 1 : 0)) {
		return false;
	}
	RefreshTodoLists();
	return true;
}

bool CalendarScheduleApp::ToggleTaskDeletedByRowId(int rowid) {
	if (rowid <= 0) {
		return false;
	}
	for (const auto &task : visible_tasks_) {
		if (task.rowid == rowid) {
			const bool next_deleted = !task.deleted;
			if (!UpdateTaskFlagByRowId(task.rowid, "delete", next_deleted ? 1 : 0)) {
				return false;
			}
			RefreshTodoLists();
			return true;
		}
	}
	return false;
}

void CalendarScheduleApp::SetTodoFilter(TodoFilter filter) {
	todo_filter_ = filter;
	UpdateTodoFilterChecks();
}

void CalendarScheduleApp::UpdateTodoFilterChecks() {
	if (radio_all_) {
		radio_all_->SetChecked(todo_filter_ == TodoFilter::All);
	}
	if (radio_today_) {
		radio_today_->SetChecked(todo_filter_ == TodoFilter::Today);
	}
	if (radio_this_week_) {
		radio_this_week_->SetChecked(todo_filter_ == TodoFilter::ThisWeek);
	}
}

void CalendarScheduleApp::RefreshTodoLists() {
	if (!listview_task_) {
		return;
	}

	std::vector<TaskEntry> tasks = QueryTasks();
	std::vector<std::string> list_items;
	visible_tasks_.clear();
	const bool show_todo = checkbox_todo_ ? checkbox_todo_->Checked() : true;
	const bool show_done = checkbox_done_ ? checkbox_done_->Checked() : true;
	const bool show_delete = checkbox_delete_ ? checkbox_delete_->Checked() : false;

	for (const auto &task : tasks) {
		if (task.deleted) {
			if (!show_delete) {
				continue;
			}
		} else if (task.done) {
			if (!show_done) {
				continue;
			}
		} else {
			if (!show_todo) {
				continue;
			}
		}

		std::string marker = "[[TODO]]";
		if (task.deleted) {
			marker = "[[DELETED]]";
		} else if (task.done) {
			marker = "[[DONE]]";
		}

		std::string line = marker + " ";
		const std::string content = NormalizeTaskDisplayText(task.content);
		line += content.empty() ? "(空任务)" : content;
		if (!task.priority.empty()) {
			line += " [" + task.priority + "]";
		}
		list_items.push_back(std::move(line));
		visible_tasks_.push_back(task);
	}

	if (list_items.empty()) {
		if (show_todo || show_done || show_delete) {
			list_items.emplace_back("暂无匹配事项");
		} else {
			list_items.emplace_back("");
		}
	}

	listview_task_->SetItems(std::move(list_items));
}

std::vector<CalendarScheduleApp::TaskEntry> CalendarScheduleApp::QueryTasks() const {
	std::vector<TaskEntry> out;
	std::lock_guard<std::recursive_mutex> lock(g_task_db_mutex);

	if (!EnsureSqliteRuntimeReady() || !EnsureSqliteSdMounted()) {
		return out;
	}

	const std::string db_path = DiscoverDataDbPath();
	if (db_path.empty()) {
		ESP_LOGW(kTag, "Data.db not found");
		return out;
	}

	sqlite3 *db = nullptr;
	int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
	if (rc != SQLITE_OK || !db) {
		ESP_LOGW(kTag, "open Data.db failed rc=%d", rc);
		if (db) {
			sqlite3_close(db);
		}
		return out;
	}

	const char *sql =
		"SELECT rowid, content, done, IFNULL(\"delete\", 0), date, priority FROM task ORDER BY date ASC, starttime ASC;";
	sqlite3_stmt *stmt = nullptr;
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK || !stmt) {
		ESP_LOGW(kTag, "prepare task query failed rc=%d", rc);
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
		return out;
	}

	const DateInfoValue today = GetTodayDateInfo();
	std::tm today_tm{};
	today_tm.tm_year = today.year - 1900;
	today_tm.tm_mon = today.month - 1;
	today_tm.tm_mday = today.day;
	today_tm.tm_hour = 12;
	std::mktime(&today_tm);
	const int monday_offset = 1 - ((today_tm.tm_wday == 0) ? 7 : today_tm.tm_wday);
	const DateInfoValue monday = ShiftDays(today, monday_offset);
	const DateInfoValue sunday = ShiftDays(monday, 6);
	const int today_key = ToYmdInt(today);
	const int monday_key = ToYmdInt(monday);
	const int sunday_key = ToYmdInt(sunday);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const int rowid = sqlite3_column_int(stmt, 0);
		const char *content_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		const char *done_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
		const char *deleted_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
		const char *date_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
		const char *priority_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
		const int done_int = sqlite3_column_int(stmt, 2);
		const int deleted_int = sqlite3_column_int(stmt, 3);

		TaskEntry task{};
		task.rowid = rowid;
		task.content = content_ptr ? std::string(content_ptr) : std::string();
		task.date = date_ptr ? std::string(date_ptr) : std::string();
		task.priority = priority_ptr ? std::string(priority_ptr) : std::string();
		task.done = IsDoneValue(done_ptr, done_int);
		task.deleted = IsDeletedValue(deleted_ptr, deleted_int);

		bool keep = true;
		if (todo_filter_ != TodoFilter::All) {
			DateInfoValue task_date{};
			if (!ParseDateYmd(task.date, task_date)) {
				keep = false;
			} else {
				const int task_key = ToYmdInt(task_date);
				if (todo_filter_ == TodoFilter::Today) {
					keep = (task_key == today_key);
				} else if (todo_filter_ == TodoFilter::ThisWeek) {
					keep = (task_key >= monday_key && task_key <= sunday_key);
				}
			}
		}

		if (keep) {
			out.push_back(std::move(task));
		}
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return out;
}

bool CalendarScheduleApp::IsClickLike(const ButtonEvent &event) {
	return event.action == ButtonAction::Click || event.action == ButtonAction::PressDown ||
		   event.action == ButtonAction::LongPress;
}

bool CalendarScheduleApp::MapButtonToKey(AppButton button, app_ui::KeyCode &out) {
	switch (button) {
		case AppButton::Up:
			out = app_ui::KeyCode::Up;
			return true;
		case AppButton::Down:
			out = app_ui::KeyCode::Down;
			return true;
		case AppButton::Left:
			out = app_ui::KeyCode::Left;
			return true;
		case AppButton::Right:
			out = app_ui::KeyCode::Right;
			return true;
		case AppButton::A:
			out = app_ui::KeyCode::A;
			return true;
		case AppButton::B:
			out = app_ui::KeyCode::B;
			return true;
		case AppButton::C:
			out = app_ui::KeyCode::C;
			return true;
		case AppButton::D:
			out = app_ui::KeyCode::D;
			return true;
		case AppButton::Select:
			out = app_ui::KeyCode::Select;
			return true;
		case AppButton::Start:
			out = app_ui::KeyCode::Start;
			return true;
		case AppButton::VolumeUp:
			out = app_ui::KeyCode::VolumeUp;
			return true;
		case AppButton::VolumeDown:
			out = app_ui::KeyCode::VolumeDown;
			return true;
		default:
			return false;
	}
}

std::unique_ptr<AppBase> MakeCalendarScheduleApp() {
	return std::make_unique<CalendarScheduleApp>();
}
