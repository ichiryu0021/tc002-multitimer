#include "pages/PageHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <string>

#include "base/log.h"
#include "live/LiveFormat.h"
#include "live/LiveService.h"
#include "managers/KeyManager.h"
#include "pages/PageRenderer.h"
#include "storage/StoragePreferences.h"
#include "time/TimeKeeper.h"

namespace {

const char* kUiStateKey = "uiState";

long long monotonicMillis() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<long long>(ts.tv_sec) * 1000LL +
			static_cast<long long>(ts.tv_nsec) / 1000000LL;
}

PageConfig makeFallbackClockPage() {
	PageConfig page;
	page.id = "p0";
	page.type = PAGE_TYPE_CLOCK;
	page.title = "CLOCK";
	page.shortTitle = "CLOCK";
	page.enabled = true;
	page.metric = LIVE_METRIC_ONLINE;
	return page;
}

void hashMix(uint32_t& hash, uint32_t value) {
	hash ^= value;
	hash *= 16777619u;
}

void hashText(uint32_t& hash, const std::string& text) {
	for (size_t i = 0; i < text.size(); ++i) {
		hashMix(hash, static_cast<uint32_t>(
				static_cast<unsigned char>(text[i])));
	}
	hashMix(hash, 0xFFFFFFFFu);
}

void hashMetricValue(uint32_t& hash, const LiveMetricValue& value) {
	hashMix(hash, value.valid ? 1u : 0u);
	const unsigned long long raw =
			static_cast<unsigned long long>(value.value);
	hashMix(hash, static_cast<uint32_t>(raw & 0xFFFFFFFFULL));
	hashMix(hash, static_cast<uint32_t>((raw >> 32) & 0xFFFFFFFFULL));
}

}  // namespace

PageStatus::PageStatus()
	: timeValid(false),
	  mode(PAGE_MODE_INVALID),
	  remainSeconds(0),
	  elapsedSeconds(0),
	  targetEpoch(0),
	  targetIndex(-1),
	  warning(false),
	  smallFont(false),
	  overlay(false),
	  nowUtc(0),
	  tzOffsetMinutes(480),
	  hasTarget(false),
	  banner(false),
	  pageIndex(-1),
	  pageCount(0) {
}

PageHost::PageHost()
	: mPageIndex(0),
	  mConfigGeneration(0),
	  mHasConfig(false),
	  mForceDirty(true),
	  mLastSignature(0),
	  mBannerUntilMs(0),
	  mBannerIndex(-1),
	  mOverlayColor(0),
	  mOverlayStartMs(0),
	  mOverlayExpireMs(0),
	  mRestorePending(false),
	  mRestoreIndex(-1),
	  mPendingPersistIndex(-1),
	  mPendingSinceMs(0),
	  mLastPersistMs(0),
	  mLastPersistedIndex(-1) {
	// 内置兜底页：真实配置由 mainLogic 的 ConfigStore::load()/setConfig 覆盖
	mConfig = TimerConfig();
	mConfig.pages.push_back(makeFallbackClockPage());
	mConfig.defaultPageId = "p0";
	mEngine.setConfig(mConfig);
}

PageHost::~PageHost() {
}

int PageHost::clampIndexLocked(int index) const {
	const int count = static_cast<int>(mConfig.pages.size());
	if (count <= 0) {
		return 0;
	}
	if (index < 0) {
		return 0;
	}
	if (index >= count) {
		return count - 1;
	}
	return index;
}

int PageHost::firstEnabledFromLocked(int start) const {
	const int count = static_cast<int>(mConfig.pages.size());
	if (count <= 0) {
		return 0;
	}
	int begin = clampIndexLocked(start);
	for (int step = 0; step < count; ++step) {
		int index = begin + step;
		if (index >= count) {
			index -= count;
		}
		if (mConfig.pages[index].enabled) {
			return index;
		}
	}
	return begin;
}

int PageHost::resolveRestoreIndexLocked() const {
	const int count = static_cast<int>(mConfig.pages.size());
	if (count <= 0) {
		return 0;
	}
	// 1) 最后停留页 id（页面顺序被编辑后仍回到同一页）
	if (mRestorePending) {
		if (!mRestoreId.empty()) {
			const int byId = findPageIndexById(mConfig, mRestoreId);
			if (byId >= 0 && mConfig.pages[byId].enabled) {
				return byId;
			}
		}
		// 2) id 丢失时的下标兜底
		if (mRestoreIndex >= 0 && mRestoreIndex < count &&
				mConfig.pages[mRestoreIndex].enabled) {
			return mRestoreIndex;
		}
	}
	// 3) 配置的开机页
	if (!mConfig.defaultPageId.empty()) {
		const int byDefault = findPageIndexById(mConfig, mConfig.defaultPageId);
		if (byDefault >= 0 && mConfig.pages[byDefault].enabled) {
			return byDefault;
		}
	}
	// 4/5) 从 last index 向后找第一个 enabled 页
	return firstEnabledFromLocked(mRestoreIndex >= 0 ? mRestoreIndex : 0);
}

int PageHost::resolveConfigIndexLocked(const std::string& keepId,
		int keepIndex) const {
	const int count = static_cast<int>(mConfig.pages.size());
	if (count <= 0) {
		return 0;
	}
	// 热应用：当前页 id 仍存在就保持该页（页面顺序/标题变化不影响下标）
	if (!keepId.empty()) {
		const int byId = findPageIndexById(mConfig, keepId);
		if (byId >= 0) {
			return byId;
		}
	}
	// id 不存在：clamp 到合法下标，并跳过 disabled 页
	return firstEnabledFromLocked(keepIndex);
}

void PageHost::updatePageInfoLocked(int index) {
	const PageConfig* page = pageAt(mConfig, index);
	if (page == NULL) {
		return;
	}
	mStatus.pageId = page->id;
	mStatus.pageIndex = index;
	mStatus.pageCount = static_cast<int>(mConfig.pages.size());
	mStatus.pageType = pageTypeId(page->type);
	mStatus.pageTitle = page->title.empty() ? effectiveShortTitle(*page)
			: page->title;
}

void PageHost::setConfig(const TimerConfig& config) {
	int newIndex = 0;
	std::string newId;
	bool firstConfig = false;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		const PageConfig* current = pageAt(mConfig, mPageIndex);
		const std::string keepId = (current != NULL) ? current->id
				: std::string();
		const int keepIndex = mPageIndex;
		firstConfig = !mHasConfig;

		mConfig = config;
		if (mConfig.pages.empty()) {
			mConfig.pages.push_back(makeFallbackClockPage());
			mConfig.defaultPageId = mConfig.pages[0].id;
		}
		mEngine.setConfig(mConfig);
		mHasConfig = true;
		++mConfigGeneration;

		newIndex = firstConfig ? resolveRestoreIndexLocked()
				: resolveConfigIndexLocked(keepId, keepIndex);
		mPageIndex = newIndex;
		mForceDirty = true;
		mLastSignature = 0;
		const PageConfig* page = pageAt(mConfig, newIndex);
		if (page != NULL) {
			newId = page->id;
			// 立即更新页信息，保证 POST /api/config 的 ui 响应就是新页
			updatePageInfoLocked(newIndex);
		}
		mRestorePending = false;
	}

	// 锁外通知 LiveService：切页/换配置后立即刷新（LiveService 不回调 PageHost）
	LiveService::getInstance().noteActivePage(newIndex);
	LOGI_TRACE("PageHost: config applied pages=%d index=%d id=%s%s",
			pageCount(), newIndex, newId.c_str(),
			firstConfig ? " (restored)" : "");
}

void PageHost::restoreUiState() {
	const std::string raw = StoragePreferences::getString(kUiStateKey,
			std::string());
	std::string id;
	int index = -1;
	bool pending = false;

	if (!raw.empty()) {
		// 格式 "<pageId>,<pageIndex>,<epochSec>"（epoch 仅用于诊断）
		const size_t first = raw.find(',');
		if (first != std::string::npos) {
			id = raw.substr(0, first);
			const size_t second = raw.find(',', first + 1);
			const std::string indexText = raw.substr(first + 1,
					(second == std::string::npos ? raw.size() : second) -
							first - 1);
			if (!indexText.empty()) {
				char* end = NULL;
				const long parsed = strtol(indexText.c_str(), &end, 10);
				if (end != NULL && *end == '\0' && parsed >= 0 &&
						parsed < kMaxPageCount) {
					index = static_cast<int>(parsed);
				}
			}
			pending = !id.empty() || index >= 0;
			if (!pending) {
				LOGW_TRACE("PageHost: malformed uiState [%s]", raw.c_str());
			}
		} else {
			LOGW_TRACE("PageHost: malformed uiState [%s]", raw.c_str());
		}
	}

	std::lock_guard<std::mutex> lock(mMutex);
	mRestoreId = id;
	mRestoreIndex = index;
	mRestorePending = pending;
	if (pending) {
		LOGI_TRACE("PageHost: uiState restored id=%s index=%d", id.c_str(),
				index);
	}
}

void PageHost::resetUiState() {
	StoragePreferences::remove(kUiStateKey);
	std::lock_guard<std::mutex> lock(mMutex);
	mRestorePending = false;
	mRestoreId.clear();
	mRestoreIndex = -1;
	mPendingPersistIndex = -1;
	mLastPersistedIndex = -1;
	mLastPersistMs = 0;
	LOGI_TRACE("PageHost: uiState cleared");
}

void PageHost::showOverlay(const std::string& text, uint32_t rgb,
		uint32_t durationMs) {
	std::lock_guard<std::mutex> lock(mMutex);
	mOverlayText = text;
	mOverlayColor = rgb;
	mOverlayStartMs = monotonicMillis();
	mOverlayExpireMs = text.empty() ? 0LL
			: mOverlayStartMs + static_cast<long long>(durationMs);
	mForceDirty = true;
}

void PageHost::invalidate() {
	std::lock_guard<std::mutex> lock(mMutex);
	mForceDirty = true;
}

void PageHost::notePageChangedLocked(int index, long long monotonicMs) {
	mPendingPersistIndex = index;
	mPendingSinceMs = monotonicMs;
	if (index == mLastPersistedIndex) {
		// 回到上次已落盘的页面：不需要再写 flash
		mPendingPersistIndex = -1;
	}
}

bool PageHost::stepPage(int delta) {
	const long long nowMs = monotonicMillis();
	int changedIndex = -1;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		const int count = static_cast<int>(mConfig.pages.size());
		if (count <= 1) {
			return false;  // 只有 1 页：旋钮无动作、不出横幅
		}
		const int current = clampIndexLocked(mPageIndex);
		for (int step = 1; step <= count; ++step) {
			int index = (current + delta * step) % count;
			if (index < 0) {
				index += count;
			}
			if (!mConfig.pages[index].enabled) {
				continue;
			}
			if (index == current) {
				break;  // 没有其它 enabled 页
			}
			mPageIndex = index;
			changedIndex = index;
			break;
		}
		if (changedIndex < 0) {
			return false;
		}
		mBannerUntilMs = nowMs + kBannerDurationMs;
		mBannerIndex = changedIndex;
		mForceDirty = true;
		mLastSignature = 0;
		notePageChangedLocked(changedIndex, nowMs);
		updatePageInfoLocked(changedIndex);
	}
	LiveService::getInstance().noteActivePage(changedIndex);
	LOGI_TRACE("PageHost: page -> %d", changedIndex);
	return true;
}

bool PageHost::nextPage() {
	return stepPage(1);
}

bool PageHost::prevPage() {
	return stepPage(-1);
}

bool PageHost::goToPage(int index) {
	const long long nowMs = monotonicMillis();
	{
		std::lock_guard<std::mutex> lock(mMutex);
		const int count = static_cast<int>(mConfig.pages.size());
		if (index < 0 || index >= count) {
			return false;
		}
		if (index == mPageIndex) {
			mForceDirty = true;
			return true;
		}
		mPageIndex = index;
		mBannerUntilMs = nowMs + kBannerDurationMs;
		mBannerIndex = index;
		mForceDirty = true;
		mLastSignature = 0;
		notePageChangedLocked(index, nowMs);
		updatePageInfoLocked(index);
	}
	LiveService::getInstance().noteActivePage(index);
	LOGI_TRACE("PageHost: goto page %d", index);
	return true;
}

bool PageHost::goToPageId(const std::string& id) {
	int index = -1;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		index = findPageIndexById(mConfig, id);
	}
	if (index < 0) {
		return false;
	}
	return goToPage(index);
}

int PageHost::currentPageIndex() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mPageIndex;
}

int PageHost::pageCount() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return static_cast<int>(mConfig.pages.size());
}

bool PageHost::requestLiveRefresh() {
	int index = 0;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		index = clampIndexLocked(mPageIndex);
		const PageConfig* page = pageAt(mConfig, index);
		if (page == NULL || page->type != PAGE_TYPE_LIVE) {
			return false;
		}
	}
	LiveService::getInstance().requestRefresh(index);
	LOGI_TRACE("PageHost: live refresh requested page=%d", index);
	return true;
}

bool PageHost::onKeyEvent(int keyCode, int keyStatus) {
	// PageManager 持有自己的 mutex 调用本函数：只改状态 + 置脏，不做 I/O/渲染
	if (keyStatus != 0) {
		return true;
	}
	if (keyCode == E_KEYCODE_CLOCKWISE) {
		nextPage();
	} else if (keyCode == E_KEYCODE_ANTI_CLOCKWISE) {
		prevPage();
	}
	return true;
}

void PageHost::updateStatusLocked(const PageConfig& page, int index, int count,
		const Stage& stage, long long nowUtc, bool timeValid,
		bool overlayActive, const std::string& overlayText,
		bool bannerActive) {
	PageStatus status;
	status.timeValid = timeValid;
	status.nowUtc = nowUtc;
	status.tzOffsetMinutes = mConfig.tzOffsetMinutes;
	status.remainSeconds = stage.remainSeconds;
	status.elapsedSeconds = stage.elapsedSeconds;
	status.targetEpoch = stage.targetEpoch;
	status.targetIndex = stage.targetIndex;
	status.warning = stage.warning;
	status.smallFont = stage.smallFont;
	status.overlay = overlayActive;
	status.overlayText = overlayActive ? overlayText : std::string();
	status.banner = bannerActive;
	status.pageId = page.id;
	status.pageIndex = index;
	status.pageCount = count;
	status.pageType = pageTypeId(page.type);
	status.pageTitle = page.title.empty() ? effectiveShortTitle(page)
			: page.title;

	if (timeValid) {
		epochToLocalDateTime(nowUtc, mConfig.tzOffsetMinutes, status.localNow);
	}
	if (timeValid &&
			(stage.kind == STAGE_COUNTDOWN || stage.kind == STAGE_COUNTUP) &&
			stage.targetEpoch > 0) {
		if (epochToLocalDateTime(stage.targetEpoch, mConfig.tzOffsetMinutes,
				status.targetLocal)) {
			status.hasTarget = true;
		}
	}

	switch (page.type) {
		case PAGE_TYPE_CLOCK:
			status.mode = timeValid ? PAGE_MODE_CLOCK : PAGE_MODE_INVALID;
			status.text = timeValid
					? formatEpochLocalTime(nowUtc, mConfig.tzOffsetMinutes)
					: std::string("--:--:--");
			if (status.text.empty()) {
				status.text = "--:--:--";
			}
			status.smallFont = false;
			break;

		case PAGE_TYPE_CLOCK_COUNTDOWN:
			status.mode = !timeValid ? PAGE_MODE_INVALID
					: (stage.kind == STAGE_COUNTDOWN ? PAGE_MODE_COUNTDOWN
							: (stage.kind == STAGE_COUNTUP ? PAGE_MODE_COUNTUP
									: PAGE_MODE_CLOCK));
			if (!timeValid) {
				status.text = "--:--:--";
				status.smallFont = false;
				break;
			}
			status.text = formatEpochLocalTime(nowUtc,
					mConfig.tzOffsetMinutes);
			if (status.text.empty()) {
				status.text = "--:--:--";
			}
			status.smallFont = false;  // 分屏两半固定 scale 1
			if (stage.kind == STAGE_COUNTDOWN) {
				bool useSmall = false;
				status.text2 = formatCountdown(stage.remainSeconds, useSmall);
			} else if (stage.kind == STAGE_COUNTUP) {
				bool useSmall = false;
				status.text2 = formatCountdown(stage.elapsedSeconds, useSmall);
			} else {
				status.text2 = "NO COUNTDOWN";
			}
			break;

		case PAGE_TYPE_DATETIME:
			status.mode = timeValid ? PAGE_MODE_CLOCK : PAGE_MODE_INVALID;
			status.text = timeValid
					? formatEpochLocalTime(nowUtc, mConfig.tzOffsetMinutes)
					: std::string("--:--:--");
			if (status.text.empty()) {
				status.text = "--:--:--";
			}
			status.text2 = timeValid ? formatDate(status.localNow) : std::string();
			status.smallFont = false;
			break;

		case PAGE_TYPE_COUNTDOWN:
			if (!timeValid) {
				status.mode = PAGE_MODE_INVALID;
				status.text = "--:--:--";
				status.smallFont = false;
				break;
			}
			if (stage.kind == STAGE_COUNTDOWN) {
				bool useSmall = false;
				status.text = formatCountdown(stage.remainSeconds, useSmall);
				status.smallFont = useSmall || stage.smallFont;
				status.mode = PAGE_MODE_COUNTDOWN;
			} else if (stage.kind == STAGE_COUNTUP) {
				bool useSmall = false;
				status.text = formatCountdown(stage.elapsedSeconds, useSmall);
				status.smallFont = useSmall || stage.smallFont;
				status.mode = PAGE_MODE_COUNTUP;
			} else {
				status.text = "NO COUNTDOWN";
				status.smallFont = false;
				status.mode = PAGE_MODE_CLOCK;
			}
			break;

		case PAGE_TYPE_LIVE: {
			status.mode = timeValid ? PAGE_MODE_CLOCK : PAGE_MODE_INVALID;
			status.text = "LIVE";
			LiveMetric metric = page.metric;
			if (metric == LIVE_METRIC_INHERIT) {
				metric = LIVE_METRIC_ONLINE;
			}
			status.text2 = liveMetricId(metric);
			status.smallFont = false;
			break;
		}
	}

	mStatus = status;
}

bool PageHost::tick(long long monotonicMs, Surface& surface) {
	// 严格绑定时钟：本帧唯一一次 time(NULL)，所有视图共用同一个 now
	const long long nowUtc = static_cast<long long>(time(NULL));
	const bool timeValid = nowUtc >= TimeKeeper::kTimeValidEpoch;

	int pageIndex = 0;
	bool livePage = false;
	unsigned long generation = 0;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		pageIndex = clampIndexLocked(mPageIndex);
		const PageConfig* page = pageAt(mConfig, pageIndex);
		livePage = page != NULL && page->type == PAGE_TYPE_LIVE;
		generation = mConfigGeneration;
	}

	// live 快照在 PageHost 锁外取：LiveService 有自己的锁，锁顺序不嵌套
	LiveSnapshot live;
	if (livePage) {
		LiveService::getInstance().snapshotForPage(pageIndex, live);
	}

	TimerConfig view;  // 只放标量 + 当前页（不整份拷贝配置）
	Stage stage;
	bool overlayActive = false;
	bool bannerActive = false;
	std::string overlayText;
	uint32_t overlayColor = 0;
	int overlayScroll = 0;
	int bannerIndex = -1;
	int bannerCount = 0;
	std::string bannerTitle;
	bool dirty = true;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (generation != mConfigGeneration) {
			mForceDirty = true;  // 取快照期间配置被热替换：本帧放弃，下帧重来
			return false;
		}
		pageIndex = clampIndexLocked(mPageIndex);
		const PageConfig* page = pageAt(mConfig, pageIndex);
		if (page == NULL) {
			mForceDirty = true;
			return false;
		}

		view.tzOffsetMinutes = mConfig.tzOffsetMinutes;
		view.warnSeconds = mConfig.warnSeconds;
		view.colorClock = mConfig.colorClock;
		view.colorCountdown = mConfig.colorCountdown;
		view.colorWarning = mConfig.colorWarning;
		view.colorDim = mConfig.colorDim;
		view.splitBottomWhenIdle = mConfig.splitBottomWhenIdle;
		view.pages.push_back(*page);

		// 与 PageRenderer 共用同一次 evaluate(now)
		stage = mEngine.evaluate(nowUtc);

		overlayActive = !mOverlayText.empty() &&
				monotonicMs < mOverlayExpireMs;
		if (overlayActive) {
			overlayText = mOverlayText;
			overlayColor = mOverlayColor;
			// 单行 IP 超宽：按时间水平滚动；两行 AP 凭据不滚动。
			if (overlayText.find('\n') == std::string::npos &&
					overlayText.find(' ') == std::string::npos) {
				const int width = PageRenderer::measureText(overlayText, 2, 1);
				if (width > PageRenderer::kScreenWidth) {
					long long elapsed = monotonicMs - mOverlayStartMs;
					if (elapsed < 0) {
						elapsed = 0;
					}
					overlayScroll = static_cast<int>(
							(elapsed / 60) % static_cast<long long>(width + 8));
				}
			}
		}

		// 覆盖层最高优先级；覆盖层激活时抑制横幅（设计 §2.5）
		bannerActive = !overlayActive && mBannerIndex >= 0 &&
				monotonicMs < mBannerUntilMs;
		bannerCount = static_cast<int>(mConfig.pages.size());
		if (bannerActive) {
			bannerIndex = mBannerIndex;
			if (bannerIndex < 0 || bannerIndex >= bannerCount) {
				bannerActive = false;
				bannerIndex = -1;
			} else {
				bannerTitle = effectiveShortTitle(
						mConfig.pages[bannerIndex]);
			}
		}
		if (!bannerActive) {
			bannerIndex = -1;
		}

		// 脏帧签名：秒变化 / 切页 / 横幅 / 覆盖层 / live 数据变化
		uint32_t hash = 2166136261u;
		hashMix(hash, static_cast<uint32_t>(pageIndex));
		hashMix(hash, static_cast<uint32_t>(page->type));
		hashMix(hash, timeValid ? 1u : 0u);
		hashMix(hash, static_cast<uint32_t>(stage.kind));
		const unsigned long long remainRaw =
				static_cast<unsigned long long>(stage.remainSeconds);
		hashMix(hash, static_cast<uint32_t>(remainRaw & 0xFFFFFFFFULL));
		hashMix(hash, static_cast<uint32_t>((remainRaw >> 32) &
				0xFFFFFFFFULL));
		const unsigned long long elapsedRaw =
				static_cast<unsigned long long>(stage.elapsedSeconds);
		hashMix(hash, static_cast<uint32_t>(elapsedRaw & 0xFFFFFFFFULL));
		hashMix(hash, static_cast<uint32_t>((elapsedRaw >> 32) &
				0xFFFFFFFFULL));
		hashMix(hash, stage.targetIndex >= 0
				? static_cast<uint32_t>(stage.targetIndex) : 0xFFFFFFFFu);
		hashMix(hash, stage.warning ? 1u : 0u);
		hashMix(hash, stage.smallFont ? 1u : 0u);
		hashMix(hash, static_cast<uint32_t>(
				mConfig.tzOffsetMinutes + 1024));
		hashMix(hash, static_cast<uint32_t>(mConfig.splitBottomWhenIdle));
		hashMix(hash, mConfig.colorClock);
		hashMix(hash, mConfig.colorCountdown);
		hashMix(hash, mConfig.colorWarning);
		hashMix(hash, mConfig.colorDim);
		if (!overlayActive && !bannerActive &&
				page->type != PAGE_TYPE_LIVE) {
			// 会显示时间的页面：秒变化必须重绘（仍复用本帧同一个 now）
			hashMix(hash, static_cast<uint32_t>(nowUtc & 0xFFFFFFFFLL));
		}
		hashMix(hash, overlayActive ? 1u : 0u);
		hashText(hash, overlayText);
		hashMix(hash, overlayColor);
		hashMix(hash, static_cast<uint32_t>(overlayScroll));
		hashMix(hash, bannerActive ? 1u : 0u);
		hashMix(hash, static_cast<uint32_t>(bannerIndex));
		hashMix(hash, static_cast<uint32_t>(bannerCount));
		// 当前页配置本身（live 指标/房间/标签改动要立即反映到画面）
		hashText(hash, page->id);
		hashMix(hash, static_cast<uint32_t>(page->metric + 1));
		for (size_t roomIndex = 0; roomIndex < page->rooms.size() &&
				roomIndex < static_cast<size_t>(kLiveCellCount); ++roomIndex) {
			const LiveRoomConfig& room = page->rooms[roomIndex];
			hashMix(hash, static_cast<uint32_t>(room.platform));
			hashText(hash, room.room);
			hashText(hash, room.label);
			hashMix(hash, room.enabled ? 1u : 0u);
			hashMix(hash, static_cast<uint32_t>(room.metric + 1));
		}
		for (int i = 0; i < kLiveCellCount; ++i) {
			const LiveRoomData& room = live.rooms[i];
			hashMix(hash, static_cast<uint32_t>(room.state));
			hashMetricValue(hash, room.online);
			hashMetricValue(hash, room.likes);
			hashMetricValue(hash, room.fans);
			hashMetricValue(hash, room.liveStatus);
			const unsigned long long updatedRaw =
					static_cast<unsigned long long>(room.updatedUtc);
			hashMix(hash, static_cast<uint32_t>(updatedRaw & 0xFFFFFFFFULL));
		}

		if (!mForceDirty && hash == mLastSignature) {
			dirty = false;  // 稳态：不重绘、不发 SPI（仍需推进 uiState 节流）
		} else {
			mLastSignature = hash;
			mForceDirty = false;

			updateStatusLocked(*page, pageIndex, bannerCount, stage, nowUtc,
					timeValid, overlayActive, overlayText, bannerActive);
		}
	}

	if (!dirty) {
		persistTick(monotonicMs, nowUtc);
		return false;
	}

	// 渲染在锁外进行：view + stage + live 都是本帧私有拷贝，PageRenderer 是纯函数
	if (overlayActive) {
		PageRenderer::renderOverlayTextScrolled(overlayText, overlayColor,
				overlayScroll, surface);
	} else if (bannerActive) {
		PageRenderer::renderPageBanner(bannerIndex, bannerCount, bannerTitle,
				view, surface);
	} else {
		PageRenderer::renderPage(view, 0, nowUtc, stage, timeValid, live,
				surface);
	}

	persistTick(monotonicMs, nowUtc);
	return true;
}

void PageHost::persistTick(long long monotonicMs, long long nowUtc) {
	int index = -1;
	std::string id;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mPendingPersistIndex < 0) {
			return;
		}
		if (mConfig.pages.size() <= 1) {
			mPendingPersistIndex = -1;  // 只有 1 页：写 uiState 没有意义
			return;
		}
		if (monotonicMs - mPendingSinceMs < kStablePersistMs) {
			return;  // 页面稳定 >= 30s 才落盘
		}
		if (monotonicMs - mLastPersistMs < kMinPersistIntervalMs) {
			return;  // 两次落盘至少间隔 120s
		}
		index = clampIndexLocked(mPendingPersistIndex);
		const PageConfig* page = pageAt(mConfig, index);
		if (page == NULL) {
			mPendingPersistIndex = -1;
			return;
		}
		id = page->id;
	}

	char buffer[80];
	snprintf(buffer, sizeof(buffer), "%s,%d,%lld", id.c_str(), index, nowUtc);
	const bool ok = StoragePreferences::putString(kUiStateKey,
			std::string(buffer));

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mLastPersistMs = monotonicMs;  // 失败也节流，下一轮（>=120s）再重试
		if (ok) {
			mLastPersistedIndex = index;
			mPendingPersistIndex = -1;
			LOGI_TRACE("PageHost: persisted uiState page=%d id=%s", index,
					id.c_str());
		} else {
			LOGW_TRACE("PageHost: save uiState failed page=%d", index);
		}
	}
}

bool PageHost::getStatus(PageStatus& out) const {
	std::lock_guard<std::mutex> lock(mMutex);
	out = mStatus;
	return true;
}
