#include "net/HttpServer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "config/ConfigStore.h"
#include "config/WifiStore.h"
#include "live/LiveService.h"
#include "managers/PageManager.h"
#include "net/ConfigMode.h"
#include "net/mini_json.h"
#include "net/WifiManager_task.h"
#include "pages/TimerPage.h"
#include "time/TimeKeeper.h"
#include "timer/TimeMath.h"
#include "web/editor_gz.h"
#include "base/log.h"

namespace {

const int kHeaderMaxBytes = 2048;
const int kBodyMaxBytes = 16384;      // v3：请求体上限 16 KB（契约 §6）
const int kConfigMaxBytes = 12288;    // v3：规范化配置上限 12 KB
const int kRecvTimeoutSec = 3;
const int kListenBacklog = 4;
const int kMaxActiveClients = 8;

HttpServer::ApplyCallback gApplyCallback;
std::atomic<int> gActiveClients(0);

struct HttpRequest {
	std::string method;
	std::string path;
	std::map<std::string, std::string> headers;
	std::string body;
};

std::string lowerCopy(const std::string& s) {
	std::string out = s;
	for (size_t i = 0; i < out.size(); ++i) {
		if (out[i] >= 'A' && out[i] <= 'Z') {
			out[i] = static_cast<char>(out[i] - 'A' + 'a');
		}
	}
	return out;
}

std::string trimToken(const std::string& s) {
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) {
		++b;
	}
	size_t e = s.size();
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) {
		--e;
	}
	return s.substr(b, e - b);
}

bool sendRaw(int fd, const void* data, size_t len) {
	const char* p = static_cast<const char*>(data);
	size_t left = len;
	while (left > 0) {
		ssize_t n = send(fd, p, left, MSG_NOSIGNAL);
		if (n > 0) {
			p += n;
			left -= static_cast<size_t>(n);
		} else if (errno == EINTR) {
			continue;
		} else {
			return false;
		}
	}
	return true;
}

bool sendAll(int fd, const std::string& data) {
	return sendRaw(fd, data.data(), data.size());
}

// 托管内嵌的手机编辑页：正文为 gzip 原始字节（构建期压缩，浏览器自动解压），
// Content-Length 必须与实际发送字节严格一致。
void sendEditorPage(int fd) {
	std::string header = "HTTP/1.1 200 OK\r\n";
	header += "Content-Type: text/html; charset=utf-8\r\n";
	header += "Content-Encoding: gzip\r\n";
	header += "Content-Length: " + std::to_string(EDITOR_GZ_LEN) + "\r\n";
	header += "Cache-Control: no-cache\r\n";
	header += "Vary: Accept-Encoding\r\n";
	header += "Connection: close\r\n";
	header += "Access-Control-Allow-Origin: *\r\n";
	header += "\r\n";
	if (!sendAll(fd, header)) {
		return;
	}
	sendRaw(fd, EDITOR_GZ, static_cast<size_t>(EDITOR_GZ_LEN));
}

// 配网热点（SoftAP）下，手机通常只输入 IP（默认 80 端口）。
// 这里在 80 端口监听，用连接的本端地址 302 到同一个 IP 的 8080 配置页。
std::string localIpOfSocket(int fd) {
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
		return std::string();
	}
	char buf[INET_ADDRSTRLEN] = { 0 };
	if (inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf)) == NULL) {
		return std::string();
	}
	if (strcmp(buf, "0.0.0.0") == 0) {
		return std::string();
	}
	return std::string(buf);
}

void handleRedirectClient(int fd) {
	struct timeval rcvTimeout;
	rcvTimeout.tv_sec = 1;
	rcvTimeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout));

	char request[512];
	recv(fd, request, sizeof(request), 0);  // 只用于触发读取，不解析 Host

	std::string host = localIpOfSocket(fd);
	if (host.empty()) {
		host = "192.168.4.1";  // 官方 SoftAP 常见默认地址，回退用
	}
	const std::string location = "http://" + host + ":8080/";
	const std::string response =
			"HTTP/1.1 302 Found\r\n"
			"Location: " + location + "\r\n"
			"Content-Length: 0\r\n"
			"Cache-Control: no-store\r\n"
			"Connection: close\r\n"
			"\r\n";
	sendAll(fd, response);
	close(fd);
}

void redirectLoop(int serverFd) {
	while (true) {
		struct sockaddr_in clientAddr;
		socklen_t addrLen = sizeof(clientAddr);
		int clientFd = accept(serverFd,
				reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
		if (clientFd < 0) {
			if (errno == EINTR) {
				continue;
			}
			sleep(1);
			continue;
		}
		try {
			std::thread(handleRedirectClient, clientFd).detach();
		} catch (...) {
			close(clientFd);
		}
	}
}

bool startRedirectServer() {
	int serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd < 0) {
		return false;
	}
	int reuse = 1;
	setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(80);
	if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr),
			sizeof(addr)) != 0) {
		close(serverFd);
		return false;
	}
	if (listen(serverFd, kListenBacklog) != 0) {
		close(serverFd);
		return false;
	}
	std::thread(redirectLoop, serverFd).detach();
	return true;
}

void sendJson(int fd, int status, const char* reason,
		const std::string& body) {
	char statusLine[32];
	snprintf(statusLine, sizeof(statusLine), "HTTP/1.1 %d ", status);
	std::string resp = statusLine;
	resp += reason;
	resp += "\r\n";
	resp += "Content-Type: application/json; charset=utf-8\r\n";
	resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	resp += "Cache-Control: no-store\r\n";
	resp += "X-Content-Type-Options: nosniff\r\n";
	resp += "Connection: close\r\n";
	resp += "Access-Control-Allow-Origin: *\r\n";
	resp += "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n";
	resp += "Access-Control-Allow-Headers: Content-Type\r\n";
	resp += "\r\n";
	resp += body;
	sendAll(fd, resp);
}

void sendOkJson(int fd, const JsonValue& value) {
	sendJson(fd, 200, "OK", value.dump(-1));
}

void sendErrorJson(int fd, int status, const char* reason,
		const std::string& error) {
	JsonValue v = JsonValue::newObject();
	v.set("ok", JsonValue(false));
	v.set("error", JsonValue(error));
	sendJson(fd, status, reason, v.dump(-1));
}

void sendNoContent(int fd) {
	const char* resp =
			"HTTP/1.1 204 No Content\r\n"
			"Connection: close\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
			"Access-Control-Allow-Headers: Content-Type\r\n"
			"Content-Length: 0\r\n"
			"\r\n";
	sendAll(fd, resp);
}

// 纯数字串转非负长整型，非法返回 -1
long parseNonNegativeInt(const std::string& s) {
	if (s.empty()) {
		return -1;
	}
	long v = 0;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] < '0' || s[i] > '9') {
			return -1;
		}
		// 先判超限再乘加，避免超长数字串在 32 位 long 上溢出
		if (v > kBodyMaxBytes) {
			return v;
		}
		v = v * 10 + (s[i] - '0');
	}
	return v;
}

long readUptimeSec() {
	FILE* fp = fopen("/proc/uptime", "r");
	if (fp == NULL) {
		return 0;
	}
	double up = 0.0;
	int matched = fscanf(fp, "%lf", &up);
	fclose(fp);
	if (matched != 1 || up < 0.0) {
		return 0;
	}
	return static_cast<long>(up);
}

TimerConfig loadConfigOrDefault(long long nowUtc, bool* migrated) {
	TimerConfig config;
	bool wasMigrated = false;
	if (!ConfigStore::load(config, nowUtc, &wasMigrated)) {
		// 磁盘无配置/损坏时返回默认 v3 页表（与 mainLogic 启动兜底一致）
		config = ConfigStore::defaultConfig();
		wasMigrated = false;
	}
	if (migrated != NULL) {
		*migrated = wasMigrated;
	}
	return config;
}

JsonValue buildDeviceJson(int tzOffsetMinutes) {
	const long long now = TimeKeeper::getInstance().now();
	const bool timeValid = now >= TimeKeeper::kTimeValidEpoch;

	LocalDateTime local;
	epochToLocalDateTime(now, tzOffsetMinutes, local);

	JsonValue device = JsonValue::newObject();
	device.set("ip", JsonValue(wifiIp()));
	device.set("ssid", JsonValue(wifiSsid()));
	device.set("wifi", JsonValue(wifiIsUp()));
	device.set("uptimeSec", JsonValue(static_cast<long long>(readUptimeSec())));
	device.set("timeUtc", JsonValue(now));
	device.set("timeValid", JsonValue(timeValid));
	device.set("tzOffsetMinutes", JsonValue(tzOffsetMinutes));
	device.set("localTime", JsonValue(formatDateTime(local)));
	device.set("lastSyncUtc",
			JsonValue(TimeKeeper::getInstance().lastSyncUtc()));
	device.set("apMode", JsonValue(ConfigMode::getInstance().isActive()));
	device.set("apSsid", JsonValue(ConfigMode::getInstance().ssid()));
	device.set("apIp", JsonValue(ConfigMode::getInstance().ip()));
	return device;
}

// 读取当前页状态；TimerPage 尚未注册时返回 false（接口仍可用）
bool readPageStatus(PageStatus& status) {
	TimerPage* page = static_cast<TimerPage*>(
			PageManager::getInstance().getPage("TimerPage"));
	if (page == NULL) {
		return false;
	}
	return page->getStatus(status);
}

// GET /api/config 的 ui 块（运行时只读，POST 不接受）
JsonValue buildUiJson(const PageStatus& status) {
	JsonValue ui = JsonValue::newObject();
	ui.set("pageId", JsonValue(status.pageId));
	ui.set("pageIndex", JsonValue(status.pageIndex));
	ui.set("pageCount", JsonValue(status.pageCount));
	ui.set("pageTitle", JsonValue(status.pageTitle));
	ui.set("pageType", JsonValue(status.pageType));
	return ui;
}

// GET /api/status 的 page 块（契约 §6：{id,index,title,type,count}）
JsonValue buildPageJson(const PageStatus& status) {
	JsonValue page = JsonValue::newObject();
	page.set("id", JsonValue(status.pageId));
	page.set("index", JsonValue(status.pageIndex));
	page.set("title", JsonValue(status.pageTitle));
	page.set("type", JsonValue(status.pageType));
	page.set("count", JsonValue(status.pageCount));
	return page;
}

void handleGetConfig(int fd) {
	ConfigMode::getInstance().touch();
	const long long now = TimeKeeper::getInstance().now();
	bool migrated = false;
	TimerConfig config = loadConfigOrDefault(now, &migrated);
	if (migrated) {
		LOGI_TRACE("HttpServer: loaded v1 config, migrated in memory");
	}

	JsonValue configJson;
	std::string parseErr;
	if (!JsonValue::parse(ConfigStore::toJson(config), configJson, parseErr)) {
		sendErrorJson(fd, 500, "Internal Server Error",
				std::string("config serialize error: ") + parseErr);
		return;
	}

	JsonValue root = JsonValue::newObject();
	root.set("ok", JsonValue(true));
	root.set("config", configJson);

	// v3：当前页（运行时只读）+ 直播数据源健康摘要
	PageStatus status;
	const bool havePage = readPageStatus(status);
	if (!havePage) {
		LOGW_TRACE("HttpServer: TimerPage not available for /api/config ui");
	}

	JsonValue device = buildDeviceJson(config.tzOffsetMinutes);
	// web-v3 兼容：device.page 与顶层 ui 同义（只增字段，不改 v2 语义）
	device.set("page", buildPageJson(status));
	root.set("device", device);
	root.set("ui", buildUiJson(status));

	JsonValue live = JsonValue::newObject();
	live.set("source", JsonValue(liveSourceId(config.live.kind)));
	live.set("refreshSec", JsonValue(config.live.refreshSec));
	JsonValue liveStatus;
	if (LiveService::getInstance().statusJson(liveStatus)) {
		const JsonValue& source = liveStatus["source"];
		live.set("reachable", source["reachable"]);
		live.set("lastOkUtc", source["lastOkUtc"]);
		live.set("error", source["error"]);
	}
	root.set("live", live);
	sendOkJson(fd, root);
}

void handlePostConfig(int fd, const HttpRequest& req) {
	ConfigMode::getInstance().touch();
	JsonValue body;
	std::string err;
	if (!JsonValue::parse(req.body, body, err)) {
		sendErrorJson(fd, 400, "Bad Request",
				std::string("invalid json: ") + err);
		return;
	}
	if (!body.isObject()) {
		sendErrorJson(fd, 400, "Bad Request", "config root must be an object");
		return;
	}

	// 兼容直接提交 v2 对象和 {"config":{...}} 两种形式；不接受 v1 schema。
	const JsonValue* configValue = &body;
	if (body.contains("config")) {
		configValue = &body["config"];
		if (!configValue->isObject()) {
			sendErrorJson(fd, 400, "Bad Request",
					"config must be an object");
			return;
		}
	}

	// 旧页面可能仍按 v1（tzOffset + points）提交；明确提示刷新，避免只报缺字段。
	if (!configValue->contains("tzOffsetMinutes") &&
			(configValue->contains("tzOffset") ||
					configValue->contains("points"))) {
		sendErrorJson(fd, 400, "Bad Request",
				"v1 config is not accepted, refresh the page");
		return;
	}

	TimerConfig config;
	if (!ConfigStore::fromJson(*configValue, config, err)) {
		sendErrorJson(fd, 400, "Bad Request", err);
		return;
	}

	// 规范化配置上限 12 KB：正常 v3 最坏约 6 KB，超出说明输入异常
	if (ConfigStore::toJson(config).size() >
			static_cast<size_t>(kConfigMaxBytes)) {
		sendErrorJson(fd, 413, "Payload Too Large", "config too large");
		return;
	}

	if (!ConfigStore::save(config)) {
		sendErrorJson(fd, 500, "Internal Server Error", "save config failed");
		return;
	}

	// applyCallback 内只做 PageHost/LiveService 热应用 + 页下标 clamp
	if (gApplyCallback) {
		gApplyCallback(config);
	}

	JsonValue ok = JsonValue::newObject();
	ok.set("ok", JsonValue(true));
	PageStatus status;
	if (readPageStatus(status)) {
		ok.set("ui", buildUiJson(status));
		ok.set("page", buildPageJson(status));  // web-v3 兼容
	}
	sendOkJson(fd, ok);
}

// POST /api/page：{"id":"p1"} / {"index":1} / {"reset":true}（切页不写配置）
void handlePostPage(int fd, const HttpRequest& req) {
	ConfigMode::getInstance().touch();
	JsonValue body;
	std::string err;
	if (!JsonValue::parse(req.body, body, err) || !body.isObject()) {
		sendErrorJson(fd, 400, "Bad Request", "invalid json");
		return;
	}

	TimerPage* page = static_cast<TimerPage*>(
			PageManager::getInstance().getPage("TimerPage"));
	if (page == NULL) {
		sendErrorJson(fd, 503, "Service Unavailable", "page not available");
		return;
	}

	if (body.contains("reset")) {
		if (!body["reset"].isBool() || !body["reset"].asBool(false)) {
			sendErrorJson(fd, 400, "Bad Request", "reset must be true");
			return;
		}
		page->resetUiState();
		JsonValue ok = JsonValue::newObject();
		ok.set("ok", JsonValue(true));
		ok.set("reset", JsonValue(true));
		sendOkJson(fd, ok);
		return;
	}

	int targetIndex = -1;
	if (body.contains("id")) {
		if (!body["id"].isString()) {
			sendErrorJson(fd, 400, "Bad Request", "id must be a string");
			return;
		}
		const std::string id = body["id"].asString();
		if (!page->goToPageId(id)) {
			sendErrorJson(fd, 400, "Bad Request", "unknown page id");
			return;
		}
		targetIndex = page->currentPageIndex();
	} else if (body.contains("index")) {
		if (!body["index"].isNumber()) {
			sendErrorJson(fd, 400, "Bad Request", "index must be a number");
			return;
		}
		const long long index = body["index"].asInt64(-1);
		if (index < 0 || index >= page->pageCount()) {
			sendErrorJson(fd, 400, "Bad Request", "page index out of range");
			return;
		}
		if (!page->goToPage(static_cast<int>(index))) {
			sendErrorJson(fd, 400, "Bad Request", "page switch failed");
			return;
		}
		targetIndex = static_cast<int>(index);
	} else {
		sendErrorJson(fd, 400, "Bad Request", "missing id or index");
		return;
	}

	JsonValue ok = JsonValue::newObject();
	ok.set("ok", JsonValue(true));
	ok.set("index", JsonValue(targetIndex));
	ok.set("pageCount", JsonValue(page->pageCount()));
	PageStatus status;
	if (page->getStatus(status)) {
		ok.set("page", buildPageJson(status));  // web-v3 兼容
	}
	sendOkJson(fd, ok);
}

// GET /api/live/status
void handleGetLiveStatus(int fd) {
	ConfigMode::getInstance().touch();
	JsonValue status;
	if (!LiveService::getInstance().statusJson(status)) {
		sendErrorJson(fd, 500, "Internal Server Error",
				"live status unavailable");
		return;
	}
	sendOkJson(fd, status);
}

// POST /api/live/refresh：{"page":N}（可省略=当前页）；只排队，不等待网络
void handlePostLiveRefresh(int fd, const HttpRequest& req) {
	ConfigMode::getInstance().touch();
	int pageIndex = -1;
	if (!req.body.empty()) {
		JsonValue body;
		std::string err;
		if (!JsonValue::parse(req.body, body, err) || !body.isObject()) {
			sendErrorJson(fd, 400, "Bad Request", "invalid json");
			return;
		}
		if (body.contains("page")) {
			if (!body["page"].isNumber()) {
				sendErrorJson(fd, 400, "Bad Request",
						"page must be a number");
				return;
			}
			const long long index = body["page"].asInt64(-1);
			if (index < 0 || index >= LiveService::getInstance().pageCount()) {
				sendErrorJson(fd, 400, "Bad Request",
						"page index out of range");
				return;
			}
			pageIndex = static_cast<int>(index);
		}
	}

	LiveService& live = LiveService::getInstance();
	live.requestRefresh(pageIndex);

	JsonValue ok = JsonValue::newObject();
	ok.set("ok", JsonValue(true));
	ok.set("queued", JsonValue(true));
	ok.set("page", JsonValue(pageIndex >= 0 ? pageIndex : live.activePage()));
	sendOkJson(fd, ok);
}

// POST /api/live：push 模式写入缓存（body 同 bridge 响应格式）
void handlePostLive(int fd, const HttpRequest& req) {
	ConfigMode::getInstance().touch();
	JsonValue body;
	std::string err;
	if (!JsonValue::parse(req.body, body, err) || !body.isObject()) {
		sendErrorJson(fd, 400, "Bad Request", "invalid json");
		return;
	}
	int updated = 0;
	std::string pushError;
	if (!LiveService::getInstance().pushJson(body, updated, pushError)) {
		sendErrorJson(fd, 400, "Bad Request",
				pushError.empty() ? "invalid live payload" : pushError);
		return;
	}
	JsonValue ok = JsonValue::newObject();
	ok.set("ok", JsonValue(true));
	ok.set("updated", JsonValue(updated));
	ok.set("ts", JsonValue(static_cast<long long>(time(NULL))));
	sendOkJson(fd, ok);
}

void handlePostTime(int fd, const HttpRequest& req) {
	JsonValue body;
	std::string err;
	if (!JsonValue::parse(req.body, body, err) || !body.isObject() ||
			!body.contains("epoch") || !body["epoch"].isNumber()) {
		sendErrorJson(fd, 400, "Bad Request", "missing numeric epoch");
		return;
	}

	long long epoch = body["epoch"].asInt64(0);
	if (epoch < TimeKeeper::kTimeValidEpoch || epoch > TimeKeeper::kMaxEpoch) {
		sendErrorJson(fd, 400, "Bad Request", "epoch out of range");
		return;
	}

	if (!TimeKeeper::getInstance().setUtc(epoch)) {
		sendErrorJson(fd, 500, "Internal Server Error", "set time failed");
		return;
	}

	bool migrated = false;
	const TimerConfig config = loadConfigOrDefault(epoch, &migrated);
	LocalDateTime local;
	epochToLocalDateTime(epoch, config.tzOffsetMinutes, local);

	JsonValue ok = JsonValue::newObject();
	ok.set("ok", JsonValue(true));
	ok.set("timeUtc", JsonValue(epoch));
	ok.set("localTime", JsonValue(formatDateTime(local)));
	sendOkJson(fd, ok);
}

void scheduleWifiReconnect() {
	// 让响应先发完，再断开旧网络重连；重连失败时后台会继续按列表重试。
	std::thread([]() {
		usleep(800 * 1000);
		WifiBootstrap::getInstance().reconnect();
	}).detach();
}

bool saveWifiNetworksAndReply(int fd, const std::vector<WifiNetwork>& networks) {
	if (!WifiStore::saveNetworks(networks)) {
		sendErrorJson(fd, 500, "Internal Server Error", "save wifi list failed");
		return false;
	}

	JsonValue ok = JsonValue::newObject();
	ok.set("ok", JsonValue(true));
	ok.set("count", JsonValue(static_cast<int>(networks.size())));
	ok.set("message", JsonValue("saved, reconnecting"));
	sendOkJson(fd, ok);
	// SoftAP 与 STA 不能同时工作：先把 AP 关掉，再让 WiFi bootstrap 用新列表重连。
	if (ConfigMode::getInstance().isActive()) {
		LOGW_TRACE("HttpServer: leaving SoftAP before WiFi reconnect");
		ConfigMode::getInstance().requestExit();
	}
	scheduleWifiReconnect();
	return true;
}

void handleGetWifi(int fd) {
	ConfigMode::getInstance().touch();
	std::vector<WifiNetwork> networks;
	std::string source;
	WifiStore::loadNetworks(networks, &source);

	JsonValue array = JsonValue::newArray();
	for (size_t i = 0; i < networks.size(); ++i) {
		const WifiNetwork& network = networks[i];
		JsonValue item = JsonValue::newObject();
		item.set("ssid", JsonValue(network.ssid));
		item.set("enabled", JsonValue(network.enabled));
		item.set("hasPassword", JsonValue(!network.password.empty()));
		array.pushBack(item);
	}

	JsonValue current = JsonValue::newObject();
	current.set("ssid", JsonValue(wifiSsid()));
	current.set("ip", JsonValue(wifiIp()));
	current.set("wifi", JsonValue(wifiIsUp()));

	JsonValue root = JsonValue::newObject();
	root.set("ok", JsonValue(true));
	root.set("current", current);
	root.set("max", JsonValue(static_cast<int>(WifiStore::kMaxNetworks)));
	root.set("networks", array);
	root.set("source", JsonValue(source));
	sendOkJson(fd, root);
}

void handlePostWifiList(int fd, const HttpRequest& req) {
	ConfigMode::getInstance().touch();
	JsonValue body;
	std::string err;
	if (!JsonValue::parse(req.body, body, err) || !body.isObject()) {
		sendErrorJson(fd, 400, "Bad Request", "invalid json");
		return;
	}
	if (!body.contains("networks") || !body["networks"].isArray()) {
		sendErrorJson(fd, 400, "Bad Request", "missing networks array");
		return;
	}
	const JsonValue& items = body["networks"];
	if (items.size() > static_cast<size_t>(WifiStore::kMaxNetworks)) {
		sendErrorJson(fd, 400, "Bad Request", "too many networks (max 8)");
		return;
	}

	// 密码不回传给网页；同 SSID 留空时沿用设备里已有的密码。
	std::vector<WifiNetwork> existing;
	WifiStore::loadNetworks(existing, NULL);

	std::vector<WifiNetwork> networks;
	for (size_t i = 0; i < items.size(); ++i) {
		const JsonValue& item = items.at(i);
		if (!item.isObject()) {
			sendErrorJson(fd, 400, "Bad Request",
					"network item " + std::to_string(i) + " must be an object");
			return;
		}
		if (!item.contains("ssid") || !item["ssid"].isString()) {
			sendErrorJson(fd, 400, "Bad Request",
					"network item " + std::to_string(i) + " missing string ssid");
			return;
		}
		WifiNetwork network;
		network.ssid = item["ssid"].asString();
		if (network.ssid.empty() || network.ssid.size() > 32) {
			sendErrorJson(fd, 400, "Bad Request",
					"network item " + std::to_string(i) +
							" ssid length must be 1..32");
			return;
		}
		for (size_t j = 0; j < networks.size(); ++j) {
			if (networks[j].ssid == network.ssid) {
				sendErrorJson(fd, 400, "Bad Request",
						"duplicate ssid: " + network.ssid);
				return;
			}
		}

		if (item.contains("enabled")) {
			if (!item["enabled"].isBool()) {
				sendErrorJson(fd, 400, "Bad Request",
						"network item " + std::to_string(i) +
								" enabled must be bool");
				return;
			}
			network.enabled = item["enabled"].asBool(true);
		}

		std::string password;
		if (item.contains("password")) {
			if (!item["password"].isString()) {
				sendErrorJson(fd, 400, "Bad Request",
						"network item " + std::to_string(i) +
								" password must be string");
				return;
			}
			password = item["password"].asString();
			if (password.size() > 64) {
				sendErrorJson(fd, 400, "Bad Request",
						"network item " + std::to_string(i) +
								" password too long");
				return;
			}
		}
		if (password.empty()) {
			for (size_t j = 0; j < existing.size(); ++j) {
				if (existing[j].ssid == network.ssid &&
						!existing[j].password.empty()) {
					password = existing[j].password;
					break;
				}
			}
		}
		network.password = password;
		networks.push_back(network);
	}

	if (!saveWifiNetworksAndReply(fd, networks)) {
		return;
	}
	LOGI_TRACE("HttpServer: saved %u WiFi networks",
			static_cast<unsigned int>(networks.size()));
}

// 兼容旧页面的单网络接口；保留已有其他网络的密码与顺序。
void handlePostWifi(int fd, const HttpRequest& req) {
	JsonValue body;
	std::string err;
	if (!JsonValue::parse(req.body, body, err) || !body.isObject()) {
		sendErrorJson(fd, 400, "Bad Request", "invalid json");
		return;
	}
	if (!body.contains("ssid") || !body["ssid"].isString()) {
		sendErrorJson(fd, 400, "Bad Request", "missing string ssid");
		return;
	}
	std::string ssid = body["ssid"].asString();
	std::string pwd;
	if (body.contains("password") && body["password"].isString()) {
		pwd = body["password"].asString();
	}
	if (ssid.empty() || ssid.size() > 32) {
		sendErrorJson(fd, 400, "Bad Request", "ssid length must be 1..32");
		return;
	}
	if (pwd.size() > 64) {
		sendErrorJson(fd, 400, "Bad Request", "password too long");
		return;
	}

	std::vector<WifiNetwork> networks;
	WifiStore::loadNetworks(networks, NULL);
	bool found = false;
	for (size_t i = 0; i < networks.size(); ++i) {
		if (networks[i].ssid == ssid) {
			// 旧接口的空密码不再清空已保存密码，避免误传空值把网络弄断；
			// 开放网络本来就没有密码。
			if (!pwd.empty() || networks[i].password.empty()) {
				networks[i].password = pwd;
			}
			networks[i].enabled = true;
			found = true;
			break;
		}
	}
	if (!found) {
		if (networks.size() >= static_cast<size_t>(WifiStore::kMaxNetworks)) {
			sendErrorJson(fd, 400, "Bad Request", "too many networks (max 8)");
			return;
		}
		WifiNetwork network;
		network.ssid = ssid;
		network.password = pwd;
		network.enabled = true;
		networks.insert(networks.begin(), network);
	}

	if (!saveWifiNetworksAndReply(fd, networks)) {
		return;
	}
	LOGI_TRACE("HttpServer: saved WiFi ssid=%s via legacy API", ssid.c_str());
}

const char* modeName(PageMode mode) {
	switch (mode) {
	case PAGE_MODE_CLOCK:
		return "clock";
	case PAGE_MODE_COUNTDOWN:
		return "countdown";
	case PAGE_MODE_COUNTUP:
		return "countup";
	case PAGE_MODE_INVALID:
	default:
		return "invalid";
	}
}

void handleGetStatus(int fd) {
	ConfigMode::getInstance().touch();
	PageStatus status;
	bool havePage = false;
	TimerPage* page = static_cast<TimerPage*>(
			PageManager::getInstance().getPage("TimerPage"));
	if (page != NULL && page->getStatus(status)) {
		havePage = true;
	}

	int tzOffsetMinutes = status.tzOffsetMinutes;
	if (!havePage) {
		const long long now = TimeKeeper::getInstance().now();
		bool migrated = false;
		const TimerConfig config = loadConfigOrDefault(now, &migrated);
		tzOffsetMinutes = config.tzOffsetMinutes;
		status = PageStatus();
		status.timeValid = now >= TimeKeeper::kTimeValidEpoch;
		status.mode = PAGE_MODE_INVALID;
		status.text = "--:--:--";
		status.nowUtc = now;
		status.tzOffsetMinutes = tzOffsetMinutes;
		LOGW_TRACE("HttpServer: TimerPage not available for /api/status");
	}

	JsonValue state = JsonValue::newObject();
	state.set("mode", JsonValue(modeName(status.mode)));
	state.set("text", JsonValue(status.text));
	state.set("text2", JsonValue(status.text2));
	state.set("remainSeconds", JsonValue(status.remainSeconds));
	state.set("targetUtc", JsonValue(status.targetEpoch));
	state.set("targetLocal",
			JsonValue(status.hasTarget ? formatDateTime(status.targetLocal)
					: std::string()));
	state.set("warning", JsonValue(status.warning));
	state.set("smallFont", JsonValue(status.smallFont));
	state.set("overlay", JsonValue(status.overlay));
	state.set("banner", JsonValue(status.banner));
	state.set("page", buildPageJson(status));

	JsonValue root = JsonValue::newObject();
	root.set("ok", JsonValue(true));
	JsonValue device = buildDeviceJson(tzOffsetMinutes);
	device.set("page", buildPageJson(status));  // web-v3 兼容
	root.set("device", device);
	root.set("state", state);
	sendOkJson(fd, root);
}

// 返回 false 表示请求格式错误，已发送 400
bool parseRequest(const std::string& raw, size_t headerEnd, HttpRequest& req) {
	std::string headerPart = raw.substr(0, headerEnd);

	size_t lineEnd = headerPart.find("\r\n");
	if (lineEnd == std::string::npos) {
		return false;
	}
	std::string requestLine = headerPart.substr(0, lineEnd);
	{
		size_t sp1 = requestLine.find(' ');
		if (sp1 == std::string::npos) {
			return false;
		}
		size_t sp2 = requestLine.find(' ', sp1 + 1);
		if (sp2 == std::string::npos) {
			return false;
		}
		req.method = requestLine.substr(0, sp1);
		std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
		size_t q = target.find('?');
		req.path = (q == std::string::npos) ? target : target.substr(0, q);
	}

	size_t pos = lineEnd + 2;
	while (pos < headerPart.size()) {
		size_t eol = headerPart.find("\r\n", pos);
		if (eol == std::string::npos) {
			eol = headerPart.size();
		}
		std::string line = headerPart.substr(pos, eol - pos);
		pos = eol + 2;
		size_t colon = line.find(':');
		if (colon == std::string::npos) {
			continue;
		}
		std::string key = lowerCopy(trimToken(line.substr(0, colon)));
		std::string val = trimToken(line.substr(colon + 1));
		if (!key.empty()) {
			req.headers[key] = val;
		}
	}

	req.body = raw.substr(headerEnd + 4);
	return true;
}

void handleClient(int fd) {
	struct ActiveClientGuard {
		ActiveClientGuard() { gActiveClients.fetch_add(1); }
		~ActiveClientGuard() { gActiveClients.fetch_sub(1); }
	} guard;

	if (gActiveClients.load() > kMaxActiveClients) {
		sendErrorJson(fd, 503, "Service Unavailable", "too many connections");
		close(fd);
		return;
	}

	struct timeval rcvTimeout;
	rcvTimeout.tv_sec = kRecvTimeoutSec;
	rcvTimeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &rcvTimeout, sizeof(rcvTimeout));

	std::string raw;
	char buf[512];
	size_t headerEnd = std::string::npos;
	while (headerEnd == std::string::npos) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0) {
			close(fd);
			return;  // 超时或对端直接关闭，静默丢弃
		}
		raw.append(buf, static_cast<size_t>(n));
		if (raw.size() > static_cast<size_t>(kHeaderMaxBytes)) {
			sendErrorJson(fd, 400, "Bad Request", "request headers too large");
			close(fd);
			return;
		}
		headerEnd = raw.find("\r\n\r\n");
	}

	HttpRequest req;
	if (!parseRequest(raw, headerEnd, req)) {
		sendErrorJson(fd, 400, "Bad Request", "malformed request");
		close(fd);
		return;
	}

	long contentLength = 0;
	std::map<std::string, std::string>::iterator clIt =
			req.headers.find("content-length");
	if (clIt != req.headers.end()) {
		contentLength = parseNonNegativeInt(clIt->second);
		if (contentLength < 0) {
			contentLength = 0;
		}
	}
	if (contentLength > kBodyMaxBytes) {
		sendErrorJson(fd, 413, "Payload Too Large", "request body too large");
		close(fd);
		return;
	}

	while (static_cast<long>(req.body.size()) < contentLength) {
		ssize_t n = recv(fd, buf,
				std::min(sizeof(buf),
						static_cast<size_t>(contentLength - req.body.size())),
				0);
		if (n <= 0) {
			sendErrorJson(fd, 400, "Bad Request", "incomplete request body");
			close(fd);
			return;
		}
		req.body.append(buf, static_cast<size_t>(n));
	}
	if (static_cast<long>(req.body.size()) > contentLength) {
		req.body.resize(static_cast<size_t>(contentLength));
	}

	if (req.method == "OPTIONS") {
		sendNoContent(fd);
	} else if (req.method == "GET" &&
			(req.path == "/" || req.path == "/index.html")) {
		sendEditorPage(fd);
	} else if (req.method == "GET" && req.path == "/api/config") {
		handleGetConfig(fd);
	} else if (req.method == "POST" && req.path == "/api/config") {
		handlePostConfig(fd, req);
	} else if (req.method == "POST" && req.path == "/api/time") {
		handlePostTime(fd, req);
	} else if (req.method == "GET" && req.path == "/api/wifi") {
		handleGetWifi(fd);
	} else if (req.method == "POST" && req.path == "/api/wifi/list") {
		handlePostWifiList(fd, req);
	} else if (req.method == "POST" && req.path == "/api/wifi") {
		handlePostWifi(fd, req);
	} else if (req.method == "POST" && req.path == "/api/page") {
		handlePostPage(fd, req);
	} else if (req.method == "GET" && req.path == "/api/live/status") {
		handleGetLiveStatus(fd);
	} else if (req.method == "POST" && req.path == "/api/live/refresh") {
		handlePostLiveRefresh(fd, req);
	} else if (req.method == "POST" && req.path == "/api/live") {
		handlePostLive(fd, req);
	} else if (req.method == "GET" && req.path == "/api/status") {
		handleGetStatus(fd);
	} else if (req.method == "GET" && req.path == "/favicon.ico") {
		sendNoContent(fd);
	} else {
		sendErrorJson(fd, 404, "Not Found", "not found");
	}

	close(fd);
}

void acceptLoop(int serverFd) {
	while (true) {
		struct sockaddr_in clientAddr;
		socklen_t addrLen = sizeof(clientAddr);
		int clientFd = accept(serverFd,
				reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
		if (clientFd < 0) {
			if (errno == EINTR) {
				continue;
			}
			sleep(1);
			continue;
		}
		// 先限流再创建线程，避免连接洪峰把线程/栈资源打满
		if (gActiveClients.load() >= kMaxActiveClients) {
			sendErrorJson(clientFd, 503, "Service Unavailable",
					"too many connections");
			close(clientFd);
			continue;
		}
		try {
			std::thread(handleClient, clientFd).detach();
		} catch (...) {
			close(clientFd);
		}
	}
}

}  // namespace

// static
bool HttpServer::start(int port, ApplyCallback applyCallback) {
	int serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd < 0) {
		return false;
	}

	int reuse = 1;
	setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(static_cast<uint16_t>(port));
	if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr),
			sizeof(addr)) != 0) {
		close(serverFd);
		return false;
	}
	if (listen(serverFd, kListenBacklog) != 0) {
		close(serverFd);
		return false;
	}

	gApplyCallback = applyCallback;
	std::thread(acceptLoop, serverFd).detach();
	// 配网热点下手机只输入 IP（80 端口）；失败不影响 8080 主服务。
	if (!startRedirectServer()) {
		LOGW_TRACE("http server: port 80 redirect unavailable (already in use?)");
	} else {
		LOGI_TRACE("http server: port 80 -> 8080 redirect enabled");
	}
	return true;
}
