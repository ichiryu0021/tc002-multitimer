// 直播数据服务（设备端，单后台线程）。
//
// 数据流（契约 §5 / 设计 §5.1）：
//   HttpServer 线程 / mainLogic --setConfig()--> LiveService（只换配置快照）
//   LiveService 后台线程 --HttpClient/BilibiliClient--> bridge / B 站
//   结果 --applyUpdate--> LiveCache（mMutex 保护）
//   UI 线程 PageHost::tick --snapshotForPage()--> LiveSnapshot --PageRenderer-->
//
// 硬性规则：
// - 网络 I/O 期间不持锁：先取配置快照，解锁后发请求，结果回写时再取锁；
// - 不回调 PageHost、不画屏、不碰 SPI、不写 flash；
// - 同一时间只允许一个在途请求；切页/按键置 urgent，当前请求结束后立刻再发一次；
// - source=off 不请求；push 不轮询（由 POST /api/live 写缓存）。
#ifndef LIVE_LIVESERVICE_H_
#define LIVE_LIVESERVICE_H_

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "live/LiveCache.h"
#include "live/LiveTypes.h"
#include "net/mini_json.h"
#include "pages/PageTypes.h"
#include "timer/TimerTypes.h"

class LiveService {
public:
	static LiveService& getInstance();

	// 幂等；启动后常驻，进程退出前 shutdown()
	void start();
	void shutdown();
	bool isStarted() const;

	// 由完整 TimerConfig 派生：数据源 + 每页房间表（未配置房间不计入，最多 24 个）
	void setConfig(const TimerConfig& config);

	// UI 线程：切页后调用，置 urgent 立即刷新新页
	void noteActivePage(int pageIndex);

	// 任意线程：请求刷新；pageIndex<0 表示当前页。只置标记 + notify，不阻塞。
	void requestRefresh(int pageIndex = -1);
	void refreshNow();

	// UI 线程：取当前页 2 个槽位快照（短锁 + 小拷贝）
	bool snapshotForPage(int pageIndex, LiveSnapshot& out) const;

	// HTTP 线程：GET /api/live/status
	bool statusJson(JsonValue& out) const;

	// push 模式：POST /api/live（body 同 bridge 响应格式）
	bool pushJson(const JsonValue& body, int& updated, std::string& error);
	bool push(const std::vector<LiveRoomUpdate>& updates, int& updated);

	int activePage() const;
	int pageCount() const;
	LiveSourceKind sourceKind() const;
	int refreshSec() const;

private:
	LiveService();
	LiveService(const LiveService&);
	LiveService& operator=(const LiveService&);

	void run();

	// 网络层（无锁调用）：一次最多取当前页的 2 个房间。
	// 失败也会为每个请求的房间填一条 ok=false 的 update，保证"每个槽位都有结果"。
	bool fetchPage(LiveSourceKind kind, const std::string& url, int timeoutMs,
			const std::vector<LiveRoomKey>& keys,
			std::vector<LiveRoomUpdate>& updates, std::string& error) const;

	void applyResultsLocked(const std::vector<LiveRoomUpdate>& updates,
			long long monoMs, long long utcSec, bool networkOk,
			const std::string& error);

	mutable std::mutex mMutex;
	std::condition_variable mCondition;
	bool mStarted;
	bool mStop;

	LiveSourceConfig mSource;
	std::vector<std::vector<LiveRoomKey> > mPageKeys;
	std::vector<std::vector<std::string> > mPageLabels;  // 与 mPageKeys 并行
	int mActivePage;
	bool mUrgent;
	int mRequestPage;  // 本次 urgent 要刷新的页（-1=用 mActivePage）

	long long mLastAttemptMonoMs;
	long long mLastAttemptUtc;
	long long mLastOkMonoMs;
	long long mLastOkUtc;
	bool mLastAttemptOk;
	std::string mLastError;

	LiveCache mCache;
};

#endif  // LIVE_LIVESERVICE_H_
