#include "net/WifiManager_task.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "base/base.h"
#include <base/wifi.h>
#include "net/NetManager.h"

#include "config/WifiStore.h"
#include "net/ConfigMode.h"
#include "net/SntpClient.h"
#include "time/TimeKeeper.h"

#define WIFIMANAGER NETMANAGER->getWifiManager()

namespace {
const int kAutoConnectWaitSec = 15;     // 系统已保存网络时，等待自动连上
const int kCredentialConnectWaitSec = 30;
const unsigned int kRetryIntervalSec = 15;
const unsigned int kLinkCheckIntervalSec = 2;
const long long kTimeValidEpoch = 1700000000LL;  // 2023-11
// 联网且供电时每分钟校时一次；失败时下一轮继续重试。
const long long kTimeSyncIntervalSec = 60;
const long long kTimeSyncCheckIntervalMs = 30 * 1000LL;  // 每 30 秒检查一次节流
const char* kWlanIfName = "wlan0";

long long monotonicMillis() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<long long>(ts.tv_sec) * 1000LL +
			static_cast<long long>(ts.tv_nsec) / 1000000LL;
}

// 超过 15 秒没有连上就认为这次连接失败；每 500ms 查一次
bool waitConnected(WifiManager* wm, int seconds) {
	if (wm == NULL) {
		return false;
	}
	for (int i = 0; i < seconds * 2; ++i) {
		if (wm->isConnected()) {
			return true;
		}
		usleep(500 * 1000);
	}
	return false;
}
}  // namespace

WifiBootstrap& WifiBootstrap::getInstance() {
	static WifiBootstrap instance;
	return instance;
}

WifiBootstrap::WifiBootstrap()
	: mStarted(false),
	  mConnected(false),
	  mNotified(false),
	  mReconnectRequested(false),
	  mWifiReady(false),
	  mLastTimeSync(0),
	  mTimeSyncInFlight(false) {
	// 重启后 TimeKeeper 可能刚从 flash 恢复 lastSync；用它初始化节流基准，
	// 避免开机后立刻重复对时（真正需要时 6 小时节流会放行）。
	const long long restoredSync = TimeKeeper::getInstance().lastSyncUtc();
	if (restoredSync > 0 &&
			restoredSync <= static_cast<long long>(0xFFFFFFFFUL)) {
		mLastTimeSync.store(static_cast<unsigned long>(restoredSync));
	}
}

void WifiBootstrap::start() {
	bool expected = false;
	if (!mStarted.compare_exchange_strong(expected, true)) {
		return;
	}
	std::thread(&WifiBootstrap::run, this).detach();
}

bool WifiBootstrap::isUp() const {
	return mConnected.load();
}

std::string WifiBootstrap::ip() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mIp;
}

std::string WifiBootstrap::ssid() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mSsid;
}

void WifiBootstrap::setOnConnected(OnConnectedCallback cb) {
	std::lock_guard<std::mutex> lock(mCbMutex);
	mOnConnected = cb;
}

void WifiBootstrap::setIp(const std::string& ip) {
	std::lock_guard<std::mutex> lock(mMutex);
	mIp = ip;
}

void WifiBootstrap::setSsid(const std::string& ssid) {
	std::lock_guard<std::mutex> lock(mMutex);
	mSsid = ssid;
}

void WifiBootstrap::reconnect() {
	if (!mWifiReady.load()) {
		return;  // WiFi 还没初始化，等自举流程自己走
	}
	// SoftAP 与 STA 不能同时工作：先请求 UI 线程关闭 AP，等它退出后再重连。
	if (ConfigMode::getInstance().isActive()) {
		ConfigMode::getInstance().requestExit();
		for (int i = 0; i < 30 && ConfigMode::getInstance().isActive(); ++i) {
			usleep(100 * 1000);
		}
	}
	mReconnectRequested.store(true);
	WifiManager* wm = WIFIMANAGER;
	if (wm != NULL) {
		wm->disconnect();
	}
}

void WifiBootstrap::notifyConnected(const std::string& ip) {
	if (mNotified.exchange(true)) {
		return;  // 仅通知首次连上
	}
	OnConnectedCallback cb;
	{
		std::lock_guard<std::mutex> lock(mCbMutex);
		cb = mOnConnected;
	}
	if (cb) {
		cb(ip);
	}
}

std::string WifiBootstrap::queryIp() {
	WifiManager* wm = WIFIMANAGER;
	if (wm != NULL) {
		const char* wmIp = wm->getIp();
		if (wmIp != NULL && wmIp[0] != '\0' &&
				std::strcmp(wmIp, "0.0.0.0") != 0) {
			return std::string(wmIp);
		}
	}

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return std::string();
	}

	struct ifreq ifr;
	std::memset(&ifr, 0, sizeof(ifr));
	std::strncpy(ifr.ifr_name, kWlanIfName, IFNAMSIZ - 1);

	std::string result;
	if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
		struct sockaddr_in* sin =
				reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);
		char buf[INET_ADDRSTRLEN] = {0};
		if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) != NULL &&
				std::strcmp(buf, "0.0.0.0") != 0) {
			result.assign(buf);
		}
	}
	close(fd);
	return result;
}

bool WifiBootstrap::waitForConnection(int seconds) {
	return waitConnected(WIFIMANAGER, seconds);
}

bool WifiBootstrap::connectWithSavedCredential() {
	std::vector<WifiNetwork> networks;
	std::string source;
	if (!WifiStore::loadNetworks(networks, &source)) {
		LOGW_TRACE("WifiBootstrap: no saved WiFi credential");
		return false;
	}

	WifiManager* wm = WIFIMANAGER;
	if (wm == NULL) {
		return false;
	}

	// 按页面保存的顺序依次尝试；系统 wpa_supplicant 已保存的网络由外层
	// waitForConnection() 先自动连接，这里只处理自动连接失败的情况。
	for (size_t i = 0; i < networks.size(); ++i) {
		const WifiNetwork& network = networks[i];
		if (!network.enabled || network.ssid.empty()) {
			continue;
		}
		if (ConfigMode::getInstance().isActive()) {
			LOGW_TRACE("WifiBootstrap: AP mode active, abort credential loop");
			return false;  // 配网热点与 STA 互斥
		}
		if (wm->isConnected()) {
			return true;  // 等待期间系统可能已经自动连上
		}
		LOGI_TRACE("WifiBootstrap: connecting ssid=%s source=%s (%u/%u)",
				network.ssid.c_str(), source.c_str(),
				static_cast<unsigned int>(i + 1),
				static_cast<unsigned int>(networks.size()));
		wm->connect(network.ssid, network.password);
		if (waitForConnection(kCredentialConnectWaitSec)) {
			LOGI_TRACE("WifiBootstrap: connected ssid=%s", network.ssid.c_str());
			return true;
		}
		LOGW_TRACE("WifiBootstrap: connect failed ssid=%s",
				network.ssid.c_str());
	}
	return false;
}

void WifiBootstrap::syncTimeIfNeeded() {
	const long long before = static_cast<long long>(time(NULL));
	const unsigned long last = mLastTimeSync.load();
	// 时间未同步，或距上次成功对时超过 60 秒，就走网络；每分钟一次。
	if (before >= kTimeValidEpoch && last > 0 && before >= last &&
			before - last < kTimeSyncIntervalSec) {
		return;
	}

	LOGD_TRACE("WifiBootstrap: SNTP sync start (system time=%lld)", before);
	if (SntpClient::syncNow(2000)) {
		const long long synced = static_cast<long long>(time(NULL));
		const bool wasValid = before >= kTimeValidEpoch;
		// 在线时每分钟都会调用；noteSynced 不立即写 flash，由 TimeKeeper::tick()
		// 每 300 秒统一落盘，避免每分钟磨损 flash。
		TimeKeeper::getInstance().noteSynced(synced);
		if (!wasValid) {
			TimeKeeper::getInstance().saveState();
		}
		mLastTimeSync.store(static_cast<unsigned long>(synced));
		const long long delta = synced - before;
		if (!wasValid || delta >= 2 || delta <= -2) {
			LOGI_TRACE("WifiBootstrap: SNTP sync ok, time=%lld delta=%lld",
					synced, delta);
		} else {
			LOGD_TRACE("WifiBootstrap: SNTP check ok, time=%lld", synced);
		}
	} else {
		LOGW_TRACE("WifiBootstrap: SNTP sync failed (offline or UDP 123 blocked)");
	}
}

void WifiBootstrap::maybeSyncTimeAsync() {
	bool expected = false;
	if (!mTimeSyncInFlight.compare_exchange_strong(expected, true)) {
		return;  // 已有一轮 SNTP 在跑，不排队重复请求
	}
	try {
		std::thread([this]() {
			syncTimeIfNeeded();
			mTimeSyncInFlight.store(false);
		}).detach();
	} catch (...) {
		mTimeSyncInFlight.store(false);
		LOGW_TRACE("WifiBootstrap: failed to start SNTP thread");
	}
}

void WifiBootstrap::onLinkUp() {
	std::string ipStr = queryIp();
	// isConnected 可能早于 DHCP 完成，最多再等 5 秒拿地址
	for (int i = 0; i < 10 && ipStr.empty(); ++i) {
		usleep(500 * 1000);
		ipStr = queryIp();
	}

	std::string ssidStr;
	WifiManager* wm = WIFIMANAGER;
	if (wm != NULL) {
		WifiInfo* info = wm->getConnectionInfo();
		if (info != NULL) {
			ssidStr = info->getSsid();
		}
	}

	setIp(ipStr);
	setSsid(ssidStr);
	mConnected.store(true);
	LOGI_TRACE("WifiBootstrap: connected ssid=%s ip=%s",
			ssidStr.c_str(), ipStr.c_str());
	// 先让 LED 显示 IP（overlay 用单调时钟计时，之后 SNTP 跳时间不会把它抹掉），
	// 再把 SNTP 放到后台线程；这样即使 UDP 123 被墙，用户也能马上看到地址。
	notifyConnected(ipStr);
	maybeSyncTimeAsync();
}

void WifiBootstrap::run() {
	LOGI_TRACE("WifiBootstrap: task started");

	WifiManager* wm = WIFIMANAGER;
	if (wm == NULL) {
		LOGE_TRACE("WifiBootstrap: WifiManager is null");
		return;
	}

	while (true) {
		// 配网热点与 STA 不能共存；AP 模式期间暂停自动连接/重连，
		// 由网页保存 WiFi 或长按退出 AP 后再恢复。
		if (ConfigMode::getInstance().isActive()) {
			sleep(2);
			continue;
		}
		if (!mWifiReady.load()) {
			if (!base::wifiOnAndWait(15)) {
				LOGE_TRACE("WifiBootstrap: wifiOnAndWait failed, retry later");
				sleep(kRetryIntervalSec);
				continue;
			}
			mWifiReady.store(true);
		}

		// 情况 A：系统 wpa_supplicant 已经保存过网络，上电会自动连
		if (waitForConnection(kAutoConnectWaitSec)) {
			onLinkUp();
		} else if (connectWithSavedCredential()) {
			// 情况 B：自动连不上，用读到的凭据主动连
			onLinkUp();
		} else {
			LOGW_TRACE("WifiBootstrap: not connected, retry in %us",
					kRetryIntervalSec);
			sleep(kRetryIntervalSec);
			continue;
		}

		// 在线监控：掉线或收到 reconnect 请求后回到外层重连。
		// SNTP 单独放到后台线程，2 秒链路检查不会被 2 秒超时的网络请求拖住；
		// 在线期间每 60 秒尝试一次（内部另有 6 小时成功节流）。
		long long nextTimeSyncCheckMs =
				monotonicMillis() + kTimeSyncCheckIntervalMs;
		while (true) {
			sleep(kLinkCheckIntervalSec);
			if (mReconnectRequested.exchange(false)) {
				LOGW_TRACE("WifiBootstrap: reconnect requested");
				break;
			}
			if (!wm->isConnected()) {
				LOGW_TRACE("WifiBootstrap: link lost");
				break;
			}
			if (monotonicMillis() >= nextTimeSyncCheckMs) {
				nextTimeSyncCheckMs =
						monotonicMillis() + kTimeSyncCheckIntervalMs;
				maybeSyncTimeAsync();
			}
		}

		mConnected.store(false);
		setIp(std::string());
		setSsid(std::string());
		sleep(3);
	}
}

void startWifiBootstrap() {
	WifiBootstrap::getInstance().start();
}

bool wifiIsUp() {
	return WifiBootstrap::getInstance().isUp();
}

std::string wifiIp() {
	return WifiBootstrap::getInstance().ip();
}

std::string wifiSsid() {
	return WifiBootstrap::getInstance().ssid();
}
