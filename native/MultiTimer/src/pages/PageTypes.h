// v3 页面 / 直播配置数据结构（纯 C++11，不依赖任何平台 SDK 头）。
//
// 契约来源：docs/V3-CONTRACT.md §2。字段名、枚举值、上限都以契约为准；
// 本头被 timer/TimerTypes.h include，因此这里**不能**反向 include TimerTypes.h。
//
// 约定：
// - 枚举到 JSON 字符串的映射（*Id / parse*）全部放在本头，ConfigStore 与
//   PageRenderer / LiveFormat 共用，避免两处各写一套字符串表。
// - `LIVE_METRIC_INHERIT` 只用于 `pages[].rooms[].metric`：JSON 里空字符串表示
//   “使用页面默认指标”，与显式的 `"none"` 区分开。
#ifndef PAGES_PAGETYPES_H_
#define PAGES_PAGETYPES_H_

#include <stdint.h>

#include <string>
#include <vector>

// 页面类型（契约 §2 pages[].type）。数值只用于 RAM 比较，不落盘。
enum PageType {
	PAGE_TYPE_CLOCK = 0,            // 全屏时钟
	PAGE_TYPE_CLOCK_COUNTDOWN = 1,  // 上半时钟 + 下半倒计时/正计时
	PAGE_TYPE_COUNTDOWN = 2,        // 倒计时/正计时；无目标显示 NO COUNTDOWN
	PAGE_TYPE_LIVE = 3,             // 上下两行直播数据（一页 2 个房间）
	PAGE_TYPE_DATETIME = 4          // 日期 + 时间（时间大字 + 日期小字）
};

// 直播平台（契约 §3 平台颜色：B/D/K；H/Y/C 为扩展位，颜色见 LiveFormat）。
enum LivePlatform {
	LIVE_PLATFORM_BILIBILI = 0,
	LIVE_PLATFORM_DOUYIN = 1,
	LIVE_PLATFORM_KUAISHOU = 2,
	LIVE_PLATFORM_HUYA = 3,
	LIVE_PLATFORM_DOUYU = 4,
	LIVE_PLATFORM_CUSTOM = 5
};

// 指标（契约 §2 指标枚举）。v3.0 屏幕上只显示 online/likes/fans/status；
// title 保留给网页 / 推送；none 表示不显示值。
enum LiveMetric {
	LIVE_METRIC_INHERIT = -1,  // 仅 rooms[].metric：继承页面默认
	LIVE_METRIC_NONE = 0,
	LIVE_METRIC_ONLINE = 1,
	LIVE_METRIC_LIKES = 2,
	LIVE_METRIC_FANS = 3,
	LIVE_METRIC_STATUS = 4,
	LIVE_METRIC_TITLE = 5
};

// clock_countdown 页无未来目标时，下半区显示日期还是空白（契约 §2 display）。
enum SplitIdle {
	SPLIT_IDLE_DATE = 0,   // "date"（默认）
	SPLIT_IDLE_BLANK = 1   // "blank"
};

// 直播数据源（契约 §2 live.source）：
//   off    = 不取数，直播页显示 `--`
//   bridge = 抖音/快手走局域网 bridge（明文 HTTP）
//   hybrid = B 站设备直连 + 抖音/快手走 bridge（契约 §2/§5）
//   push   = 外部 POST /api/live 推送
enum LiveSourceKind {
	LIVE_SOURCE_OFF = 0,
	LIVE_SOURCE_BRIDGE = 1,
	LIVE_SOURCE_HYBRID = 2,
	LIVE_SOURCE_PUSH = 3
};

// ---- 契约上限（ConfigStore 校验、网页提示、测试共用）----
const int kMaxPageCount = 12;          // pages 1..12
const int kMaxRoomsPerLivePage = 2;    // live 页 rooms 1..2（一页两个直播间）
const int kMaxPageIdChars = 8;         // id [a-z0-9_-]{1,8}
const int kMaxPageTitleChars = 16;     // title <= 16 个字符（UTF-8 码点）
const int kMaxShortTitleChars = 8;     // shortTitle <= 8 个可打印 ASCII
const int kMaxRoomChars = 40;          // room <= 40
const int kMaxRoomLabelChars = 3;      // label <= 3 ASCII
const int kMaxLiveValueChars = 6;      // live 单元格 value <= 6 字符（契约 §3）
const int kMaxLiveUrlChars = 160;      // live.url <= 160
const int kMinLiveRefreshSec = 15;
const int kMaxLiveRefreshSec = 300;
const int kDefaultLiveRefreshSec = 30;
const int kMinLiveTimeoutMs = 1000;
const int kMaxLiveTimeoutMs = 5000;
const int kDefaultLiveTimeoutMs = 3000;

// 一个直播房间槽位（契约 §2 pages[].rooms[]）。
struct LiveRoomConfig {
	LivePlatform platform;
	std::string room;    // 空 = 空槽（不轮询、不显示）
	std::string label;   // 已规范化为大写、0..3 字符；空 = 自动生成
	bool enabled;        // false = 该格留黑
	LiveMetric metric;   // INHERIT = 用页默认

	LiveRoomConfig()
		: platform(LIVE_PLATFORM_BILIBILI),
		  enabled(true),
		  metric(LIVE_METRIC_INHERIT) {
	}
};

// 一个页面（契约 §2 pages[]）。
struct PageConfig {
	std::string id;          // [a-z0-9_-]{1,8}，唯一
	PageType type;
	std::string title;       // <=16 字符，仅网页/API
	std::string shortTitle;  // <=8 可打印 ASCII；空 = 类型短名
	bool enabled;            // 旋钮跳过 disabled
	LiveMetric metric;       // live 页默认指标
	std::vector<LiveRoomConfig> rooms;  // 仅 live 页，1..4（可少于 4，渲染补空）

	PageConfig()
		: type(PAGE_TYPE_CLOCK),
		  enabled(true),
		  metric(LIVE_METRIC_ONLINE) {
	}
};

// 全局直播数据源配置（契约 §2 live）。
struct LiveSourceConfig {
	LiveSourceKind kind;
	std::string url;
	int refreshSec;
	int timeoutMs;

	LiveSourceConfig()
		: kind(LIVE_SOURCE_OFF),
		  refreshSec(kDefaultLiveRefreshSec),
		  timeoutMs(kDefaultLiveTimeoutMs) {
	}
};

// ---- 枚举 <-> JSON 字符串 ----

inline const char* pageTypeId(PageType type) {
	switch (type) {
		case PAGE_TYPE_CLOCK:
			return "clock";
		case PAGE_TYPE_CLOCK_COUNTDOWN:
			return "clock_countdown";
		case PAGE_TYPE_COUNTDOWN:
			return "countdown";
		case PAGE_TYPE_LIVE:
			return "live";
		case PAGE_TYPE_DATETIME:
			return "datetime";
	}
	return "clock";
}

inline bool parsePageType(const std::string& id, PageType& out) {
	if (id == "clock") {
		out = PAGE_TYPE_CLOCK;
		return true;
	}
	if (id == "clock_countdown") {
		out = PAGE_TYPE_CLOCK_COUNTDOWN;
		return true;
	}
	if (id == "countdown") {
		out = PAGE_TYPE_COUNTDOWN;
		return true;
	}
	if (id == "live") {
		out = PAGE_TYPE_LIVE;
		return true;
	}
	if (id == "datetime") {
		out = PAGE_TYPE_DATETIME;
		return true;
	}
	return false;
}

// 切页横幅的兜底短名（shortTitle 为空时使用）。
// 注意 "COUNTDOWN" 是 9 个字符，只作为“空 shortTitle”的运行时显示名，
// 不写入配置；shortTitle 本身的硬上限仍是 8（契约 §2）。
inline const char* pageTypeShortName(PageType type) {
	switch (type) {
		case PAGE_TYPE_CLOCK:
			return "CLOCK";
		case PAGE_TYPE_CLOCK_COUNTDOWN:
			return "CLOCK+CD";
		case PAGE_TYPE_COUNTDOWN:
			return "COUNTDOWN";
		case PAGE_TYPE_LIVE:
			return "LIVE";
		case PAGE_TYPE_DATETIME:
			return "DATETIME";
	}
	return "CLOCK";
}

inline bool isLivePage(PageType type) {
	return type == PAGE_TYPE_LIVE;
}

inline std::string effectiveShortTitle(const PageConfig& page) {
	if (!page.shortTitle.empty()) {
		return page.shortTitle;
	}
	return std::string(pageTypeShortName(page.type));
}

inline const char* livePlatformId(LivePlatform platform) {
	switch (platform) {
		case LIVE_PLATFORM_BILIBILI:
			return "bilibili";
		case LIVE_PLATFORM_DOUYIN:
			return "douyin";
		case LIVE_PLATFORM_KUAISHOU:
			return "kuaishou";
		case LIVE_PLATFORM_HUYA:
			return "huya";
		case LIVE_PLATFORM_DOUYU:
			return "douyu";
		case LIVE_PLATFORM_CUSTOM:
			return "custom";
	}
	return "bilibili";
}

inline bool parseLivePlatform(const std::string& id, LivePlatform& out) {
	if (id == "bilibili") {
		out = LIVE_PLATFORM_BILIBILI;
		return true;
	}
	if (id == "douyin") {
		out = LIVE_PLATFORM_DOUYIN;
		return true;
	}
	if (id == "kuaishou") {
		out = LIVE_PLATFORM_KUAISHOU;
		return true;
	}
	if (id == "huya") {
		out = LIVE_PLATFORM_HUYA;
		return true;
	}
	if (id == "douyu") {
		out = LIVE_PLATFORM_DOUYU;
		return true;
	}
	if (id == "custom") {
		out = LIVE_PLATFORM_CUSTOM;
		return true;
	}
	return false;
}

// INHERIT -> ""（空字符串 = 用页默认，契约 §2）。
inline const char* liveMetricId(LiveMetric metric) {
	switch (metric) {
		case LIVE_METRIC_NONE:
			return "none";
		case LIVE_METRIC_ONLINE:
			return "online";
		case LIVE_METRIC_LIKES:
			return "likes";
		case LIVE_METRIC_FANS:
			return "fans";
		case LIVE_METRIC_STATUS:
			return "status";
		case LIVE_METRIC_TITLE:
			return "title";
		case LIVE_METRIC_INHERIT:
			return "";
	}
	return "";
}

// 空字符串解析为 INHERIT；其余非法字符串返回 false。
inline bool parseLiveMetric(const std::string& id, LiveMetric& out) {
	if (id.empty()) {
		out = LIVE_METRIC_INHERIT;
		return true;
	}
	if (id == "none") {
		out = LIVE_METRIC_NONE;
		return true;
	}
	if (id == "online") {
		out = LIVE_METRIC_ONLINE;
		return true;
	}
	if (id == "likes") {
		out = LIVE_METRIC_LIKES;
		return true;
	}
	if (id == "fans") {
		out = LIVE_METRIC_FANS;
		return true;
	}
	if (id == "status") {
		out = LIVE_METRIC_STATUS;
		return true;
	}
	if (id == "title") {
		out = LIVE_METRIC_TITLE;
		return true;
	}
	return false;
}

// v3.0 屏幕 2x2 单元格支持显示的指标。
inline bool isMetricDisplayable(LiveMetric metric) {
	return metric == LIVE_METRIC_ONLINE || metric == LIVE_METRIC_LIKES ||
			metric == LIVE_METRIC_FANS || metric == LIVE_METRIC_STATUS;
}

inline const char* liveSourceId(LiveSourceKind kind) {
	switch (kind) {
		case LIVE_SOURCE_OFF:
			return "off";
		case LIVE_SOURCE_BRIDGE:
			return "bridge";
		case LIVE_SOURCE_HYBRID:
			return "hybrid";
		case LIVE_SOURCE_PUSH:
			return "push";
	}
	return "off";
}

inline bool parseLiveSource(const std::string& id, LiveSourceKind& out) {
	if (id == "off") {
		out = LIVE_SOURCE_OFF;
		return true;
	}
	if (id == "bridge") {
		out = LIVE_SOURCE_BRIDGE;
		return true;
	}
	if (id == "hybrid") {
		out = LIVE_SOURCE_HYBRID;
		return true;
	}
	if (id == "push") {
		out = LIVE_SOURCE_PUSH;
		return true;
	}
	return false;
}

inline const char* splitIdleId(SplitIdle value) {
	return value == SPLIT_IDLE_BLANK ? "blank" : "date";
}

inline bool parseSplitIdle(const std::string& id, SplitIdle& out) {
	if (id == "date") {
		out = SPLIT_IDLE_DATE;
		return true;
	}
	if (id == "blank") {
		out = SPLIT_IDLE_BLANK;
		return true;
	}
	return false;
}

#endif  // PAGES_PAGETYPES_H_
