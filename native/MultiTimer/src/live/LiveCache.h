// 直播数据缓存（纯 C++11，不依赖平台 SDK；由 LiveService::mMutex 统一保护）。
//
// 设计要点（契约 §5 / 设计 §5.3）：
// - 键 = (platform, room)，跨页共享；最多 24 条（配置上限 12 页 x 2 房间）；
// - 值 = 各指标（valid 与 0 严格区分）+ 最近成功/尝试时间 + 失败计数；
// - 状态（EMPTY/WAIT/OK/STALE/ERROR）不落库，由 snapshot 时按单调时钟现算：
//     从未成功 + 未尝试 -> WAIT
//     从未成功 + 尝试过 -> ERROR
//     有成功记录，年龄 <= max(3 x refreshSec, 60s) -> OK
//     有成功记录，年龄超过阈值 -> STALE（旧值继续显示，dim）
// - 本类不做 I/O、不加锁（调用方负责），不做时间显示格式化。
#ifndef LIVE_LIVECACHE_H_
#define LIVE_LIVECACHE_H_

#include <map>
#include <string>
#include <vector>

#include "live/LiveTypes.h"
#include "pages/PageTypes.h"

// 房间唯一键。room 为空表示“空槽”（未配置），不进入缓存。
struct LiveRoomKey {
	LivePlatform platform;
	std::string room;

	LiveRoomKey();
	LiveRoomKey(LivePlatform platform, const std::string& room);

	bool empty() const {
		return room.empty();
	}
	bool operator==(const LiveRoomKey& other) const;
	bool operator<(const LiveRoomKey& other) const;
};

// 一次抓取结果（bridge 单项 / B 站直连 / push 项）。
// hasXxx=false 表示该指标本次没有提供（保留旧值，不要清空）。
struct LiveRoomUpdate {
	LiveRoomKey key;
	bool ok;
	bool hasOnline;
	bool hasLikes;
	bool hasFans;
	bool hasLiveStatus;
	long long online;
	long long likes;
	long long fans;
	long long liveStatus;  // 0=未开播 1=直播中
	std::string title;
	std::string error;

	LiveRoomUpdate();
	void clear();

	void setOnline(long long value);
	void setLikes(long long value);
	void setFans(long long value);
	void setLiveStatus(long long value);
};

// 缓存条目（statusJson / 调试用）。渲染只读 snapshotPage 的结果。
struct LiveRoomEntry {
	LiveRoomKey key;
	LiveMetricValue online;
	LiveMetricValue likes;
	LiveMetricValue fans;
	LiveMetricValue liveStatus;
	long long updatedUtc;       // 最近成功时的 UTC 秒（0=从未成功）
	long long lastOkMonoMs;     // 最近成功的单调毫秒（0=从未成功）
	long long lastAttemptMonoMs;
	int failures;               // 连续失败次数
	bool attempted;             // 是否尝试过
	std::string lastError;

	LiveRoomEntry();
	bool hasAnyValue() const;
};

class LiveCache {
public:
	LiveCache();

	// 用配置里的房间重建房间表；已有条目保留（同房间跨页共享）。
	// refreshSec 用于陈旧阈值：max(3 x refreshSec, 60s)。
	void setRooms(const std::vector<LiveRoomKey>& rooms, int refreshSec);

	bool hasRoom(const LiveRoomKey& key) const;

	void noteAttempt(const LiveRoomKey& key, long long monoMs);
	void applyUpdate(const LiveRoomUpdate& update, long long monoMs,
			long long utcSec);
	void applyFailure(const LiveRoomKey& key, long long monoMs,
			const std::string& error);

	// 当前页 2 个槽位的快照（槽位顺序 = PageConfig::rooms 下标）。
	void snapshotPage(const std::vector<LiveRoomKey>& pageRooms,
			long long monoMs, LiveSnapshot& out) const;

	bool entry(const LiveRoomKey& key, LiveRoomEntry& out) const;
	void entries(std::vector<LiveRoomEntry>& out) const;
	void keys(std::vector<LiveRoomKey>& out) const;

	// 单个房间的当前状态（/api/live/status 用）；未配置/不在缓存返回 EMPTY。
	LiveRoomState state(const LiveRoomKey& key, long long monoMs) const;

	int refreshSec() const {
		return mRefreshSec;
	}
	long long staleMs() const;

private:
	LiveRoomState stateFor(const LiveRoomEntry& entry,
			long long monoMs) const;

	std::map<LiveRoomKey, LiveRoomEntry> mEntries;
	int mRefreshSec;
};

// 房间状态 -> /api/live/status 的字符串（"empty"/"wait"/"ok"/"stale"/"error"）。
const char* liveRoomStateId(LiveRoomState state);

#endif  // LIVE_LIVECACHE_H_
