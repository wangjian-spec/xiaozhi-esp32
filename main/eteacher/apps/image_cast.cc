#include "eteacher/apps/image_cast.h"

#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>

#include <qrcode.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <string>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

namespace {

static const char *TAG = "ImageCastApp";

constexpr int kHttpPort = 8080;
constexpr int kW = 400;
constexpr int kH = 300;
constexpr int kStride = (kW + 7) / 8;
constexpr int kBitmapBytes = kStride * kH;

static const char kIndexHtml[] = R"HTML(
<!doctype html>
<html lang="zh">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>小智墨水屏投屏</title>
  <style>
    body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial; margin:16px;}
    .row{display:flex; gap:12px; flex-wrap:wrap; align-items:flex-start;}
    canvas{border:1px solid #ccc; image-rendering:pixelated;}
    button{padding:10px 14px;}
    label{display:inline-flex; gap:8px; align-items:center;}
    .hint{color:#666; font-size:12px;}
    .status{white-space:pre-wrap; font-size:13px;}
  </style>
</head>
<body>
  <h2>小智墨水屏投屏</h2>
  <div class="row">
    <div>
      <input id="file" type="file" accept="image/*" />
      <div style="margin-top:10px; display:flex; gap:12px; flex-wrap:wrap;">
        <label><input id="cover" type="checkbox" checked /> 裁切填充（cover）</label>
        <label><input id="invert" type="checkbox" /> 反色</label>
      </div>
      <div style="margin-top:10px; display:flex; gap:10px;">
        <button id="send" disabled>发送到墨水屏</button>
        <button id="clear">清空预览</button>
      </div>
      <div class="hint" style="margin-top:10px;">提示：图像会被缩放到 400x300，并进行抖动（二值化）以适配黑白墨水屏。</div>
      <div id="status" class="status" style="margin-top:10px;"></div>
    </div>
    <div>
      <div class="hint">预览（400x300）</div>
      <canvas id="cv" width="400" height="300"></canvas>
    </div>
  </div>

<script>
const W=400,H=300;
const fileEl=document.getElementById('file');
const sendEl=document.getElementById('send');
const clearEl=document.getElementById('clear');
const coverEl=document.getElementById('cover');
const invertEl=document.getElementById('invert');
const statusEl=document.getElementById('status');
const cv=document.getElementById('cv');
const ctx=cv.getContext('2d', {willReadFrequently:true});
let loaded=false;

function setStatus(s){ statusEl.textContent = s; }
function clearCanvas(){ ctx.fillStyle='#fff'; ctx.fillRect(0,0,W,H); loaded=false; sendEl.disabled=true; setStatus(''); }
clearCanvas();

function drawImageToCanvas(img){
  ctx.fillStyle='#fff'; ctx.fillRect(0,0,W,H);
  const iw=img.naturalWidth, ih=img.naturalHeight;
  const scale = coverEl.checked ? Math.max(W/iw, H/ih) : Math.min(W/iw, H/ih);
  const dw = iw*scale, dh = ih*scale;
  const dx = (W - dw)/2, dy = (H - dh)/2;
  ctx.drawImage(img, dx, dy, dw, dh);
  loaded=true;
  sendEl.disabled=false;
}

fileEl.addEventListener('change', async () => {
  const f=fileEl.files && fileEl.files[0];
  if(!f){ return; }
  const url=URL.createObjectURL(f);
  const img=new Image();
  img.onload=()=>{ drawImageToCanvas(img); URL.revokeObjectURL(url); };
  img.onerror=()=>{ setStatus('加载图片失败'); URL.revokeObjectURL(url); };
  img.src=url;
});

clearEl.addEventListener('click', ()=>{ clearCanvas(); });

function ditherTo1bpp(){
  const im=ctx.getImageData(0,0,W,H);
  const data=im.data;
  // error buffer for grayscale; float32 is fine for 400x300
  const err = new Float32Array(W*H);
  const out = new Uint8Array((W/8)*H);

  function idx(x,y){ return y*W + x; }

  for(let y=0;y<H;y++){
    for(let x=0;x<W;x++){
      const i = (y*W + x)*4;
      const r=data[i], g=data[i+1], b=data[i+2];
      let gray = 0.299*r + 0.587*g + 0.114*b;
      if(invertEl.checked){ gray = 255 - gray; }
      gray += err[idx(x,y)];
      const newv = (gray < 128) ? 0 : 255;
      const e = gray - newv;

      // newv==0 => black pixel
      if(newv === 0){
        const byteIndex = y*(W/8) + (x>>3);
        out[byteIndex] |= (0x80 >> (x & 7));
      }

      // Floyd–Steinberg diffusion
      if(x+1 < W) err[idx(x+1,y)] += e * (7/16);
      if(y+1 < H){
        if(x > 0)   err[idx(x-1,y+1)] += e * (3/16);
        err[idx(x,y+1)] += e * (5/16);
        if(x+1 < W) err[idx(x+1,y+1)] += e * (1/16);
      }
    }
  }
  return out;
}

sendEl.addEventListener('click', async () => {
  if(!loaded){ return; }
  try{
    setStatus('处理中...');
    const payload = ditherTo1bpp();
    setStatus('上传中...');
    const resp = await fetch('/bitmap', {
      method:'POST',
      headers:{'Content-Type':'application/octet-stream'},
      body: payload
    });
    const text = await resp.text();
    setStatus((resp.ok?'成功: ':'失败: ') + text);
  }catch(e){
    setStatus('异常: ' + (e && e.message ? e.message : String(e)));
  }
});
</script>
</body>
</html>
)HTML";

struct QrDrawCtx {
    Adafruit_GFX *gfx;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

static QrDrawCtx *g_qr_draw_ctx = nullptr;

class QrDrawCtxGuard {
public:
    explicit QrDrawCtxGuard(QrDrawCtx *ctx) { g_qr_draw_ctx = ctx; }
    ~QrDrawCtxGuard() { g_qr_draw_ctx = nullptr; }
    QrDrawCtxGuard(const QrDrawCtxGuard &) = delete;
    QrDrawCtxGuard &operator=(const QrDrawCtxGuard &) = delete;
};

void DrawQrToGfx(esp_qrcode_handle_t qrcode)
{
    auto *d = g_qr_draw_ctx;
    if (!d || !d->gfx)
    {
        return;
    }

    const int size = esp_qrcode_get_size(qrcode);
    const int border = 2;
    const int total = size + border * 2;

    int scale = std::min(d->w / total, d->h / total);
    if (scale < 1)
    {
        scale = 1;
    }

    const int qr_w = total * scale;
    const int qr_h = total * scale;
    const int start_x = d->x + (d->w - qr_w) / 2;
    const int start_y = d->y + (d->h - qr_h) / 2;

    d->gfx->fillRect(start_x, start_y, qr_w, qr_h, GxEPD_WHITE);

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            if (esp_qrcode_get_module(qrcode, x, y))
            {
                const int px = start_x + (x + border) * scale;
                const int py = start_y + (y + border) * scale;
                d->gfx->fillRect(px, py, scale, scale, GxEPD_BLACK);
            }
        }
    }
}

bool DrawQr(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, const std::string &text)
{
    QrDrawCtx qctx{.gfx = &gfx, .x = x, .y = y, .w = w, .h = h};
    QrDrawCtxGuard guard(&qctx);

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = &DrawQrToGfx;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;

    esp_err_t err = esp_qrcode_generate(&cfg, text.c_str());
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_qrcode_generate failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

struct BitmapDrawCtx {
    CustomEpdDisplay *epd;
    uint8_t *data;
};

void DrawBitmapCb(Adafruit_GFX &gfx, void *ctx)
{
    auto *b = static_cast<BitmapDrawCtx *>(ctx);
    if (!b || !b->epd || !b->data)
    {
        return;
    }

    gfx.fillScreen(GxEPD_WHITE);
    gfx.drawBitmap(0, 0, b->data, kW, kH, GxEPD_BLACK);
}

void DeleteBitmapCtx(void *ctx)
{
    auto *b = static_cast<BitmapDrawCtx *>(ctx);
    if (b)
    {
        free(b->data);
        delete b;
    }
}

class ImageCastApp : public AppBase {
public:
    MenuMeta GetMenuMeta() const override { return MenuMeta{"image_cast", "Image Cast", "WiFi photo -> EPD"}; }

    void OnEnter(AppContext &ctx) override
    {
        EnsureWifiMode();
        StartServer();
        Render(ctx);
    }

    void OnExit(AppContext &ctx) override
    {
        (void)ctx;
        StopServer();
    }

    void OnButton(AppContext &ctx, const ButtonEvent &event) override
    {
        if (event.id == AppButton::Start)
        {
            EnsureWifiMode();
            Render(ctx);
        }
    }

private:
    enum class WifiMode {
        Unknown,
        Station,
        ConfigAp,
    };

    httpd_handle_t server_ = nullptr;
    WifiMode wifi_mode_ = WifiMode::Unknown;
    std::string ssid_;
    std::string url_;

    static esp_err_t HandleIndex(httpd_req_t *req)
    {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    static esp_err_t HandleBitmap(httpd_req_t *req)
    {
        auto *self = static_cast<ImageCastApp *>(req->user_ctx);
        if (!self)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ctx");
            return ESP_FAIL;
        }

        if (req->content_len != kBitmapBytes)
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "bad len: %d, expect %d", (int)req->content_len, kBitmapBytes);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
            return ESP_FAIL;
        }

        uint8_t *buf = static_cast<uint8_t *>(malloc(kBitmapBytes));
        if (!buf)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }

        int received = 0;
        while (received < kBitmapBytes)
        {
            int r = httpd_req_recv(req, reinterpret_cast<char *>(buf + received), kBitmapBytes - received);
            if (r <= 0)
            {
                free(buf);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
                return ESP_FAIL;
            }
            received += r;
        }

        auto *epd = dynamic_cast<CustomEpdDisplay *>(Board::GetInstance().GetDisplay());
        if (!epd)
        {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no epd");
            return ESP_FAIL;
        }

        auto *draw = new BitmapDrawCtx();
        draw->epd = epd;
        draw->data = buf;

        bool ok = EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kFast,
            &DrawBitmapCb,
            draw,
            &DeleteBitmapCtx,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));

        if (!ok)
        {
            DeleteBitmapCtx(draw);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schedule failed");
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    void EnsureWifiMode()
    {
        auto &wifi = WifiManager::GetInstance();
        (void)wifi.Initialize();

        if (wifi.IsConnected())
        {
            wifi_mode_ = WifiMode::Station;
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
        wifi_mode_ = WifiMode::ConfigAp;
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
            ESP_LOGW(TAG, "httpd_start failed: %s", esp_err_to_name(err));
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

        httpd_uri_t bitmap = {
            .uri = "/bitmap",
            .method = HTTP_POST,
            .handler = &HandleBitmap,
            .user_ctx = this,
        };
        httpd_register_uri_handler(server_, &bitmap);

        ESP_LOGI(TAG, "ImageCast server started on port %d", kHttpPort);
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

    void Render(AppContext &ctx)
    {
        auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
        if (!epd)
        {
            ctx.board.GetDisplay()->SetChatMessage("system", "ImageCast: no EPD display");
            return;
        }

        struct UiDrawCtx {
            CustomEpdDisplay *epd;
            WifiMode mode;
            std::string ssid;
            std::string url;
        };

        auto *u = new UiDrawCtx();
        u->epd = epd;
        u->mode = wifi_mode_;
        u->ssid = ssid_;
        u->url = url_;

        auto draw = [](Adafruit_GFX &gfx, void *p) {
            auto *u = static_cast<UiDrawCtx *>(p);
            if (!u || !u->epd)
            {
                return;
            }

            gfx.fillScreen(GxEPD_WHITE);
            const int16_t x = 8;
            int16_t y = 20;
            u->epd->DrawUtf8(x, y, "Image Cast", "wenquanyi_11pt", GxEPD_BLACK);
            y += 20;

            if (u->mode == WifiMode::Station)
            {
                std::string line1 = "WiFi: " + u->ssid;
                u->epd->DrawUtf8(x, y, line1, "wenquanyi_11pt", GxEPD_BLACK);
                y += 18;
                u->epd->DrawUtf8(x, y, "同一局域网打开:", "wenquanyi_11pt", GxEPD_BLACK);
                y += 18;
                u->epd->DrawUtf8(x, y, u->url.empty() ? "(IP 未获取)" : u->url, "wenquanyi_11pt", GxEPD_BLACK);

                if (!u->url.empty())
                {
                    DrawQr(gfx, 200, 70, 190, 190, u->url);
                }

                u->epd->DrawUtf8(x, 290, "按 Select 刷新状态", "wenquanyi_11pt", GxEPD_BLACK);
                return;
            }

            // Config AP
            u->epd->DrawUtf8(x, y, "1) 扫码连接热点", "wenquanyi_11pt", GxEPD_BLACK);
            y += 18;
            std::string ap = "SSID: " + u->ssid;
            u->epd->DrawUtf8(x, y, ap, "wenquanyi_11pt", GxEPD_BLACK);
            y += 18;
            u->epd->DrawUtf8(x, y, "2) 浏览器打开", "wenquanyi_11pt", GxEPD_BLACK);
            y += 18;
            u->epd->DrawUtf8(x, y, u->url.empty() ? "(URL 未获取)" : u->url, "wenquanyi_11pt", GxEPD_BLACK);

            const std::string wifi_qr = std::string("WIFI:T:nopass;S:") + u->ssid + ";;";
            DrawQr(gfx, 10, 120, 180, 170, wifi_qr);
            if (!u->url.empty())
            {
                DrawQr(gfx, 210, 120, 180, 170, u->url);
            }

            u->epd->DrawUtf8(8, 112, "WiFi", "wenquanyi_11pt", GxEPD_BLACK);
            u->epd->DrawUtf8(208, 112, "URL", "wenquanyi_11pt", GxEPD_BLACK);
            u->epd->DrawUtf8(8, 290, "按 Select 刷新状态", "wenquanyi_11pt", GxEPD_BLACK);
        };

        auto del = [](void *p) { delete static_cast<UiDrawCtx *>(p); };

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            draw,
            u,
            del,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
    }
};

} // namespace

std::unique_ptr<AppBase> MakeImageCastApp()
{
    return std::make_unique<ImageCastApp>();
}
