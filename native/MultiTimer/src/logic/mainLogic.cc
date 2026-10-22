#include <atomic>
#include <memory>
#include <string>
#include <time.h>

#include "base/base.h"
#include "config/ConfigStore.h"
#include "live/LiveService.h"
#include "managers/KeyManager.h"
#include "managers/PageManager.h"
#include "managers/McuManager.h"
#include "net/ConfigMode.h"
#include "net/HttpServer.h"
#include "net/SntpClient.h"
#include "net/WifiManager_task.h"
#include "pages/TimerPage.h"
#include "time/TimeKeeper.h"
#include <os/SystemProperties.h>

// v3：250ms 渲染节拍（脏帧才真正 sendLedData）；每秒再分频做时钟/SoftAP 维护
static S_ACTIVITY_TIMEER REGISTER_ACTIVITY_TIMER_TAB[] = {
	{1, 250},
};

namespace {

const unsigned long kLongPressMs = 3000;
// 旋钮一格一次事件；这里只防机械/驱动重复上报（非软件消抖）
const unsigned long kKnobDebounceMs = 120;
const unsigned long kKnobButtonDebounceMs = 300;

// 32 位 ARM 上避免 atomic<long long>（无 libatomic）；毫秒回绕用无符号减法判断。
std::atomic<bool> sRightPressed(false);
std::atomic<bool> sRightLongHandled(false);
std::atomic<unsigned long> sRightDownMs(0);
std::atomic<unsigned long> sLastKnobMs(0);
std::atomic<unsigned long> sLastKnobButtonMs(0);

unsigned long monotonicMs32() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<unsigned long>(ts.tv_sec) * 1000UL +
			static_cast<unsigned long>(ts.tv_nsec) / 1000000UL;
}

TimerPage* timerPage() {
	return static_cast<TimerPage*>(
			PageManager::getInstance().getPage("TimerPage"));
}

void showMiddleStatus() {
	TimerPage* page = timerPage();
	if (page == NULL) {
		return;
	}
	ConfigMode& configMode = ConfigMode::getInstance();
	if (configMode.isActive()) {
		const std::string apIp = configMode.ip();
		page->showOverlay(apIp.empty() ? configMode.ssid() : apIp,
				0x00FF00, 12000);
		return;
	}
	const std::string ip = wifiIp();
	page->showOverlay(ip.empty() ? "NO WIFI" : ip,
			ip.empty() ? 0xFF8000 : 0x00FF00, ip.empty() ? 6000 : 12000);
}

}  // namespace

// 启动时系统时间无效时，ConfigStore 无法把 v1 points 折算成带日期目标
// （DESIGN §3：保留原文件）。等 SNTP/网页对时把时间变有效后，在主线程重试一次迁移。
static bool sPendingConfigMigration = false;

/**
 * 当界面构造时触发
 */
static void onUI_init() {
	// 防砖属性，必须最先设置
	SystemProperties::setString("sys.zkapp.state", "running");

	// 初始化 MCU 串口
	McuManager::getInstance().initialize(new PixelMcuProto::McuParse("/dev/ttyS1", 1500000));

	// 每次开机必须先请求一次版本，LED 才可以显示画面
	std::string mcuVer;
	McuManager::getInstance().queryMcuVersion(mcuVer);
	LOGI_TRACE("mcuVer : [%s]", mcuVer.c_str());

	// 设备无 RTC：在页面注册/配置加载前，用上次持久化的时钟恢复系统时间；
	// 系统时间已有效时只恢复“最近对时”，不覆盖更准确的当前时间。
	TimeKeeper::getInstance().restore();

	// 注册页面宿主（注册名仍是 "TimerPage"，PageManager/HttpServer 不变）
	PageManager::getInstance().registerPage(
			std::unique_ptr<PageBase>(new TimerPage()));

	TimerPage* page = timerPage();
	if (page != NULL) {
		// 当前页恢复必须在 setConfig 之前：最后停留页 > defaultPageId > 第一页
		page->restoreUiState();
	}

	// 加载 /data/multitimer.conf（v3；v2/v1 自动迁移并写回一次）。
	// 缺失/损坏时 load 会给出全新安装的默认 4 页配置，屏幕仍有可用页面。
	TimerConfig loadedConfig;
	bool migrated = false;
	const long long nowUtc = static_cast<long long>(time(NULL));
	const bool loaded = ConfigStore::load(loadedConfig, nowUtc, &migrated);
	if (page != NULL) {
		page->setConfig(loadedConfig);
	}
	SntpClient::setServer(loadedConfig.ntpServer);
	if (loaded) {
		if (migrated) {
			if (ConfigStore::save(loadedConfig)) {
				LOGI_TRACE("mainLogic: migrated legacy config to v3");
			} else {
				LOGW_TRACE("mainLogic: failed to save migrated v3 config");
			}
		} else if (nowUtc < TimeKeeper::kTimeValidEpoch) {
			// 可能是需要“当前日期”的 v1；时间有效后在 onUI_Timer 里再试一次
			sPendingConfigMigration = true;
			LOGW_TRACE("mainLogic: clock invalid, defer v1 config migration");
		}
		LOGI_TRACE("mainLogic: loaded /data/multitimer.conf (%u pages, %u targets)",
				static_cast<unsigned int>(loadedConfig.pages.size()),
				static_cast<unsigned int>(loadedConfig.targets.size()));
	} else {
		LOGI_TRACE("mainLogic: no valid config, using default v3 pages");
	}

	// 直播服务：单后台线程；配置变化只换快照，不影响 UI 主循环
	LiveService& liveService = LiveService::getInstance();
	liveService.start();
	liveService.setConfig(loadedConfig);
	if (page != NULL) {
		liveService.noteActivePage(page->currentPageIndex());
	}

	// 显示当前页面（PageManager::navigateTo -> onEnter + draw）
	PageManager::getInstance().navigateTo("TimerPage");

	// 按键路由：
	// - 中键：短按显示 IP/NO WIFI（单次，不再长按）
	// - 右键：长按 3s 进入/退出 SoftAP 配网
	// - 旋钮 CW/CCW：下一页/上一页（顺时针正向、逆时针反向）
	// - 旋钮按键：当前页是 live 时立即刷新（不阻塞）
	KeyManager::getInstance().addKeyEventCallback([](int keyCode, int keyStatus) {
		if (keyCode == E_KEYCODE_MIDDLE_BUTTON) {
			if (keyStatus == 0) {
				LOGI_TRACE("mainLogic: middle key pressed");
				showMiddleStatus();
			}
			return;
		}

		if (keyCode == E_KEYCODE_RIGHT_BUTTON) {
			if (keyStatus == 1) {
				sRightDownMs.store(monotonicMs32());
				sRightPressed.store(true);
				sRightLongHandled.store(false);
				LOGI_TRACE("mainLogic: right key down");
				return;
			}
			if (keyStatus == 0) {
				sRightPressed.store(false);
				sRightLongHandled.store(false);
				return;
			}
			return;
		}

		if (keyCode == E_KEYCODE_CLOCKWISE ||
				keyCode == E_KEYCODE_ANTI_CLOCKWISE) {
			if (keyStatus != 0) {
				return;  // 旋钮事件恒为 0；其它状态不处理
			}
			const unsigned long nowMs = monotonicMs32();
			const unsigned long lastMs = sLastKnobMs.load();
			if (lastMs != 0 && nowMs - lastMs < kKnobDebounceMs) {
				return;  // 同一格被重复上报：丢弃
			}
			sLastKnobMs.store(nowMs);
			TimerPage* page = timerPage();
			if (page == NULL) {
				return;
			}
			if (keyCode == E_KEYCODE_CLOCKWISE) {
				page->nextPage();
			} else {
				page->prevPage();
			}
			return;
		}

		if (keyCode == E_KEYCODE_KNOB_BUTTON) {
			if (keyStatus != 1) {
				return;  // 按下沿触发一次
			}
			const unsigned long nowMs = monotonicMs32();
			const unsigned long lastMs = sLastKnobButtonMs.load();
			if (lastMs != 0 &&
					nowMs - lastMs < kKnobButtonDebounceMs) {
				return;
			}
			sLastKnobButtonMs.store(nowMs);
			TimerPage* page = timerPage();
			if (page != NULL) {
				page->requestLiveRefresh();
			}
			return;
		}
		// 左键不分配；右键长按在 onUI_Timer 中处理
	});
	KeyManager::getInstance().start();

	// SoftAP 就绪后显示热点 IP，手机浏览器打开该 IP 会自动跳到 :8080。
	ConfigMode::getInstance().setOnReady([](const std::string& ip) {
		TimerPage* page = timerPage();
		if (page != NULL) {
			page->showOverlay(ip, 0x00FF00, 12000);
		}
	});

	// 以下网络初始化全部为非阻塞后台任务，任何失败都不影响倒计时主逻辑。
	// WiFi 首次连上时在 LED 上以绿色 IP 提示 12 秒（回调内只做线程安全状态写入）
	WifiBootstrap::getInstance().setOnConnected(
			[](const std::string& ip) {
				TimerPage* page = timerPage();
				if (page != NULL) {
					page->showOverlay(ip.empty() ? "NO IP" : ip,
							ip.empty() ? 0xFF8000 : 0x00FF00, 12000);
				}
				// WiFi 恢复后让直播立即重试一次，不等下一个 refreshSec
				LiveService::getInstance().refreshNow();
			});
	startWifiBootstrap();

	// HTTP 配置服务：POST /api/config 保存成功后热应用到 PageHost + LiveService
	if (!HttpServer::start(8080,
			[](const TimerConfig& config) {
				SntpClient::setServer(config.ntpServer);
				TimerPage* page = timerPage();
				if (page != NULL) {
					page->setConfig(config);  // 内部 clamp 当前页并通知 LiveService
				}
				LiveService::getInstance().setConfig(config);
				if (page != NULL) {
					LiveService::getInstance().noteActivePage(
							page->currentPageIndex());
				}
			})) {
		LOGE_TRACE("mainLogic: http server failed to bind 0.0.0.0:8080");
		TimerPage* page = timerPage();
		if (page != NULL) {
			page->showOverlay("HTTP ERR", 0xFF0000, 6000);
		}
	}
}

/**
 * 当切换到该界面时触发
 */
static void onUI_intent(const Intent *intentPtr) {
	if (intentPtr != NULL) {
		//TODO
	}
}

/*
 * 当界面显示时触发
 */
static void onUI_show() {
	// 从其他 Activity 返回时仍显示当前页
	PageManager::getInstance().navigateTo("TimerPage");
}

/*
 * 当界面隐藏时触发
 */
static void onUI_hide() {
}

/*
 * 当界面完全退出时触发
 */
static void onUI_quit() {
	// 停掉直播后台线程（detach 线程在下一个等待点退出）
	LiveService::getInstance().shutdown();
}

/**
 * 串口数据回调接口
 */
static void onProtocolDataUpdate(const SProtocolData &data) {
}

/**
 * 定时器触发函数（250ms）
 * 不建议在此函数中写耗时操作，否则将影响UI刷新
 * 参数： id
 *         当前所触发定时器的id，与注册时的id相同
 * 返回值: true
 *             继续运行当前定时器
 *         false
 *             停止运行当前定时器
 */
static bool onUI_Timer(int id) {
	if (id != 1) {
		return true;
	}

	// 每 tick（250ms）：PageHost 内部判断脏帧，只有秒变化/切页/横幅/覆盖层/
	// 直播数据变化才 sendLedData；稳态仍约 1 帧/秒。
	TimerPage* page = timerPage();
	if (page != NULL) {
		page->tick();
	}

	// 每 4 tick（1s）：时钟持久化、SoftAP 维护、延迟迁移检查
	static unsigned long sTickCount = 0;
	++sTickCount;
	const bool secondTick = (sTickCount % 4) == 0;
	if (secondTick) {
		// 页面绘制结束后再做时钟维护（内部每 300 秒节流写 flash），
		// 避免 TimeKeeper 的持久化阻塞本秒的 LED 刷新。
		TimeKeeper::getInstance().tick();
		ConfigMode::getInstance().tick();

		// 启动时因时间无效而未能执行的 v1 迁移：时间有效后补做一次。
		// 只尝试一次；期间若网页已保存 v3，load 会读到 v3（migrated=false），不会覆盖。
		if (sPendingConfigMigration && TimeKeeper::getInstance().isValid()) {
			sPendingConfigMigration = false;
			TimerConfig migratedConfig;
			bool migrated = false;
			if (ConfigStore::load(migratedConfig,
					static_cast<long long>(time(NULL)), &migrated) && migrated) {
				if (ConfigStore::save(migratedConfig)) {
					TimerPage* timer = timerPage();
					if (timer != NULL) {
						timer->setConfig(migratedConfig);
					}
					LiveService::getInstance().setConfig(migratedConfig);
					LOGI_TRACE("mainLogic: deferred v1 migration completed");
				} else {
					LOGW_TRACE("mainLogic: failed to save deferred v3 config");
				}
			}
		}
	}

	// 右键长按 3 秒：进入/退出 SoftAP 配网模式（每 tick 检查，只读原子变量）。
	if (sRightPressed.load() && !sRightLongHandled.load()) {
		const unsigned long nowMs = monotonicMs32();
		const unsigned long downMs = sRightDownMs.load();
		if (nowMs - downMs >= kLongPressMs) {
			LOGI_TRACE("mainLogic: right long press detected (%lu ms)",
					nowMs - downMs);
			sRightLongHandled.store(true);
			ConfigMode& configMode = ConfigMode::getInstance();
			if (configMode.isActive()) {
				configMode.exit();
				if (page != NULL) {
					page->showOverlay("AP OFF", 0xFF8000, 4000);
				}
				LOGI_TRACE("mainLogic: SoftAP config mode off");
			} else if (configMode.enter()) {
				if (page != NULL) {
					// 两行显示热点名与固定密码，用户在没有网络时也能从 LED 读到。
					page->showOverlay(configMode.ssid() + "\n" +
							ConfigMode::password(), 0x00FF00, 10000);
				}
				LOGI_TRACE("mainLogic: SoftAP config mode on ssid=%s",
						configMode.ssid().c_str());
			} else {
				if (page != NULL) {
					page->showOverlay("AP ERR", 0xFF0000, 6000);
				}
				LOGW_TRACE("mainLogic: SoftAP enter failed");
			}
		}
	}
	return true;
}

/**
 * 有新的触摸事件时触发
 * 参数：ev
 *         新的触摸事件
 * 返回值：true
 *            表示该触摸事件在此被拦截，系统不再将此触摸事件传递到控件上
 *         false
 *            触摸事件将继续传递到控件上
 */
static bool onmainActivityTouchEvent(const MotionEvent &ev) {
	switch (ev.mActionStatus) {
		case MotionEvent::E_ACTION_DOWN:
			break;
		case MotionEvent::E_ACTION_MOVE:
			break;
		case MotionEvent::E_ACTION_UP:
			break;
		default:
			break;
	}
	return false;
}
