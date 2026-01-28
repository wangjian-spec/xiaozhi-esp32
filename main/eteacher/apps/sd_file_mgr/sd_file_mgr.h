#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <esp_http_server.h>

#include "app_manager/app_base.h"

class CustomEpdDisplay;

class SdFileMgrApp : public AppBase {
public:
	SdFileMgrApp();

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;
	void OnTick(AppContext &ctx) override;

private:
	struct Entry {
		std::string name;
		bool is_dir = false;
		uint64_t size = 0;
	};

	enum class View {
		kModeSelect,
		kConnecting,
		kBrowser,
	};

	enum class NetMode {
		kNone,
		kAp,
		kSta,
	};

	void Render(AppContext &ctx);
	void RenderModeSelect(AppContext &ctx);
	void RenderConnecting(AppContext &ctx);
	void RenderBrowser(AppContext &ctx);

	void EnterMode(AppContext &ctx, NetMode mode);
	void UpdateConnectionInfo();
	void StartServer();
	void StopServer();

	bool LoadEntries(AppContext &ctx);
	bool ListEntries(const std::string &path, std::vector<Entry> *out, std::string *err);
	bool DeletePath(const std::string &path, std::string *err);
	bool CopyFile(const std::string &src, const std::string &dst, std::string *err);
	bool WriteFileFromRequest(httpd_req_t *req, const std::string &raw_path, std::string *err);
	bool ReadFileToResponse(httpd_req_t *req, const std::string &path, std::string *err);
	void EnterFolder(AppContext &ctx);
	void ExitFolder(AppContext &ctx);
	void MoveSelection(int delta_row, int delta_col);
	void EnsureSelectionVisible(CustomEpdDisplay *epd);
	void ResetSelection();

	static SdFileMgrApp *GetApp(httpd_req_t *req);
	static esp_err_t HandleIndex(httpd_req_t *req);
	static esp_err_t HandleList(httpd_req_t *req);
	static esp_err_t HandleDownload(httpd_req_t *req);
	static esp_err_t HandleUpload(httpd_req_t *req);
	static esp_err_t HandleMkdir(httpd_req_t *req);
	static esp_err_t HandleDelete(httpd_req_t *req);
	static esp_err_t HandleCopy(httpd_req_t *req);

	View view_ = View::kModeSelect;
	NetMode mode_ = NetMode::kNone;
	int mode_selected_ = 0;

	std::vector<Entry> entries_;
	int selected_index_ = 0;
	int page_offset_ = 0;
	int columns_ = 2;
	std::string current_path_ = "/";

	std::string status_line_;
	std::string url_;
	std::string ssid_;
	bool active_ = false;
	uint32_t tick_accum_ms_ = 0;

	httpd_handle_t server_ = nullptr;
	Board *board_ = nullptr;
	std::mutex sd_mutex_;

	static constexpr int kHttpPort = 8082;
};

std::unique_ptr<AppBase> MakeSdFileMgrApp();
