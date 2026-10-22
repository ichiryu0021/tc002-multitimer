#include "live/LiveFormat.h"

#include <stdio.h>

namespace {

char upperAscii(char c) {
	if (c >= 'a' && c <= 'z') {
		return static_cast<char>(c - 'a' + 'A');
	}
	return c;
}

bool isLabelChar(char c) {
	const bool digit = c >= '0' && c <= '9';
	const bool upper = c >= 'A' && c <= 'Z';
	return digit || upper || c == '.' || c == '_' || c == '-';
}

std::string formatWanTenths(long long tenths) {
	char buf[16];
	if (tenths % 10 == 0) {
		snprintf(buf, sizeof(buf), "%lldw", tenths / 10);
	} else {
		snprintf(buf, sizeof(buf), "%lld.%lldw", tenths / 10, tenths % 10);
	}
	return std::string(buf);
}

std::string formatYiTenths(long long tenths) {
	char buf[16];
	if (tenths % 10 == 0) {
		snprintf(buf, sizeof(buf), "%lldy", tenths / 10);
	} else {
		snprintf(buf, sizeof(buf), "%lld.%lldy", tenths / 10, tenths % 10);
	}
	return std::string(buf);
}

}  // namespace

namespace LiveFormat {

std::string formatCount(long long value) {
	if (value < 0) {
		return "--";
	}
	if (value < 10000) {
		char buf[24];
		snprintf(buf, sizeof(buf), "%lld", value);
		return std::string(buf);
	}
	if (value >= 10000000000LL) {  // >= 10^10：上限截断（设计 §2.4.7）
		return "999y";
	}
	if (value < 10000000) {  // 1e4..1e7-1：万，1 位小数
		const long long tenths = (value + 500) / 1000;
		if (tenths < 10000) {
			return formatWanTenths(tenths);
		}
		// 999.9999w 四舍五入到 1000.0w（7 字符）：升级为亿
		return formatYiTenths((value + 5000000) / 10000000);
	}
	// 1e7..1e10-1：亿，1 位小数（最大 100y，<=6 字符）
	return formatYiTenths((value + 5000000) / 10000000);
}

std::string formatValue(const LiveMetricValue& value) {
	if (!value.valid) {
		return "--";
	}
	return formatCount(value.value);
}

std::string statusText(const LiveMetricValue& value) {
	if (!value.valid) {
		return "--";
	}
	return value.value != 0 ? "LIVE" : "OFF";
}

std::string formatMetric(const LiveMetricValue& value, LiveMetric metric) {
	switch (metric) {
		case LIVE_METRIC_ONLINE:
		case LIVE_METRIC_LIKES:
		case LIVE_METRIC_FANS:
			return formatValue(value);
		case LIVE_METRIC_STATUS:
			return statusText(value);
		case LIVE_METRIC_NONE:
		case LIVE_METRIC_TITLE:
		case LIVE_METRIC_INHERIT:
			return "--";
	}
	return "--";
}

const char* platformLetter(LivePlatform platform) {
	switch (platform) {
		case LIVE_PLATFORM_BILIBILI:
			return "B";
		case LIVE_PLATFORM_DOUYIN:
			return "D";
		case LIVE_PLATFORM_KUAISHOU:
			return "K";
		case LIVE_PLATFORM_HUYA:
			return "H";
		case LIVE_PLATFORM_DOUYU:
			return "Y";
		case LIVE_PLATFORM_CUSTOM:
			return "C";
	}
	return "B";
}

uint32_t platformColor(LivePlatform platform) {
	switch (platform) {
		case LIVE_PLATFORM_BILIBILI:
			return 0xFF6699;  // 契约 §3
		case LIVE_PLATFORM_DOUYIN:
			return 0xFFFFFF;  // 契约 §3
		case LIVE_PLATFORM_KUAISHOU:
			return 0xFF9900;  // 契约 §3
		case LIVE_PLATFORM_HUYA:
			return 0xFFB300;  // 设计稿扩展位
		case LIVE_PLATFORM_DOUYU:
			return 0xFF6A00;  // 设计稿扩展位
		case LIVE_PLATFORM_CUSTOM:
			return 0xFFFFFF;
	}
	return 0xFFFFFF;
}

std::string normalizeLabel(const std::string& raw) {
	std::string out;
	out.reserve(kMaxRoomLabelChars);
	for (size_t i = 0; i < raw.size() && out.size() < static_cast<size_t>(
			kMaxRoomLabelChars); ++i) {
		const char c = upperAscii(raw[i]);
		if (isLabelChar(c)) {
			out.push_back(c);
		}
	}
	return out;
}

std::string defaultLabel(LivePlatform platform, const std::string& room) {
	std::string tail;
	// 从房间号尾部向前取最多 2 个合法字符（顺序保持原样）
	for (size_t i = room.size(); i > 0 &&
			tail.size() < 2; --i) {
		const char c = upperAscii(room[i - 1]);
		if (isLabelChar(c)) {
			tail.insert(tail.begin(), c);
		}
	}
	const char* letter = platformLetter(platform);
	std::string out(letter != NULL ? letter : "B");
	out += tail;
	return out;
}

std::string labelFor(const LiveRoomConfig& room) {
	const std::string normalized = normalizeLabel(room.label);
	if (!normalized.empty()) {
		return normalized;
	}
	return defaultLabel(room.platform, room.room);
}

LiveMetric effectiveMetric(const PageConfig& page, const LiveRoomConfig& room) {
	LiveMetric metric = room.metric;
	if (metric == LIVE_METRIC_INHERIT) {
		metric = page.metric;
	}
	if (metric == LIVE_METRIC_INHERIT) {
		metric = LIVE_METRIC_ONLINE;
	}
	return metric;
}

LiveMetricValue selectMetricValue(const LiveRoomData& room, LiveMetric metric) {
	switch (metric) {
		case LIVE_METRIC_ONLINE:
			return room.online;
		case LIVE_METRIC_LIKES:
			return room.likes;
		case LIVE_METRIC_FANS:
			return room.fans;
		case LIVE_METRIC_STATUS:
			return room.liveStatus;
		case LIVE_METRIC_NONE:
		case LIVE_METRIC_TITLE:
		case LIVE_METRIC_INHERIT:
			return LiveMetricValue();
	}
	return LiveMetricValue();
}

bool isCellConfigured(const LiveRoomConfig& room) {
	return room.enabled && !room.room.empty();
}

}  // namespace LiveFormat
