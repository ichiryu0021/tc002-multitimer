#include "net/ConfigMode.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "base/log.h"
#include "net/NetManager.h"
#include "net/WifiManager_task.h"

#define SOFTAPMANAGER NETMANAGER->getSoftApManager()

namespace {

const char* kApSsid = "TC002-Setup";
const char* kApPassword = "12345678";
const long long kApTimeoutMs = 15LL * 60LL * 1000LL;  // 15 分钟无操作自动关闭

long long monotonicMillis() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<long long>(ts.tv_sec) * 1000LL +
			static_cast<long long>(ts.tv_nsec) / 1000000LL;
}

}  // namespace

ConfigMode& ConfigMode::getInstance() {
	static ConfigMode instance;
	return instance;
}

ConfigMode::ConfigMode()
	: mActive(false),
	  mExitRequested(false),
	  mAnnounced(false),
	  mStartedMs(0),
	  mTimeoutMs(kApTimeoutMs),
	  mSsid(kApSsid) {
}

bool ConfigMode::enter(int timeoutMs) {
	if (mActive.load()) {
		return true;
	}

	SoftApManager* ap = SOFTAPMANAGER;
	if (ap == NULL) {
		LOGE_TRACE("ConfigMode: SoftApManager is null");
		return false;
	}

	mExitRequested.store(false);
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSsid = kApSsid;
		mIp.clear();
		mAnnounced = false;
		mStartedMs = monotonicMillis();
		mTimeoutMs = timeoutMs > 0 ? timeoutMs : kApTimeoutMs;
	}

	LOGI_TRACE("ConfigMode: enabling SoftAP ssid=%s", kApSsid);
	ap->setSsidAndPwd(kApSsid, kApPassword);
	ap->setEnable(true);
	mActive.store(true);
	return true;
}

void ConfigMode::exit() {
	mExitRequested.store(false);
	if (!mActive.load()) {
		return;
	}
	SoftApManager* ap = SOFTAPMANAGER;
	if (ap != NULL) {
		ap->setEnable(false);
	}
	// 等 setEnable(false) 发起后再置 false，避免 reconnect() 在 AP teardown
	// 期间就抢 STA。
	mActive.store(false);
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mIp.clear();
		mAnnounced = false;
	}
	LOGI_TRACE("ConfigMode: SoftAP disabled");
}

bool ConfigMode::isActive() const {
	return mActive.load();
}

void ConfigMode::requestExit() {
	if (mActive.load()) {
		mExitRequested.store(true);
	}
}

void ConfigMode::touch() {
	if (!mActive.load()) {
		return;
	}
	std::lock_guard<std::mutex> lock(mMutex);
	mStartedMs = monotonicMillis();
}

void ConfigMode::tick() {
	if (!mActive.load()) {
		return;
	}
	if (mExitRequested.exchange(false)) {
		LOGI_TRACE("ConfigMode: exit requested");
		exit();
		return;
	}

	SoftApManager* ap = SOFTAPMANAGER;
	if (ap == NULL) {
		LOGW_TRACE("ConfigMode: SoftApManager disappeared, exiting AP mode");
		exit();
		return;
	}

	const ESoftApState state = ap->getSoftApState();
	if (state == E_SOFTAP_ENABLE_ERROR) {
		LOGW_TRACE("ConfigMode: SoftAP enable error, exiting AP mode");
		exit();
		return;
	}

	if (state == E_SOFTAP_ENABLED) {
		const char* ip = ap->getIp();
		if (ip != NULL && ip[0] != '\0') {
			std::string ipText(ip);
			std::function<void(const std::string&)> callback;
			bool announce = false;
			{
				std::lock_guard<std::mutex> lock(mMutex);
				if (mIp != ipText) {
					mIp = ipText;
					if (!mAnnounced) {
						mAnnounced = true;
						announce = true;
						callback = mOnReady;
					}
				}
			}
			if (announce) {
				LOGI_TRACE("ConfigMode: SoftAP ready ssid=%s ip=%s",
						kApSsid, ipText.c_str());
				if (callback) {
					callback(ipText);
				}
			}
		}
	}

	long long elapsed = 0;
	long long timeoutMs = 0;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		elapsed = monotonicMillis() - mStartedMs;
		timeoutMs = mTimeoutMs;
	}
	if (elapsed > timeoutMs) {
		LOGW_TRACE("ConfigMode: SoftAP timeout, exiting AP mode");
		exit();
	}
}

std::string ConfigMode::ssid() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mSsid;
}

std::string ConfigMode::ip() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mIp;
}

// static
const char* ConfigMode::password() {
	return kApPassword;
}

void ConfigMode::setOnReady(std::function<void(const std::string& ip)> callback) {
	std::lock_guard<std::mutex> lock(mMutex);
	mOnReady = callback;
}
