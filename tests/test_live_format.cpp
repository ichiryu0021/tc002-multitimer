// Host tests for live/LiveFormat: value compression, labels, platform colors,
// metric selection (contract v3 §3).
#include "test_framework.h"

#include <string>

#include "live/LiveFormat.h"
#include "pages/PageRenderer.h"
#include "pages/PageTypes.h"

TF_TEST(live_format_count_small_values) {
	TF_EQ(LiveFormat::formatCount(0), std::string("0"));
	TF_EQ(LiveFormat::formatCount(1), std::string("1"));
	TF_EQ(LiveFormat::formatCount(45), std::string("45"));
	TF_EQ(LiveFormat::formatCount(999), std::string("999"));
	TF_EQ(LiveFormat::formatCount(1000), std::string("1000"));
	TF_EQ(LiveFormat::formatCount(9999), std::string("9999"));
}

TF_TEST(live_format_count_wan_one_decimal) {
	TF_EQ(LiveFormat::formatCount(10000), std::string("1w"));
	TF_EQ(LiveFormat::formatCount(12000), std::string("1.2w"));
	TF_EQ(LiveFormat::formatCount(12345), std::string("1.2w"));
	TF_EQ(LiveFormat::formatCount(15000), std::string("1.5w"));
	TF_EQ(LiveFormat::formatCount(99999), std::string("10w"));
	TF_EQ(LiveFormat::formatCount(100000), std::string("10w"));
	TF_EQ(LiveFormat::formatCount(123456), std::string("12.3w"));
	TF_EQ(LiveFormat::formatCount(999999), std::string("100w"));
	TF_EQ(LiveFormat::formatCount(1000000), std::string("100w"));
	TF_EQ(LiveFormat::formatCount(1234567), std::string("123.5w"));
	TF_EQ(LiveFormat::formatCount(9999000), std::string("999.9w"));
}

TF_TEST(live_format_count_wan_upgrades_to_yi) {
	// 999.9999w 进位到 1000.0w（7 字符）时升级为亿
	TF_EQ(LiveFormat::formatCount(9999999), std::string("0.1y"));
	TF_EQ(LiveFormat::formatCount(10000000), std::string("0.1y"));
}

TF_TEST(live_format_count_yi) {
	TF_EQ(LiveFormat::formatCount(12345678), std::string("0.1y"));
	TF_EQ(LiveFormat::formatCount(99999999), std::string("1y"));
	TF_EQ(LiveFormat::formatCount(100000000), std::string("1y"));
	TF_EQ(LiveFormat::formatCount(123456789), std::string("1.2y"));
	TF_EQ(LiveFormat::formatCount(990000000), std::string("9.9y"));
	TF_EQ(LiveFormat::formatCount(999999999), std::string("10y"));
	TF_EQ(LiveFormat::formatCount(1000000000), std::string("10y"));
	TF_EQ(LiveFormat::formatCount(9999999999LL), std::string("100y"));
}

TF_TEST(live_format_count_cap_and_negative) {
	TF_EQ(LiveFormat::formatCount(10000000000LL), std::string("999y"));
	TF_EQ(LiveFormat::formatCount(1234567890123LL), std::string("999y"));
	TF_EQ(LiveFormat::formatCount(9223372036854775807LL), std::string("999y"));
	TF_EQ(LiveFormat::formatCount(-1), std::string("--"));
	TF_EQ(LiveFormat::formatCount(-12345), std::string("--"));
	TF_EQ(LiveFormat::formatCount(-9223372036854775807LL - 1),
			std::string("--"));
}

TF_TEST(live_format_count_width_and_length) {
	const long long values[] = {
		0LL, 45LL, 9999LL, 12345LL, 123456LL, 9999999LL, 123456789LL,
		990000000LL, 9999999999LL, 10000000000LL,
		9223372036854775807LL
	};
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		const std::string text = LiveFormat::formatCount(values[i]);
		TF_CHECK_MSG(text.size() <= static_cast<size_t>(kMaxLiveValueChars),
				"value too long: " + text);
		TF_CHECK_MSG(PageRenderer::measureText(text, 1, 1) <= 21,
				"value too wide: " + text);
	}
	TF_EQ(LiveFormat::formatCount(123456), std::string("12.3w"));
}

TF_TEST(live_format_value_invalid_and_valid) {
	LiveMetricValue value;
	TF_EQ(LiveFormat::formatValue(value), std::string("--"));

	value.set(0);
	TF_EQ(LiveFormat::formatValue(value), std::string("0"));

	value.set(12345);
	TF_EQ(LiveFormat::formatValue(value), std::string("1.2w"));

	value.clear();
	TF_EQ(LiveFormat::formatValue(value), std::string("--"));
}

TF_TEST(live_format_metric_numeric) {
	LiveMetricValue value;
	value.set(990000000);
	TF_EQ(LiveFormat::formatMetric(value, LIVE_METRIC_ONLINE),
			std::string("9.9y"));
	TF_EQ(LiveFormat::formatMetric(value, LIVE_METRIC_LIKES),
			std::string("9.9y"));
	TF_EQ(LiveFormat::formatMetric(value, LIVE_METRIC_FANS),
			std::string("9.9y"));

	LiveMetricValue invalid;
	TF_EQ(LiveFormat::formatMetric(invalid, LIVE_METRIC_ONLINE),
			std::string("--"));
}

TF_TEST(live_format_metric_status) {
	TF_EQ(LiveFormat::statusText(LiveMetricValue()), std::string("--"));

	LiveMetricValue live;
	live.set(1);
	TF_EQ(LiveFormat::statusText(live), std::string("LIVE"));
	TF_EQ(LiveFormat::formatMetric(live, LIVE_METRIC_STATUS),
			std::string("LIVE"));

	LiveMetricValue off;
	off.set(0);
	TF_EQ(LiveFormat::statusText(off), std::string("OFF"));
	TF_EQ(LiveFormat::formatMetric(off, LIVE_METRIC_STATUS),
			std::string("OFF"));

	LiveMetricValue other;
	other.set(7);
	TF_EQ(LiveFormat::statusText(other), std::string("LIVE"));
}

TF_TEST(live_format_metric_none_and_title) {
	LiveMetricValue value;
	value.set(12345);
	TF_EQ(LiveFormat::formatMetric(value, LIVE_METRIC_NONE),
			std::string("--"));
	TF_EQ(LiveFormat::formatMetric(value, LIVE_METRIC_TITLE),
			std::string("--"));
	TF_EQ(LiveFormat::formatMetric(value, LIVE_METRIC_INHERIT),
			std::string("--"));
}

TF_TEST(live_format_platform_ids_and_parse) {
	TF_EQ(std::string(livePlatformId(LIVE_PLATFORM_BILIBILI)),
			std::string("bilibili"));
	TF_EQ(std::string(livePlatformId(LIVE_PLATFORM_DOUYIN)),
			std::string("douyin"));
	TF_EQ(std::string(livePlatformId(LIVE_PLATFORM_KUAISHOU)),
			std::string("kuaishou"));
	TF_EQ(std::string(livePlatformId(LIVE_PLATFORM_HUYA)),
			std::string("huya"));
	TF_EQ(std::string(livePlatformId(LIVE_PLATFORM_DOUYU)),
			std::string("douyu"));
	TF_EQ(std::string(livePlatformId(LIVE_PLATFORM_CUSTOM)),
			std::string("custom"));

	LivePlatform platform = LIVE_PLATFORM_CUSTOM;
	TF_CHECK(parseLivePlatform("bilibili", platform));
	TF_EQ(static_cast<int>(platform),
			static_cast<int>(LIVE_PLATFORM_BILIBILI));
	TF_CHECK(parseLivePlatform("douyin", platform));
	TF_EQ(static_cast<int>(platform), static_cast<int>(LIVE_PLATFORM_DOUYIN));
	TF_CHECK(parseLivePlatform("kuaishou", platform));
	TF_EQ(static_cast<int>(platform),
			static_cast<int>(LIVE_PLATFORM_KUAISHOU));

	TF_CHECK(!parseLivePlatform("", platform));
	TF_CHECK(!parseLivePlatform("Bilibili", platform));
	TF_CHECK(!parseLivePlatform("tiktok", platform));
	TF_CHECK(!parseLivePlatform("bilibili ", platform));
}

TF_TEST(live_format_platform_letters) {
	TF_EQ(std::string(LiveFormat::platformLetter(LIVE_PLATFORM_BILIBILI)),
			std::string("B"));
	TF_EQ(std::string(LiveFormat::platformLetter(LIVE_PLATFORM_DOUYIN)),
			std::string("D"));
	TF_EQ(std::string(LiveFormat::platformLetter(LIVE_PLATFORM_KUAISHOU)),
			std::string("K"));
	TF_EQ(std::string(LiveFormat::platformLetter(LIVE_PLATFORM_HUYA)),
			std::string("H"));
	TF_EQ(std::string(LiveFormat::platformLetter(LIVE_PLATFORM_DOUYU)),
			std::string("Y"));
	TF_EQ(std::string(LiveFormat::platformLetter(LIVE_PLATFORM_CUSTOM)),
			std::string("C"));
}

TF_TEST(live_format_platform_colors) {
	// 契约 §3：B #FF6699、D #FFFFFF、K #FF9900
	TF_EQ(LiveFormat::platformColor(LIVE_PLATFORM_BILIBILI), 0xFF6699u);
	TF_EQ(LiveFormat::platformColor(LIVE_PLATFORM_DOUYIN), 0xFFFFFFu);
	TF_EQ(LiveFormat::platformColor(LIVE_PLATFORM_KUAISHOU), 0xFF9900u);
	TF_EQ(LiveFormat::platformColor(LIVE_PLATFORM_HUYA), 0xFFB300u);
	TF_EQ(LiveFormat::platformColor(LIVE_PLATFORM_DOUYU), 0xFF6A00u);
	TF_EQ(LiveFormat::platformColor(LIVE_PLATFORM_CUSTOM), 0xFFFFFFu);
}

TF_TEST(live_format_default_label_numeric_rooms) {
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_BILIBILI, "21452505"),
			std::string("B05"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_DOUYIN, "88776655"),
			std::string("D55"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_KUAISHOU, "3x8k2m9"),
			std::string("KM9"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_BILIBILI, "7"),
			std::string("B7"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_BILIBILI, "123"),
			std::string("B23"));
}

TF_TEST(live_format_default_label_alpha_rooms) {
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_KUAISHOU, "abc"),
			std::string("KBC"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_DOUYIN, "a"),
			std::string("DA"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_BILIBILI, ""),
			std::string("B"));
	TF_EQ(LiveFormat::defaultLabel(LIVE_PLATFORM_CUSTOM, ""),
			std::string("C"));
}

TF_TEST(live_format_normalize_label) {
	TF_EQ(LiveFormat::normalizeLabel("b25"), std::string("B25"));
	TF_EQ(LiveFormat::normalizeLabel("b25x"), std::string("B25"));
	TF_EQ(LiveFormat::normalizeLabel("M/W"), std::string("MW"));
	TF_EQ(LiveFormat::normalizeLabel("a.b"), std::string("A.B"));
	TF_EQ(LiveFormat::normalizeLabel("a_b"), std::string("A_B"));
	TF_EQ(LiveFormat::normalizeLabel("a-b"), std::string("A-B"));
	TF_EQ(LiveFormat::normalizeLabel(""), std::string(""));
	TF_EQ(LiveFormat::normalizeLabel("!!!"), std::string(""));
	TF_EQ(LiveFormat::normalizeLabel("中文"), std::string(""));
	TF_EQ(LiveFormat::normalizeLabel("   "), std::string(""));
}

TF_TEST(live_format_label_for_prefers_explicit) {
	LiveRoomConfig room;
	room.platform = LIVE_PLATFORM_KUAISHOU;
	room.room = "3x8k2m9";
	TF_EQ(LiveFormat::labelFor(room), std::string("KM9"));

	room.label = "k3x";
	TF_EQ(LiveFormat::labelFor(room), std::string("K3X"));

	room.label = "???";
	TF_EQ(LiveFormat::labelFor(room), std::string("KM9"));
}

TF_TEST(live_format_effective_metric) {
	PageConfig page;
	page.metric = LIVE_METRIC_LIKES;
	LiveRoomConfig room;

	TF_EQ(static_cast<int>(LiveFormat::effectiveMetric(page, room)),
			static_cast<int>(LIVE_METRIC_LIKES));

	room.metric = LIVE_METRIC_STATUS;
	TF_EQ(static_cast<int>(LiveFormat::effectiveMetric(page, room)),
			static_cast<int>(LIVE_METRIC_STATUS));

	room.metric = LIVE_METRIC_NONE;
	TF_EQ(static_cast<int>(LiveFormat::effectiveMetric(page, room)),
			static_cast<int>(LIVE_METRIC_NONE));

	// 页默认意外为 INHERIT 时回退 online（防御）
	page.metric = LIVE_METRIC_INHERIT;
	room.metric = LIVE_METRIC_INHERIT;
	TF_EQ(static_cast<int>(LiveFormat::effectiveMetric(page, room)),
			static_cast<int>(LIVE_METRIC_ONLINE));
}

TF_TEST(live_format_select_metric_value) {
	LiveRoomData room;
	room.online.set(11);
	room.likes.set(22);
	room.fans.set(33);
	room.liveStatus.set(1);

	TF_EQ(LiveFormat::selectMetricValue(room, LIVE_METRIC_ONLINE).value, 11LL);
	TF_EQ(LiveFormat::selectMetricValue(room, LIVE_METRIC_LIKES).value, 22LL);
	TF_EQ(LiveFormat::selectMetricValue(room, LIVE_METRIC_FANS).value, 33LL);
	TF_EQ(LiveFormat::selectMetricValue(room, LIVE_METRIC_STATUS).value, 1LL);

	TF_CHECK(!LiveFormat::selectMetricValue(room, LIVE_METRIC_NONE).valid);
	TF_CHECK(!LiveFormat::selectMetricValue(room, LIVE_METRIC_TITLE).valid);
	TF_CHECK(!LiveFormat::selectMetricValue(room, LIVE_METRIC_INHERIT).valid);

	LiveRoomData empty;
	TF_CHECK(!LiveFormat::selectMetricValue(empty, LIVE_METRIC_ONLINE).valid);
}

TF_TEST(live_format_metric_ids_round_trip) {
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_ONLINE)), std::string("online"));
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_LIKES)), std::string("likes"));
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_FANS)), std::string("fans"));
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_STATUS)), std::string("status"));
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_TITLE)), std::string("title"));
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_NONE)), std::string("none"));
	TF_EQ(std::string(liveMetricId(LIVE_METRIC_INHERIT)), std::string(""));

	const LiveMetric metrics[] = {
		LIVE_METRIC_NONE, LIVE_METRIC_ONLINE, LIVE_METRIC_LIKES,
		LIVE_METRIC_FANS, LIVE_METRIC_STATUS, LIVE_METRIC_TITLE
	};
	for (size_t i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i) {
		LiveMetric parsed = LIVE_METRIC_INHERIT;
		TF_CHECK_MSG(parseLiveMetric(liveMetricId(metrics[i]), parsed),
				std::string("cannot parse: ") + liveMetricId(metrics[i]));
		TF_EQ(static_cast<int>(parsed), static_cast<int>(metrics[i]));
	}

	LiveMetric parsed = LIVE_METRIC_ONLINE;
	TF_CHECK(parseLiveMetric("", parsed));
	TF_EQ(static_cast<int>(parsed), static_cast<int>(LIVE_METRIC_INHERIT));
	TF_CHECK(!parseLiveMetric("gifts", parsed));
	TF_CHECK(!parseLiveMetric("Online", parsed));
}

TF_TEST(live_format_source_ids) {
	TF_EQ(std::string(liveSourceId(LIVE_SOURCE_OFF)), std::string("off"));
	TF_EQ(std::string(liveSourceId(LIVE_SOURCE_BRIDGE)),
			std::string("bridge"));
	TF_EQ(std::string(liveSourceId(LIVE_SOURCE_HYBRID)),
			std::string("hybrid"));
	TF_EQ(std::string(liveSourceId(LIVE_SOURCE_PUSH)), std::string("push"));

	LiveSourceKind kind = LIVE_SOURCE_OFF;
	TF_CHECK(parseLiveSource("hybrid", kind));
	TF_EQ(static_cast<int>(kind), static_cast<int>(LIVE_SOURCE_HYBRID));
	TF_CHECK(parseLiveSource("bridge", kind));
	TF_EQ(static_cast<int>(kind), static_cast<int>(LIVE_SOURCE_BRIDGE));
	TF_CHECK(!parseLiveSource("bilibili", kind));
	TF_CHECK(!parseLiveSource("", kind));
}

TF_TEST(live_format_cell_configured) {
	LiveRoomConfig room;
	room.room = "123";
	TF_CHECK(LiveFormat::isCellConfigured(room));

	room.enabled = false;
	TF_CHECK(!LiveFormat::isCellConfigured(room));

	room.enabled = true;
	room.room.clear();
	TF_CHECK(!LiveFormat::isCellConfigured(room));
}

TF_TEST(live_format_page_type_ids) {
	TF_EQ(std::string(pageTypeId(PAGE_TYPE_CLOCK)), std::string("clock"));
	TF_EQ(std::string(pageTypeId(PAGE_TYPE_CLOCK_COUNTDOWN)),
			std::string("clock_countdown"));
	TF_EQ(std::string(pageTypeId(PAGE_TYPE_COUNTDOWN)),
			std::string("countdown"));
	TF_EQ(std::string(pageTypeId(PAGE_TYPE_LIVE)), std::string("live"));

	PageType type = PAGE_TYPE_CLOCK;
	TF_CHECK(parsePageType("live", type));
	TF_EQ(static_cast<int>(type), static_cast<int>(PAGE_TYPE_LIVE));
	TF_CHECK(!parsePageType("split", type));
	TF_CHECK(!parsePageType("Live", type));
}

TF_TEST(live_format_type_short_names) {
	TF_EQ(std::string(pageTypeShortName(PAGE_TYPE_CLOCK)),
			std::string("CLOCK"));
	TF_EQ(std::string(pageTypeShortName(PAGE_TYPE_CLOCK_COUNTDOWN)),
			std::string("CLOCK+CD"));
	TF_EQ(std::string(pageTypeShortName(PAGE_TYPE_COUNTDOWN)),
			std::string("COUNTDOWN"));
	TF_EQ(std::string(pageTypeShortName(PAGE_TYPE_LIVE)),
			std::string("LIVE"));

	PageConfig page;
	page.type = PAGE_TYPE_COUNTDOWN;
	TF_EQ(effectiveShortTitle(page), std::string("COUNTDOWN"));
	page.shortTitle = "CD";
	TF_EQ(effectiveShortTitle(page), std::string("CD"));
}
