#include "pages/TimerPage.h"

#include <time.h>

#include <string>

#include "base/log.h"

namespace {

long long monotonicMillis() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<long long>(ts.tv_sec) * 1000LL +
			static_cast<long long>(ts.tv_nsec) / 1000000LL;
}

}  // namespace

TimerPage::TimerPage()
	: PageBase("TimerPage"), mSurface(52, 16, Color(0, 0, 0)) {
	mFrame.reserve(static_cast<size_t>(52 * 16 * 3));
}

TimerPage::~TimerPage() {
}

void TimerPage::setConfig(const TimerConfig& config) {
	mHost.setConfig(config);
}

void TimerPage::showOverlay(const std::string& text, uint32_t rgb,
		uint32_t durationMs) {
	mHost.showOverlay(text, rgb, durationMs);
}

void TimerPage::tick() {
	renderHostFrame();
}

void TimerPage::restoreUiState() {
	mHost.restoreUiState();
}

void TimerPage::resetUiState() {
	mHost.resetUiState();
}

bool TimerPage::nextPage() {
	return mHost.nextPage();
}

bool TimerPage::prevPage() {
	return mHost.prevPage();
}

bool TimerPage::goToPage(int index) {
	return mHost.goToPage(index);
}

bool TimerPage::goToPageId(const std::string& id) {
	return mHost.goToPageId(id);
}

int TimerPage::currentPageIndex() const {
	return mHost.currentPageIndex();
}

int TimerPage::pageCount() const {
	return mHost.pageCount();
}

void TimerPage::requestLiveRefresh() {
	mHost.requestLiveRefresh();
}

bool TimerPage::getStatus(PageStatus& out) const {
	return mHost.getStatus(out);
}

void TimerPage::draw() {
	// PageManager 的 navigateTo/重绘入口：强制出一帧
	mHost.invalidate();
	renderHostFrame();
}

void TimerPage::onEnter() {
	LOGD_TRACE("TimerPage: onEnter");
	mHost.invalidate();
}

void TimerPage::onExit() {
	LOGD_TRACE("TimerPage: onExit");
}

bool TimerPage::onKeyEvent(int keyCode, int keyStatus) {
	return mHost.onKeyEvent(keyCode, keyStatus);
}

void TimerPage::renderHostFrame() {
	if (!mHost.tick(monotonicMillis(), mSurface)) {
		return;  // 脏帧才发 SPI（稳态约 1 帧/秒）
	}
	mSurface.extractRGB(mFrame);
	sendLedData(mFrame);
}
