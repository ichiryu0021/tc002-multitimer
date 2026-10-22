// Host tests for config/ConfigStore v3: schema validation, v2/v1 -> v3
// migration, default 4 pages, page/room/metric/url rules, JSON round trip and
// atomic disk save (contract v3 §2).
#include "test_framework.h"

#include <cstdio>
#include <string>
#include <vector>

#include "config/ConfigStore.h"
#include "net/mini_json.h"
#include "timer/TimeMath.h"
#include "timer/TimerEngine.h"

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace {

// The host test build redirects ConfigStore's /data directory (see
// tests/run_tests.ps1 -DMULTITIMER_HOST_CONFIG_DIR=tests/out/data). When the
// macro is absent the disk tests are skipped instead of touching /data.
#ifdef MULTITIMER_HOST_CONFIG_DIR
#define TEST_STRINGIFY_INNER(x) #x
#define TEST_STRINGIFY(x) TEST_STRINGIFY_INNER(x)
const char* kConfigDir = TEST_STRINGIFY(MULTITIMER_HOST_CONFIG_DIR);
const bool kDiskTestsEnabled = true;
#else
const char* kConfigDir = "";
const bool kDiskTestsEnabled = false;
#endif

std::string configFilePath() {
	return std::string(kConfigDir) + "/multitimer.conf";
}

std::string configTmpFilePath() {
	return configFilePath() + ".tmp";
}

void ensureConfigDir() {
	if (!kDiskTestsEnabled) {
		return;
	}
#if defined(_WIN32)
	_mkdir(kConfigDir);
#else
	mkdir(kConfigDir, 0755);
#endif
}

bool writeTextFile(const std::string& path, const std::string& text) {
	FILE* fp = fopen(path.c_str(), "wb");
	if (fp == NULL) {
		return false;
	}
	const size_t written = fwrite(text.data(), 1, text.size(), fp);
	const bool ok = (written == text.size());
	fclose(fp);
	return ok;
}

std::string readTextFile(const std::string& path) {
	std::string content;
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == NULL) {
		return content;
	}
	char buf[512];
	size_t n = 0;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		content.append(buf, n);
	}
	fclose(fp);
	return content;
}

bool fileExists(const std::string& path) {
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == NULL) {
		return false;
	}
	fclose(fp);
	return true;
}

bool loadJson(const std::string& text, TimerConfig& config, std::string& err) {
	JsonValue root;
	std::string parseErr;
	if (!JsonValue::parse(text, root, parseErr)) {
		err = "json parse failed: " + parseErr;
		return false;
	}
	return ConfigStore::fromJson(root, config, err);
}

long long epochOf(int year, int month, int day, int hour, int minute,
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

DateTarget makeTarget(int year, int month, int day, int hour, int minute,
		int second, bool enabled = true) {
	DateTarget target;
	target.year = year;
	target.month = month;
	target.day = day;
	target.hour = hour;
	target.minute = minute;
	target.second = second;
	target.enabled = enabled;
	return target;
}

PageConfig makePage(const std::string& id, PageType type) {
	PageConfig page;
	page.id = id;
	page.type = type;
	return page;
}

std::string replaceOnce(std::string text, const std::string& from,
		const std::string& to) {
	const size_t pos = text.find(from);
	if (pos != std::string::npos) {
		text.replace(pos, from.size(), to);
	}
	return text;
}

// {"version":3,"tzOffsetMinutes":0,<extra>,"pages":[{"type":"clock"}]}
std::string v3JsonWithPages(const std::string& extraFields,
		const std::string& pagesJson) {
	std::string out = "{\"version\":3,\"tzOffsetMinutes\":0";
	if (!extraFields.empty()) {
		out += ",";
		out += extraFields;
	}
	out += ",\"pages\":";
	out += pagesJson;
	out += "}";
	return out;
}

std::string v3Json(const std::string& extraFields) {
	return v3JsonWithPages(extraFields, R"json([{"type":"clock"}])json");
}

std::string livePageJson(const std::string& roomsJson,
		const std::string& extraFields = std::string()) {
	std::string page = "[{\"id\":\"p0\",\"type\":\"live\"";
	if (!extraFields.empty()) {
		page += ",";
		page += extraFields;
	}
	page += ",\"rooms\":";
	page += roomsJson;
	page += "}]";
	return page;
}

std::string makeTargetsJson(int count) {
	std::string text = "[";
	for (int i = 0; i < count; ++i) {
		if (i != 0) {
			text += ",";
		}
		char buf[96];
		std::snprintf(buf, sizeof(buf),
				"{\"date\":\"2026-10-23\",\"time\":\"%02d:00:00\"}", i % 24);
		text += buf;
	}
	text += "]";
	return text;
}

std::string makePagesJson(int count) {
	std::string text = "[";
	for (int i = 0; i < count; ++i) {
		if (i != 0) {
			text += ",";
		}
		text += "{\"type\":\"clock\"}";
	}
	text += "]";
	return text;
}

std::string repeat(char c, int count) {
	return std::string(static_cast<size_t>(count), c);
}

const char* kValidV3 =
		R"json({"version":3,"tzOffsetMinutes":480,"warnSeconds":10,"ntpServer":"","colors":{"clock":"#FFFFFF","countdown":"#00FF00","warning":"#FF2020","dim":"#808080"},"display":{"splitBottomWhenIdle":"date"},"targets":[{"date":"2026-10-23","time":"19:25:00","enabled":true},{"date":"2026-10-24","time":"08:00:00","enabled":false}],"live":{"source":"bridge","url":"http://192.168.31.10:8081/live","refreshSec":30,"timeoutMs":3000},"defaultPageId":"p1","pages":[{"id":"p0","type":"clock","title":"时钟","shortTitle":"CLOCK","enabled":true},{"id":"p1","type":"clock_countdown","title":"时钟+倒计时","shortTitle":"CLOCK+CD","enabled":true},{"id":"p2","type":"countdown","title":"倒计时","shortTitle":"","enabled":false},{"id":"p3","type":"live","title":"直播","shortTitle":"LIVE","enabled":true,"metric":"online","rooms":[{"platform":"bilibili","room":"21452505","label":"B25","enabled":true,"metric":""},{"platform":"kuaishou","room":"3x8k2m9","label":"","enabled":true,"metric":"fans"}]}]})json";

const char* kMinimalV3 =
		R"json({"version":3,"tzOffsetMinutes":480,"pages":[{"type":"clock"}]})json";

const char* kValidV2 =
		R"json({"version":2,"tzOffsetMinutes":480,"warnSeconds":10,"colors":{"clock":"#FFFFFF","countdown":"#00FF00","warning":"#FF2020"},"targets":[{"date":"2026-10-23","time":"19:25:00","enabled":true},{"date":"2026-10-24","time":"08:00:00","enabled":false}]})json";

}  // namespace

TF_TEST(config_parse_color) {
	uint32_t rgb = 0;
	TF_CHECK(ConfigStore::parseColor("#00FF00", rgb));
	TF_EQ(rgb, 0x00FF00u);

	TF_CHECK(ConfigStore::parseColor("#00ff00", rgb));
	TF_EQ(rgb, 0x00FF00u);

	TF_CHECK(ConfigStore::parseColor("#FFFFFF", rgb));
	TF_EQ(rgb, 0xFFFFFFu);

	TF_CHECK(ConfigStore::parseColor("#000000", rgb));
	TF_EQ(rgb, 0u);

	TF_CHECK(ConfigStore::parseColor("#1a2B3c", rgb));
	TF_EQ(rgb, 0x1A2B3Cu);

	const char* bad[] = {
		"#GGGGGG", "#00FF0", "00FF00", "#00FF00X", "", "#12345",
		"#1234567", " #12345", "#12 456", "#+12345", "#"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TF_CHECK_MSG(!ConfigStore::parseColor(bad[i], rgb),
				std::string("accepted invalid color: ") + bad[i]);
	}
}

TF_TEST(config_from_json_v3_full) {
	TimerConfig config;
	std::string err;
	TF_CHECK_MSG(loadJson(kValidV3, config, err), err);

	TF_EQ(config.tzOffsetMinutes, 480);
	TF_EQ(config.warnSeconds, 10);
	TF_EQ(config.ntpServer, std::string(""));
	TF_EQ(config.colorClock, 0xFFFFFFu);
	TF_EQ(config.colorCountdown, 0x00FF00u);
	TF_EQ(config.colorWarning, 0xFF2020u);
	TF_EQ(config.colorDim, 0x808080u);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_BRIDGE));
	TF_EQ(config.live.url, std::string("http://192.168.31.10:8081/live"));
	TF_EQ(config.live.refreshSec, 30);
	TF_EQ(config.live.timeoutMs, 3000);
	TF_EQ(config.defaultPageId, std::string("p1"));

	TF_EQ(static_cast<int>(config.targets.size()), 2);
	TF_EQ(config.targets[0].year, 2026);
	TF_EQ(config.targets[0].month, 10);
	TF_EQ(config.targets[0].day, 23);
	TF_EQ(config.targets[0].hour, 19);
	TF_EQ(config.targets[0].minute, 25);
	TF_EQ(config.targets[0].second, 0);
	TF_EQ(config.targets[0].enabled, true);
	TF_EQ(config.targets[1].day, 24);
	TF_EQ(config.targets[1].hour, 8);
	TF_EQ(config.targets[1].enabled, false);

	TF_EQ(static_cast<int>(config.pages.size()), 4);
	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(static_cast<int>(config.pages[0].type),
			static_cast<int>(PAGE_TYPE_CLOCK));
	TF_EQ(config.pages[0].title, std::string("时钟"));
	TF_EQ(config.pages[0].shortTitle, std::string("CLOCK"));
	TF_EQ(config.pages[0].enabled, true);
	TF_EQ(static_cast<int>(config.pages[0].metric),
			static_cast<int>(LIVE_METRIC_ONLINE));
	TF_CHECK(config.pages[0].rooms.empty());

	TF_EQ(static_cast<int>(config.pages[1].type),
			static_cast<int>(PAGE_TYPE_CLOCK_COUNTDOWN));
	TF_EQ(static_cast<int>(config.pages[2].type),
			static_cast<int>(PAGE_TYPE_COUNTDOWN));
	TF_EQ(config.pages[2].enabled, false);
	TF_EQ(config.pages[2].shortTitle, std::string(""));

	TF_EQ(static_cast<int>(config.pages[3].type),
			static_cast<int>(PAGE_TYPE_LIVE));
	TF_EQ(static_cast<int>(config.pages[3].metric),
			static_cast<int>(LIVE_METRIC_ONLINE));
	TF_EQ(static_cast<int>(config.pages[3].rooms.size()), 2);
	TF_EQ(static_cast<int>(config.pages[3].rooms[0].platform),
			static_cast<int>(LIVE_PLATFORM_BILIBILI));
	TF_EQ(config.pages[3].rooms[0].room, std::string("21452505"));
	TF_EQ(config.pages[3].rooms[0].label, std::string("B25"));
	TF_EQ(config.pages[3].rooms[0].enabled, true);
	TF_EQ(static_cast<int>(config.pages[3].rooms[0].metric),
			static_cast<int>(LIVE_METRIC_INHERIT));
	TF_EQ(static_cast<int>(config.pages[3].rooms[1].platform),
			static_cast<int>(LIVE_PLATFORM_KUAISHOU));
	TF_EQ(config.pages[3].rooms[1].room, std::string("3x8k2m9"));
	TF_EQ(config.pages[3].rooms[1].label, std::string(""));
	TF_EQ(static_cast<int>(config.pages[3].rooms[1].metric),
			static_cast<int>(LIVE_METRIC_FANS));
}

TF_TEST(config_from_json_v3_defaults) {
	TimerConfig config;
	std::string err;
	TF_CHECK_MSG(loadJson(kMinimalV3, config, err), err);

	TF_EQ(config.tzOffsetMinutes, 480);
	TF_EQ(config.warnSeconds, 10);
	TF_EQ(config.ntpServer, std::string(""));
	TF_EQ(config.colorClock, 0xFFFFFFu);
	TF_EQ(config.colorCountdown, 0x00FF00u);
	TF_EQ(config.colorWarning, 0xFF2020u);
	TF_EQ(config.colorDim, 0x808080u);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_OFF));
	TF_EQ(config.live.url, std::string(""));
	TF_EQ(config.live.refreshSec, 30);
	TF_EQ(config.live.timeoutMs, 3000);
	TF_CHECK(config.targets.empty());

	TF_EQ(static_cast<int>(config.pages.size()), 1);
	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(static_cast<int>(config.pages[0].type),
			static_cast<int>(PAGE_TYPE_CLOCK));
	TF_EQ(config.pages[0].title, std::string(""));
	TF_EQ(config.pages[0].shortTitle, std::string(""));
	TF_EQ(config.pages[0].enabled, true);
	TF_EQ(static_cast<int>(config.pages[0].metric),
			static_cast<int>(LIVE_METRIC_ONLINE));
	TF_EQ(config.defaultPageId, std::string("p0"));
}

TF_TEST(config_from_json_version_rejected) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(kValidV3, config, err), err);

	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"version\":3,",
			"\"version\":2,"), config, err));
	TF_CHECK(err.find("v2 config is not accepted") != std::string::npos);
	TF_CHECK(err.find("refresh the page") != std::string::npos);

	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"version\":3,",
			"\"version\":4,"), config, err));
	TF_CHECK(err.find("version") != std::string::npos);
	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"version\":3,",
			"\"version\":1,"), config, err));
	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"version\":3,",
			"\"version\":\"3\","), config, err));
	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"version\":3,",
			"\"version\":null,"), config, err));
	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"version\":3,", ""),
			config, err));  // POST 必须显式 version:3
	TF_CHECK(err.find("version") != std::string::npos);
}

TF_TEST(config_from_json_tz_offset_minutes) {
	TimerConfig config;
	std::string err;

	// Missing / wrong type / fractional values are rejected.
	TF_CHECK(!loadJson(
			R"json({"version":3,"pages":[{"type":"clock"}]})json", config, err));
	TF_CHECK(err.find("tzOffsetMinutes") != std::string::npos);
	TF_CHECK(!loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":\"480\""), config, err));
	TF_CHECK(err.find("tzOffsetMinutes") != std::string::npos);
	TF_CHECK(!loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":8.5"), config, err));
	TF_CHECK(!loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":null"), config, err));

	// Range is -720..840 minutes.
	TF_CHECK(!loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":-721"), config, err));
	TF_CHECK(err.find("tzOffsetMinutes") != std::string::npos);
	TF_CHECK(!loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":841"), config, err));
	TF_CHECK(!loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":4800000000"), config, err));

	TF_CHECK_MSG(loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":-720"), config, err), err);
	TF_EQ(config.tzOffsetMinutes, -720);
	TF_CHECK_MSG(loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":840"), config, err), err);
	TF_EQ(config.tzOffsetMinutes, 840);
	TF_CHECK_MSG(loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":330"), config, err), err);
	TF_EQ(config.tzOffsetMinutes, 330);
	TF_CHECK_MSG(loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":345"), config, err), err);
	TF_EQ(config.tzOffsetMinutes, 345);
	TF_CHECK_MSG(loadJson(replaceOnce(v3Json(""), "\"tzOffsetMinutes\":0",
			"\"tzOffsetMinutes\":765"), config, err), err);
	TF_EQ(config.tzOffsetMinutes, 765);

	// v1 schema is not accepted by fromJson.
	TF_CHECK(!loadJson(R"json({"tzOffset":8,"points":[]})json", config, err));
}

TF_TEST(config_from_json_warn_seconds) {
	TimerConfig config;
	std::string err;

	TF_CHECK(!loadJson(v3Json("\"warnSeconds\":-1"), config, err));
	TF_CHECK(err.find("warnSeconds") != std::string::npos);
	TF_CHECK(!loadJson(v3Json("\"warnSeconds\":601"), config, err));
	TF_CHECK(!loadJson(v3Json("\"warnSeconds\":\"10\""), config, err));
	TF_CHECK(!loadJson(v3Json("\"warnSeconds\":10.5"), config, err));

	TF_CHECK_MSG(loadJson(v3Json("\"warnSeconds\":0"), config, err), err);
	TF_EQ(config.warnSeconds, 0);
	TF_CHECK_MSG(loadJson(v3Json("\"warnSeconds\":600"), config, err), err);
	TF_EQ(config.warnSeconds, 600);
	TF_CHECK_MSG(loadJson(v3Json("\"warnSeconds\":42"), config, err), err);
	TF_EQ(config.warnSeconds, 42);
}

TF_TEST(config_from_json_colors_and_dim) {
	TimerConfig config;
	std::string err;

	TF_CHECK(!loadJson(v3Json("\"colors\":[]"), config, err));
	TF_CHECK(err.find("colors") != std::string::npos);

	const char* keys[] = { "clock", "countdown", "warning", "dim" };
	const char* badValues[] = {
		"\"#GGGGGG\"", "\"#00FF0\"", "\"00FF00\"", "\"#00FF00X\"",
		"\"\"", "123", "null", "true"
	};
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) {
		for (size_t i = 0; i < sizeof(badValues) / sizeof(badValues[0]);
				++i) {
			const std::string text = v3Json(
					std::string("\"colors\":{\"") + keys[k] + "\":" +
					badValues[i] + "}");
			TF_CHECK_MSG(!loadJson(text, config, err),
					std::string("accepted invalid color: ") + keys[k] +
							" = " + badValues[i]);
			TF_CHECK(err.find("color") != std::string::npos);
		}
	}

	// Partial colors: only the keys present override defaults.
	TF_CHECK_MSG(loadJson(v3Json(
			"\"colors\":{\"warning\":\"#123456\"}"), config, err), err);
	TF_EQ(config.colorClock, 0xFFFFFFu);
	TF_EQ(config.colorCountdown, 0x00FF00u);
	TF_EQ(config.colorWarning, 0x123456u);
	TF_EQ(config.colorDim, 0x808080u);

	// dim is configurable and lowercase is accepted.
	TF_CHECK_MSG(loadJson(v3Json(
			"\"colors\":{\"dim\":\"#00ff00\"}"), config, err), err);
	TF_EQ(config.colorDim, 0x00FF00u);
}

TF_TEST(config_from_json_display_split_idle) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3Json(""), config, err), err);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));

	TF_CHECK_MSG(loadJson(v3Json(
			"\"display\":{\"splitBottomWhenIdle\":\"blank\"}"), config, err),
			err);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_BLANK));

	TF_CHECK_MSG(loadJson(v3Json(
			"\"display\":{\"splitBottomWhenIdle\":\"date\"}"), config, err),
			err);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));

	// 非枚举值/非对象：回默认，不报错（契约 §2 只约束两个枚举值）
	TF_CHECK_MSG(loadJson(v3Json(
			"\"display\":{\"splitBottomWhenIdle\":\"nope\"}"), config, err),
			err);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));
	TF_CHECK_MSG(loadJson(v3Json("\"display\":[]"), config, err), err);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));
	TF_CHECK_MSG(loadJson(v3Json(
			"\"display\":{\"wrap\":true,\"splitBottomWhenIdle\":\"blank\"}"),
			config, err), err);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_BLANK));
}

TF_TEST(config_from_json_targets_limits) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3Json("\"targets\":" + makeTargetsJson(12)),
			config, err), err);
	TF_EQ(static_cast<int>(config.targets.size()), 12);

	TF_CHECK(!loadJson(v3Json("\"targets\":" + makeTargetsJson(13)),
			config, err));
	TF_CHECK(err.find("too many") != std::string::npos);

	TF_CHECK(!loadJson(v3Json("\"targets\":{}"), config, err));
	TF_CHECK(err.find("targets") != std::string::npos);

	TF_CHECK_MSG(loadJson(v3Json("\"targets\":[]"), config, err), err);
	TF_CHECK(config.targets.empty());
}

TF_TEST(config_from_json_invalid_targets_dropped) {
	// 12 array slots, only one valid entry: invalid ones are dropped, not
	// rejected (the 12-item cap applies to the array length).
	const std::string targets =
			R"json([{"date":"2026-02-30","time":"10:00:00"},{"date":"2026-10-23","time":"24:00:00"},{"date":"2026-10-23","time":"10:00"},{"date":"26-10-23","time":"10:00:00"},{"date":20261023,"time":"10:00:00"},{"time":"10:00:00"},{"date":"2026-10-23"},{"enabled":true},42,null,{"date":"2026-10-23","time":"08:00:00","enabled":true},{"date":"1999-12-31","time":"23:59:59"}])json";
	TimerConfig config;
	std::string err;
	TF_CHECK_MSG(loadJson(v3Json("\"targets\":" + std::string(targets)),
			config, err), err);
	TF_EQ(static_cast<int>(config.targets.size()), 1);
	TF_EQ(config.targets[0].year, 2026);
	TF_EQ(config.targets[0].month, 10);
	TF_EQ(config.targets[0].day, 23);
	TF_EQ(config.targets[0].hour, 8);
	TF_EQ(config.targets[0].enabled, true);
}

TF_TEST(config_from_json_target_enabled) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3Json(
			"\"targets\":[{\"date\":\"2026-10-23\",\"time\":\"08:00:00\"}]"),
			config, err), err);
	TF_EQ(static_cast<int>(config.targets.size()), 1);
	TF_EQ(config.targets[0].enabled, true);

	TF_CHECK_MSG(loadJson(v3Json(
			"\"targets\":[{\"date\":\"2026-10-23\",\"time\":\"08:00:00\",\"enabled\":false}]"),
			config, err), err);
	TF_EQ(config.targets[0].enabled, false);

	// A non-boolean enabled makes the single item invalid: it is dropped.
	TF_CHECK_MSG(loadJson(v3Json(
			"\"targets\":[{\"date\":\"2026-10-23\",\"time\":\"08:00:00\",\"enabled\":\"yes\"},{\"date\":\"2026-10-24\",\"time\":\"09:00:00\"}]"),
			config, err), err);
	TF_EQ(static_cast<int>(config.targets.size()), 1);
	TF_EQ(config.targets[0].day, 24);
}

TF_TEST(config_from_json_ignores_unknown_fields) {
	TimerConfig config;
	std::string err;
	const std::string text =
			R"json({"version":3,"tzOffsetMinutes":60,"warnSeconds":5,"future":123,"nested":{"a":[1,2]},"colors":{"clock":"#111111","countup":"#222222"},"display":{"wrap":true},"targets":[{"date":"2026-10-23","time":"08:00:00","label":"x","enabled":true}],"live":{"source":"off","future":true},"pages":[{"id":"p0","type":"clock","future":[1],"rooms":[]}]})json";
	TF_CHECK_MSG(loadJson(text, config, err), err);
	TF_EQ(config.tzOffsetMinutes, 60);
	TF_EQ(config.warnSeconds, 5);
	TF_EQ(config.colorClock, 0x111111u);
	TF_EQ(config.colorCountdown, 0x00FF00u);  // default kept
	TF_EQ(config.colorWarning, 0xFF2020u);
	TF_CHECK(config.targets[0].hour == 8);
	TF_EQ(static_cast<int>(config.pages.size()), 1);
	TF_CHECK(config.pages[0].rooms.empty());
}

TF_TEST(config_from_json_non_object_roots) {
	TimerConfig config;
	std::string err;
	TF_CHECK(!loadJson("[]", config, err));
	TF_CHECK(!loadJson(R"json("text")json", config, err));
	TF_CHECK(!loadJson("null", config, err));
	TF_CHECK(!loadJson("42", config, err));
	TF_CHECK(!loadJson("true", config, err));
}

TF_TEST(config_failure_does_not_commit) {
	TimerConfig original;
	original.tzOffsetMinutes = 180;
	original.warnSeconds = 7;
	original.colorClock = 0x010203;
	original.colorDim = 0x040506;
	original.pages.push_back(makePage("keep", PAGE_TYPE_CLOCK));
	original.defaultPageId = "keep";
	original.targets.push_back(makeTarget(2026, 10, 23, 1, 2, 3, false));

	TimerConfig out = original;
	std::string err;
	TF_CHECK(!loadJson(
			replaceOnce(kValidV3, "\"tzOffsetMinutes\":480",
					"\"tzOffsetMinutes\":9999"), out, err));
	TF_EQ(out.tzOffsetMinutes, 180);
	TF_EQ(out.warnSeconds, 7);
	TF_EQ(out.colorClock, 0x010203u);
	TF_EQ(out.colorDim, 0x040506u);
	TF_EQ(static_cast<int>(out.pages.size()), 1);
	TF_EQ(out.pages[0].id, std::string("keep"));
	TF_EQ(out.defaultPageId, std::string("keep"));
	TF_EQ(static_cast<int>(out.targets.size()), 1);
	TF_EQ(out.targets[0].day, 23);
	TF_EQ(out.targets[0].enabled, false);

	TF_CHECK(!loadJson(replaceOnce(kValidV3, "\"#FF2020\"", "\"nope\""),
			out, err));
	TF_EQ(out.tzOffsetMinutes, 180);
	TF_EQ(static_cast<int>(out.pages.size()), 1);
}

TF_TEST(config_from_json_pages_required_and_limits) {
	TimerConfig config;
	std::string err;

	TF_CHECK(!loadJson(
			R"json({"version":3,"tzOffsetMinutes":0})json", config, err));
	TF_CHECK(err.find("pages") != std::string::npos);
	TF_CHECK(!loadJson(v3JsonWithPages("", "[]"), config, err));
	TF_CHECK(err.find("pages") != std::string::npos);
	TF_CHECK(!loadJson(v3JsonWithPages("", "{}"), config, err));
	TF_CHECK(err.find("pages") != std::string::npos);
	TF_CHECK(!loadJson(v3JsonWithPages("", makePagesJson(13)), config, err));
	TF_CHECK(err.find("too many pages") != std::string::npos);
	TF_CHECK_MSG(loadJson(v3JsonWithPages("", makePagesJson(12)), config,
			err), err);
	TF_EQ(static_cast<int>(config.pages.size()), 12);

	// Page items are structural: non-object / missing type / unknown type.
	TF_CHECK(!loadJson(v3JsonWithPages("", "[42]"), config, err));
	TF_CHECK(err.find("page") != std::string::npos);
	TF_CHECK(!loadJson(v3JsonWithPages("", "[{}]"), config, err));
	TF_CHECK(err.find("type") != std::string::npos);
	TF_CHECK(!loadJson(v3JsonWithPages("", "[{\"type\":\"split\"}]"),
			config, err));
	TF_CHECK(err.find("unknown page type") != std::string::npos);
	TF_CHECK(!loadJson(v3JsonWithPages("", "[{\"type\":\"CLOCK\"}]"),
			config, err));
}

TF_TEST(config_from_json_page_types) {
	const char* types[] = { "clock", "clock_countdown", "countdown", "live" };
	const PageType expected[] = {
		PAGE_TYPE_CLOCK, PAGE_TYPE_CLOCK_COUNTDOWN, PAGE_TYPE_COUNTDOWN,
		PAGE_TYPE_LIVE
	};
	for (size_t i = 0; i < 4; ++i) {
		std::string page = std::string("[{\"type\":\"") + types[i] + "\"";
		if (expected[i] == PAGE_TYPE_LIVE) {
			page += ",\"rooms\":[{\"platform\":\"bilibili\",\"room\":\"1\"}]";
		}
		page += "}]";
		TimerConfig config;
		std::string err;
		TF_CHECK_MSG(loadJson(v3JsonWithPages("", page), config, err), err);
		TF_EQ(static_cast<int>(config.pages.size()), 1);
		TF_EQ(static_cast<int>(config.pages[0].type),
				static_cast<int>(expected[i]));
	}
}

TF_TEST(config_from_json_page_ids) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"id":"p0","type":"clock"},{"id":"p1","type":"countdown"}])json"),
			config, err), err);
	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(config.pages[1].id, std::string("p1"));

	// Invalid / duplicate / missing ids are generated from the index.
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"id":"P0","type":"clock"},{"id":"toolongid","type":"clock"},{"id":"p2","type":"clock"},{"id":"p2","type":"clock"},{"type":"clock"}])json"),
			config, err), err);
	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(config.pages[1].id, std::string("p1"));
	TF_EQ(config.pages[2].id, std::string("p2"));
	TF_EQ(config.pages[3].id, std::string("p3"));
	TF_EQ(config.pages[4].id, std::string("p4"));
	for (size_t i = 0; i < config.pages.size(); ++i) {
		const std::string& id = config.pages[i].id;
		TF_CHECK(id.size() >= 1 && id.size() <= 8);
		for (size_t k = 0; k < id.size(); ++k) {
			const char c = id[k];
			TF_CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
					c == '_' || c == '-');
		}
	}

	// Explicit legal ids that collide with generated ones: generator picks the
	// next free suffix (still <= 8 chars, still unique).
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"id":"p0","type":"clock"},{"type":"clock"},{"type":"clock"}])json"),
			config, err), err);
	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(config.pages[1].id, std::string("p1"));
	TF_EQ(config.pages[2].id, std::string("p2"));
}

TF_TEST(config_from_json_page_title_and_short_title) {
	TimerConfig config;
	std::string err;

	// 16 个中文码点完整保留，第 17 个起截断。
	const std::string sixteen =
			"一二三四五六七八九十甲乙丙丁戊己";
	const std::string seventeen = sixteen + "庚";
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			"[{\"type\":\"clock\",\"title\":\"" + seventeen + "\"}]"),
			config, err), err);
	TF_EQ(config.pages[0].title, sixteen);

	// 控制字符被剔除，引号/反斜杠按 JSON 解码后保留。
	const std::string controlTitle =
			"[{\"type\":\"clock\",\"title\":\"a\\u0007b\\\"c\\\\d\"}]";
	TF_CHECK_MSG(loadJson(v3JsonWithPages("", controlTitle), config, err),
			err);
	TF_EQ(config.pages[0].title, std::string("ab\"c\\d"));

	// shortTitle: <=8 printable ASCII；非 ASCII / 控制字符剔除；大小写保留。
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"clock","shortTitle":"COUNTDOWN"}])json"),
			config, err), err);
	TF_EQ(config.pages[0].shortTitle, std::string("COUNTDOW"));

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"clock","shortTitle":"Clock"}])json"),
			config, err), err);
	TF_EQ(config.pages[0].shortTitle, std::string("Clock"));

	const std::string controlShortTitle =
			"[{\"type\":\"clock\",\"shortTitle\":\"a\\u0007中b\"}]";
	TF_CHECK_MSG(loadJson(v3JsonWithPages("", controlShortTitle), config,
			err), err);
	TF_EQ(config.pages[0].shortTitle, std::string("ab"));

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"clock","shortTitle":""}])json"),
			config, err), err);
	TF_EQ(config.pages[0].shortTitle, std::string(""));

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"countdown","shortTitle":"ABCDEFGHIJK"}])json"),
			config, err), err);
	TF_EQ(config.pages[0].shortTitle, std::string("ABCDEFGH"));
}

TF_TEST(config_from_json_page_enabled_and_metric) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"clock"}])json"), config, err), err);
	TF_EQ(config.pages[0].enabled, true);
	TF_EQ(static_cast<int>(config.pages[0].metric),
			static_cast<int>(LIVE_METRIC_ONLINE));

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"clock","enabled":false}])json"), config, err),
			err);
	TF_EQ(config.pages[0].enabled, false);

	// 非布尔 enabled：宽容取默认 true（不静默变成 false）
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"clock","enabled":"no"}])json"), config, err),
			err);
	TF_EQ(config.pages[0].enabled, true);

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","metric":"likes","rooms":[{"platform":"douyin","room":"1"}]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].metric),
			static_cast<int>(LIVE_METRIC_LIKES));

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","metric":"status","rooms":[{"platform":"douyin","room":"1"}]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].metric),
			static_cast<int>(LIVE_METRIC_STATUS));

	// 非法页指标回退 online
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","metric":"gifts","rooms":[{"platform":"douyin","room":"1"}]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].metric),
			static_cast<int>(LIVE_METRIC_ONLINE));
}

TF_TEST(config_from_json_live_rooms_required) {
	TimerConfig config;
	std::string err;

	TF_CHECK(!loadJson(v3JsonWithPages("",
			R"json([{"type":"live"}])json"), config, err));
	TF_CHECK(err.find("rooms") != std::string::npos);

	TF_CHECK(!loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[]}])json"), config, err));
	TF_CHECK(!loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[{"platform":"bilibili","room":"1"},{"platform":"bilibili","room":"2"},{"platform":"bilibili","room":"3"}]}])json"),
			config, err));
	TF_CHECK(err.find("1..2") != std::string::npos);

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[{"platform":"bilibili","room":"1"}]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].rooms.size()), 1);

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[{"platform":"bilibili","room":"1"},{"platform":"douyin","room":"2"}]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].rooms.size()), 2);
}

TF_TEST(config_from_json_live_room_normalization) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[
				{"platform":"tiktok","room":"123"},
				{"platform":"douyin","room":"ok","label":"b25x","enabled":false,"metric":"gifts"}
			]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].rooms.size()), 2);
	// 非法 platform：整格变空槽（保持数组长度）
	TF_EQ(config.pages[0].rooms[0].room, std::string(""));
	TF_EQ(config.pages[0].rooms[0].enabled, false);
	TF_EQ(config.pages[0].rooms[0].label, std::string(""));
	TF_EQ(static_cast<int>(config.pages[0].rooms[0].metric),
			static_cast<int>(LIVE_METRIC_INHERIT));

	TF_EQ(static_cast<int>(config.pages[0].rooms[1].platform),
			static_cast<int>(LIVE_PLATFORM_DOUYIN));
	TF_EQ(config.pages[0].rooms[1].room, std::string("ok"));
	TF_EQ(config.pages[0].rooms[1].label, std::string("B25"));
	TF_EQ(config.pages[0].rooms[1].enabled, false);
	TF_EQ(static_cast<int>(config.pages[0].rooms[1].metric),
			static_cast<int>(LIVE_METRIC_INHERIT));  // gifts 非法 -> 继承

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[
				{"platform":"bilibili","room":"12 3"},
				{"platform":"kuaishou","room":"abcdefghijklmnopqrstuvwxyz0123456789abcde"}
			]}])json"),
			config, err), err);
	TF_EQ(config.pages[0].rooms[0].room, std::string(""));
	TF_EQ(config.pages[0].rooms[1].room, std::string(""));
}

TF_TEST(config_from_json_live_room_duplicates) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[
				{"platform":"bilibili","room":"21452505"},
				{"platform":"bilibili","room":"21452505"}
			]}])json"),
			config, err), err);
	TF_EQ(config.pages[0].rooms[0].room, std::string("21452505"));
	TF_EQ(config.pages[0].rooms[1].room, std::string(""));
	TF_EQ(config.pages[0].rooms[1].enabled, false);

	// 同房间号不同平台是合法的两个房间
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[
				{"platform":"bilibili","room":"123"},
				{"platform":"douyin","room":"123"}
			]}])json"),
			config, err), err);
	TF_EQ(config.pages[0].rooms[0].room, std::string("123"));
	TF_EQ(config.pages[0].rooms[1].room, std::string("123"));

	// 空槽（room=""）不去重：默认页两个空槽都保留
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[
				{"platform":"bilibili","room":""},
				{"platform":"bilibili","room":""}
			]}])json"),
			config, err), err);
	TF_EQ(config.pages[0].rooms[0].enabled, true);
	TF_EQ(config.pages[0].rooms[1].enabled, true);
}

TF_TEST(config_from_json_live_source) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3Json(""), config, err), err);
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_OFF));

	const char* sources[] = { "off", "bridge", "hybrid", "push" };
	const LiveSourceKind kinds[] = {
		LIVE_SOURCE_OFF, LIVE_SOURCE_BRIDGE, LIVE_SOURCE_HYBRID,
		LIVE_SOURCE_PUSH
	};
	for (size_t i = 0; i < 4; ++i) {
		TF_CHECK_MSG(loadJson(v3Json(
				std::string("\"live\":{\"source\":\"") + sources[i] + "\"}"),
				config, err), err);
		TF_EQ(static_cast<int>(config.live.kind),
				static_cast<int>(kinds[i]));
	}

	TF_CHECK(!loadJson(v3Json("\"live\":{\"source\":\"bilibili\"}"),
			config, err));
	TF_CHECK(err.find("live.source") != std::string::npos);
	TF_CHECK(!loadJson(v3Json("\"live\":{\"source\":1}"), config, err));
	TF_CHECK(!loadJson(v3Json("\"live\":[]"), config, err));
}

TF_TEST(config_from_json_live_url) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3Json(
			"\"live\":{\"source\":\"bridge\",\"url\":\"http://192.168.1.2:8081/live\"}"),
			config, err), err);
	TF_EQ(config.live.url, std::string("http://192.168.1.2:8081/live"));

	// bridge/hybrid 允许空 url（抖音/快手显示 --，契约 §2）
	TF_CHECK_MSG(loadJson(v3Json(
			"\"live\":{\"source\":\"bridge\",\"url\":\"\"}"), config, err),
			err);
	TF_EQ(config.live.url, std::string(""));
	TF_CHECK_MSG(loadJson(v3Json(
			"\"live\":{\"source\":\"hybrid\"}"), config, err), err);
	TF_EQ(config.live.url, std::string(""));

	const char* bad[] = {
		"https://192.168.1.2/live", "ftp://host/live", "192.168.1.2:8081",
		"http://", "http://host/a b", "http://host/a\tb"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TF_CHECK_MSG(!loadJson(v3Json(
				std::string("\"live\":{\"source\":\"bridge\",\"url\":\"") +
				bad[i] + "\"}"), config, err),
				std::string("accepted invalid url: ") + bad[i]);
	}

	const std::string longUrl = "http://" + repeat('a', 154);  // 161 字符
	TF_CHECK(!loadJson(v3Json(
			"\"live\":{\"source\":\"bridge\",\"url\":\"" + longUrl + "\"}"),
			config, err));
	TF_CHECK(err.find("live.url") != std::string::npos);

	TF_CHECK(!loadJson(v3Json("\"live\":{\"url\":123}"), config, err));
	TF_CHECK(err.find("live.url") != std::string::npos);
}

TF_TEST(config_from_json_live_refresh_and_timeout) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(v3Json(""), config, err), err);
	TF_EQ(config.live.refreshSec, 30);
	TF_EQ(config.live.timeoutMs, 3000);

	TF_CHECK_MSG(loadJson(v3Json(
			"\"live\":{\"refreshSec\":15,\"timeoutMs\":1000}"), config, err),
			err);
	TF_EQ(config.live.refreshSec, 15);
	TF_EQ(config.live.timeoutMs, 1000);
	TF_CHECK_MSG(loadJson(v3Json(
			"\"live\":{\"refreshSec\":300,\"timeoutMs\":5000}"), config, err),
			err);
	TF_EQ(config.live.refreshSec, 300);
	TF_EQ(config.live.timeoutMs, 5000);

	TF_CHECK(!loadJson(v3Json("\"live\":{\"refreshSec\":14}"), config, err));
	TF_CHECK(err.find("refreshSec") != std::string::npos);
	TF_CHECK(!loadJson(v3Json("\"live\":{\"refreshSec\":301}"), config, err));
	TF_CHECK(!loadJson(v3Json("\"live\":{\"refreshSec\":\"30\"}"), config,
			err));
	TF_CHECK(!loadJson(v3Json("\"live\":{\"refreshSec\":30.5}"), config, err));
	TF_CHECK(!loadJson(v3Json("\"live\":{\"timeoutMs\":999}"), config, err));
	TF_CHECK(err.find("timeoutMs") != std::string::npos);
	TF_CHECK(!loadJson(v3Json("\"live\":{\"timeoutMs\":5001}"), config, err));
	TF_CHECK(!loadJson(v3Json("\"live\":{\"timeoutMs\":null}"), config, err));
}

TF_TEST(config_from_json_default_page_id) {
	TimerConfig config;
	std::string err;

	TF_CHECK_MSG(loadJson(kMinimalV3, config, err), err);
	TF_EQ(config.defaultPageId, std::string("p0"));

	TF_CHECK_MSG(loadJson(v3JsonWithPages("\"defaultPageId\":\"p2\"",
			R"json([{"id":"p0","type":"clock"},{"id":"p1","type":"clock"},{"id":"p2","type":"clock"}])json"),
			config, err), err);
	TF_EQ(config.defaultPageId, std::string("p2"));

	// 引用不存在 / 非字符串 / 缺失：回退第一页
	TF_CHECK_MSG(loadJson(v3JsonWithPages("\"defaultPageId\":\"nope\"",
			R"json([{"id":"first","type":"clock"},{"id":"second","type":"clock"}])json"),
			config, err), err);
	TF_EQ(config.defaultPageId, std::string("first"));
	TF_CHECK_MSG(loadJson(v3JsonWithPages("\"defaultPageId\":3",
			R"json([{"id":"first","type":"clock"}])json"),
			config, err), err);
	TF_EQ(config.defaultPageId, std::string("first"));

	// 引用 disabled 页面仍是合法引用（契约只要求“存在”）
	TF_CHECK_MSG(loadJson(v3JsonWithPages("\"defaultPageId\":\"p1\"",
			R"json([{"id":"p0","type":"clock"},{"id":"p1","type":"clock","enabled":false}])json"),
			config, err), err);
	TF_EQ(config.defaultPageId, std::string("p1"));
}

TF_TEST(config_to_json_field_order_and_format) {
	TimerConfig config = ConfigStore::defaultConfig();
	config.tzOffsetMinutes = -300;
	config.warnSeconds = 30;
	config.ntpServer = "ntp.example.com:123";
	config.colorClock = 0x010203;
	config.colorCountdown = 0x00ABCD;
	config.colorWarning = 0xFF2020;
	config.colorDim = 0x808080;
	config.splitBottomWhenIdle = SPLIT_IDLE_BLANK;
	config.live.kind = LIVE_SOURCE_HYBRID;
	config.live.url = "http://192.168.1.9:8081/live";
	config.live.refreshSec = 45;
	config.live.timeoutMs = 1500;
	config.defaultPageId = "p1";
	config.targets.push_back(makeTarget(2026, 10, 23, 19, 25, 0, true));
	config.targets.push_back(makeTarget(2026, 10, 24, 8, 0, 5, false));

	const std::string json = ConfigStore::toJson(config);

	const size_t pVersion = json.find("\"version\"");
	const size_t pTz = json.find("\"tzOffsetMinutes\"");
	const size_t pWarn = json.find("\"warnSeconds\"");
	const size_t pNtp = json.find("\"ntpServer\"");
	const size_t pColors = json.find("\"colors\"");
	const size_t pDisplay = json.find("\"display\"");
	const size_t pTargets = json.find("\"targets\"");
	const size_t pLive = json.find("\"live\"");
	const size_t pDefaultPage = json.find("\"defaultPageId\"");
	const size_t pPages = json.find("\"pages\"");
	TF_CHECK(pVersion != std::string::npos);
	TF_CHECK(pTz != std::string::npos);
	TF_CHECK(pWarn != std::string::npos);
	TF_CHECK(pNtp != std::string::npos);
	TF_CHECK(pColors != std::string::npos);
	TF_CHECK(pDisplay != std::string::npos);
	TF_CHECK(pTargets != std::string::npos);
	TF_CHECK(pLive != std::string::npos);
	TF_CHECK(pDefaultPage != std::string::npos);
	TF_CHECK(pPages != std::string::npos);
	TF_CHECK(pVersion < pTz);
	TF_CHECK(pTz < pWarn);
	TF_CHECK(pWarn < pNtp);
	TF_CHECK(pNtp < pColors);
	TF_CHECK(pColors < pDisplay);
	TF_CHECK(pDisplay < pTargets);
	TF_CHECK(pTargets < pLive);
	TF_CHECK(pLive < pDefaultPage);
	TF_CHECK(pDefaultPage < pPages);

	TF_CHECK(json.find("\n  \"version\": 3,") != std::string::npos);
	TF_CHECK(json.find("\n  \"tzOffsetMinutes\": -300,") != std::string::npos);
	TF_CHECK(json.find("\n  \"warnSeconds\": 30,") != std::string::npos);
	TF_CHECK(json.find("\n  \"ntpServer\": \"ntp.example.com:123\",") !=
			std::string::npos);
	TF_CHECK(json.find("\n    \"clock\": \"#010203\",") != std::string::npos);
	TF_CHECK(json.find("\n    \"countdown\": \"#00ABCD\",") !=
			std::string::npos);
	TF_CHECK(json.find("\n    \"warning\": \"#FF2020\",") !=
			std::string::npos);
	TF_CHECK(json.find("\n    \"dim\": \"#808080\"\n") != std::string::npos);
	TF_CHECK(json.find("\n    \"splitBottomWhenIdle\": \"blank\"\n") !=
			std::string::npos);
	TF_CHECK(json.find("\n    \"source\": \"hybrid\",") != std::string::npos);
	TF_CHECK(json.find("\n    \"url\": \"http://192.168.1.9:8081/live\",") !=
			std::string::npos);
	TF_CHECK(json.find("\n    \"refreshSec\": 45,") != std::string::npos);
	TF_CHECK(json.find("\n    \"timeoutMs\": 1500\n") != std::string::npos);
	TF_CHECK(json.find("\"defaultPageId\": \"p1\",") != std::string::npos);
	TF_CHECK(json.find("\"date\": \"2026-10-23\"") != std::string::npos);
	TF_CHECK(json.find("\"time\": \"19:25:00\"") != std::string::npos);
	TF_CHECK(json.find("\"time\": \"08:00:05\"") != std::string::npos);
	TF_CHECK(json.find("\"enabled\": false") != std::string::npos);

	// Colors keep their contract order inside the object.
	TF_CHECK(json.find("\"clock\"") < json.find("\"countdown\""));
	TF_CHECK(json.find("\"countdown\"") < json.find("\"warning\""));
	TF_CHECK(json.find("\"warning\"") < json.find("\"dim\""));

	// Page fields order: id < type < title < shortTitle < enabled < metric < rooms
	const size_t pageStart = json.find("\"pages\"");
	TF_CHECK(pageStart != std::string::npos);
	TF_CHECK(json.find("\"id\": \"p0\",", pageStart) != std::string::npos);
	TF_CHECK(json.find("\"type\": \"clock\",", pageStart) !=
			std::string::npos);
	TF_CHECK(json.find("\"rooms\": [", pageStart) != std::string::npos);
	TF_CHECK(json.find("\"platform\": \"bilibili\",", pageStart) !=
			std::string::npos);
	TF_CHECK(json.find("\"room\": \"\",", pageStart) != std::string::npos);
	TF_CHECK(json.find("\"label\": \"\",", pageStart) != std::string::npos);
	TF_CHECK(json.find("\"metric\": \"\"", pageStart) != std::string::npos);

	// The output must be parseable JSON again.
	JsonValue root;
	std::string parseErr;
	TF_CHECK_MSG(JsonValue::parse(json, root, parseErr), parseErr);
	TF_CHECK(root.isObject());
	TF_EQ(root["version"].asInt(0), 3);
	TF_EQ(root["tzOffsetMinutes"].asInt(0), -300);
	TF_EQ(root["live"]["source"].asString(), std::string("hybrid"));
}

TF_TEST(config_to_json_empty_targets) {
	TimerConfig config = ConfigStore::defaultConfig();
	const std::string json = ConfigStore::toJson(config);
	TF_CHECK(json.find("\"targets\": [],") != std::string::npos);

	TimerConfig back;
	std::string err;
	TF_CHECK_MSG(loadJson(json, back, err), err);
	TF_CHECK(back.targets.empty());
}

TF_TEST(config_to_json_escapes_strings) {
	TimerConfig config = ConfigStore::defaultConfig();
	config.pages[0].title = "a\"b\\c";
	config.pages[0].shortTitle = "X";
	config.pages[1].title = "时钟";
	config.pages[2].title = "x\ty";
	config.ntpServer = "";
	config.live.url = "";

	const std::string json = ConfigStore::toJson(config);
	TF_CHECK(json.find("a\\\"b\\\\c") != std::string::npos);
	TF_CHECK(json.find("\"时钟\"") != std::string::npos);
	TF_CHECK(json.find("x\\u0009y") != std::string::npos);

	JsonValue root;
	std::string parseErr;
	TF_CHECK_MSG(JsonValue::parse(json, root, parseErr), parseErr);
	TF_CHECK(root.isObject());
	const JsonValue& pages = root["pages"];
	TF_EQ(static_cast<int>(pages.size()), 4);
	TF_EQ(pages.at(0)["title"].asString(), std::string("a\"b\\c"));
	TF_EQ(pages.at(1)["title"].asString(), std::string("时钟"));
	TF_EQ(pages.at(2)["title"].asString(), std::string("x\ty"));

	TimerConfig back;
	std::string err;
	TF_CHECK_MSG(loadJson(json, back, err), err);
	TF_EQ(back.pages[0].title, std::string("a\"b\\c"));
	TF_EQ(back.pages[1].title, std::string("时钟"));
	TF_EQ(back.pages[2].title, std::string("xy"));  // 控制字符在 fromJson 丢弃
}

TF_TEST(config_v3_round_trip_full) {
	TimerConfig config;
	std::string err;
	TF_CHECK_MSG(loadJson(kValidV3, config, err), err);

	const std::string json = ConfigStore::toJson(config);
	TimerConfig back;
	TF_CHECK_MSG(loadJson(json, back, err), err);

	TF_EQ(back.tzOffsetMinutes, 480);
	TF_EQ(back.warnSeconds, 10);
	TF_EQ(back.ntpServer, std::string(""));
	TF_EQ(back.colorClock, config.colorClock);
	TF_EQ(back.colorCountdown, config.colorCountdown);
	TF_EQ(back.colorWarning, config.colorWarning);
	TF_EQ(back.colorDim, config.colorDim);
	TF_EQ(static_cast<int>(back.splitBottomWhenIdle),
			static_cast<int>(config.splitBottomWhenIdle));
	TF_EQ(static_cast<int>(back.live.kind),
			static_cast<int>(config.live.kind));
	TF_EQ(back.live.url, config.live.url);
	TF_EQ(back.live.refreshSec, config.live.refreshSec);
	TF_EQ(back.live.timeoutMs, config.live.timeoutMs);
	TF_EQ(back.defaultPageId, config.defaultPageId);
	TF_EQ(static_cast<int>(back.targets.size()),
			static_cast<int>(config.targets.size()));
	for (size_t i = 0; i < config.targets.size(); ++i) {
		TF_EQ(back.targets[i].year, config.targets[i].year);
		TF_EQ(back.targets[i].month, config.targets[i].month);
		TF_EQ(back.targets[i].day, config.targets[i].day);
		TF_EQ(back.targets[i].hour, config.targets[i].hour);
		TF_EQ(back.targets[i].minute, config.targets[i].minute);
		TF_EQ(back.targets[i].second, config.targets[i].second);
		TF_EQ(back.targets[i].enabled, config.targets[i].enabled);
	}

	TF_EQ(static_cast<int>(back.pages.size()),
			static_cast<int>(config.pages.size()));
	for (size_t i = 0; i < config.pages.size(); ++i) {
		TF_EQ(back.pages[i].id, config.pages[i].id);
		TF_EQ(static_cast<int>(back.pages[i].type),
				static_cast<int>(config.pages[i].type));
		TF_EQ(back.pages[i].title, config.pages[i].title);
		TF_EQ(back.pages[i].shortTitle, config.pages[i].shortTitle);
		TF_EQ(back.pages[i].enabled, config.pages[i].enabled);
		TF_EQ(static_cast<int>(back.pages[i].metric),
				static_cast<int>(config.pages[i].metric));
		TF_EQ(static_cast<int>(back.pages[i].rooms.size()),
				static_cast<int>(config.pages[i].rooms.size()));
		for (size_t r = 0; r < config.pages[i].rooms.size(); ++r) {
			TF_EQ(static_cast<int>(back.pages[i].rooms[r].platform),
					static_cast<int>(config.pages[i].rooms[r].platform));
			TF_EQ(back.pages[i].rooms[r].room,
					config.pages[i].rooms[r].room);
			TF_EQ(back.pages[i].rooms[r].label,
					config.pages[i].rooms[r].label);
			TF_EQ(back.pages[i].rooms[r].enabled,
					config.pages[i].rooms[r].enabled);
			TF_EQ(static_cast<int>(back.pages[i].rooms[r].metric),
					static_cast<int>(config.pages[i].rooms[r].metric));
		}
	}
}

TF_TEST(config_default_config_has_four_pages) {
	const TimerConfig config = ConfigStore::defaultConfig();
	TF_EQ(config.tzOffsetMinutes, 480);
	TF_EQ(config.warnSeconds, 10);
	TF_EQ(config.colorDim, 0x808080u);
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_OFF));
	TF_CHECK(config.targets.empty());
	TF_EQ(static_cast<int>(config.pages.size()), 4);

	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(static_cast<int>(config.pages[0].type),
			static_cast<int>(PAGE_TYPE_CLOCK));
	TF_EQ(config.pages[1].id, std::string("p1"));
	TF_EQ(static_cast<int>(config.pages[1].type),
			static_cast<int>(PAGE_TYPE_CLOCK_COUNTDOWN));
	TF_EQ(config.pages[2].id, std::string("p2"));
	TF_EQ(static_cast<int>(config.pages[2].type),
			static_cast<int>(PAGE_TYPE_COUNTDOWN));
	TF_EQ(config.pages[3].id, std::string("p3"));
	TF_EQ(static_cast<int>(config.pages[3].type),
			static_cast<int>(PAGE_TYPE_LIVE));
	TF_EQ(config.defaultPageId, std::string("p0"));

	TF_EQ(static_cast<int>(config.pages[3].rooms.size()), 2);
	TF_EQ(static_cast<int>(config.pages[3].rooms[0].platform),
			static_cast<int>(LIVE_PLATFORM_BILIBILI));
	TF_EQ(static_cast<int>(config.pages[3].rooms[1].platform),
			static_cast<int>(LIVE_PLATFORM_DOUYIN));
	TF_EQ(config.pages[3].rooms[0].room, std::string(""));
	TF_EQ(config.pages[3].rooms[1].room, std::string(""));
	TF_EQ(config.pages[3].rooms[0].enabled, true);
	TF_EQ(config.pages[3].rooms[1].enabled, true);

	// 默认配置必须是合法的 v3：toJson -> fromJson 往返不丢字段
	TimerConfig back;
	std::string err;
	TF_CHECK_MSG(loadJson(ConfigStore::toJson(config), back, err), err);
	TF_EQ(static_cast<int>(back.pages.size()), 4);
	TF_EQ(back.pages[3].id, std::string("p3"));
	TF_EQ(static_cast<int>(back.pages[3].type),
			static_cast<int>(PAGE_TYPE_LIVE));
	TF_EQ(static_cast<int>(back.pages[3].rooms.size()), 2);
}

TF_TEST(config_save_load_round_trip) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	std::remove(configFilePath().c_str());
	std::remove(configTmpFilePath().c_str());

	TimerConfig config;
	std::string err;
	TF_CHECK_MSG(loadJson(kValidV3, config, err), err);

	TF_CHECK(ConfigStore::save(config));
	TF_CHECK(fileExists(configFilePath()));
	TF_CHECK(!fileExists(configTmpFilePath()));  // tmp renamed away

	const std::string content = readTextFile(configFilePath());
	TF_CHECK(content.find("\"version\": 3") != std::string::npos);
	TF_CHECK(content.find("\"tzOffsetMinutes\": 480") != std::string::npos);
	TF_CHECK(content.find("\"source\": \"bridge\"") != std::string::npos);
	TF_CHECK(!content.empty());
	TF_CHECK(content[content.size() - 1] == '\n');

	TimerConfig back;
	bool migrated = true;
	TF_CHECK(ConfigStore::load(back, 0, &migrated));
	TF_EQ(migrated, false);
	TF_EQ(back.tzOffsetMinutes, 480);
	TF_EQ(back.colorDim, 0x808080u);
	TF_EQ(static_cast<int>(back.pages.size()), 4);
	TF_EQ(back.pages[3].id, std::string("p3"));
	TF_EQ(static_cast<int>(back.pages[3].rooms.size()), 2);
	TF_EQ(back.pages[3].rooms[0].room, std::string("21452505"));
	TF_EQ(static_cast<int>(back.pages[3].rooms[1].metric),
			static_cast<int>(LIVE_METRIC_FANS));
	TF_EQ(static_cast<int>(back.targets.size()), 2);
	TF_EQ(back.defaultPageId, std::string("p1"));

	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_missing_file_returns_default) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	std::remove(configFilePath().c_str());

	TimerConfig config;
	bool migrated = true;
	TF_CHECK(!ConfigStore::load(config, 0, &migrated));
	TF_EQ(migrated, false);
	// 全新安装：默认 4 页（含 p3 live 2 空槽）
	TF_EQ(static_cast<int>(config.pages.size()), 4);
	TF_EQ(config.defaultPageId, std::string("p0"));
	TF_EQ(config.pages[3].id, std::string("p3"));
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_OFF));
}

TF_TEST(config_load_corrupt_file_returns_default) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	std::remove(configFilePath().c_str());

	const char* corrupt[] = {
		"", "{", "{not json}", "[]", "\"text\"", "null",
		"{\"tzOffsetMinutes\":480,",
		"{\"tzOffsetMinutes\":480} trailing",
		R"json({"version":3,"tzOffsetMinutes":480})json",  // 缺 pages
		R"json({"version":3,"tzOffsetMinutes":480,"pages":[]})json",
		R"json({"version":2})json",  // v2 缺 tz
		R"json({"version":2,"tzOffsetMinutes":9999})json"
	};
	for (size_t i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
		TF_CHECK_MSG(writeTextFile(configFilePath(), corrupt[i]),
				std::string("cannot write test file for: ") + corrupt[i]);
		TimerConfig config;
		bool migrated = true;
		TF_CHECK_MSG(!ConfigStore::load(config, 0, &migrated),
				std::string("accepted corrupt config: ") + corrupt[i]);
		TF_EQ(migrated, false);
		TF_EQ(static_cast<int>(config.pages.size()), 4);
	}
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_version4_keeps_file) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const std::string future =
			R"json({"version":4,"tzOffsetMinutes":480,"pages":[{"type":"clock"}]})json";
	TF_CHECK(writeTextFile(configFilePath(), future));

	TimerConfig config;
	bool migrated = true;
	TF_CHECK(!ConfigStore::load(config, 0, &migrated));
	TF_EQ(migrated, false);
	TF_EQ(readTextFile(configFilePath()), future);  // 不覆盖更高版本文件
	TF_EQ(static_cast<int>(config.pages.size()), 4);
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v3_without_version_field) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	// 缺 version 但含 pages：读取时按 v3 接受（POST 仍要求 version:3）
	const std::string text =
			R"json({"tzOffsetMinutes":330,"pages":[{"id":"p9","type":"countdown"}]})json";
	TF_CHECK(writeTextFile(configFilePath(), text));

	TimerConfig config;
	bool migrated = true;
	TF_CHECK(ConfigStore::load(config, 0, &migrated));
	TF_EQ(migrated, false);
	TF_EQ(config.tzOffsetMinutes, 330);
	TF_EQ(static_cast<int>(config.pages.size()), 1);
	TF_EQ(config.pages[0].id, std::string("p9"));
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v2_migrates_to_v3) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const std::string v2 =
			R"json({"version":2,"tzOffsetMinutes":-300,"warnSeconds":25,"ntpServer":"pool.ntp.org","colors":{"clock":"#112233","countdown":"#445566","warning":"#778899"},"targets":[{"date":"2026-10-23","time":"19:25:00","enabled":true},{"date":"2026-10-24","time":"08:00:00","enabled":false}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v2));

	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config, 1792658309LL, &migrated));
	TF_EQ(migrated, true);
	TF_EQ(config.tzOffsetMinutes, -300);
	TF_EQ(config.warnSeconds, 25);
	TF_EQ(config.ntpServer, std::string("pool.ntp.org"));
	TF_EQ(config.colorClock, 0x112233u);
	TF_EQ(config.colorCountdown, 0x445566u);
	TF_EQ(config.colorWarning, 0x778899u);
	TF_EQ(config.colorDim, 0x808080u);  // 补齐
	TF_EQ(static_cast<int>(config.splitBottomWhenIdle),
			static_cast<int>(SPLIT_IDLE_DATE));
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_OFF));
	TF_EQ(config.live.refreshSec, 30);
	TF_EQ(config.live.timeoutMs, 3000);
	TF_EQ(static_cast<int>(config.targets.size()), 2);
	TF_EQ(config.targets[0].day, 23);
	TF_EQ(config.targets[1].enabled, false);

	TF_EQ(static_cast<int>(config.pages.size()), 3);
	TF_EQ(config.pages[0].id, std::string("p0"));
	TF_EQ(static_cast<int>(config.pages[0].type),
			static_cast<int>(PAGE_TYPE_CLOCK));
	TF_EQ(static_cast<int>(config.pages[1].type),
			static_cast<int>(PAGE_TYPE_CLOCK_COUNTDOWN));
	TF_EQ(static_cast<int>(config.pages[2].type),
			static_cast<int>(PAGE_TYPE_COUNTDOWN));
	TF_EQ(config.defaultPageId, std::string("p0"));
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v2_migration_save_is_idempotent) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	TF_CHECK(writeTextFile(configFilePath(), kValidV2));

	TimerConfig migratedConfig;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(migratedConfig, 1792658309LL, &migrated));
	TF_EQ(migrated, true);
	TF_CHECK(ConfigStore::save(migratedConfig));

	const std::string saved = readTextFile(configFilePath());
	TF_CHECK(saved.find("\"version\": 3") != std::string::npos);

	TimerConfig reloaded;
	bool migratedAgain = true;
	TF_CHECK(ConfigStore::load(reloaded, 1792658309LL, &migratedAgain));
	TF_EQ(migratedAgain, false);
	TF_EQ(static_cast<int>(reloaded.pages.size()), 3);
	TF_EQ(static_cast<int>(reloaded.targets.size()), 2);
	TF_EQ(reloaded.colorDim, 0x808080u);
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v2_with_legacy_field_names_still_v2) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	TF_CHECK(writeTextFile(configFilePath(),
			R"json({"version":2,"tzOffsetMinutes":0,"tzOffset":8,"points":[{"time":"09:00"}],"targets":[]})json"));
	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config, 1792658309LL, &migrated));
	TF_EQ(migrated, true);
	TF_EQ(config.tzOffsetMinutes, 0);
	TF_CHECK(config.targets.empty());
	TF_EQ(static_cast<int>(config.pages.size()), 3);
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v2_without_version_field) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	// 缺 version、含 tzOffsetMinutes/targets、无 pages：按 v2 迁移
	const std::string v2 =
			R"json({"tzOffsetMinutes":120,"warnSeconds":5,"targets":[{"date":"2026-10-23","time":"08:00:00","enabled":true}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v2));

	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config, 1792658309LL, &migrated));
	TF_EQ(migrated, true);
	TF_EQ(config.tzOffsetMinutes, 120);
	TF_EQ(config.warnSeconds, 5);
	TF_EQ(static_cast<int>(config.pages.size()), 3);
	TF_EQ(static_cast<int>(config.targets.size()), 1);
	TF_EQ(config.defaultPageId, std::string("p0"));
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v2_invalid_timezone_rejected) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const char* bad[] = {
		R"json({"version":2,"tzOffsetMinutes":"8"})json",
		R"json({"version":2,"tzOffsetMinutes":9999})json",
		R"json({"version":2,"warnSeconds":10})json"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TF_CHECK(writeTextFile(configFilePath(), bad[i]));
		TimerConfig config;
		bool migrated = true;
		TF_CHECK(!ConfigStore::load(config, 1792658309LL, &migrated));
		TF_EQ(migrated, false);
		TF_EQ(static_cast<int>(config.pages.size()), 4);
	}
	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v1_migration_points) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const std::string v1 =
			R"json({"version":1,"tzOffset":8,"warnSeconds":15,"colors":{"countdown":"#00FF00","warning":"#FF2020","countup":"#0080FF"},"points":[{"time":"09:00","enabled":true},{"time":"18:30:00","enabled":false}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v1));

	// nowUtc == 2026-10-22 16:38:29 local at UTC+8 -> local date 2026-10-22.
	const long long nowUtc = epochOf(2026, 10, 22, 16, 38, 29, 480);
	TF_CHECK(nowUtc > 0);

	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config, nowUtc, &migrated));
	TF_EQ(migrated, true);
	TF_EQ(config.tzOffsetMinutes, 480);
	TF_EQ(config.warnSeconds, 15);
	TF_EQ(config.colorCountdown, 0x00FF00u);
	TF_EQ(config.colorWarning, 0xFF2020u);
	TF_EQ(config.colorClock, 0xFFFFFFu);  // v1 has no clock color
	TF_EQ(config.colorDim, 0x808080u);    // 补 v3 dim
	TF_EQ(static_cast<int>(config.live.kind),
			static_cast<int>(LIVE_SOURCE_OFF));
	TF_EQ(static_cast<int>(config.pages.size()), 3);
	TF_EQ(config.defaultPageId, std::string("p0"));
	TF_EQ(static_cast<int>(config.targets.size()), 2);

	TF_EQ(config.targets[0].year, 2026);
	TF_EQ(config.targets[0].month, 10);
	TF_EQ(config.targets[0].day, 22);
	TF_EQ(config.targets[0].hour, 9);
	TF_EQ(config.targets[0].minute, 0);
	TF_EQ(config.targets[0].second, 0);
	TF_EQ(config.targets[0].enabled, true);

	TF_EQ(config.targets[1].year, 2026);
	TF_EQ(config.targets[1].month, 10);
	TF_EQ(config.targets[1].day, 22);
	TF_EQ(config.targets[1].hour, 18);
	TF_EQ(config.targets[1].minute, 30);
	TF_EQ(config.targets[1].second, 0);
	TF_EQ(config.targets[1].enabled, false);

	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v1_migration_uses_migrated_timezone_date) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	// 2026-10-22 01:00 UTC is 2026-10-21 20:00 at UTC-5.
	const long long nowUtc = epochOf(2026, 10, 22, 1, 0, 0, 0);
	const std::string v1 =
			R"json({"tzOffset":-5,"warnSeconds":10,"points":[{"time":"23:30:00"},{"time":"07:05"}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v1));

	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config, nowUtc, &migrated));
	TF_EQ(migrated, true);
	TF_EQ(config.tzOffsetMinutes, -300);
	TF_EQ(static_cast<int>(config.targets.size()), 2);

	TF_EQ(config.targets[0].year, 2026);
	TF_EQ(config.targets[0].month, 10);
	TF_EQ(config.targets[0].day, 21);
	TF_EQ(config.targets[0].hour, 23);
	TF_EQ(config.targets[0].minute, 30);

	TF_EQ(config.targets[1].day, 21);
	TF_EQ(config.targets[1].hour, 7);
	TF_EQ(config.targets[1].minute, 5);

	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v1_without_valid_now_keeps_file) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const std::string v1 =
			R"json({"tzOffset":8,"warnSeconds":10,"points":[{"time":"09:00"}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v1));

	const long long invalidTimes[] = { 0LL, 1LL, 1699999999LL };
	for (size_t i = 0; i < sizeof(invalidTimes) / sizeof(invalidTimes[0]); ++i) {
		TimerConfig config;
		bool migrated = true;
		TF_CHECK(ConfigStore::load(config, invalidTimes[i], &migrated));
		// Not migrated: mainLogic must keep the v1 file untouched.
		TF_EQ(migrated, false);
		TF_EQ(config.tzOffsetMinutes, 480);
		TF_CHECK(config.targets.empty());
		TF_EQ(static_cast<int>(config.pages.size()), 3);
	}
	// load() never writes; the original v1 content is still on disk.
	TF_EQ(readTextFile(configFilePath()), v1);

	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v1_limits_and_invalid_points) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	// Invalid entries are interleaved: they are dropped, and at most eight
	// valid v1 points survive the migration (v1 cap).
	std::string v1 =
			R"json({"tzOffset":0,"points":[{"time":"24:00"},{"time":"9:00"},)json";
	for (int i = 0; i < 10; ++i) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "{\"time\":\"%02d:00\"},", i);
		v1 += buf;
	}
	v1 += R"json({"time":"12:60"},42,{"time":900}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v1));

	const long long nowUtc = epochOf(2026, 10, 22, 12, 0, 0, 0);
	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config, nowUtc, &migrated));
	TF_EQ(migrated, true);
	TF_EQ(config.tzOffsetMinutes, 0);
	TF_EQ(static_cast<int>(config.targets.size()), 8);  // v1 cap
	for (size_t i = 0; i < config.targets.size(); ++i) {
		TF_EQ(config.targets[i].year, 2026);
		TF_EQ(config.targets[i].month, 10);
		TF_EQ(config.targets[i].day, 22);
		TF_EQ(config.targets[i].hour, static_cast<int>(i));
		TF_CHECK(config.targets[i].second == 0);
	}

	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v1_without_points) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	TF_CHECK(writeTextFile(configFilePath(),
			R"json({"tzOffset":5,"warnSeconds":20})json"));

	TimerConfig config;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(config,
			epochOf(2026, 10, 22, 12, 0, 0, 300), &migrated));
	TF_EQ(migrated, true);  // v1 file converted, nothing to date-shift
	TF_EQ(config.tzOffsetMinutes, 300);
	TF_EQ(config.warnSeconds, 20);
	TF_CHECK(config.targets.empty());
	TF_EQ(static_cast<int>(config.pages.size()), 3);

	std::remove(configFilePath().c_str());
}

TF_TEST(config_load_v1_invalid_timezone_rejected) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	TF_CHECK(writeTextFile(configFilePath(),
			R"json({"tzOffset":15,"points":[{"time":"09:00"}]})json"));

	TimerConfig config;
	bool migrated = true;
	TF_CHECK(!ConfigStore::load(config,
			epochOf(2026, 10, 22, 12, 0, 0, 0), &migrated));
	TF_EQ(migrated, false);

	TF_CHECK(writeTextFile(configFilePath(),
			R"json({"tzOffset":"8","points":[]})json"));
	TF_CHECK(!ConfigStore::load(config,
			epochOf(2026, 10, 22, 12, 0, 0, 0), &migrated));

	std::remove(configFilePath().c_str());
}

TF_TEST(config_migrated_v1_round_trips_to_v3) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const std::string v1 =
			R"json({"tzOffset":8,"warnSeconds":10,"points":[{"time":"09:00"},{"time":"18:30"}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v1));

	const long long nowUtc = epochOf(2026, 10, 22, 16, 38, 29, 480);
	TimerConfig migratedConfig;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(migratedConfig, nowUtc, &migrated));
	TF_EQ(migrated, true);

	// mainLogic saves the migrated config once; reload must be plain v3.
	TF_CHECK(ConfigStore::save(migratedConfig));
	TimerConfig reloaded;
	bool migratedAgain = true;
	TF_CHECK(ConfigStore::load(reloaded, nowUtc, &migratedAgain));
	TF_EQ(migratedAgain, false);
	TF_EQ(reloaded.tzOffsetMinutes, 480);
	TF_EQ(static_cast<int>(reloaded.targets.size()), 2);
	TF_EQ(reloaded.targets[0].day, 22);
	TF_EQ(reloaded.targets[0].hour, 9);
	TF_EQ(reloaded.targets[1].hour, 18);
	TF_EQ(reloaded.targets[1].minute, 30);
	TF_EQ(static_cast<int>(reloaded.pages.size()), 3);

	std::remove(configFilePath().c_str());
}

TF_TEST(config_live_hybrid_round_trip) {
	TimerConfig config = ConfigStore::defaultConfig();
	config.live.kind = LIVE_SOURCE_HYBRID;
	config.live.url = "http://192.168.31.10:8081/live";
	config.live.refreshSec = 60;
	config.live.timeoutMs = 2000;

	const std::string json = ConfigStore::toJson(config);
	TF_CHECK(json.find("\"source\": \"hybrid\"") != std::string::npos);
	TF_CHECK(json.find("\"refreshSec\": 60") != std::string::npos);
	TF_CHECK(json.find("\"timeoutMs\": 2000") != std::string::npos);

	TimerConfig back;
	std::string err;
	TF_CHECK_MSG(loadJson(json, back, err), err);
	TF_EQ(static_cast<int>(back.live.kind),
			static_cast<int>(LIVE_SOURCE_HYBRID));
	TF_EQ(back.live.url, std::string("http://192.168.31.10:8081/live"));
	TF_EQ(back.live.refreshSec, 60);
	TF_EQ(back.live.timeoutMs, 2000);
}

TF_TEST(config_live_single_room_round_trip) {
	TimerConfig config;
	std::string err;
	TF_CHECK_MSG(loadJson(v3JsonWithPages("",
			R"json([{"type":"live","rooms":[{"platform":"douyin","room":"88776655","label":"D55","metric":"status"}]}])json"),
			config, err), err);
	TF_EQ(static_cast<int>(config.pages[0].rooms.size()), 1);

	const std::string json = ConfigStore::toJson(config);
	TimerConfig back;
	TF_CHECK_MSG(loadJson(json, back, err), err);
	TF_EQ(static_cast<int>(back.pages[0].rooms.size()), 1);
	TF_EQ(back.pages[0].rooms[0].room, std::string("88776655"));
	TF_EQ(back.pages[0].rooms[0].label, std::string("D55"));
	TF_EQ(static_cast<int>(back.pages[0].rooms[0].metric),
			static_cast<int>(LIVE_METRIC_STATUS));
}

TF_TEST(config_ntp_server_default_and_round_trip) {
	TimerConfig minimal;
	std::string err;
	TF_CHECK_MSG(loadJson(kMinimalV3, minimal, err), err);
	TF_EQ(minimal.ntpServer, std::string(""));

	TimerConfig config = ConfigStore::defaultConfig();
	config.ntpServer = "ntp.example.com:123";
	const std::string json = ConfigStore::toJson(config);
	TF_CHECK_MSG(json.find("\"ntpServer\": \"ntp.example.com:123\"") !=
			std::string::npos, json);

	TimerConfig back;
	TF_CHECK_MSG(loadJson(json, back, err), err);
	TF_EQ(back.ntpServer, std::string("ntp.example.com:123"));
}

TF_TEST(config_ntp_server_accepts_host_and_ip) {
	const char* good[] = {
		"", "ntp.aliyun.com", "192.168.1.2", "pool.ntp.org:123",
		"time.example.com:65535"
	};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
		TimerConfig config;
		std::string err;
		TF_CHECK_MSG(loadJson(v3Json(std::string("\"ntpServer\":\"") +
				good[i] + "\""), config, err),
				std::string("rejected good ntpServer: ") + good[i]);
		TF_EQ(config.ntpServer, std::string(good[i]));
	}
}

TF_TEST(config_ntp_server_rejects_invalid) {
	const char* bad[] = {
		"bad host", "http://ntp", "host:0", "host:65536", "host:12x",
		"host:123456", "host:"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TimerConfig config;
		std::string err;
		TF_CHECK_MSG(!loadJson(v3Json(std::string("\"ntpServer\":\"") +
				bad[i] + "\""), config, err),
				std::string("accepted invalid ntpServer: ") + bad[i]);
	}
	{
		TimerConfig config;
		std::string err;
		const std::string longHost(65, 'a');
		TF_CHECK(!loadJson(v3Json("\"ntpServer\":\"" + longHost + "\""),
				config, err));
	}
	{
		TimerConfig config;
		std::string err;
		TF_CHECK(!loadJson(v3Json("\"ntpServer\":123"), config, err));
		TF_CHECK(err.find("ntpServer") != std::string::npos);
	}
}

TF_TEST(config_ntp_server_v1_migration_empty) {
	if (!kDiskTestsEnabled) {
		return;
	}
	ensureConfigDir();
	const std::string v1 =
			R"json({"tzOffset":8,"points":[{"time":"09:00"}]})json";
	TF_CHECK(writeTextFile(configFilePath(), v1));

	TimerConfig migratedConfig;
	bool migrated = false;
	TF_CHECK(ConfigStore::load(migratedConfig,
			epochOf(2026, 10, 22, 16, 0, 0, 480), &migrated));
	TF_EQ(migrated, true);
	TF_EQ(migratedConfig.ntpServer, std::string(""));
	std::remove(configFilePath().c_str());
}
