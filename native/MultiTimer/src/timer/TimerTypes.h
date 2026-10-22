// v3 配置数据结构（纯 C++11，不依赖平台头）。
//
// v2 字段（tzOffsetMinutes / warnSeconds / colors / ntpServer / targets）语义完全
// 不变；v3 追加 colorDim、splitBottomWhenIdle、live、defaultPageId、pages。
// 页面/直播枚举与结构见 pages/PageTypes.h（契约 §2）。
#ifndef TIMER_TIMERTYPES_H_
#define TIMER_TIMERTYPES_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "pages/PageTypes.h"

// 一个带日期的倒计时目标，时间按“当前时区的本地墙上时间”解释
struct DateTarget {
	int year;    // 2000..2099
	int month;   // 1..12
	int day;     // 1..31（按日历校验）
	int hour;    // 0..23
	int minute;  // 0..59
	int second;  // 0..59
	bool enabled;

	DateTarget()
		: year(1970), month(1), day(1), hour(0), minute(0), second(0),
		  enabled(true) {
	}
};

struct TimerConfig {
	int tzOffsetMinutes;      // -720..840，本地时间 = UTC + 该值
	int warnSeconds;          // 0..600，剩余 <= 该值进入预警色
	uint32_t colorClock;      // 时钟模式颜色
	uint32_t colorCountdown;  // 倒计时颜色
	uint32_t colorCountup;    // v3：倒计时结束后的正计时颜色
	uint32_t colorWarning;    // 预警颜色
	uint32_t colorDim;        // v3：次要文字（日期 / 陈旧直播值 / 占位）
	std::string ntpServer;    // 自定义 NTP 服务器（host 或 host:port；空=内置列表）

	SplitIdle splitBottomWhenIdle;  // v3：分屏页无目标时下半区显示日期/空白
	LiveSourceConfig live;          // v3：直播数据源（默认 off）
	std::string defaultPageId;      // v3：开机页 id（非法时回退第一页）
	std::vector<PageConfig> pages;  // v3：1..12 页，数组顺序 = 翻页顺序

	std::vector<DateTarget> targets;  // 最多 12 个

	TimerConfig()
		: tzOffsetMinutes(480),
		  warnSeconds(10),
		  colorClock(0xFFFFFF),
		  colorCountdown(0x00FF00),
		  colorCountup(0x00BFFF),
		  colorWarning(0xFF2020),
		  colorDim(0x808080),
		  splitBottomWhenIdle(SPLIT_IDLE_DATE) {
	}
};

// 按 id 查页面下标；找不到返回 -1（纯函数，PageHost / ConfigStore 共用）。
inline int findPageIndexById(const TimerConfig& config, const std::string& id) {
	if (id.empty()) {
		return -1;
	}
	for (size_t i = 0; i < config.pages.size(); ++i) {
		if (config.pages[i].id == id) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// 第一个 enabled 页；没有则 -1（运行时回退由调用方决定）。
inline int firstEnabledPageIndex(const TimerConfig& config) {
	for (size_t i = 0; i < config.pages.size(); ++i) {
		if (config.pages[i].enabled) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// 越界安全访问；越界返回 NULL。
inline const PageConfig* pageAt(const TimerConfig& config, int index) {
	if (index < 0 || static_cast<size_t>(index) >= config.pages.size()) {
		return NULL;
	}
	return &config.pages[static_cast<size_t>(index)];
}

#endif  // TIMER_TIMERTYPES_H_
