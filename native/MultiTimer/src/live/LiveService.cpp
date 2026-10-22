#include "live/LiveService.h"

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "base/log.h"
#include "live/LiveFormat.h"
#include "net/HttpClient.h"
#include "net/WifiManager_task.h"
#include "time/TimeKeeper.h"

// B 站直连由 bili-tls Agent 提供。头文件尚未落地时按契约接口声明并给出弱符号
// 兜底实现，保证 device 侧可以先编译/链接；BilibiliClient.h 出现后自动切换。
#if defined(__has_include)
#if __has_include("net/BilibiliClient.h")
#define MULTITIMER_HAS_BILIBILI_CLIENT 1
#include "net/BilibiliClient.h"
#endif
#endif

#ifndef MULTITIMER_HAS_BILIBILI_CLIENT
#include "net/BilibiliParse.h"  // BilibiliRoomData（与 BilibiliClient.h 同一结构）

class BilibiliClient {
public:
	static bool fetchRoom(const std::string& roomId, BilibiliRoomData& out);
};

// 弱定义：链接时若 bili-tls 提供强符号（BilibiliClient.cpp），强符号优先。
bool __attribute__((weak)) BilibiliClient::fetchRoom(const std::string& roomId,
		BilibiliRoomData& out) {
	(void) roomId;
	out.clear();
	out.ok = false;
	out.error = "bilibili client pending";
	return false;
}
#endif  // MULTITIMER_HAS_BILIBILI_CLIENT

namespace {

// bridge 响应 / push body 的原始字节上限（任务口径 16 KB）
const size_t kMaxLiveResponseBytes = 16U * 1024U;

long long monotonicMillis() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<long long>(ts.tv_sec) * 1000LL +
			static_cast<long long>(ts.tv_nsec) / 1000000LL;
}

// 本机 libssl.so.1.1 实测 SSL_connect 直接返回 no protocols available
// （141640BF），所有 TLS 站点都不可用。B 站直连因此进入 60 分钟退避，
// 期间直接走 bridge，避免每个房间白等 2×timeout。退避到期后再试一次。
std::atomic<unsigned long> gBilibiliDirectRetryAtMs(0);
const unsigned long kBilibiliDirectBackoffMs = 60UL * 60UL * 1000UL;

bool bilibiliDirectSuspended() {
	const unsigned long until = gBilibiliDirectRetryAtMs.load();
	if (until == 0) {
		return false;
	}
	const unsigned long now = static_cast<unsigned long>(monotonicMillis());
	return static_cast<long>(now - until) < 0;
}

void suspendBilibiliDirect() {
	const unsigned long now = static_cast<unsigned long>(monotonicMillis());
	gBilibiliDirectRetryAtMs.store(now + kBilibiliDirectBackoffMs);
}

void clearBilibiliDirectSuspension() {
	gBilibiliDirectRetryAtMs.store(0);
}

int clampInt(int value, int minimum, int maximum) {
	if (value < minimum) {
		return minimum;
	}
	if (value > maximum) {
		return maximum;
	}
	return value;
}

bool readMetric(const JsonValue& object, const char* key, long long& out) {
	if (!object.contains(key)) {
		return false;
	}
	const JsonValue& value = object[key];
	if (value.isNumber()) {
		out = value.asInt64(0);
		return true;
	}
	if (value.isBool()) {
		out = value.asBool(false) ? 1LL : 0LL;
		return true;
	}
	return false;
}

// 解析 bridge 响应 / push body（契约 §5）。expected 非空时只接受其中的房间；
// 缺失的房间不在这里补失败，由调用方决定（bridge 判失败，push 保留旧值）。
bool parseLiveBody(const JsonValue& root,
		const std::vector<LiveRoomKey>& expected,
		std::vector<LiveRoomUpdate>& updates, std::string& error) {
	updates.clear();
	error.clear();
	if (!root.isObject()) {
		error = "live response must be an object";
		return false;
	}
	if (root.contains("ok") && root["ok"].isBool() &&
			!root["ok"].asBool(false)) {
		error = root.contains("error") && root["error"].isString()
				? root["error"].asString()
				: std::string("live source reported failure");
		return false;
	}
	if (!root.contains("rooms") || !root["rooms"].isArray()) {
		error = "missing rooms array";
		return false;
	}

	const JsonValue& rooms = root["rooms"];
	for (size_t i = 0; i < rooms.size(); ++i) {
		const JsonValue& item = rooms.at(i);
		if (!item.isObject()) {
			continue;
		}
		std::string platformText;
		std::string roomText;
		if (item.contains("platform") && item["platform"].isString()) {
			platformText = item["platform"].asString();
		}
		if (item.contains("room") && item["room"].isString()) {
			roomText = item["room"].asString();
		}
		if (roomText.empty() && item.contains("key") &&
				item["key"].isString()) {
			const std::string key = item["key"].asString();
			const size_t colon = key.find(':');
			if (colon != std::string::npos) {
				platformText = key.substr(0, colon);
				roomText = key.substr(colon + 1);
			}
		}
		LivePlatform platform;
		if (roomText.empty() || !parseLivePlatform(platformText, platform)) {
			continue;
		}

		LiveRoomKey key(platform, roomText);
		if (!expected.empty()) {
			bool found = false;
			for (size_t j = 0; j < expected.size(); ++j) {
				if (expected[j] == key) {
					found = true;
					break;
				}
			}
			if (!found) {
				continue;  // 未配置的房间：忽略
			}
		}

		LiveRoomUpdate update;
		update.key = key;
		update.ok = true;
		if (item.contains("ok") && item["ok"].isBool()) {
			update.ok = item["ok"].asBool(true);
		}
		if (item.contains("error") && item["error"].isString()) {
			const std::string text = item["error"].asString();
			if (!text.empty()) {
				update.ok = false;
				update.error = text;
			}
		}
		if (item.contains("title") && item["title"].isString()) {
			update.title = item["title"].asString();
		}

		const JsonValue& metrics =
				(item.contains("metrics") && item["metrics"].isObject())
						? item["metrics"]
						: item;
		long long number = 0;
		if (readMetric(metrics, "online", number)) {
			update.setOnline(number);
		}
		if (readMetric(metrics, "likes", number)) {
			update.setLikes(number);
		}
		if (readMetric(metrics, "fans", number)) {
			update.setFans(number);
		}
		if (readMetric(metrics, "status", number)) {
			update.setLiveStatus(number != 0 ? 1LL : 0LL);
		}
		updates.push_back(update);
	}
	return true;
}

// 为一个房间集合整体补失败结果
void appendFailures(const std::vector<LiveRoomKey>& keys,
		const std::string& error, std::vector<LiveRoomUpdate>& updates) {
	for (size_t i = 0; i < keys.size(); ++i) {
		if (keys[i].empty()) {
			continue;
		}
		LiveRoomUpdate update;
		update.key = keys[i];
		update.ok = false;
		update.error = error;
		updates.push_back(update);
	}
}

std::string appendRoomsQuery(const std::string& url,
		const std::vector<LiveRoomKey>& keys) {
	std::string out = url;
	if (out.empty()) {
		return out;
	}
	const char last = out[out.size() - 1];
	if (out.find('?') == std::string::npos) {
		out += '?';
	} else if (last != '?' && last != '&') {
		out += '&';
	}
	out += "rooms=";
	bool first = true;
	for (size_t i = 0; i < keys.size(); ++i) {
		if (keys[i].empty()) {
			continue;
		}
		if (!first) {
			out += ',';
		}
		out += livePlatformId(keys[i].platform);
		out += ':';
		out += keys[i].room;
		first = false;
	}
	return out;
}

}  // namespace

LiveService& LiveService::getInstance() {
	static LiveService instance;
	return instance;
}

LiveService::LiveService()
	: mStarted(false),
	  mStop(false),
	  mActivePage(0),
	  mUrgent(false),
	  mRequestPage(-1),
	  mLastAttemptMonoMs(0),
	  mLastAttemptUtc(0),
	  mLastOkMonoMs(0),
	  mLastOkUtc(0),
	  mLastAttemptOk(false) {
}

void LiveService::start() {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mStarted) {
			return;
		}
		mStarted = true;
		mStop = false;
	}
	try {
		std::thread(&LiveService::run, this).detach();
	} catch (...) {
		std::lock_guard<std::mutex> lock(mMutex);
		mStarted = false;
		LOGW_TRACE("LiveService: failed to start worker thread");
	}
}

void LiveService::shutdown() {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mStarted) {
			mStop = true;
			mCondition.notify_all();
			return;
		}
		mStop = true;
		mStarted = false;
	}
	mCondition.notify_all();
}

bool LiveService::isStarted() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mStarted;
}

void LiveService::setConfig(const TimerConfig& config) {
	std::vector<std::vector<LiveRoomKey> > pageKeys;
	std::vector<std::vector<std::string> > pageLabels;
	std::vector<LiveRoomKey> allKeys;
	const size_t maxRooms = 24;

	pageKeys.reserve(config.pages.size());
	pageLabels.reserve(config.pages.size());
	for (size_t page = 0; page < config.pages.size(); ++page) {
		std::vector<LiveRoomKey> keys;
		std::vector<std::string> labels;
		const PageConfig& pageConfig = config.pages[page];
		if (pageConfig.type == PAGE_TYPE_LIVE) {
			for (size_t slot = 0; slot < pageConfig.rooms.size() &&
					slot < static_cast<size_t>(kMaxRoomsPerLivePage); ++slot) {
				const LiveRoomConfig& room = pageConfig.rooms[slot];
				if (!LiveFormat::isCellConfigured(room)) {
					// 保持槽位对齐：未配置槽位用空键占位（渲染留黑）
					keys.push_back(LiveRoomKey());
					labels.push_back(std::string());
					continue;
				}
				if (allKeys.size() >= maxRooms) {
					keys.push_back(LiveRoomKey());
					labels.push_back(std::string());
					continue;
				}
				keys.push_back(LiveRoomKey(room.platform, room.room));
				labels.push_back(LiveFormat::labelFor(room));
				allKeys.push_back(LiveRoomKey(room.platform, room.room));
			}
		}
		pageKeys.push_back(keys);
		pageLabels.push_back(labels);
	}

	const LiveSourceKind logKind = config.live.kind;
	const int logRefreshSec = clampInt(config.live.refreshSec,
			kMinLiveRefreshSec, kMaxLiveRefreshSec);
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSource = config.live;
		mSource.refreshSec = logRefreshSec;
		mSource.timeoutMs = clampInt(mSource.timeoutMs, kMinLiveTimeoutMs,
				kMaxLiveTimeoutMs);
		mPageKeys.swap(pageKeys);
		mPageLabels.swap(pageLabels);
		if (mActivePage < 0 ||
				mActivePage >= static_cast<int>(mPageKeys.size())) {
			mActivePage = 0;
		}
		mCache.setRooms(allKeys, mSource.refreshSec);
		// 配置变化后立刻刷一次（下一轮循环不再等待 refreshSec）
		mUrgent = true;
		mRequestPage = mActivePage;
		mLastAttemptMonoMs = 0;
	}
	mCondition.notify_all();
	LOGI_TRACE("LiveService: config source=%s refreshSec=%d rooms=%u pages=%u",
			liveSourceId(logKind), logRefreshSec,
			static_cast<unsigned int>(allKeys.size()),
			static_cast<unsigned int>(config.pages.size()));
}

void LiveService::noteActivePage(int pageIndex) {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (pageIndex < 0 ||
				pageIndex >= static_cast<int>(mPageKeys.size())) {
			return;
		}
		mActivePage = pageIndex;
		mRequestPage = pageIndex;
		mUrgent = true;
	}
	mCondition.notify_all();
}

void LiveService::requestRefresh(int pageIndex) {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (pageIndex >= 0) {
			if (pageIndex >= static_cast<int>(mPageKeys.size())) {
				return;
			}
			// 只做一次性刷新，不改变 UI 的当前页（避免网页请求劫持轮询目标）
			mRequestPage = pageIndex;
		} else {
			mRequestPage = mActivePage;
		}
		mUrgent = true;
	}
	mCondition.notify_all();
}

void LiveService::refreshNow() {
	requestRefresh(-1);
}

bool LiveService::snapshotForPage(int pageIndex, LiveSnapshot& out) const {
	std::lock_guard<std::mutex> lock(mMutex);
	const long long monoMs = monotonicMillis();
	if (pageIndex < 0 ||
			pageIndex >= static_cast<int>(mPageKeys.size())) {
		LiveSnapshot empty;
		out = empty;
		return false;
	}
	mCache.snapshotPage(mPageKeys[static_cast<size_t>(pageIndex)], monoMs, out);
	return true;
}

bool LiveService::statusJson(JsonValue& out) const {
	std::lock_guard<std::mutex> lock(mMutex);
	const long long monoMs = monotonicMillis();

	JsonValue root = JsonValue::newObject();
	root.set("ok", JsonValue(true));

	JsonValue source = JsonValue::newObject();
	source.set("kind", JsonValue(liveSourceId(mSource.kind)));
	source.set("url", JsonValue(mSource.url));
	source.set("reachable", JsonValue(mLastAttemptOk));
	source.set("lastAttemptUtc", JsonValue(mLastAttemptUtc));
	source.set("lastOkUtc", JsonValue(mLastOkUtc));
	source.set("error", JsonValue(mLastError));
	root.set("source", source);
	root.set("refreshSec", JsonValue(mSource.refreshSec));
	root.set("timeoutMs", JsonValue(mSource.timeoutMs));
	root.set("activePage", JsonValue(mActivePage));

	JsonValue rooms = JsonValue::newArray();
	for (size_t page = 0; page < mPageKeys.size(); ++page) {
		for (size_t slot = 0; slot < mPageKeys[page].size(); ++slot) {
			const LiveRoomKey& key = mPageKeys[page][slot];
			if (key.empty()) {
				continue;
			}
			LiveRoomEntry entry;
			const bool found = mCache.entry(key, entry);
			long long ageSec = 0;
			if (found && entry.lastOkMonoMs > 0) {
				const long long age = monoMs - entry.lastOkMonoMs;
				ageSec = age > 0 ? age / 1000 : 0;
			}
			JsonValue item = JsonValue::newObject();
			item.set("page", JsonValue(static_cast<int>(page)));
			item.set("slot", JsonValue(static_cast<int>(slot)));
			item.set("platform", JsonValue(livePlatformId(key.platform)));
			item.set("room", JsonValue(key.room));
			item.set("label", JsonValue(
					slot < mPageLabels[page].size()
							? mPageLabels[page][slot]
							: std::string()));
			item.set("state", JsonValue(liveRoomStateId(
					mCache.state(key, monoMs))));
			item.set("updatedUtc", JsonValue(found ? entry.updatedUtc : 0LL));
			item.set("ageSec", JsonValue(ageSec));
			if (found) {
				JsonValue metrics = JsonValue::newObject();
				if (entry.online.valid) {
					metrics.set("online", JsonValue(entry.online.value));
				}
				if (entry.likes.valid) {
					metrics.set("likes", JsonValue(entry.likes.value));
				}
				if (entry.fans.valid) {
					metrics.set("fans", JsonValue(entry.fans.value));
				}
				if (entry.liveStatus.valid) {
					metrics.set("status", JsonValue(entry.liveStatus.value));
				}
				item.set("metrics", metrics);
				if (!entry.lastError.empty()) {
					item.set("error", JsonValue(entry.lastError));
				}
			}
			rooms.pushBack(item);
		}
	}
	root.set("rooms", rooms);
	out = root;
	return true;
}

bool LiveService::pushJson(const JsonValue& body, int& updated,
		std::string& error) {
	updated = 0;
	error.clear();
	std::vector<LiveRoomKey> expected;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mCache.keys(expected);
	}
	std::vector<LiveRoomUpdate> updates;
	if (!parseLiveBody(body, expected, updates, error)) {
		return false;
	}
	return push(updates, updated);
}

bool LiveService::push(const std::vector<LiveRoomUpdate>& updates,
		int& updated) {
	updated = 0;
	std::lock_guard<std::mutex> lock(mMutex);
	const long long monoMs = monotonicMillis();
	const long long utcSec = static_cast<long long>(time(NULL));
	bool anyOk = false;
	for (size_t i = 0; i < updates.size(); ++i) {
		const LiveRoomUpdate& update = updates[i];
		if (!mCache.hasRoom(update.key)) {
			continue;  // 未配置/已删除的房间
		}
		if (update.ok) {
			mCache.applyUpdate(update, monoMs, utcSec);
			++updated;
			anyOk = true;
		} else {
			mCache.applyFailure(update.key, monoMs, update.error);
		}
	}
	if (anyOk) {
		mLastAttemptOk = true;
		mLastOkMonoMs = monoMs;
		mLastOkUtc = utcSec;
		mLastError.clear();
	}
	return updated > 0;
}

int LiveService::activePage() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mActivePage;
}

int LiveService::pageCount() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return static_cast<int>(mPageKeys.size());
}

LiveSourceKind LiveService::sourceKind() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mSource.kind;
}

int LiveService::refreshSec() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mSource.refreshSec;
}

void LiveService::run() {
	while (true) {
		std::vector<LiveRoomKey> keys;
		LiveSourceKind kind = LIVE_SOURCE_OFF;
		std::string url;
		int timeoutMs = kDefaultLiveTimeoutMs;
		int refreshSec = kDefaultLiveRefreshSec;
		bool urgent = false;

		{
			std::unique_lock<std::mutex> lock(mMutex);
			if (mStop) {
				return;
			}
			const long long nowMono = monotonicMillis();
			urgent = mUrgent;
			int fetchPage = mActivePage;
			if (mRequestPage >= 0 &&
					mRequestPage < static_cast<int>(mPageKeys.size())) {
				fetchPage = mRequestPage;
			}

			kind = mSource.kind;
			url = mSource.url;
			timeoutMs = clampInt(mSource.timeoutMs, kMinLiveTimeoutMs,
					kMaxLiveTimeoutMs);
			refreshSec = clampInt(mSource.refreshSec, kMinLiveRefreshSec,
					kMaxLiveRefreshSec);

			if (fetchPage < 0 ||
					fetchPage >= static_cast<int>(mPageKeys.size())) {
				mUrgent = false;
				mRequestPage = -1;
				mCondition.wait_for(lock, std::chrono::seconds(1));
				continue;
			}
			keys = mPageKeys[static_cast<size_t>(fetchPage)];

			bool hasRoom = false;
			for (size_t i = 0; i < keys.size(); ++i) {
				if (!keys[i].empty()) {
					hasRoom = true;
					break;
				}
			}

			// off：不请求；push：不轮询；本页没有房间：不请求
			if (kind == LIVE_SOURCE_OFF || kind == LIVE_SOURCE_PUSH ||
					!hasRoom) {
				mUrgent = false;
				mRequestPage = -1;
				mCondition.wait_for(lock, std::chrono::seconds(1));
				continue;
			}
			// 无网络时跳过本轮（不消耗 timeout），WiFi 恢复后下一轮自动生效；
			// 保留 mUrgent，WiFi 回来后立刻刷一次。
			if (!wifiIsUp()) {
				mCondition.wait_for(lock, std::chrono::seconds(1));
				continue;
			}

			const long long nextDue =
					(mLastAttemptMonoMs > 0)
							? mLastAttemptMonoMs +
									static_cast<long long>(refreshSec) * 1000LL
							: 0;
			if (!urgent && nextDue > nowMono) {
				mCondition.wait_for(lock,
						std::chrono::milliseconds(nextDue - nowMono));
				continue;
			}

			// 单飞：先记尝试时间，网络期间不再持锁
			mUrgent = false;
			mRequestPage = -1;
			mLastAttemptMonoMs = nowMono;
			mLastAttemptUtc = static_cast<long long>(time(NULL));
			for (size_t i = 0; i < keys.size(); ++i) {
				if (!keys[i].empty()) {
					mCache.noteAttempt(keys[i], nowMono);
				}
			}
		}

		std::vector<LiveRoomUpdate> updates;
		std::string error;
		const bool networkOk = fetchPage(kind, url, timeoutMs, keys, updates,
				error);
		const long long resultMono = monotonicMillis();
		const long long resultUtc = static_cast<long long>(time(NULL));

		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mStop) {
				return;
			}
			applyResultsLocked(updates, resultMono, resultUtc, networkOk,
					error);
		}
	}
}

bool LiveService::fetchPage(LiveSourceKind kind, const std::string& url,
		int timeoutMs, const std::vector<LiveRoomKey>& keys,
		std::vector<LiveRoomUpdate>& updates, std::string& error) const {
	updates.clear();
	error.clear();

	std::vector<LiveRoomKey> bridgeKeys;
	for (size_t i = 0; i < keys.size(); ++i) {
		const LiveRoomKey& key = keys[i];
		if (key.empty()) {
			continue;
		}
		if (kind == LIVE_SOURCE_HYBRID &&
				key.platform == LIVE_PLATFORM_BILIBILI) {
			// B 站优先设备直连：镜像携带 OpenSSL 1.1.1w 到 /res/lib，
			// TlsClient 优先加载它；系统自带 1.1.0i 没有 TLS client
			// method，只有 bundle 缺失时才会失败。失败且配置了 bridge
			// 时回退 bridge，并进入 60 分钟退避，避免每轮空等。
			std::string directError;
			if (!bilibiliDirectSuspended()) {
				BilibiliRoomData data;
				const bool fetched = BilibiliClient::fetchRoom(key.room, data,
						timeoutMs);
				if (fetched && data.ok) {
					clearBilibiliDirectSuspension();
					LiveRoomUpdate update;
					update.key = key;
					update.ok = true;
					update.setOnline(data.online);
					update.setLikes(data.likes);
					update.setFans(data.fans);
					update.setLiveStatus(data.liveStatus != 0 ? 1LL : 0LL);
					update.title = data.title;
					updates.push_back(update);
					continue;
				}
				directError = data.error.empty()
						? std::string("bilibili direct failed")
						: data.error;
				suspendBilibiliDirect();
				LOGW_TRACE("LiveService: bilibili direct suspended: %s",
						directError.c_str());
			} else {
				directError = "bilibili direct suspended after failure";
			}
			if (!url.empty()) {
				bridgeKeys.push_back(key);
				continue;
			}
			LiveRoomUpdate update;
			update.key = key;
			update.ok = false;
			update.error = directError;
			updates.push_back(update);
			continue;
		}
		bridgeKeys.push_back(key);
	}

	if (bridgeKeys.empty()) {
		return true;
	}
	if (url.empty()) {
		error = "live.url is not configured";
		appendFailures(bridgeKeys, error, updates);
		return false;
	}

	const std::string requestUrl = appendRoomsQuery(url, bridgeKeys);
	HttpClientResponse response;
	if (!HttpClient::get(requestUrl, timeoutMs, kMaxLiveResponseBytes,
			response)) {
		error = response.error.empty() ? std::string("bridge request failed")
				: response.error;
		appendFailures(bridgeKeys, error, updates);
		return false;
	}
	if (response.statusCode != 200) {
		char buffer[48];
		snprintf(buffer, sizeof(buffer), "bridge http status %d",
				response.statusCode);
		error = buffer;
		appendFailures(bridgeKeys, error, updates);
		return false;
	}

	JsonValue root;
	std::string parseError;
	if (!JsonValue::parse(response.body, root, parseError)) {
		error = "bridge json: " + parseError;
		appendFailures(bridgeKeys, error, updates);
		return false;
	}
	std::vector<LiveRoomUpdate> parsed;
	if (!parseLiveBody(root, bridgeKeys, parsed, parseError)) {
		error = parseError.empty() ? std::string("bridge response invalid")
				: parseError;
		appendFailures(bridgeKeys, error, updates);
		return false;
	}

	for (size_t i = 0; i < parsed.size(); ++i) {
		updates.push_back(parsed[i]);
	}
	// 响应里缺失的房间：该格失败（保留旧值，按陈旧规则变暗）
	for (size_t i = 0; i < bridgeKeys.size(); ++i) {
		bool found = false;
		for (size_t j = 0; j < parsed.size(); ++j) {
			if (parsed[j].key == bridgeKeys[i]) {
				found = true;
				break;
			}
		}
		if (!found) {
			LiveRoomUpdate update;
			update.key = bridgeKeys[i];
			update.ok = false;
			update.error = "missing in bridge response";
			updates.push_back(update);
		}
	}
	return true;
}

void LiveService::applyResultsLocked(
		const std::vector<LiveRoomUpdate>& updates, long long monoMs,
		long long utcSec, bool networkOk, const std::string& error) {
	bool anyItemOk = false;
	for (size_t i = 0; i < updates.size(); ++i) {
		const LiveRoomUpdate& update = updates[i];
		if (!mCache.hasRoom(update.key)) {
			continue;
		}
		if (update.ok) {
			mCache.applyUpdate(update, monoMs, utcSec);
			anyItemOk = true;
		} else {
			mCache.applyFailure(update.key, monoMs, update.error);
		}
	}

	if (networkOk && anyItemOk) {
		mLastAttemptOk = true;
		mLastOkMonoMs = monoMs;
		mLastOkUtc = utcSec;
		mLastError.clear();
		return;
	}
	mLastAttemptOk = false;
	mLastError = error.empty() ? std::string("no live data") : error;
}
