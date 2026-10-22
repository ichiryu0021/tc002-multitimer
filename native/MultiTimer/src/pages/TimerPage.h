// 注册名仍为 "TimerPage"（PageManager 只认识这个页面）。
//
// v3 起本类退化为 PageHost 的薄适配层：
// - PageHost 负责页表/翻页/横幅/覆盖层/状态/持久化/单帧时间源/渲染；
// - 本类只做 PageBase 的 SPI 发送（sendLedData）与对 mainLogic/HttpServer 的
//   稳定接口（setConfig/showOverlay/tick/getStatus + 翻页/刷新）。
#ifndef PAGES_TIMERPAGE_H_
#define PAGES_TIMERPAGE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "pages/PageBase.h"
#include "pages/PageHost.h"
#include "utils/Surface.h"

class TimerPage : public PageBase {
public:
	TimerPage();
	virtual ~TimerPage();

	// 热应用 v3 配置（任意线程；PageHost 内部加锁）
	void setConfig(const TimerConfig& config);

	// 覆盖层（IP/NO WIFI/AP ...），最高优先级；可从任意线程调用
	void showOverlay(const std::string& text, uint32_t rgb,
			uint32_t durationMs);

	// UI 定时器（250ms）：脏帧才发 SPI
	void tick();

	// 启动时恢复“最后停留页”（必须在 setConfig 之前调用）
	void restoreUiState();
	void resetUiState();

	// 旋钮/API 翻页（跳过 disabled；goToPage 为显式跳转）
	bool nextPage();
	bool prevPage();
	bool goToPage(int index);
	bool goToPageId(const std::string& id);
	int currentPageIndex() const;
	int pageCount() const;

	// 旋钮按键：当前页是 live 时立即刷新（异步）
	void requestLiveRefresh();

	// HTTP 线程读取显示状态
	bool getStatus(PageStatus& out) const;

	PageHost& host() {
		return mHost;
	}

	virtual void draw() override;
	virtual void onEnter() override;
	virtual void onExit() override;
	virtual bool onKeyEvent(int keyCode, int keyStatus) override;

private:
	void renderHostFrame();

	PageHost mHost;
	Surface mSurface;
	std::vector<uint8_t> mFrame;  // 复用，避免每帧 new[]/delete[]
};

#endif  // PAGES_TIMERPAGE_H_
