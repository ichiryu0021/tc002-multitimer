// 页面宿主（v3）：页表、当前页、翻页、切页横幅、覆盖层、状态快照、单帧时间源。
//
// 责任边界：
// - UI 线程：tick() 每帧只取一次 time(NULL) -> TimerEngine::evaluate(now) ->
//   PageRenderer::renderPage，输出 52x16 Surface；脏帧才返回 true。
// - 按键/HTTP 线程：nextPage/prevPage/goToPage/setConfig/showOverlay 只改状态
//   + 置脏标记，不渲染、不发 SPI、不做网络/存储 I/O。
// - 不在锁内调用 LiveService 网络：live 快照在 PageHost 解锁后取；
//   LiveService 不回调 PageHost（锁顺序固定为 PageHost -> LiveService，绝不反向）。
// - 当前页持久化：StoragePreferences["uiState"] = "id,index,epoch"，
//   稳定 >=30s 且距上次写 >=120s 才落盘（设计 §4.2）。
//
// 注册名仍是 "TimerPage"（PageManager 只认识 TimerPage，TimerPage 委托本类）。
#ifndef PAGES_PAGEHOST_H_
#define PAGES_PAGEHOST_H_

#include <stdint.h>

#include <mutex>
#include <string>

#include "live/LiveTypes.h"
#include "pages/PageTypes.h"
#include "timer/TimeMath.h"
#include "timer/TimerEngine.h"
#include "timer/TimerTypes.h"
#include "utils/Surface.h"

// 页面计时状态（v2 语义，供 /api/status 使用）
enum PageMode {
	PAGE_MODE_INVALID = 0,
	PAGE_MODE_CLOCK = 1,
	PAGE_MODE_COUNTDOWN = 2,
	PAGE_MODE_COUNTUP = 3
};

// 供 HTTP /api/status、/api/config 使用的状态快照（互斥锁保护，整份拷贝）
struct PageStatus {
	// v2 字段（含义不变）
	bool timeValid;
	PageMode mode;
	std::string text;         // 该页主文本（时钟/倒计时/--:--:--；live 页为 "LIVE"）
	std::string text2;        // v3：分屏下半 / live 指标名；其余为空
	long long remainSeconds;
	long long elapsedSeconds; // v3：正计时已过秒数
	long long targetEpoch;
	int targetIndex;
	bool warning;
	bool smallFont;
	bool overlay;             // 当前是否被 IP/NO WIFI 覆盖
	std::string overlayText;
	long long nowUtc;
	int tzOffsetMinutes;
	LocalDateTime localNow;
	LocalDateTime targetLocal;  // 仅倒计时有效
	bool hasTarget;
	bool banner;              // 切页横幅是否正在显示

	// v3 页面信息
	std::string pageId;
	int pageIndex;
	int pageCount;
	std::string pageType;     // "clock"/"clock_countdown"/"countdown"/"live"
	std::string pageTitle;    // title 优先，空则 shortTitle/类型名（仅展示用）

	PageStatus();
};

class PageHost {
public:
	PageHost();
	~PageHost();

	// 热应用配置：换配置 + 下标 clamp（当前页 id 仍存在则保持该页）。
	// 首次调用前应先 restoreUiState()（恢复“最后停留页”）。
	void setConfig(const TimerConfig& config);

	// 读取 StoragePreferences["uiState"]（启动时、setConfig 之前调用）
	void restoreUiState();
	// 删除 uiState（POST /api/page {"reset":true}）
	void resetUiState();

	// 覆盖层：IP / NO WIFI / AP ...，最高优先级；durationMs 到期自动恢复
	void showOverlay(const std::string& text, uint32_t rgb,
			uint32_t durationMs);

	// 强制下一帧重绘
	void invalidate();

	// UI 线程（250ms）：返回 true 表示 surface 已更新，需要 sendLedData
	bool tick(long long monotonicMs, Surface& surface);

	// 翻页：跳过 enabled=false，默认循环；只在页面真正改变时返回 true
	bool nextPage();
	bool prevPage();
	bool goToPage(int index);       // 显式跳转（不过滤 enabled）
	bool goToPageId(const std::string& id);
	int currentPageIndex() const;
	int pageCount() const;

	// 旋钮按键：当前页是 live 时触发一次立即刷新（异步，不阻塞）
	bool requestLiveRefresh();

	// PageManager::onKeyEvent 转发（锁内只改状态，不渲染）
	bool onKeyEvent(int keyCode, int keyStatus);

	bool getStatus(PageStatus& out) const;

	static const int kBannerDurationMs = 1200;
	static const int kStablePersistMs = 30000;
	static const int kMinPersistIntervalMs = 120000;

private:
	PageHost(const PageHost&);
	PageHost& operator=(const PageHost&);

	bool stepPage(int delta);
	int clampIndexLocked(int index) const;
	int firstEnabledFromLocked(int start) const;
	int resolveRestoreIndexLocked() const;
	int resolveConfigIndexLocked(const std::string& keepId,
			int keepIndex) const;
	void notePageChangedLocked(int index, long long monotonicMs);
	void updatePageInfoLocked(int index);
	void persistTick(long long monotonicMs, long long nowUtc);
	void updateStatusLocked(const PageConfig& page, int index, int count,
			const Stage& stage, long long nowUtc, bool timeValid,
			bool overlayActive, const std::string& overlayText,
			bool bannerActive);

	TimerEngine mEngine;
	TimerConfig mConfig;
	mutable std::mutex mMutex;  // 保护以下全部成员（锁内不做 I/O）
	int mPageIndex;
	unsigned long mConfigGeneration;
	bool mHasConfig;
	bool mForceDirty;           // 下一帧必画
	uint32_t mLastSignature;    // 脏帧判定（时间/页面/横幅/覆盖层/live 的哈希）

	long long mBannerUntilMs;   // CLOCK_MONOTONIC
	int mBannerIndex;

	std::string mOverlayText;
	uint32_t mOverlayColor;
	long long mOverlayStartMs;   // CLOCK_MONOTONIC，覆盖层开始时间（滚动用）
	long long mOverlayExpireMs; // CLOCK_MONOTONIC，0 表示无覆盖

	PageStatus mStatus;

	// uiState 恢复
	bool mRestorePending;
	std::string mRestoreId;
	int mRestoreIndex;

	// uiState 落盘节流
	int mPendingPersistIndex;
	long long mPendingSinceMs;
	long long mLastPersistMs;
	int mLastPersistedIndex;
};

#endif  // PAGES_PAGEHOST_H_
