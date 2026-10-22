// Host tests for pages/PageRenderer: 52x16 pixel/geometry assertions for all
// four page types, banner and overlay (contract v3 §3).
#include "test_framework.h"

#include <string>

#include "live/LiveTypes.h"
#include "pages/PageRenderer.h"
#include "pages/PageTypes.h"
#include "timer/TimeMath.h"
#include "timer/TimerTypes.h"
#include "utils/Surface.h"

namespace {

const uint32_t kBlack = 0x000000;

long long localEpoch(int year, int month, int day, int hour, int minute,
		int second, int tzOffsetMinutes) {
	LocalDateTime local;
	local.year = year;
	local.month = month;
	local.day = day;
	local.hour = hour;
	local.minute = minute;
	local.second = second;
	long long epoch = 0;
	if (!localDateTimeToEpoch(local, tzOffsetMinutes, epoch)) {
		return -1;
	}
	return epoch;
}

bool surfacesEqual(const Surface& a, const Surface& b) {
	if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) {
		return false;
	}
	for (int y = 0; y < a.getHeight(); ++y) {
		for (int x = 0; x < a.getWidth(); ++x) {
			if (a.getPixel(x, y).toRGB888() != b.getPixel(x, y).toRGB888()) {
				return false;
			}
		}
	}
	return true;
}

struct Bounds {
	int minX;
	int maxX;
	int minY;
	int maxY;
	int count;

	Bounds() : minX(999), maxX(-1), minY(999), maxY(-1), count(0) {
	}
};

Bounds litBounds(const Surface& surface, int y0, int y1) {
	Bounds bounds;
	for (int y = y0; y <= y1; ++y) {
		for (int x = 0; x < surface.getWidth(); ++x) {
			if (surface.getPixel(x, y).toRGB888() == kBlack) {
				continue;
			}
			++bounds.count;
			if (x < bounds.minX) {
				bounds.minX = x;
			}
			if (x > bounds.maxX) {
				bounds.maxX = x;
			}
			if (y < bounds.minY) {
				bounds.minY = y;
			}
			if (y > bounds.maxY) {
				bounds.maxY = y;
			}
		}
	}
	return bounds;
}

bool allBlackInRows(const Surface& surface, int y0, int y1) {
	return litBounds(surface, y0, y1).count == 0;
}

bool allLitPixelsColor(const Surface& surface, uint32_t rgb) {
	for (int y = 0; y < surface.getHeight(); ++y) {
		for (int x = 0; x < surface.getWidth(); ++x) {
			const uint32_t value = surface.getPixel(x, y).toRGB888();
			if (value != kBlack && value != rgb) {
				return false;
			}
		}
	}
	return true;
}

bool anyPixelWithColor(const Surface& surface, uint32_t rgb) {
	for (int y = 0; y < surface.getHeight(); ++y) {
		for (int x = 0; x < surface.getWidth(); ++x) {
			if (surface.getPixel(x, y).toRGB888() == rgb) {
				return true;
			}
		}
	}
	return false;
}

PageConfig makePage(PageType type, const char* id) {
	PageConfig page;
	page.id = id;
	page.type = type;
	return page;
}

LiveRoomConfig makeRoom(LivePlatform platform, const char* room,
		const char* label = "", bool enabled = true,
		LiveMetric metric = LIVE_METRIC_INHERIT) {
	LiveRoomConfig config;
	config.platform = platform;
	config.room = room;
	config.label = label;
	config.enabled = enabled;
	config.metric = metric;
	return config;
}

// 一页 2 个房间（契约 §2/§3）。
PageConfig makeLivePage() {
	PageConfig page = makePage(PAGE_TYPE_LIVE, "p0");
	page.metric = LIVE_METRIC_ONLINE;
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_BILIBILI, "21452505"));
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_KUAISHOU, "3x8k2m9"));
	return page;
}

TimerConfig makeConfig() {
	TimerConfig config;
	config.pages.push_back(makePage(PAGE_TYPE_CLOCK, "p0"));
	return config;
}

Stage countdownStage(long long remainSeconds, bool warning = false,
		bool smallFont = false) {
	Stage stage;
	stage.kind = STAGE_COUNTDOWN;
	stage.remainSeconds = remainSeconds;
	stage.warning = warning;
	stage.smallFont = smallFont;
	return stage;
}

void drawLiveRow(Surface& surface, const std::string& label,
		const std::string& value, int rowIndex, uint32_t labelColor,
		uint32_t valueColor) {
	PageRenderer::LiveCellLayout layout;
	PageRenderer::layoutLiveCell(label, value, rowIndex, layout);
	const int baseline = (rowIndex == 0) ? PageRenderer::kLiveTopBaseline
										 : PageRenderer::kLiveBottomBaseline;
	PageRenderer::drawText(surface, layout.label, layout.labelX, baseline, 1, 1,
			labelColor);
	PageRenderer::drawText(surface, value, layout.valueX, baseline, 1, 1,
			valueColor);
}

}  // namespace

TF_TEST(renderer_measure_text_contract_table) {
	TF_EQ(PageRenderer::measureText("16:38:29", 1, 1), 27);
	TF_EQ(PageRenderer::measureText("2026-10-22", 1, 1), 39);
	TF_EQ(PageRenderer::measureText("4d04:05:06", 1, 1), 35);
	TF_EQ(PageRenderer::measureText("1.2w", 1, 1), 13);
	TF_EQ(PageRenderer::measureText("12.3w", 1, 1), 17);
	TF_EQ(PageRenderer::measureText("999.9w", 1, 1), 21);
	TF_EQ(PageRenderer::measureText("9999", 1, 1), 15);
	TF_EQ(PageRenderer::measureText("B25", 1, 1), 11);
	TF_EQ(PageRenderer::measureText("2/6", 1, 1), 11);
	TF_EQ(PageRenderer::measureText("COUNTDOWN", 1, 1), 39);
	TF_EQ(PageRenderer::measureText("16:38:29", 2, 1), 47);
	TF_EQ(PageRenderer::measureText("16:38:29", 2, 2), 54);
	TF_EQ(PageRenderer::measureText("--:--:--", 2, 2), 54);

	// 可见宽度算法：数字 3、冒号/点 1、M/W 5、空格 2
	TF_EQ(PageRenderer::glyphVisibleWidth('0'), 3);
	TF_EQ(PageRenderer::glyphVisibleWidth(':'), 1);
	TF_EQ(PageRenderer::glyphVisibleWidth('M'), 5);
	TF_EQ(PageRenderer::glyphVisibleWidth(' '), 0);
	TF_EQ(PageRenderer::charAdvance(' ', 1), 2);
	TF_EQ(PageRenderer::charAdvance('M', 2), 10);
	TF_EQ(PageRenderer::measureText(" ", 1, 1), 2);
	TF_EQ(PageRenderer::measureText("", 1, 1), 0);

	TF_EQ(PageRenderer::truncateToWidth("", 52), std::string(""));
	TF_EQ(PageRenderer::truncateToWidth("WWW", 5), std::string("W"));
	TF_EQ(PageRenderer::truncateToWidth("B25", 52), std::string("B25"));
}

TF_TEST(renderer_clock_page_geometry_and_color) {
	TimerConfig config = makeConfig();
	config.colorClock = 0x00FF00;
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);
	TF_CHECK(now > 0);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), surface));

	// "16:38:29" scale2 自动收紧 spacing 到 1 => 47px，居中 x=2..48，行 3..12
	const Bounds bounds = litBounds(surface, 0, 15);
	TF_EQ(bounds.minX, 2);
	TF_EQ(bounds.maxX, 48);
	TF_EQ(bounds.minY, 3);
	TF_EQ(bounds.maxY, 12);
	TF_CHECK(allLitPixelsColor(surface, 0x00FF00));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected, "16:38:29", 2, 2, 0x00FF00);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_clock_page_invalid_time) {
	TimerConfig config = makeConfig();
	config.colorClock = 0x00FF00;
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), false,
			LiveSnapshot(), surface));

	TF_CHECK(allLitPixelsColor(surface, 0x404040));
	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected, "--:--:--", 2, 2, 0x404040);
	TF_CHECK(surfacesEqual(surface, expected));

	const Bounds bounds = litBounds(surface, 0, 15);
	TF_EQ(bounds.minX, 2);
	TF_EQ(bounds.maxX, 48);
	// "--:--:--" 只有横线笔画，墨迹落在 scale2 字形框（3..12）内的中部
	TF_CHECK(bounds.minY >= 3);
	TF_CHECK(bounds.maxY <= 12);
}

TF_TEST(renderer_clock_page_respects_timezone) {
	TimerConfig config = makeConfig();
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface shanghai(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), shanghai));

	config.tzOffsetMinutes = 0;  // 08:38:29
	Surface utc(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), utc));
	TF_CHECK(!surfacesEqual(shanghai, utc));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected,
			formatEpochLocalTime(now, 0), 2, 2, config.colorClock);
	TF_CHECK(surfacesEqual(utc, expected));
}

TF_TEST(renderer_split_page_geometry) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);
	const Stage stage = countdownStage(3661);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, stage, true,
			LiveSnapshot(), surface));

	const Bounds top = litBounds(surface, 0, 6);
	const Bounds bottom = litBounds(surface, 7, 15);
	TF_CHECK(top.count > 0);
	TF_CHECK(bottom.count > 0);
	TF_EQ(top.minY, 2);
	TF_EQ(top.maxY, 6);
	TF_EQ(bottom.minY, 9);
	TF_EQ(bottom.maxY, 13);
	TF_CHECK(allBlackInRows(surface, 0, 1));
	TF_CHECK(allBlackInRows(surface, 7, 8));
	TF_CHECK(allBlackInRows(surface, 14, 15));
}

TF_TEST(renderer_split_page_uses_same_now_for_both_halves) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);
	const Stage stage = countdownStage(3661);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, stage, true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	const std::string clockText =
			formatEpochLocalTime(now, config.tzOffsetMinutes);
	PageRenderer::drawTextCentered(expected, clockText, 7, 1,
			PageRenderer::timeSpacingForWidth(clockText), config.colorClock);
	PageRenderer::drawTextCentered(expected, "01:01:01", 14, 1,
			PageRenderer::timeSpacingForWidth("01:01:01"), config.colorCountdown);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_split_page_idle_shows_no_countdown) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	config.colorDim = 0x808080;
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	const std::string clockText =
			formatEpochLocalTime(now, config.tzOffsetMinutes);
	PageRenderer::drawTextCentered(expected, clockText, 7, 1,
			PageRenderer::timeSpacingForWidth(clockText), config.colorClock);
	PageRenderer::drawTextCentered(expected, "NO COUNTDOWN", 14, 1, 0,
			0x808080);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(anyPixelWithColor(surface, 0x808080));
}

TF_TEST(renderer_datetime_page_shows_date_and_time) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_DATETIME, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), surface));

	LocalDateTime local;
	TF_CHECK(epochToLocalDateTime(now, config.tzOffsetMinutes, local));
	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected,
			formatEpochLocalTime(now, config.tzOffsetMinutes), 10, 2, 1,
			config.colorClock);
	PageRenderer::drawTextCentered(expected, formatDate(local), 15, 1, 0,
			config.colorDim);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_split_page_no_target_shows_no_countdown) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	config.splitBottomWhenIdle = SPLIT_IDLE_BLANK;  // 旧设置必须被忽略
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	const std::string clockText =
			formatEpochLocalTime(now, config.tzOffsetMinutes);
	PageRenderer::drawTextCentered(expected, clockText, 7, 1,
			PageRenderer::timeSpacingForWidth(clockText), config.colorClock);
	PageRenderer::drawTextCentered(expected, "NO COUNTDOWN", 14, 1, 0,
			config.colorDim);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(anyPixelWithColor(surface, config.colorDim));
}

TF_TEST(renderer_split_page_countup_after_target) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);
	Stage stage;
	stage.kind = STAGE_COUNTUP;
	stage.targetEpoch = now - 3661;
	stage.elapsedSeconds = 3661;

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, stage, true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	const std::string clockText =
			formatEpochLocalTime(now, config.tzOffsetMinutes);
	PageRenderer::drawTextCentered(expected, clockText, 7, 1,
			PageRenderer::timeSpacingForWidth(clockText), config.colorClock);
	PageRenderer::drawTextCentered(expected, "01:01:01", 14, 1,
			PageRenderer::timeSpacingForWidth("01:01:01"),
			config.colorCountup);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(anyPixelWithColor(surface, config.colorCountup));
}

TF_TEST(renderer_split_page_warning_only_bottom) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);
	const Stage stage = countdownStage(5, true);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, stage, true,
			LiveSnapshot(), surface));

	const Bounds top = litBounds(surface, 0, 6);
	const Bounds bottom = litBounds(surface, 7, 15);
	TF_CHECK(top.count > 0);
	TF_CHECK(bottom.count > 0);

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	const std::string clockText =
			formatEpochLocalTime(now, config.tzOffsetMinutes);
	PageRenderer::drawTextCentered(expected, clockText, 7, 1,
			PageRenderer::timeSpacingForWidth(clockText), config.colorClock);
	PageRenderer::drawTextCentered(expected, "00:00:05", 14, 1,
			PageRenderer::timeSpacingForWidth("00:00:05"), config.colorWarning);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(anyPixelWithColor(surface, config.colorWarning));
}

TF_TEST(renderer_split_page_invalid_time_keeps_bottom_blank) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), false,
			LiveSnapshot(), surface));

	TF_CHECK(allBlackInRows(surface, 8, 15));
	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected, "--:--:--", 7, 1,
			PageRenderer::timeSpacingForWidth("--:--:--"), 0x404040);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_split_page_100h_stays_scale1) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);
	const Stage stage = countdownStage(360000, false, true);  // 100h

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, stage, true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	const std::string clockText =
			formatEpochLocalTime(now, config.tzOffsetMinutes);
	PageRenderer::drawTextCentered(expected, clockText, 7, 1,
			PageRenderer::timeSpacingForWidth(clockText), config.colorClock);
	PageRenderer::drawTextCentered(expected, "4d04:00:00", 14, 1,
			PageRenderer::timeSpacingForWidth("4d04:00:00"),
			config.colorCountdown);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(allBlackInRows(surface, 7, 8));
}

TF_TEST(renderer_countdown_page_scale2) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_COUNTDOWN, "p0");
	config.colorCountdown = 0x00FF00;
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, countdownStage(3661),
			true, LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected, "01:01:01", 2, 2, 0x00FF00);
	TF_CHECK(surfacesEqual(surface, expected));

	const Bounds bounds = litBounds(surface, 0, 15);
	TF_EQ(bounds.minY, 3);
	TF_EQ(bounds.maxY, 12);
}

TF_TEST(renderer_countdown_page_small_font_for_100h) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now,
			countdownStage(360000, false, true), true, LiveSnapshot(),
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected, "4d04:00:00", 1, 1,
			config.colorCountdown);
	TF_CHECK(surfacesEqual(surface, expected));

	const Bounds bounds = litBounds(surface, 0, 15);
	TF_EQ(bounds.minY, 5);
	TF_EQ(bounds.maxY, 9);
}

TF_TEST(renderer_countdown_page_no_target_shows_no_countdown) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_COUNTDOWN, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected, "NO COUNTDOWN", 10, 1, 0,
			config.colorDim);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_countdown_page_invalid_time) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_COUNTDOWN, "p0");
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, countdownStage(100), false,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected, "--:--:--", 2, 2, 0x404040);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_countdown_page_warning_color) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_COUNTDOWN, "p0");
	config.colorWarning = 0xFF2020;
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now,
			countdownStage(9, true), true, LiveSnapshot(), surface));

	TF_CHECK(allLitPixelsColor(surface, 0xFF2020));
}

TF_TEST(renderer_timezone_change_immediate) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_CLOCK, "p0");
	const long long now = localEpoch(2026, 10, 22, 16, 38, 29, 480);

	Surface shanghai(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), shanghai));

	config.tzOffsetMinutes = 330;  // +05:30 -> 14:08:29
	Surface india(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, now, Stage(), true,
			LiveSnapshot(), india));
	TF_CHECK(!surfacesEqual(shanghai, india));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawScaledText(expected, formatEpochLocalTime(now, 330), 2,
			2, config.colorClock);
	TF_CHECK(surfacesEqual(india, expected));
}

TF_TEST(renderer_live_page_two_rows_geometry_and_colors) {
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(12345);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(678);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), false, live,
			surface));

	// 上行基线 6 -> 行 1..5；下行基线 14 -> 行 9..13
	const Bounds top = litBounds(surface, 0, 8);
	const Bounds bottom = litBounds(surface, 9, 15);
	TF_CHECK(top.count > 0);
	TF_CHECK(bottom.count > 0);
	TF_EQ(top.minY, 1);
	TF_EQ(top.maxY, 5);
	TF_EQ(bottom.minY, 9);
	TF_EQ(bottom.maxY, 13);
	TF_CHECK(allBlackInRows(surface, 0, 0));
	TF_CHECK(allBlackInRows(surface, 6, 8));
	TF_CHECK(allBlackInRows(surface, 14, 15));
	TF_CHECK(allBlackInRows(surface, 0, 15) == false);

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "1.2w", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "678", 1, 0xFF9900, 0xFFFFFF);
	TF_CHECK(surfacesEqual(surface, expected));

	TF_CHECK(anyPixelWithColor(surface, 0xFF6699));
	TF_CHECK(anyPixelWithColor(surface, 0xFF9900));
	TF_CHECK(anyPixelWithColor(surface, 0xFFFFFF));
}

TF_TEST(renderer_live_page_label_default_matches_explicit) {
	TimerConfig automatic = makeConfig();
	automatic.pages[0] = makeLivePage();
	TimerConfig explicitLabels = automatic;
	explicitLabels.pages[0].rooms[0].label = "B05";
	explicitLabels.pages[0].rooms[1].label = "KM9";

	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(45);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(999);

	Surface a(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	Surface b(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(automatic, 0, 0, Stage(), true, live, a));
	TF_CHECK(PageRenderer::renderPage(explicitLabels, 0, 0, Stage(), true, live,
			b));
	TF_CHECK(surfacesEqual(a, b));
}

TF_TEST(renderer_live_page_value_compression) {
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(123456);   // 12.3w
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(990000000);  // 9.9y

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "12.3w", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "9.9y", 1, 0xFF9900, 0xFFFFFF);
	TF_CHECK(surfacesEqual(surface, expected));

	PageRenderer::LiveCellLayout layout;
	PageRenderer::layoutLiveCell("KM9", "9.9y", 1, layout);
	TF_EQ(layout.valueX + layout.valueWidth - 1, 51);
}

TF_TEST(renderer_live_page_error_shows_dashes_dim) {
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	config.colorDim = 0x808080;
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(45);
	live.rooms[1].state = LIVE_ROOM_ERROR;

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "45", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "--", 1, 0xFF9900, 0x808080);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(anyPixelWithColor(surface, 0x808080));
}

TF_TEST(renderer_live_page_wait_shows_dashes_dim) {
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(45);
	live.rooms[1].state = LIVE_ROOM_WAIT;

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "45", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "--", 1, 0xFF9900, config.colorDim);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_all_rooms_failed_shows_dashes) {
	// 两个房间都配置了、都失败：每行 `--` dim（契约 §3），不是整页 NO LIVE
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	config.colorDim = 0x808080;
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_ERROR;
	live.rooms[1].state = LIVE_ROOM_STALE;

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "--", 0, 0xFF6699, 0x808080);
	drawLiveRow(expected, "KM9", "--", 1, 0xFF9900, 0x808080);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(anyPixelWithColor(surface, 0xFF6699));
	TF_CHECK(anyPixelWithColor(surface, 0xFF9900));
}

TF_TEST(renderer_live_page_zero_is_valid_value) {
	// online=0 是有效值（契约 §5：不要用 0 判断失败）
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(0);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(0);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "0", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "0", 1, 0xFF9900, 0xFFFFFF);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_without_rooms_shows_no_live) {
	TimerConfig config = makeConfig();
	config.pages[0] = makePage(PAGE_TYPE_LIVE, "p0");
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected, "NO LIVE", 10, 1, 1,
			config.colorDim);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_stale_value_dim) {
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(45);
	live.rooms[1].state = LIVE_ROOM_STALE;
	live.rooms[1].online.set(12345);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "45", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "1.2w", 1, 0xFF9900, config.colorDim);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_disabled_room_blank) {
	TimerConfig config = makeConfig();
	config.pages[0] = makeLivePage();
	config.pages[0].rooms[0].enabled = false;
	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(45);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(678);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	TF_CHECK(allBlackInRows(surface, 0, 8));
	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "KM9", "678", 1, 0xFF9900, 0xFFFFFF);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_no_live_when_no_configured_rooms) {
	TimerConfig config = makeConfig();
	PageConfig page = makePage(PAGE_TYPE_LIVE, "p0");
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_BILIBILI, ""));
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_DOUYIN, "", "", true));
	config.pages[0] = page;
	config.colorDim = 0x808080;

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true,
			LiveSnapshot(), surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected, "NO LIVE", 10, 1, 1, 0x808080);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_no_live_when_all_rooms_disabled) {
	TimerConfig config = makeConfig();
	PageConfig page = makePage(PAGE_TYPE_LIVE, "p0");
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_BILIBILI, "1", "", false));
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_DOUYIN, "2", "", false));
	config.pages[0] = page;

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true,
			LiveSnapshot(), surface));
	TF_CHECK(anyPixelWithColor(surface, config.colorDim));
	TF_CHECK(!anyPixelWithColor(surface, 0xFF6699));
}

TF_TEST(renderer_live_page_status_metric) {
	TimerConfig config = makeConfig();
	PageConfig page = makeLivePage();
	page.metric = LIVE_METRIC_STATUS;
	config.pages[0] = page;

	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].liveStatus.set(1);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].liveStatus.set(0);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "LIVE", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "OFF", 1, 0xFF9900, 0xFFFFFF);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_room_metric_override) {
	TimerConfig config = makeConfig();
	PageConfig page = makeLivePage();
	page.metric = LIVE_METRIC_ONLINE;
	page.rooms[1].metric = LIVE_METRIC_FANS;
	config.pages[0] = page;

	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(45);
	live.rooms[0].fans.set(999);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(11);
	live.rooms[1].fans.set(9000000);  // 0.9y

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "45", 0, 0xFF6699, 0xFFFFFF);
	drawLiveRow(expected, "KM9", "900w", 1, 0xFF9900, 0xFFFFFF);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_metric_none_shows_dashes) {
	TimerConfig config = makeConfig();
	PageConfig page = makeLivePage();
	page.metric = LIVE_METRIC_NONE;
	config.pages[0] = page;
	config.colorDim = 0x808080;

	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(12345);
	live.rooms[1].state = LIVE_ROOM_OK;
	live.rooms[1].online.set(678);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	drawLiveRow(expected, "B05", "--", 0, 0xFF6699, 0x808080);
	drawLiveRow(expected, "KM9", "--", 1, 0xFF9900, 0x808080);
	TF_CHECK(surfacesEqual(surface, expected));
}

TF_TEST(renderer_live_page_single_room_keeps_bottom_blank) {
	TimerConfig config = makeConfig();
	PageConfig page = makePage(PAGE_TYPE_LIVE, "p0");
	page.rooms.push_back(makeRoom(LIVE_PLATFORM_BILIBILI, "21452505"));
	config.pages[0] = page;

	LiveSnapshot live;
	live.rooms[0].state = LIVE_ROOM_OK;
	live.rooms[0].online.set(12345);

	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(PageRenderer::renderPage(config, 0, 0, Stage(), true, live,
			surface));

	TF_CHECK(allBlackInRows(surface, 8, 15));
	TF_CHECK(!allBlackInRows(surface, 0, 8));
}

TF_TEST(renderer_live_layout_fit_ladder) {
	PageRenderer::LiveCellLayout layout;

	TF_CHECK(PageRenderer::layoutLiveCell("B25", "1.2w", 0, layout));
	TF_EQ(layout.label, std::string("B25"));
	TF_EQ(layout.labelX, PageRenderer::kLiveTextLeft);
	TF_EQ(layout.valueX, PageRenderer::kLiveTextRight - layout.valueWidth + 1);
	TF_EQ(layout.valueWidth, 13);
	TF_EQ(layout.gap, 2);
	TF_CHECK(layout.fits);
	TF_CHECK(layout.labelWidth + layout.gap + layout.valueWidth <=
			PageRenderer::kLiveTextWidth);

	// 最长合法组合：3 字符宽 label + 6 字符 value，仍应 <=51
	TF_CHECK(PageRenderer::layoutLiveCell("MMM", "999.9w", 1, layout));
	TF_EQ(layout.label, std::string("MMM"));
	TF_EQ(layout.valueWidth, 21);
	TF_EQ(layout.labelWidth, 17);
	TF_CHECK(layout.fits);
	TF_CHECK(layout.labelWidth + layout.gap + layout.valueWidth <= 51);

	// 超过 3 字符的 label 被截断，数值永不截断
	TF_CHECK(PageRenderer::layoutLiveCell("MMMM", "999.9w", 0, layout));
	TF_EQ(layout.label, std::string("MMM"));
	TF_CHECK(layout.fits);

	// 行号越界
	TF_CHECK(!PageRenderer::layoutLiveCell("B25", "1.2w", 2, layout));
	TF_CHECK(!PageRenderer::layoutLiveCell("B25", "1.2w", -1, layout));
}

TF_TEST(renderer_banner_layout) {
	TimerConfig config = makeConfig();
	config.colorClock = 0x00FF00;
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::renderPageBanner(1, 4, "CLOCK+CD", config, surface);

	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected, "CLOCK+CD", 7, 1, 1, 0x00FF00);
	PageRenderer::drawTextCentered(expected, "2/4", 14, 1, 1, 0x00FF00);
	TF_CHECK(surfacesEqual(surface, expected));

	const Bounds top = litBounds(surface, 0, 8);
	const Bounds bottom = litBounds(surface, 9, 15);
	TF_EQ(top.minY, 2);
	TF_EQ(top.maxY, 6);
	TF_EQ(bottom.minY, 9);
	TF_EQ(bottom.maxY, 13);
	TF_CHECK(anyPixelWithColor(surface, 0x00FF00));
}

TF_TEST(renderer_banner_truncates_long_title) {
	TimerConfig config = makeConfig();
	const std::string longTitle(60, 'A');
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::renderPageBanner(0, 1, longTitle, config, surface);

	const std::string truncated =
			PageRenderer::truncateToWidth(longTitle, 52);
	TF_CHECK(truncated.size() < longTitle.size());
	Surface expected(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::drawTextCentered(expected, truncated, 7, 1, 1,
			config.colorClock);
	PageRenderer::drawTextCentered(expected, "1/1", 14, 1, 1,
			config.colorClock);
	TF_CHECK(surfacesEqual(surface, expected));
	TF_CHECK(allBlackInRows(surface, 0, 0));
}

TF_TEST(renderer_banner_empty_title_does_not_crash) {
	TimerConfig config = makeConfig();
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::renderPageBanner(9, 0, "", config, surface);
	TF_CHECK(anyPixelWithColor(surface, config.colorClock));
}

TF_TEST(renderer_overlay_two_lines) {
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::renderOverlayText("IP 192.168.1.100", 0x00FF00, surface);

	// 两行：line0 基线 6（行 1..5），line1 基线 14（行 9..13）
	const Bounds top = litBounds(surface, 0, 8);
	const Bounds bottom = litBounds(surface, 9, 15);
	TF_CHECK(top.count > 0);
	TF_CHECK(bottom.count > 0);
	TF_EQ(top.minY, 1);
	TF_EQ(top.maxY, 5);
	TF_EQ(bottom.minY, 9);
	TF_EQ(bottom.maxY, 13);
	TF_CHECK(allBlackInRows(surface, 6, 8));
	TF_CHECK(allLitPixelsColor(surface, 0x00FF00));
}

TF_TEST(renderer_overlay_single_line) {
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	// 不含空格/换行的覆盖文本走单行分支，基线 10（行 5..9）
	PageRenderer::renderOverlayText("NOWIFI", 0xFF8000, surface);

	const Bounds bounds = litBounds(surface, 0, 15);
	TF_CHECK(bounds.count > 0);
	TF_EQ(bounds.minY, 5);
	TF_EQ(bounds.maxY, 9);
	TF_CHECK(allLitPixelsColor(surface, 0xFF8000));
}

TF_TEST(renderer_overlay_truncates_to_screen) {
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	PageRenderer::renderOverlayText(std::string(100, 'W'), 0xFFFFFF, surface);
	const Bounds bounds = litBounds(surface, 0, 15);
	TF_CHECK(bounds.count > 0);
	TF_CHECK(bounds.minX >= 0);
	TF_CHECK(bounds.maxX <= PageRenderer::kScreenWidth - 1);
}

TF_TEST(renderer_invalid_page_index_returns_false_and_clears) {
	TimerConfig config = makeConfig();
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(!PageRenderer::renderPage(config, 3, 0, Stage(), true,
			LiveSnapshot(), surface));
	TF_CHECK(allBlackInRows(surface, 0, 15));

	TF_CHECK(!PageRenderer::renderPage(config, -1, 0, Stage(), true,
			LiveSnapshot(), surface));
	TF_CHECK(allBlackInRows(surface, 0, 15));
}

TF_TEST(renderer_empty_pages_returns_false) {
	TimerConfig config;  // pages 为空
	Surface surface(PageRenderer::kScreenWidth, PageRenderer::kScreenHeight);
	TF_CHECK(!PageRenderer::renderPage(config, 0, 0, Stage(), true,
			LiveSnapshot(), surface));
	TF_CHECK(allBlackInRows(surface, 0, 15));
}
