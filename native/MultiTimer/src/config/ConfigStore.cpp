#include "config/ConfigStore.h"

#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "timer/TimeMath.h"

namespace {

// 设备正式路径固定为 /data/multitimer.conf。
// 主机单测通过编译宏把配置目录重定向到 tests/out/data，以便在没有 /data 的
// PC 上真正执行 load()/save()/v2 迁移；设备构建不定义该宏，行为与契约一致。
#ifdef MULTITIMER_HOST_CONFIG_DIR
#define MULTITIMER_CONFIG_STRINGIFY_INNER(x) #x
#define MULTITIMER_CONFIG_STRINGIFY(x) MULTITIMER_CONFIG_STRINGIFY_INNER(x)
#define MULTITIMER_CONFIG_DIR \
	MULTITIMER_CONFIG_STRINGIFY(MULTITIMER_HOST_CONFIG_DIR)
#else
#define MULTITIMER_CONFIG_DIR "/data"
#endif

const char* kConfigFileName = "/multitimer.conf";

// 与 v2 一致：now >= 2023-11-14 才认为系统时间有效
const long long kTimeValidEpoch = 1700000000LL;
// v1 points 上限（v2 设计：最多 8 个 HH:MM）
const size_t kMaxLegacyPoints = 8;
const int kWarnSecondsDefault = 10;
const int kTzOffsetMinutesMin = -720;
const int kTzOffsetMinutesMax = 840;
const char* kV2NotAccepted = "v2 config is not accepted, refresh the page";

// 串行化写临时文件，避免并发 POST 保存相互踩踏
std::mutex gSaveMutex;

std::string configPath() {
	return std::string(MULTITIMER_CONFIG_DIR) + kConfigFileName;
}

std::string configTmpPath() {
	return configPath() + ".tmp";
}

bool readFile(const std::string& path, std::string& out) {
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == NULL) {
		return false;
	}
	char buf[512];
	size_t n = 0;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		out.append(buf, n);
	}
	const bool ok = (ferror(fp) == 0);
	fclose(fp);
	return ok;
}

bool isDigitChar(char c) {
	return c >= '0' && c <= '9';
}

bool isAsciiAlphaNum(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z');
}

// 严格校验 HH:MM（v1 points 兼容用）
bool parseHHMM(const std::string& text, int& hour, int& minute) {
	if (text.size() != 5 || text[2] != ':') {
		return false;
	}
	if (!isDigitChar(text[0]) || !isDigitChar(text[1]) ||
			!isDigitChar(text[3]) || !isDigitChar(text[4])) {
		return false;
	}
	const int h = (text[0] - '0') * 10 + (text[1] - '0');
	const int m = (text[3] - '0') * 10 + (text[4] - '0');
	if (h > 23 || m > 59) {
		return false;
	}
	hour = h;
	minute = m;
	return true;
}

// JSON number -> 整数：拒绝小数与超出 long long 的浮点
bool jsonInteger(const JsonValue& value, long long& out) {
	if (!value.isNumber()) {
		return false;
	}
	const double number = value.asDouble();
	if (number < -9.0e18 || number > 9.0e18) {
		return false;
	}
	const long long integer = value.asInt64(0);
	if (static_cast<double>(integer) != number) {
		return false;
	}
	out = integer;
	return true;
}

std::string toHexColor(uint32_t rgb) {
	char buf[8];
	snprintf(buf, sizeof(buf), "#%06X",
			static_cast<unsigned int>(rgb & 0x00FFFFFFU));
	return std::string(buf);
}

std::string intToString(int value) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%d", value);
	return std::string(buf);
}

// JSON 字符串转义：UTF-8 原样透传，仅转义 " \ 与控制字符（v3 新增字符串字段）。
std::string jsonEscape(const std::string& text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		const unsigned char uc = static_cast<unsigned char>(c);
		if (c == '"' || c == '\\') {
			out.push_back('\\');
			out.push_back(c);
		} else if (uc < 0x20) {
			char buf[8];
			snprintf(buf, sizeof(buf), "\\u%04X",
					static_cast<unsigned int>(uc));
			out += buf;
		} else {
			out.push_back(c);
		}
	}
	return out;
}

// 自定义 NTP：空串合法（使用内置列表），否则 host 或 host:port。
bool isValidNtpServer(const std::string& text) {
	if (text.empty()) {
		return true;
	}
	if (text.size() > 64) {
		return false;
	}
	std::string host = text;
	std::string port;
	const size_t colon = text.rfind(':');
	if (colon != std::string::npos) {
		if (text.find(':') != colon) {
			return false;  // 不支持裸 IPv6
		}
		host = text.substr(0, colon);
		port = text.substr(colon + 1);
		if (port.empty() || port.size() > 5) {
			return false;
		}
		int portValue = 0;
		for (size_t i = 0; i < port.size(); ++i) {
			if (port[i] < '0' || port[i] > '9') {
				return false;
			}
			portValue = portValue * 10 + (port[i] - '0');
		}
		if (portValue < 1 || portValue > 65535) {
			return false;
		}
	}
	if (host.empty()) {
		return false;
	}
	for (size_t i = 0; i < host.size(); ++i) {
		const char c = host[i];
		const bool ok = isAsciiAlphaNum(c) || c == '.' || c == '-' || c == '_';
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool isPageIdChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
			c == '-';
}

bool isValidPageId(const std::string& id) {
	if (id.empty() || id.size() > static_cast<size_t>(kMaxPageIdChars)) {
		return false;
	}
	for (size_t i = 0; i < id.size(); ++i) {
		if (!isPageIdChar(id[i])) {
			return false;
		}
	}
	return true;
}

// title <= 16 个 UTF-8 码点；剔除控制字符，截断不拆半个字符。
std::string sanitizeTitle(const std::string& text, size_t maxChars) {
	std::string out;
	out.reserve(text.size());
	size_t chars = 0;
	size_t i = 0;
	while (i < text.size()) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c < 0x20 || c == 0x7F) {
			++i;  // 控制字符丢弃
			continue;
		}
		size_t len = 1;
		if ((c & 0xE0) == 0xC0) {
			len = 2;
		} else if ((c & 0xF0) == 0xE0) {
			len = 3;
		} else if ((c & 0xF8) == 0xF0) {
			len = 4;
		} else if ((c & 0xC0) == 0x80) {
			++i;  // 孤立的续字节：丢弃
			continue;
		}
		if (i + len > text.size()) {
			break;
		}
		bool valid = true;
		for (size_t k = 1; k < len; ++k) {
			if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
				valid = false;
				break;
			}
		}
		if (!valid) {
			++i;
			continue;
		}
		if (chars >= maxChars) {
			break;
		}
		out.append(text, i, len);
		++chars;
		i += len;
	}
	return out;
}

// shortTitle：只保留可打印 ASCII 0x20..0x7E，最多 8 字符；保留大小写。
std::string sanitizeShortTitle(const std::string& text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size() &&
			out.size() < static_cast<size_t>(kMaxShortTitleChars); ++i) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c >= 0x20 && c <= 0x7E) {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

// room：空串（空槽）或 1..40 个 [A-Za-z0-9_-]（bridge URL 不编码，契约 §5）
bool isValidRoom(const std::string& room) {
	if (room.empty()) {
		return true;
	}
	if (room.size() > static_cast<size_t>(kMaxRoomChars)) {
		return false;
	}
	for (size_t i = 0; i < room.size(); ++i) {
		const char c = room[i];
		if (!isAsciiAlphaNum(c) && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

char upperAscii(char c) {
	if (c >= 'a' && c <= 'z') {
		return static_cast<char>(c - 'a' + 'A');
	}
	return c;
}

// label：转大写、只保留 [A-Z0-9._-]、最多 3 字符；空表示自动生成。
std::string normalizeLabel(const std::string& text) {
	std::string out;
	out.reserve(kMaxRoomLabelChars);
	for (size_t i = 0; i < text.size() &&
			out.size() < static_cast<size_t>(kMaxRoomLabelChars); ++i) {
		const char c = upperAscii(text[i]);
		const bool ok = (c >= 'A' && c <= 'Z') || isDigitChar(c) || c == '.' ||
				c == '_' || c == '-';
		if (ok) {
			out.push_back(c);
		}
	}
	return out;
}

// live.url：空串合法（抖音/快手显示 --）；非空只接受明文 http://，<=160，无空格。
bool isValidLiveUrl(const std::string& url) {
	if (url.empty()) {
		return true;
	}
	if (url.size() > static_cast<size_t>(kMaxLiveUrlChars)) {
		return false;
	}
	if (url.compare(0, 7, "http://") != 0 || url.size() <= 7) {
		return false;
	}
	for (size_t i = 0; i < url.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(url[i]);
		if (c <= 0x20 || c == 0x7F) {
			return false;
		}
	}
	return true;
}

LiveRoomConfig makeEmptyRoomSlot() {
	LiveRoomConfig room;
	room.platform = LIVE_PLATFORM_BILIBILI;
	room.enabled = false;
	room.metric = LIVE_METRIC_INHERIT;
	return room;
}

PageConfig makePage(const char* id, PageType type, const char* title,
		const char* shortTitle) {
	PageConfig page;
	page.id = id;
	page.type = type;
	page.title = title;
	page.shortTitle = shortTitle;
	page.enabled = true;
	page.metric = LIVE_METRIC_ONLINE;
	return page;
}

// v1/v2 迁移生成的 3 页（契约 §2：不自动创建 live 页）
void appendMigrationPages(TimerConfig& config) {
	config.pages.clear();
	config.pages.push_back(makePage("p0", PAGE_TYPE_CLOCK, "时钟", "CLOCK"));
	config.pages.push_back(
			makePage("p1", PAGE_TYPE_CLOCK_COUNTDOWN, "时钟+倒计时", "CLOCK+CD"));
	// shortTitle 留空：运行时用类型短名 "COUNTDOWN"（9 字符 > 8 的上限，
	// 按契约“空则 type 名”显示，不写入配置，保证 JSON 往返稳定）。
	config.pages.push_back(makePage("p2", PAGE_TYPE_COUNTDOWN, "倒计时", ""));
	config.defaultPageId = "p0";
}

bool containsString(const std::vector<std::string>& values,
		const std::string& value) {
	return std::find(values.begin(), values.end(), value) != values.end();
}

std::string generatePageId(int index, const std::vector<std::string>& used) {
	char buf[16];
	snprintf(buf, sizeof(buf), "p%d", index);
	const std::string base(buf);
	if (!containsString(used, base)) {
		return base;
	}
	for (int suffix = 2; suffix < 1000; ++suffix) {
		snprintf(buf, sizeof(buf), "p%d_%d", index, suffix);
		if (!containsString(used, buf)) {
			return std::string(buf);
		}
	}
	return base;
}

bool parseColors(const JsonValue& root, TimerConfig& config,
		std::string& err) {
	if (!root.contains("colors")) {
		return true;
	}
	const JsonValue& colors = root["colors"];
	if (!colors.isObject()) {
		err = "colors must be an object";
		return false;
	}
	struct ColorField {
		const char* key;
		uint32_t* value;
	};
	const ColorField fields[5] = {
		{ "clock", &config.colorClock },
		{ "countdown", &config.colorCountdown },
		{ "countup", &config.colorCountup },
		{ "warning", &config.colorWarning },
		{ "dim", &config.colorDim },
	};
	for (size_t i = 0; i < 5; ++i) {
		if (!colors.contains(fields[i].key)) {
			continue;
		}
		const JsonValue& item = colors[fields[i].key];
		if (!item.isString() ||
				!ConfigStore::parseColor(item.asString(), *fields[i].value)) {
			err = std::string("invalid color: ") + fields[i].key;
			return false;
		}
	}
	return true;
}

// display 缺省/非法一律回默认（契约 §2 只约束两个枚举值）
void parseDisplayLenient(const JsonValue& root, TimerConfig& config) {
	if (!root.contains("display") || !root["display"].isObject()) {
		return;
	}
	const JsonValue& display = root["display"];
	if (!display.contains("splitBottomWhenIdle") ||
			!display["splitBottomWhenIdle"].isString()) {
		return;
	}
	SplitIdle value;
	if (parseSplitIdle(display["splitBottomWhenIdle"].asString(), value)) {
		config.splitBottomWhenIdle = value;
	}
}

bool parseTargets(const JsonValue& root, TimerConfig& config,
		std::string& err) {
	if (!root.contains("targets")) {
		return true;
	}
	const JsonValue& targets = root["targets"];
	if (!targets.isArray()) {
		err = "targets must be an array";
		return false;
	}
	if (targets.size() > static_cast<size_t>(ConfigStore::kMaxTargets)) {
		err = "too many targets (max 12)";
		return false;
	}
	for (size_t i = 0; i < targets.size(); ++i) {
		const JsonValue& item = targets.at(i);
		if (!item.isObject()) {
			continue;
		}
		const JsonValue& dateValue = item["date"];
		const JsonValue& timeValue = item["time"];
		if (!dateValue.isString() || !timeValue.isString()) {
			continue;
		}
		DateTarget target;
		if (!parseDate(dateValue.asString(), target.year, target.month,
					target.day)) {
			continue;
		}
		if (!parseTimeHMS(timeValue.asString(), target.hour, target.minute,
					target.second)) {
			continue;
		}
		if (item.contains("enabled")) {
			if (!item["enabled"].isBool()) {
				continue;  // enabled 必须是布尔，否则整体丢弃该项
			}
			target.enabled = item["enabled"].asBool(true);
		}
		config.targets.push_back(target);
	}
	return true;
}

bool parseLive(const JsonValue& root, TimerConfig& config, std::string& err) {
	if (!root.contains("live")) {
		return true;
	}
	const JsonValue& live = root["live"];
	if (!live.isObject()) {
		err = "live must be an object";
		return false;
	}
	LiveSourceConfig source;
	if (live.contains("source")) {
		if (!live["source"].isString()) {
			err = "invalid live.source";
			return false;
		}
		if (!parseLiveSource(live["source"].asString(), source.kind)) {
			err = "invalid live.source (off/bridge/hybrid/push)";
			return false;
		}
	}
	if (live.contains("url")) {
		if (!live["url"].isString()) {
			err = "live.url must be a string";
			return false;
		}
		source.url = live["url"].asString();
	}
	if (!isValidLiveUrl(source.url)) {
		err = "invalid live.url (http:// only, max 160, no spaces)";
		return false;
	}
	if (live.contains("refreshSec")) {
		long long value = 0;
		if (!jsonInteger(live["refreshSec"], value)) {
			err = "invalid live.refreshSec";
			return false;
		}
		if (value < kMinLiveRefreshSec || value > kMaxLiveRefreshSec) {
			err = "live.refreshSec out of range (15..300)";
			return false;
		}
		source.refreshSec = static_cast<int>(value);
	}
	if (live.contains("timeoutMs")) {
		long long value = 0;
		if (!jsonInteger(live["timeoutMs"], value)) {
			err = "invalid live.timeoutMs";
			return false;
		}
		if (value < kMinLiveTimeoutMs || value > kMaxLiveTimeoutMs) {
			err = "live.timeoutMs out of range (1000..5000)";
			return false;
		}
		source.timeoutMs = static_cast<int>(value);
	}
	config.live = source;
	return true;
}

// 房间条目：非法/重复 -> 空槽（该行留黑，不报错），保持 rooms 数组长度不变。
LiveRoomConfig parseRoomItem(const JsonValue& item) {
	if (!item.isObject()) {
		return makeEmptyRoomSlot();
	}
	LivePlatform platform = LIVE_PLATFORM_BILIBILI;
	const JsonValue& platformValue = item["platform"];
	if (!platformValue.isString() ||
			!parseLivePlatform(platformValue.asString(), platform)) {
		return makeEmptyRoomSlot();
	}
	const JsonValue& roomValue = item["room"];
	if (!roomValue.isString()) {
		return makeEmptyRoomSlot();
	}
	const std::string roomText = roomValue.asString();
	if (!isValidRoom(roomText)) {
		return makeEmptyRoomSlot();
	}

	LiveRoomConfig room;
	room.platform = platform;
	room.room = roomText;
	if (item.contains("label") && item["label"].isString()) {
		room.label = normalizeLabel(item["label"].asString());
	}
	if (item.contains("enabled") && item["enabled"].isBool()) {
		room.enabled = item["enabled"].asBool(true);
	}
	if (item.contains("metric") && item["metric"].isString()) {
		LiveMetric metric;
		if (parseLiveMetric(item["metric"].asString(), metric)) {
			room.metric = metric;
		}
	}
	return room;
}

bool parsePages(const JsonValue& root, TimerConfig& config,
		std::string& err) {
	if (!root.contains("pages")) {
		err = "missing pages";
		return false;
	}
	const JsonValue& pages = root["pages"];
	if (!pages.isArray()) {
		err = "pages must be an array";
		return false;
	}
	if (pages.size() < 1) {
		err = "pages must have at least 1 item";
		return false;
	}
	if (pages.size() > static_cast<size_t>(ConfigStore::kMaxPages)) {
		err = "too many pages (max 12)";
		return false;
	}

	std::vector<std::string> usedIds;
	config.pages.clear();
	for (size_t i = 0; i < pages.size(); ++i) {
		const JsonValue& item = pages.at(i);
		if (!item.isObject()) {
			err = "page must be an object";
			return false;
		}
		PageConfig page;
		const JsonValue& typeValue = item["type"];
		if (!typeValue.isString()) {
			err = "missing page type";
			return false;
		}
		if (!parsePageType(typeValue.asString(), page.type)) {
			err = "unknown page type: " + typeValue.asString();
			return false;
		}

		if (item.contains("id") && item["id"].isString()) {
			const std::string id = item["id"].asString();
			if (isValidPageId(id)) {
				page.id = id;
			}
		}
		if (page.id.empty() || containsString(usedIds, page.id)) {
			page.id = generatePageId(static_cast<int>(i), usedIds);
		}
		usedIds.push_back(page.id);

		if (item.contains("title") && item["title"].isString()) {
			page.title = sanitizeTitle(item["title"].asString(),
					static_cast<size_t>(kMaxPageTitleChars));
		}
		if (item.contains("shortTitle") && item["shortTitle"].isString()) {
			page.shortTitle =
					sanitizeShortTitle(item["shortTitle"].asString());
		}
		if (item.contains("enabled") && item["enabled"].isBool()) {
			page.enabled = item["enabled"].asBool(true);
		}
		if (item.contains("metric") && item["metric"].isString()) {
			LiveMetric metric;
			if (parseLiveMetric(item["metric"].asString(), metric) &&
					metric != LIVE_METRIC_INHERIT) {
				page.metric = metric;
			}
		}

		if (page.type == PAGE_TYPE_LIVE) {
			if (!item.contains("rooms") || !item["rooms"].isArray()) {
				err = "live page requires rooms";
				return false;
			}
			const JsonValue& rooms = item["rooms"];
			if (rooms.size() < 1 ||
					rooms.size() > static_cast<size_t>(
							ConfigStore::kMaxRoomsPerLivePage)) {
				err = "live page requires 1..2 rooms";
				return false;
			}
			std::vector<std::string> seenRooms;
			for (size_t r = 0; r < rooms.size(); ++r) {
				LiveRoomConfig room = parseRoomItem(rooms.at(r));
				if (!room.room.empty()) {
					const std::string key =
							std::string(livePlatformId(room.platform)) + ":" +
							room.room;
					if (containsString(seenRooms, key)) {
						room = makeEmptyRoomSlot();  // 同页重复房间：后者丢弃
					} else {
						seenRooms.push_back(key);
					}
				}
				page.rooms.push_back(room);
			}
		}

		config.pages.push_back(page);
	}

	// defaultPageId 必须引用存在页面；非法/缺失回退第一页（契约 §2）
	std::string requested;
	if (root.contains("defaultPageId") &&
			root["defaultPageId"].isString()) {
		requested = root["defaultPageId"].asString();
	}
	bool matched = false;
	for (size_t i = 0; i < config.pages.size(); ++i) {
		if (config.pages[i].id == requested) {
			matched = true;
			break;
		}
	}
	config.defaultPageId =
			(matched && !requested.empty()) ? requested : config.pages[0].id;
	return true;
}

// 严格 v3 解析（POST 用 requireVersion=true；load 允许缺 version 但含 pages）
bool parseV3(const JsonValue& root, TimerConfig& out, std::string& err,
		bool requireVersion) {
	if (!root.isObject()) {
		err = "config must be a JSON object";
		return false;
	}

	if (root.contains("version")) {
		long long version = 0;
		if (!jsonInteger(root["version"], version)) {
			err = "version must be 3";
			return false;
		}
		if (version == 2) {
			err = kV2NotAccepted;
			return false;
		}
		if (version != 3) {
			err = "version must be 3";
			return false;
		}
	} else if (requireVersion) {
		err = "version must be 3";
		return false;
	}

	TimerConfig config;

	// tzOffsetMinutes：必填，整数 -720..840
	if (!root.contains("tzOffsetMinutes")) {
		err = "missing tzOffsetMinutes";
		return false;
	}
	long long tzMinutes = 0;
	if (!jsonInteger(root["tzOffsetMinutes"], tzMinutes)) {
		err = "invalid tzOffsetMinutes";
		return false;
	}
	if (tzMinutes < kTzOffsetMinutesMin || tzMinutes > kTzOffsetMinutesMax) {
		err = "tzOffsetMinutes out of range (-720..840)";
		return false;
	}
	config.tzOffsetMinutes = static_cast<int>(tzMinutes);

	// warnSeconds：缺省 10，整数 0..600
	if (root.contains("warnSeconds")) {
		long long warn = 0;
		if (!jsonInteger(root["warnSeconds"], warn)) {
			err = "invalid warnSeconds";
			return false;
		}
		if (warn < 0 || warn > 600) {
			err = "warnSeconds out of range (0..600)";
			return false;
		}
		config.warnSeconds = static_cast<int>(warn);
	} else {
		config.warnSeconds = kWarnSecondsDefault;
	}

	// ntpServer：可缺省；空串表示使用内置服务器列表
	if (root.contains("ntpServer")) {
		if (!root["ntpServer"].isString()) {
			err = "ntpServer must be a string";
			return false;
		}
		const std::string ntpServer = root["ntpServer"].asString();
		if (!isValidNtpServer(ntpServer)) {
			err = "invalid ntpServer (host or host:port, max 64 chars)";
			return false;
		}
		config.ntpServer = ntpServer;
	}

	if (!parseColors(root, config, err)) {
		return false;
	}
	parseDisplayLenient(root, config);
	if (!parseTargets(root, config, err)) {
		return false;
	}
	if (!parseLive(root, config, err)) {
		return false;
	}
	if (!parsePages(root, config, err)) {
		return false;
	}

	out = config;
	return true;
}

// v1 -> v3：先按 v2 规则迁移（tzOffset 小时 -> 分钟；points -> 当天目标），
// 再补 v3 字段与 3 个默认页面。
bool migrateV1(const JsonValue& root, long long nowUtc, TimerConfig& out) {
	TimerConfig cfg;

	// 时区：v1 为小时（-12..14）；缺省按 UTC，出现但非法则视为损坏
	long long tzHours = 0;
	if (root.contains("tzOffset")) {
		if (!jsonInteger(root["tzOffset"], tzHours) || tzHours < -12 ||
				tzHours > 14) {
			return false;
		}
	}
	cfg.tzOffsetMinutes = static_cast<int>(tzHours) * 60;

	// 预警：迁移时宽容，缺失/非法都用默认 10
	if (root.contains("warnSeconds")) {
		long long warn = 0;
		if (jsonInteger(root["warnSeconds"], warn) && warn >= 0 &&
				warn <= 600) {
			cfg.warnSeconds = static_cast<int>(warn);
		}
	}

	// 颜色：v1 没有 clock/dim；countdown/warning 合法则沿用，否则用默认
	if (root.contains("colors") && root["colors"].isObject()) {
		const JsonValue& colors = root["colors"];
		uint32_t rgb = 0;
		if (colors.contains("countdown") && colors["countdown"].isString() &&
				ConfigStore::parseColor(colors["countdown"].asString(), rgb)) {
			cfg.colorCountdown = rgb;
		}
		if (colors.contains("countup") && colors["countup"].isString() &&
				ConfigStore::parseColor(colors["countup"].asString(), rgb)) {
			cfg.colorCountup = rgb;
		}
		if (colors.contains("warning") && colors["warning"].isString() &&
				ConfigStore::parseColor(colors["warning"].asString(), rgb)) {
			cfg.colorWarning = rgb;
		}
	}

	// 无法确定“当前本地日期”：不迁移 points（目标为空，调用方保留原文件）
	if (nowUtc < kTimeValidEpoch) {
		out = cfg;
		return true;
	}

	LocalDateTime localNow;
	if (!epochToLocalDateTime(nowUtc, cfg.tzOffsetMinutes, localNow)) {
		out = cfg;
		return true;
	}

	if (!root.contains("points")) {
		out = cfg;
		return true;
	}
	const JsonValue& points = root["points"];
	if (!points.isArray()) {
		out = cfg;
		return true;
	}

	for (size_t i = 0; i < points.size() &&
			cfg.targets.size() < kMaxLegacyPoints; ++i) {
		const JsonValue& item = points.at(i);
		if (!item.isObject()) {
			continue;  // 非法单项丢弃
		}
		const JsonValue& timeValue = item["time"];
		if (!timeValue.isString()) {
			continue;
		}
		const std::string text = timeValue.asString();
		int hour = 0;
		int minute = 0;
		int second = 0;
		if (text.size() == 8) {
			if (!parseTimeHMS(text, hour, minute, second)) {
				continue;
			}
		} else if (text.size() == 5) {
			if (!parseHHMM(text, hour, minute)) {
				continue;
			}
		} else {
			continue;
		}

		DateTarget target;
		target.year = localNow.year;  // 当前本地日期（不推断“明天”）
		target.month = localNow.month;
		target.day = localNow.day;
		target.hour = hour;
		target.minute = minute;
		target.second = second;
		// v1 的 enabled 缺省 true；非布尔值与旧实现一致按 true 处理
		if (item.contains("enabled") && item["enabled"].isBool()) {
			target.enabled = item["enabled"].asBool(true);
		}
		cfg.targets.push_back(target);
	}

	out = cfg;
	return true;
}

// v2 -> v3：保留 tz/warn/colors/targets/ntpServer，补 dim/display/live 与 3 页。
// 宽容处理装饰性字段（颜色/预警/NTP 非法时取默认），但 tz/targets 仍严格。
bool migrateV2(const JsonValue& root, TimerConfig& out, std::string& err) {
	TimerConfig cfg;

	if (!root.contains("tzOffsetMinutes")) {
		err = "missing tzOffsetMinutes";
		return false;
	}
	long long tzMinutes = 0;
	if (!jsonInteger(root["tzOffsetMinutes"], tzMinutes)) {
		err = "invalid tzOffsetMinutes";
		return false;
	}
	if (tzMinutes < kTzOffsetMinutesMin || tzMinutes > kTzOffsetMinutesMax) {
		err = "tzOffsetMinutes out of range (-720..840)";
		return false;
	}
	cfg.tzOffsetMinutes = static_cast<int>(tzMinutes);

	if (root.contains("warnSeconds")) {
		long long warn = 0;
		if (jsonInteger(root["warnSeconds"], warn) && warn >= 0 &&
				warn <= 600) {
			cfg.warnSeconds = static_cast<int>(warn);
		}
	}
	if (root.contains("ntpServer") && root["ntpServer"].isString()) {
		const std::string server = root["ntpServer"].asString();
		if (isValidNtpServer(server)) {
			cfg.ntpServer = server;
		}
	}
	if (root.contains("colors") && root["colors"].isObject()) {
		const JsonValue& colors = root["colors"];
		uint32_t rgb = 0;
		if (colors.contains("clock") && colors["clock"].isString() &&
				ConfigStore::parseColor(colors["clock"].asString(), rgb)) {
			cfg.colorClock = rgb;
		}
		if (colors.contains("countdown") && colors["countdown"].isString() &&
				ConfigStore::parseColor(colors["countdown"].asString(), rgb)) {
			cfg.colorCountdown = rgb;
		}
		if (colors.contains("countup") && colors["countup"].isString() &&
				ConfigStore::parseColor(colors["countup"].asString(), rgb)) {
			cfg.colorCountup = rgb;
		}
		if (colors.contains("warning") && colors["warning"].isString() &&
				ConfigStore::parseColor(colors["warning"].asString(), rgb)) {
			cfg.colorWarning = rgb;
		}
		if (colors.contains("dim") && colors["dim"].isString() &&
				ConfigStore::parseColor(colors["dim"].asString(), rgb)) {
			cfg.colorDim = rgb;
		}
	}
	if (!parseTargets(root, cfg, err)) {
		return false;
	}

	appendMigrationPages(cfg);
	out = cfg;
	return true;
}

bool loadV1(const JsonValue& root, long long nowUtc, TimerConfig& out,
		bool* migrated) {
	TimerConfig cfg;
	if (!migrateV1(root, nowUtc, cfg)) {
		return false;
	}
	appendMigrationPages(cfg);
	out = cfg;
	// 只有真正把 points 折算成带日期目标（nowUtc 有效）才算迁移完成；
	// nowUtc 无效时保留原文件，等系统时间恢复后再迁移。
	if (nowUtc >= kTimeValidEpoch && migrated != NULL) {
		*migrated = true;
	}
	return true;
}

LocalDateTime dateTimeOf(const DateTarget& target, int hour, int minute,
		int second) {
	LocalDateTime value;
	value.year = target.year;
	value.month = target.month;
	value.day = target.day;
	value.hour = hour;
	value.minute = minute;
	value.second = second;
	return value;
}

void appendTargetsJson(const TimerConfig& config, std::string& out) {
	if (config.targets.empty()) {
		out += "  \"targets\": [],\n";
		return;
	}
	out += "  \"targets\": [\n";
	for (size_t i = 0; i < config.targets.size(); ++i) {
		const DateTarget& target = config.targets[i];
		out += "    {\n";
		out += "      \"date\": \"" +
				formatDate(dateTimeOf(target, 0, 0, 0)) + "\",\n";
		out += "      \"time\": \"" +
				formatTimeHMS(dateTimeOf(target, target.hour, target.minute,
						target.second)) +
				"\",\n";
		out += "      \"enabled\": ";
		out += target.enabled ? "true" : "false";
		out += "\n";
		out += "    }";
		if (i + 1 < config.targets.size()) {
			out += ",";
		}
		out += "\n";
	}
	out += "  ],\n";
}

void appendPagesJson(const TimerConfig& config, std::string& out) {
	out += "  \"pages\": [\n";
	for (size_t i = 0; i < config.pages.size(); ++i) {
		const PageConfig& page = config.pages[i];
		out += "    {\n";
		out += "      \"id\": \"" + jsonEscape(page.id) + "\",\n";
		out += "      \"type\": \"" + std::string(pageTypeId(page.type)) +
				"\",\n";
		out += "      \"title\": \"" + jsonEscape(page.title) + "\",\n";
		out += "      \"shortTitle\": \"" + jsonEscape(page.shortTitle) +
				"\",\n";
		out += "      \"enabled\": ";
		out += page.enabled ? "true" : "false";
		if (page.type == PAGE_TYPE_LIVE) {
			out += ",\n";
			out += "      \"metric\": \"" +
					std::string(liveMetricId(page.metric)) + "\",\n";
			out += "      \"rooms\": [\n";
			for (size_t r = 0; r < page.rooms.size(); ++r) {
				const LiveRoomConfig& room = page.rooms[r];
				out += "        {\n";
				out += "          \"platform\": \"" +
						std::string(livePlatformId(room.platform)) + "\",\n";
				out += "          \"room\": \"" + jsonEscape(room.room) +
						"\",\n";
				out += "          \"label\": \"" + jsonEscape(room.label) +
						"\",\n";
				out += "          \"enabled\": ";
				out += room.enabled ? "true" : "false";
				out += ",\n";
				out += "          \"metric\": \"" +
						std::string(liveMetricId(room.metric)) + "\"\n";
				out += "        }";
				if (r + 1 < page.rooms.size()) {
					out += ",";
				}
				out += "\n";
			}
			out += "      ]\n";
		} else {
			out += "\n";
		}
		out += "    }";
		if (i + 1 < config.pages.size()) {
			out += ",";
		}
		out += "\n";
	}
	out += "  ]\n";
}

}  // namespace

// static
bool ConfigStore::parseColor(const std::string& text, uint32_t& rgb) {
	if (text.size() != 7 || text[0] != '#') {
		return false;
	}
	uint32_t v = 0;
	for (int i = 1; i < 7; ++i) {
		const char c = text[static_cast<size_t>(i)];
		int d = 0;
		if (c >= '0' && c <= '9') {
			d = c - '0';
		} else if (c >= 'a' && c <= 'f') {
			d = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			d = c - 'A' + 10;
		} else {
			return false;
		}
		v = (v << 4) | static_cast<uint32_t>(d);
	}
	rgb = v;
	return true;
}

// static
TimerConfig ConfigStore::defaultConfig() {
	TimerConfig config;  // tz=480 / warn=10 / 默认四色 / dim=#808080 / live=off
	appendMigrationPages(config);
	// 首次全新安装追加 p3 live（2 个空槽，source=off；契约 §2）
	PageConfig live = makePage("p3", PAGE_TYPE_LIVE, "直播", "LIVE");
	LiveRoomConfig room;
	room.platform = LIVE_PLATFORM_BILIBILI;
	room.enabled = true;
	room.metric = LIVE_METRIC_INHERIT;
	live.rooms.push_back(room);
	room.platform = LIVE_PLATFORM_DOUYIN;
	live.rooms.push_back(room);
	config.pages.push_back(live);
	config.defaultPageId = "p0";
	return config;
}

// static
bool ConfigStore::fromJson(const JsonValue& root, TimerConfig& out,
		std::string& err) {
	return parseV3(root, out, err, true);
}

// static
std::string ConfigStore::toJson(const TimerConfig& config) {
	std::string out;
	out.reserve(4096);
	out += "{\n";
	out += "  \"version\": 3,\n";
	out += "  \"tzOffsetMinutes\": " + intToString(config.tzOffsetMinutes) +
			",\n";
	out += "  \"warnSeconds\": " + intToString(config.warnSeconds) + ",\n";
	out += "  \"ntpServer\": \"" + jsonEscape(config.ntpServer) + "\",\n";
	out += "  \"colors\": {\n";
	out += "    \"clock\": \"" + toHexColor(config.colorClock) + "\",\n";
	out += "    \"countdown\": \"" + toHexColor(config.colorCountdown) +
			"\",\n";
	out += "    \"countup\": \"" + toHexColor(config.colorCountup) + "\",\n";
	out += "    \"warning\": \"" + toHexColor(config.colorWarning) + "\",\n";
	out += "    \"dim\": \"" + toHexColor(config.colorDim) + "\"\n";
	out += "  },\n";
	out += "  \"display\": {\n";
	out += "    \"splitBottomWhenIdle\": \"" +
			std::string(splitIdleId(config.splitBottomWhenIdle)) + "\"\n";
	out += "  },\n";
	appendTargetsJson(config, out);
	out += "  \"live\": {\n";
	out += "    \"source\": \"" +
			std::string(liveSourceId(config.live.kind)) + "\",\n";
	out += "    \"url\": \"" + jsonEscape(config.live.url) + "\",\n";
	out += "    \"refreshSec\": " + intToString(config.live.refreshSec) +
			",\n";
	out += "    \"timeoutMs\": " + intToString(config.live.timeoutMs) + "\n";
	out += "  },\n";
	out += "  \"defaultPageId\": \"" + jsonEscape(config.defaultPageId) +
			"\",\n";
	appendPagesJson(config, out);
	out += "}";
	return out;
}

// static
bool ConfigStore::load(TimerConfig& out, long long nowUtc, bool* migrated) {
	if (migrated != NULL) {
		*migrated = false;
	}
	// 无论文件是否存在，调用方总能拿到一份可用的默认配置（全新安装 4 页）。
	out = defaultConfig();

	std::string content;
	if (!readFile(configPath(), content)) {
		return false;  // 文件缺失
	}

	JsonValue root;
	std::string parseErr;
	if (!JsonValue::parse(content, root, parseErr)) {
		return false;  // 损坏的 JSON
	}
	if (!root.isObject()) {
		return false;
	}

	const bool hasVersion = root.contains("version");
	long long version = 0;
	if (hasVersion && !jsonInteger(root["version"], version)) {
		return false;
	}

	if (hasVersion) {
		if (version == 3) {
			TimerConfig config;
			if (!parseV3(root, config, parseErr, false)) {
				return false;
			}
			out = config;
			return true;
		}
		if (version == 2) {
			TimerConfig config;
			if (!migrateV2(root, config, parseErr)) {
				return false;
			}
			out = config;
			if (migrated != NULL) {
				*migrated = true;
			}
			return true;
		}
		if (version == 1) {
			return loadV1(root, nowUtc, out, migrated);
		}
		return false;  // version 0 / >=4 / 负数：保留原文件，RAM 用默认
	}

	// 无 version：按字段判定（契约 §2 迁移判定）
	if (root.contains("pages")) {
		TimerConfig config;
		if (!parseV3(root, config, parseErr, false)) {
			return false;
		}
		out = config;
		return true;
	}
	if (root.contains("tzOffsetMinutes") || root.contains("targets")) {
		TimerConfig config;
		if (!migrateV2(root, config, parseErr)) {
			return false;
		}
		out = config;
		if (migrated != NULL) {
			*migrated = true;
		}
		return true;
	}
	if (root.contains("tzOffset") || root.contains("points")) {
		return loadV1(root, nowUtc, out, migrated);
	}
	return false;
}

// static
bool ConfigStore::save(const TimerConfig& config) {
	std::lock_guard<std::mutex> lock(gSaveMutex);

	const std::string path = configPath();
	const std::string tmpPath = configTmpPath();
	std::string data = toJson(config);
	data.push_back('\n');

	FILE* fp = fopen(tmpPath.c_str(), "wb");
	if (fp == NULL) {
		return false;
	}
	const size_t written = fwrite(data.data(), 1, data.size(), fp);
	bool ok = (written == data.size());
	if (fflush(fp) != 0) {
		ok = false;
	}
	// 断电前把数据真正刷到 flash，避免只写进页缓存
	if (ok && fsync(fileno(fp)) != 0) {
		ok = false;
	}
	if (fclose(fp) != 0) {
		ok = false;
	}
	if (!ok) {
		remove(tmpPath.c_str());
		return false;
	}
	if (rename(tmpPath.c_str(), path.c_str()) != 0) {
		// 设备 libc 的 rename() 在同目录内是原子替换，正常情况一次成功。
		// 个别 libc（如 MSVC CRT，仅主机单测）不允许覆盖已存在文件，
		// 这里退化为“删除旧文件后重试”，不改变设备路径的行为。
		if (remove(path.c_str()) == 0 &&
				rename(tmpPath.c_str(), path.c_str()) == 0) {
			return true;
		}
		remove(tmpPath.c_str());
		return false;
	}
	return true;
}
