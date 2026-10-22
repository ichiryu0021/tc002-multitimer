#include "live/LiveCache.h"

#include <stddef.h>

#include <algorithm>

namespace {

// 契约 §5：全局缓存上限 24 个房间（12 页 x 2）
const size_t kMaxCachedRooms = 24;
// 陈旧阈值下限：3 x refreshSec 但不少于 60s（设计 §5.4）
const long long kMinStaleMs = 60000;

int clampRefreshSec(int value) {
	if (value < kMinLiveRefreshSec) {
		return kMinLiveRefreshSec;
	}
	if (value > kMaxLiveRefreshSec) {
		return kMaxLiveRefreshSec;
	}
	return value;
}

}  // namespace

LiveRoomKey::LiveRoomKey() : platform(LIVE_PLATFORM_BILIBILI) {
}

LiveRoomKey::LiveRoomKey(LivePlatform platformValue,
		const std::string& roomValue)
	: platform(platformValue), room(roomValue) {
}

bool LiveRoomKey::operator==(const LiveRoomKey& other) const {
	return platform == other.platform && room == other.room;
}

bool LiveRoomKey::operator<(const LiveRoomKey& other) const {
	if (platform != other.platform) {
		return static_cast<int>(platform) < static_cast<int>(other.platform);
	}
	return room < other.room;
}

LiveRoomUpdate::LiveRoomUpdate()
	: ok(true),
	  hasOnline(false),
	  hasLikes(false),
	  hasFans(false),
	  hasLiveStatus(false),
	  online(0),
	  likes(0),
	  fans(0),
	  liveStatus(0) {
}

void LiveRoomUpdate::clear() {
	key = LiveRoomKey();
	ok = true;
	hasOnline = false;
	hasLikes = false;
	hasFans = false;
	hasLiveStatus = false;
	online = 0;
	likes = 0;
	fans = 0;
	liveStatus = 0;
	title.clear();
	error.clear();
}

void LiveRoomUpdate::setOnline(long long value) {
	hasOnline = true;
	online = value;
}

void LiveRoomUpdate::setLikes(long long value) {
	hasLikes = true;
	likes = value;
}

void LiveRoomUpdate::setFans(long long value) {
	hasFans = true;
	fans = value;
}

void LiveRoomUpdate::setLiveStatus(long long value) {
	hasLiveStatus = true;
	liveStatus = value;
}

LiveRoomEntry::LiveRoomEntry()
	: updatedUtc(0),
	  lastOkMonoMs(0),
	  lastAttemptMonoMs(0),
	  failures(0),
	  attempted(false) {
}

bool LiveRoomEntry::hasAnyValue() const {
	return online.valid || likes.valid || fans.valid || liveStatus.valid;
}

LiveCache::LiveCache() : mRefreshSec(kDefaultLiveRefreshSec) {
}

void LiveCache::setRooms(const std::vector<LiveRoomKey>& rooms,
		int refreshSec) {
	mRefreshSec = clampRefreshSec(refreshSec);

	std::map<LiveRoomKey, LiveRoomEntry> next;
	for (size_t i = 0; i < rooms.size(); ++i) {
		const LiveRoomKey& key = rooms[i];
		if (key.empty()) {
			continue;
		}
		if (next.size() >= kMaxCachedRooms) {
			break;
		}
		std::map<LiveRoomKey, LiveRoomEntry>::const_iterator existing =
				mEntries.find(key);
		if (existing != mEntries.end()) {
			next.insert(*existing);  // 保留旧值：同一房间跨页/重载配置共享
			continue;
		}
		LiveRoomEntry entry;
		entry.key = key;
		next.insert(std::make_pair(key, entry));
	}
	mEntries.swap(next);
}

bool LiveCache::hasRoom(const LiveRoomKey& key) const {
	return mEntries.find(key) != mEntries.end();
}

void LiveCache::noteAttempt(const LiveRoomKey& key, long long monoMs) {
	std::map<LiveRoomKey, LiveRoomEntry>::iterator it = mEntries.find(key);
	if (it == mEntries.end()) {
		return;
	}
	it->second.attempted = true;
	it->second.lastAttemptMonoMs = monoMs;
}

void LiveCache::applyUpdate(const LiveRoomUpdate& update, long long monoMs,
		long long utcSec) {
	std::map<LiveRoomKey, LiveRoomEntry>::iterator it =
			mEntries.find(update.key);
	if (it == mEntries.end()) {
		return;  // 配置已变更，丢弃过期结果
	}
	LiveRoomEntry& entry = it->second;
	entry.attempted = true;
	entry.lastAttemptMonoMs = monoMs;

	// hasXxx 才覆盖：缺失字段保留旧值（bridge 可能只返回部分指标）
	if (update.hasOnline) {
		entry.online.set(update.online);
	}
	if (update.hasLikes) {
		entry.likes.set(update.likes);
	}
	if (update.hasFans) {
		entry.fans.set(update.fans);
	}
	if (update.hasLiveStatus) {
		entry.liveStatus.set(update.liveStatus);
	}

	if (!update.ok) {
		++entry.failures;
		entry.lastError = update.error.empty() ? std::string("failed")
				: update.error;
		return;
	}

	entry.lastOkMonoMs = monoMs;
	entry.updatedUtc = utcSec;
	entry.failures = 0;
	entry.lastError.clear();
}

void LiveCache::applyFailure(const LiveRoomKey& key, long long monoMs,
		const std::string& error) {
	std::map<LiveRoomKey, LiveRoomEntry>::iterator it = mEntries.find(key);
	if (it == mEntries.end()) {
		return;
	}
	LiveRoomEntry& entry = it->second;
	entry.attempted = true;
	entry.lastAttemptMonoMs = monoMs;
	++entry.failures;
	entry.lastError = error.empty() ? std::string("failed") : error;
}

LiveRoomState LiveCache::stateFor(const LiveRoomEntry& entry,
		long long monoMs) const {
	if (entry.lastOkMonoMs <= 0) {
		return entry.attempted ? LIVE_ROOM_ERROR : LIVE_ROOM_WAIT;
	}
	const long long age = monoMs - entry.lastOkMonoMs;
	if (age < 0) {
		return LIVE_ROOM_OK;  // 单调时钟异常时不把数据误判为陈旧
	}
	if (age > staleMs()) {
		return LIVE_ROOM_STALE;
	}
	return LIVE_ROOM_OK;
}

long long LiveCache::staleMs() const {
	const long long value = static_cast<long long>(mRefreshSec) * 3LL * 1000LL;
	return value < kMinStaleMs ? kMinStaleMs : value;
}

void LiveCache::snapshotPage(const std::vector<LiveRoomKey>& pageRooms,
		long long monoMs, LiveSnapshot& out) const {
	out.clear();
	for (int i = 0; i < kLiveCellCount; ++i) {
		LiveRoomData& cell = out.rooms[i];
		cell.clear();
		if (i >= static_cast<int>(pageRooms.size())) {
			continue;  // 空槽：EMPTY，渲染留黑
		}
		const LiveRoomKey& key = pageRooms[static_cast<size_t>(i)];
		if (key.empty()) {
			continue;
		}
		std::map<LiveRoomKey, LiveRoomEntry>::const_iterator it =
				mEntries.find(key);
		if (it == mEntries.end()) {
			cell.state = LIVE_ROOM_WAIT;
			continue;
		}
		const LiveRoomEntry& entry = it->second;
		cell.online = entry.online;
		cell.likes = entry.likes;
		cell.fans = entry.fans;
		cell.liveStatus = entry.liveStatus;
		cell.updatedUtc = entry.updatedUtc;
		cell.error = entry.lastError;
		cell.state = stateFor(entry, monoMs);
	}
}

bool LiveCache::entry(const LiveRoomKey& key, LiveRoomEntry& out) const {
	std::map<LiveRoomKey, LiveRoomEntry>::const_iterator it =
			mEntries.find(key);
	if (it == mEntries.end()) {
		return false;
	}
	out = it->second;
	return true;
}

void LiveCache::entries(std::vector<LiveRoomEntry>& out) const {
	out.clear();
	out.reserve(mEntries.size());
	for (std::map<LiveRoomKey, LiveRoomEntry>::const_iterator it =
			mEntries.begin(); it != mEntries.end(); ++it) {
		out.push_back(it->second);
	}
}

LiveRoomState LiveCache::state(const LiveRoomKey& key, long long monoMs) const {
	std::map<LiveRoomKey, LiveRoomEntry>::const_iterator it =
			mEntries.find(key);
	if (it == mEntries.end()) {
		return LIVE_ROOM_EMPTY;
	}
	return stateFor(it->second, monoMs);
}

void LiveCache::keys(std::vector<LiveRoomKey>& out) const {
	out.clear();
	out.reserve(mEntries.size());
	for (std::map<LiveRoomKey, LiveRoomEntry>::const_iterator it =
			mEntries.begin(); it != mEntries.end(); ++it) {
		out.push_back(it->first);
	}
}

// static
const char* liveRoomStateId(LiveRoomState state) {
	switch (state) {
		case LIVE_ROOM_EMPTY:
			return "empty";
		case LIVE_ROOM_WAIT:
			return "wait";
		case LIVE_ROOM_OK:
			return "ok";
		case LIVE_ROOM_STALE:
			return "stale";
		case LIVE_ROOM_ERROR:
			return "error";
	}
	return "wait";
}
