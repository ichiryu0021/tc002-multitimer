#include "time/TimeKeeper.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <string>

#include "base/log.h"
#include "storage/StoragePreferences.h"

namespace {

const char* kClockStateKey = "clockState";
const long long kPersistIntervalSec = 300;  // 运行中每 5 分钟落盘一次

// 严格解析十进制 long long（整串必须消费完），storage 内容由本类写入
bool parseLongLong(const std::string& text, long long& out) {
	if (text.empty()) {
		return false;
	}
	errno = 0;
	char* end = NULL;
	long long value = strtoll(text.c_str(), &end, 10);
	if (errno != 0 || end == NULL || *end != '\0') {
		return false;
	}
	out = value;
	return true;
}

bool setSystemTime(long long epoch) {
	struct timeval tv;
	tv.tv_sec = static_cast<time_t>(epoch);
	tv.tv_usec = 0;
	return settimeofday(&tv, NULL) == 0;
}

}  // namespace

// 头文件中的 static const 成员一旦被 odr-use（取地址/绑定引用）需要类外定义。
const long long TimeKeeper::kTimeValidEpoch;
const long long TimeKeeper::kMaxEpoch;

TimeKeeper& TimeKeeper::getInstance() {
	static TimeKeeper instance;
	return instance;
}

TimeKeeper::TimeKeeper()
	: mLastSyncUtc(0), mLastPersistUtc(0), mRestored(false) {
}

void TimeKeeper::loadState(long long& savedEpoch, long long& savedSync) const {
	savedEpoch = 0;
	savedSync = 0;

	const std::string raw =
			StoragePreferences::getString(kClockStateKey, std::string());
	if (raw.empty()) {
		return;
	}

	const size_t comma = raw.find(',');
	if (comma == std::string::npos) {
		LOGW_TRACE("TimeKeeper: malformed clockState [%s]", raw.c_str());
		return;
	}

	long long epoch = 0;
	long long sync = 0;
	if (!parseLongLong(raw.substr(0, comma), epoch) ||
			!parseLongLong(raw.substr(comma + 1), sync)) {
		LOGW_TRACE("TimeKeeper: malformed clockState [%s]", raw.c_str());
		return;
	}

	// 无效的历史值直接丢弃，避免把 1970 写回系统时间
	if (epoch < kTimeValidEpoch || epoch > kMaxEpoch) {
		epoch = 0;
	}
	if (sync < kTimeValidEpoch || sync > kMaxEpoch) {
		sync = 0;
	}
	savedEpoch = epoch;
	savedSync = sync;
}

long long TimeKeeper::readSavedEpoch() const {
	long long epoch = 0;
	long long sync = 0;
	loadState(epoch, sync);
	return epoch;
}

void TimeKeeper::restore() {
	std::lock_guard<std::mutex> lock(mMutex);
	if (mRestored) {
		return;
	}
	mRestored = true;

	long long savedEpoch = 0;
	long long savedSync = 0;
	loadState(savedEpoch, savedSync);

	const long long now = static_cast<long long>(time(NULL));
	const bool systemValid = now >= kTimeValidEpoch;

	if (!systemValid && savedEpoch >= kTimeValidEpoch) {
		if (setSystemTime(savedEpoch)) {
			mLastPersistUtc = savedEpoch;
			// 无 RTC 时恢复出来的只是“上次关机时刻”，不能当作一次真实对时；
			// lastSync 保持 0，让 WiFi 连上后立即 SNTP 校正。
			LOGI_TRACE("TimeKeeper: restored system clock from flash "
					"epoch=%lld (saved lastSync=%lld, ignored)",
					savedEpoch, savedSync);
		} else {
			LOGW_TRACE("TimeKeeper: settimeofday(%lld) failed: %s",
					savedEpoch, strerror(errno));
		}
	} else if (systemValid) {
		// 系统时间已经有效（例如框架/其它组件先对过时），只恢复“最近对时”
		mLastSyncUtc = savedSync;
		mLastPersistUtc = now;
		LOGI_TRACE("TimeKeeper: system clock valid now=%lld, restore lastSync=%lld",
				now, savedSync);
	} else {
		LOGW_TRACE("TimeKeeper: no valid saved clock (system now=%lld)", now);
	}

	if (mLastSyncUtc < 0) {
		mLastSyncUtc = 0;
	}
}

bool TimeKeeper::isValid() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return static_cast<long long>(time(NULL)) >= kTimeValidEpoch;
}

long long TimeKeeper::now() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return static_cast<long long>(time(NULL));
}

bool TimeKeeper::setUtc(long long epoch) {
	if (epoch < kTimeValidEpoch || epoch > kMaxEpoch) {
		LOGW_TRACE("TimeKeeper: setUtc out of range epoch=%lld", epoch);
		return false;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	if (!setSystemTime(epoch)) {
		LOGW_TRACE("TimeKeeper: settimeofday(%lld) failed: %s",
				epoch, strerror(errno));
		return false;
	}

	mLastSyncUtc = epoch;
	saveStateLocked();
	LOGI_TRACE("TimeKeeper: setUtc epoch=%lld", epoch);
	return true;
}

void TimeKeeper::noteSynced(long long epoch) {
	std::lock_guard<std::mutex> lock(mMutex);
	if (epoch < kTimeValidEpoch || epoch > kMaxEpoch) {
		LOGW_TRACE("TimeKeeper: noteSynced ignored invalid epoch=%lld", epoch);
		return;
	}
	mLastSyncUtc = epoch;
	// 联网时每分钟都会记录一次；这里不立即写 flash，
	// 由 tick() 每 300 秒统一落盘，避免频繁写 flash。
	LOGD_TRACE("TimeKeeper: noteSynced epoch=%lld (persist deferred)", epoch);
}

long long TimeKeeper::lastSyncUtc() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mLastSyncUtc;
}

void TimeKeeper::tick() {
	std::lock_guard<std::mutex> lock(mMutex);

	const long long now = static_cast<long long>(time(NULL));
	if (now < kTimeValidEpoch) {
		return;  // 未对时：不要用 1970 覆盖已保存的可用时间
	}

	// 时钟被向前/向后校准后也允许尽快落盘一次
	const bool due = mLastPersistUtc <= 0 ||
			now >= mLastPersistUtc + kPersistIntervalSec ||
			now < mLastPersistUtc;
	if (due) {
		saveStateLocked();
	}
}

void TimeKeeper::saveState() {
	std::lock_guard<std::mutex> lock(mMutex);
	saveStateLocked();
}

void TimeKeeper::saveStateLocked() {
	const long long now = static_cast<long long>(time(NULL));
	if (now < kTimeValidEpoch) {
		LOGW_TRACE("TimeKeeper: skip saving invalid clock now=%lld", now);
		return;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "%lld,%lld", now, mLastSyncUtc);
	const std::string value(buf);
	const bool ok = StoragePreferences::putString(kClockStateKey, value);
	// 即使写失败也进入下一轮 300 秒节流，避免每秒重试磨损 flash
	mLastPersistUtc = now;
	if (ok) {
		LOGI_TRACE("TimeKeeper: saved clockState epoch=%lld lastSync=%lld",
				now, mLastSyncUtc);
	} else {
		LOGW_TRACE("TimeKeeper: save clockState failed epoch=%lld", now);
	}
}
