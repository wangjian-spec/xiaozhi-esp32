
# EpdManager（墨水屏刷新管理器）

`EpdManager` 用于统一管理 EnglishTeacher 墨水屏（EPD）的刷新请求，避免各个 app 直接阻塞式调用 EPD 刷新接口导致：

- 刷新频率过高（残影/闪烁/功耗上升/驱动异常）
- 多个业务同时刷新造成互相抢占（显示撕裂/锁竞争）
- 局刷次数过多需要周期性“强制快刷/全刷”清理残影

它的核心思路是：

1. 把“刷新”变成任务投递（queue）
2. 由后台 FreeRTOS 任务串行执行刷新
3. 根据任务类型做最小刷新间隔限制（限频）
4. 对局刷计数，达到阈值后把“局刷”升级为“快刷”

---

## 目录结构

- `epd_manager.h`：`EpdManager` 对外 API
- `epd_manager.cc`：队列、后台任务、限频与强制升级逻辑

---

## 初始化

`EpdManager` 需要在 EPD 初始化成功后绑定 `CustomEpdDisplay*`。

当前接入点位于板级初始化流程：

- `EnglishTeacherBoard::InitializeEpd()` 在 `display_.Begin(...)` 成功后调用 `EpdManager::GetInstance().Init(&display_)`

注意：

- `Init()` 设计为“只初始化一次”，重复调用会被忽略。

---

## 任务类型

`EpdManager::TaskType`：

- `kPartial`：局刷（使用 `Rect` 指定局部窗口）
- `kFast`：快刷（逻辑上仍是全屏刷新，但走“更宽松/更适合频繁刷新的策略”，实现上使用全屏 partial window）
- `kFull`：全刷（全屏 full window）

说明：

- “快刷/全刷”的具体波形行为取决于底层驱动与面板支持；EpdManager 负责做调度和限频，不改变驱动内部实现。

---

## 刷新回调（Schedule）

对外主要接口：

```cpp
bool Schedule(TaskType type,
							Epd::DrawCallback cb,
							void* ctx = nullptr,
							void (*ctx_deleter)(void*) = nullptr,
							Rect rect = Rect());
```

### 回调约定

- `cb(Adafruit_GFX& gfx, void* ctx)` 负责“绘制完整画面内容”。
- 不要在回调里调用 `firstPage()/nextPage()`；这些由 `EpdManager` 在内部统一封装。
- 注意：因为内部使用 `firstPage()/nextPage()` 分页绘制，`cb` 可能会被调用多次（每页一次），因此回调应保持幂等、不要做有副作用的操作。

### ctx 生命周期

- `ctx` 会原样传给回调
- 若提供 `ctx_deleter`，则任务执行结束后（在 EpdManager 任务线程中）调用它释放 `ctx`
- 若队列满导致丢弃旧任务，旧任务的 `ctx_deleter` 也会被调用，避免内存泄漏

---

## 刷新限频（最小刷新间隔）

不同任务类型有最小刷新间隔限制：

- 局刷：0.3 秒（300ms）
- 快刷：1.5 秒
- 全刷：3.0 秒

如果任务到达时距上一次同类型刷新不足最小间隔，EpdManager 会在后台任务中 `vTaskDelay()` 等待到允许的最早时间再执行。

---

## 局刷计数：N 次后强制快刷

为了抑制局刷累积残影，EpdManager 会统计“自上一次快刷/全刷后”的局刷次数：

- 当局刷计数达到 `N` 后，即使这次收到的任务类型是 `kPartial`，也会被强制升级为 `kFast` 执行

可通过如下接口配置阈值：

```cpp
EpdManager::GetInstance().SetPartialForceFastEveryN(N);
```

默认值：`N = 10`。

---

## 队列策略

- 队列长度：8
- 当队列已满：会丢弃“最旧的一条任务”，再插入新任务（避免 UI 卡死、也避免无限堆积导致延迟过大）

---

## 使用示例

### 1) 局刷：只更新一个矩形区域

```cpp
struct MyCtx {
	const char* text;
};

static void DrawPartial(Adafruit_GFX& gfx, void* p) {
	auto* ctx = static_cast<MyCtx*>(p);
	gfx.setCursor(0, 16);
	gfx.setTextColor(GxEPD_BLACK);
	gfx.print(ctx->text);
}

static void DeleteMyCtx(void* p) {
	delete static_cast<MyCtx*>(p);
}

auto* ctx = new MyCtx{"hello"};
EpdManager::GetInstance().Schedule(
	EpdManager::TaskType::kPartial,
	&DrawPartial,
	ctx,
	&DeleteMyCtx,
	EpdManager::Rect(0, 0, 200, 40)
);
```

### 2) 全刷：绘制全屏

```cpp
static void DrawFull(Adafruit_GFX& gfx, void*) {
	gfx.setCursor(8, 24);
	gfx.setTextColor(GxEPD_BLACK);
	gfx.print("Menu");
}

EpdManager::GetInstance().Schedule(EpdManager::TaskType::kFull, &DrawFull);
```

---

## 注意事项

- EPD 的绘制/刷新属于慢操作；建议业务侧只做“投递刷新”，不要在业务线程里直接刷屏。
- `Rect` 建议保证在屏幕范围内，宽高为正数。
- 若你发现某些场景需要合并刷新（coalesce）或更细粒度的优先级策略，可以在后续迭代里在队列层扩展。

