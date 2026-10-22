// 系统时钟封装：校准、有效性、离线恢复、周期持久化。
// - 设备无 RTC、无 tzdata；系统时区固定 UTC（init.rc sysclktz 0）
// - 持久化使用官方 StoragePreferences（键 clockState，值 "epoch,lastSync"）
#ifndef TIME_TIMEKEEPER_H_
#define TIME_TIMEKEEPER_H_

#include <mutex>

class TimeKeeper {
public:
	static const long long kTimeValidEpoch = 1700000000LL;  // 2023-11-14
	static const long long kMaxEpoch = 4102444800LL;        // 2100-01-01

	static TimeKeeper& getInstance();

	// 启动早期调用一次：系统时间无效时用最后保存的 epoch 恢复
	void restore();

	bool isValid() const;
	long long now() const;

	// 校准系统时间（UTC 秒）；范围 2023-11..2100，成功后立即持久化
	bool setUtc(long long epoch);

	// SNTP 成功后调用：记录最近同步时间并持久化
	void noteSynced(long long epoch);

	long long lastSyncUtc() const;

	// 每秒调用（UI 定时器）；内部每 300 秒把当前时间写入 flash
	void tick();

	// 立即持久化
	void saveState();

private:
	TimeKeeper();
	TimeKeeper(const TimeKeeper&);
	TimeKeeper& operator=(const TimeKeeper&);

	long long readSavedEpoch() const;
	void saveStateLocked();
	void loadState(long long& savedEpoch, long long& savedSync) const;

	mutable std::mutex mMutex;
	long long mLastSyncUtc;
	long long mLastPersistUtc;
	bool mRestored;
};

#endif  // TIME_TIMEKEEPER_H_
