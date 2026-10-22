#include "config/WifiStore.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <vector>

#include "net/mini_json.h"

namespace {

const char* kPrimaryPath = "/data/multitimer.wifi";
const char* kPrimaryTmpPath = "/data/multitimer.wifi.tmp";

// 串行化 WiFi 列表的并发保存
std::mutex gSaveMutex;

// 按优先级排列：
//   1. 本应用保存的网络列表（用户显式操作，优先）
//   2. 系统 wpa_supplicant.conf（真正生效、开机自动连用的一份）
//   3. 官方 setting.ini 的历史快照
//   4. 官方 SDK 的 U 盘/TF 卡 test.cfg
const char* kFallbackPaths[] = {
	"/data/misc/wifi/wpa_supplicant.conf",
	"/data/setting.ini",
	"/mnt/usb1/test.cfg",
	"/mnt/extsd/test.cfg",
};

std::string trim(const std::string& s) {
	size_t b = 0;
	while (b < s.size() && isspace(static_cast<unsigned char>(s[b])) != 0) {
		++b;
	}
	size_t e = s.size();
	while (e > b && isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
		--e;
	}
	return s.substr(b, e - b);
}

std::string lowerAscii(const std::string& s) {
	std::string out = s;
	for (size_t i = 0; i < out.size(); ++i) {
		if (out[i] >= 'A' && out[i] <= 'Z') {
			out[i] = static_cast<char>(out[i] - 'A' + 'a');
		}
	}
	return out;
}

// 把 key 归一化：去掉空格/下划线/连字符并转小写，兼容 wifi_ssid / wifiSsid / WiFi-SSID。
std::string normalizeKey(const std::string& key) {
	std::string out;
	out.reserve(key.size());
	for (size_t i = 0; i < key.size(); ++i) {
		char c = key[i];
		if (c == ' ' || c == '\t' || c == '_' || c == '-') {
			continue;
		}
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
		out.push_back(c);
	}
	return out;
}

std::string unquote(const std::string& s) {
	if (s.size() >= 2 &&
			((s[0] == '"' && s[s.size() - 1] == '"') ||
			 (s[0] == '\'' && s[s.size() - 1] == '\''))) {
		return s.substr(1, s.size() - 2);
	}
	return s;
}

bool readFile(const char* path, std::string& out) {
	out.clear();
	FILE* fp = fopen(path, "rb");
	if (fp == NULL) {
		return false;
	}
	char buf[1024];
	size_t n = 0;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		out.append(buf, n);
		if (out.size() > 64 * 1024) {  // 配置文件不应这么大
			break;
		}
	}
	bool ok = (ferror(fp) == 0);
	fclose(fp);
	return ok && !out.empty();
}

bool keyIsSsid(const std::string& normalized) {
	return normalized == "ssid" || normalized == "wifissid" ||
			normalized == "wlanssid" || normalized == "networkname";
}

bool keyIsPassword(const std::string& normalized) {
	return normalized == "password" || normalized == "passwd" ||
			normalized == "pwd" || normalized == "psk" ||
			normalized == "wifipwd" || normalized == "wifipassword" ||
			normalized == "wifipasswd" || normalized == "wifipsk" ||
			normalized == "wlanpassword";
}

void applyIniPair(const std::string& key, const std::string& value,
		WifiCredential& out) {
	std::string k = normalizeKey(key);
	if (k.empty()) {
		return;
	}
	// 同一文件里可能有多个 network={}；保留第一组，避免被后面的旧网络覆盖。
	if (keyIsSsid(k)) {
		if (out.ssid.empty()) {
			out.ssid = unquote(trim(value));
		}
	} else if (keyIsPassword(k)) {
		if (out.password.empty()) {
			out.password = unquote(trim(value));
		}
	}
}

bool parseJsonObject(const JsonValue& obj, WifiCredential& out) {
	if (!obj.isObject()) {
		return false;
	}
	static const char* kSsidKeys[] = {
		"ssid", "wifiSsid", "wifi_ssid", "wlanSsid", "wlan_ssid", "name"
	};
	static const char* kPwdKeys[] = {
		"password", "pwd", "passwd", "psk", "wifiPwd", "wifi_pwd",
		"wifiPassword", "wifi_password", "wlanPassword", "wlan_password"
	};
	for (size_t i = 0; i < sizeof(kSsidKeys) / sizeof(kSsidKeys[0]); ++i) {
		if (obj.contains(kSsidKeys[i]) && obj[kSsidKeys[i]].isString()) {
			out.ssid = obj[kSsidKeys[i]].asString();
			break;
		}
	}
	for (size_t i = 0; i < sizeof(kPwdKeys) / sizeof(kPwdKeys[0]); ++i) {
		if (obj.contains(kPwdKeys[i]) && obj[kPwdKeys[i]].isString()) {
			out.password = obj[kPwdKeys[i]].asString();
			break;
		}
	}
	// 兼容 {"wifi":{"ssid":"...","password":"..."}} 这种嵌套写法
	if (out.ssid.empty() && obj.contains("wifi") && obj["wifi"].isObject()) {
		parseJsonObject(obj["wifi"], out);
	}
	return out.valid();
}

// 列表里的单个网络对象；enabled 缺省 true，出现则必须是布尔。
bool parseNetworkObject(const JsonValue& obj, WifiNetwork& out) {
	if (!obj.isObject()) {
		return false;
	}
	WifiCredential cred;
	if (!parseJsonObject(obj, cred)) {
		return false;
	}
	out.ssid = cred.ssid;
	out.password = cred.password;
	out.enabled = true;
	if (obj.contains("enabled")) {
		if (!obj["enabled"].isBool()) {
			return false;
		}
		out.enabled = obj["enabled"].asBool(true);
	}
	return true;
}

bool containsSsid(const std::vector<WifiNetwork>& list,
		const std::string& ssid) {
	for (size_t i = 0; i < list.size(); ++i) {
		if (list[i].ssid == ssid) {
			return true;
		}
	}
	return false;
}

bool writeFileAtomically(const std::string& text) {
	FILE* fp = fopen(kPrimaryTmpPath, "wb");
	if (fp == NULL) {
		return false;
	}
	size_t written = fwrite(text.data(), 1, text.size(), fp);
	bool flushOk = (fflush(fp) == 0);
	if (flushOk && fsync(fileno(fp)) != 0) {
		flushOk = false;
	}
	if (fclose(fp) != 0) {
		flushOk = false;
	}
	if (written != text.size() || !flushOk) {
		remove(kPrimaryTmpPath);
		return false;
	}
	if (rename(kPrimaryTmpPath, kPrimaryPath) != 0) {
		remove(kPrimaryTmpPath);
		return false;
	}
	return true;
}

}  // namespace

// static
bool WifiStore::parseIni(const std::string& text, WifiCredential& out) {
	out.ssid.clear();
	out.password.clear();

	std::string section;
	size_t pos = 0;
	bool firstLine = true;
	while (pos <= text.size()) {
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos) {
			eol = text.size();
		}
		std::string line = text.substr(pos, eol - pos);
		pos = (eol < text.size()) ? eol + 1 : eol + 1;

		if (!line.empty() && line[line.size() - 1] == '\r') {
			line.erase(line.size() - 1);
		}
		if (firstLine && line.size() >= 3 &&
				static_cast<unsigned char>(line[0]) == 0xEF &&
				static_cast<unsigned char>(line[1]) == 0xBB &&
				static_cast<unsigned char>(line[2]) == 0xBF) {
			line = line.substr(3);  // UTF-8 BOM
		}
		firstLine = false;

		line = trim(line);
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}
		if (line[0] == '[' && line[line.size() - 1] == ']') {
			section = lowerAscii(trim(line.substr(1, line.size() - 2)));
			continue;
		}
		// 只接受通用段/无线段；官方 setting.ini 也可能把键平铺在文件开头。
		if (!section.empty() && section != "main" && section != "wifi" &&
				section != "wlan" && section != "network" &&
				section != "general") {
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos) {
			eq = line.find(':');
		}
		if (eq == std::string::npos) {
			continue;
		}
		applyIniPair(line.substr(0, eq), line.substr(eq + 1), out);
	}

	return out.valid();
}

// static
bool WifiStore::parseJson(const std::string& text, WifiCredential& out) {
	out.ssid.clear();
	out.password.clear();

	JsonValue root;
	std::string err;
	if (!JsonValue::parse(text, root, err)) {
		return false;
	}
	return parseJsonObject(root, out);
}

// static
bool WifiStore::parseJsonNetworks(const std::string& text,
		std::vector<WifiNetwork>& out) {
	out.clear();

	JsonValue root;
	std::string err;
	if (!JsonValue::parse(text, root, err)) {
		return false;
	}

	if (root.isArray()) {
		for (size_t i = 0; i < root.size(); ++i) {
			WifiNetwork network;
			if (!parseNetworkObject(root.at(i), network) ||
					network.ssid.empty() || containsSsid(out, network.ssid)) {
				continue;
			}
			if (out.size() >= static_cast<size_t>(kMaxNetworks)) {
				return false;
			}
			out.push_back(network);
		}
		return true;
	}

	if (!root.isObject()) {
		return false;
	}

	if (root.contains("networks")) {
		const JsonValue& networks = root["networks"];
		if (!networks.isArray() ||
				networks.size() > static_cast<size_t>(kMaxNetworks)) {
			return false;
		}
		for (size_t i = 0; i < networks.size(); ++i) {
			WifiNetwork network;
			if (!parseNetworkObject(networks.at(i), network) ||
					network.ssid.empty() || containsSsid(out, network.ssid)) {
				continue;
			}
			out.push_back(network);
		}
		return true;
	}

	// 兼容 v1 单网络对象：{"ssid":"...","password":"..."}
	WifiNetwork single;
	if (!parseNetworkObject(root, single) || single.ssid.empty()) {
		return false;
	}
	out.push_back(single);
	return true;
}

// static
bool WifiStore::loadNetworks(std::vector<WifiNetwork>& out,
		std::string* source) {
	out.clear();
	if (source != NULL) {
		source->clear();
	}

	std::string text;
	if (readFile(kPrimaryPath, text)) {
		if (parseJsonNetworks(text, out)) {
			if (source != NULL) {
				*source = kPrimaryPath;
			}
			return true;  // 显式空列表也算有效，不再回退
		}
	}

	for (size_t i = 0; i < sizeof(kFallbackPaths) / sizeof(kFallbackPaths[0]);
			++i) {
		const char* path = kFallbackPaths[i];
		if (!readFile(path, text)) {
			continue;
		}
		WifiCredential cred;
		bool ok = false;
		// 先按 JSON 解析（test.cfg），失败再按 INI 解析（setting.ini / wpa_supplicant.conf）。
		if (!text.empty() && text[0] == '{') {
			ok = parseJson(text, cred);
		}
		if (!ok) {
			ok = parseIni(text, cred);
		}
		if (!ok && text[0] != '{') {
			ok = parseJson(text, cred);
		}
		if (ok && !cred.ssid.empty()) {
			WifiNetwork network;
			network.ssid = cred.ssid;
			network.password = cred.password;
			network.enabled = true;
			out.push_back(network);
			if (source != NULL) {
				*source = path;
			}
			return true;
		}
	}
	return false;
}

// static
bool WifiStore::load(WifiCredential& out) {
	out.ssid.clear();
	out.password.clear();
	out.source.clear();

	std::vector<WifiNetwork> networks;
	std::string source;
	if (!loadNetworks(networks, &source)) {
		return false;
	}
	for (size_t i = 0; i < networks.size(); ++i) {
		if (!networks[i].enabled || networks[i].ssid.empty()) {
			continue;
		}
		out.ssid = networks[i].ssid;
		out.password = networks[i].password;
		out.source = source;
		return true;
	}
	return false;
}

// static
bool WifiStore::saveNetworks(const std::vector<WifiNetwork>& networks) {
	if (networks.size() > static_cast<size_t>(kMaxNetworks)) {
		return false;
	}
	std::lock_guard<std::mutex> lock(gSaveMutex);

	JsonValue root = JsonValue::newObject();
	root.set("version", JsonValue(2));
	JsonValue array = JsonValue::newArray();
	for (size_t i = 0; i < networks.size(); ++i) {
		JsonValue item = JsonValue::newObject();
		item.set("ssid", JsonValue(networks[i].ssid));
		item.set("password", JsonValue(networks[i].password));
		item.set("enabled", JsonValue(networks[i].enabled));
		array.pushBack(item);
	}
	root.set("networks", array);

	std::string text = root.dump(2);
	text.push_back('\n');
	return writeFileAtomically(text);
}

// static
bool WifiStore::save(const WifiCredential& cred) {
	if (cred.ssid.empty()) {
		return false;
	}
	std::vector<WifiNetwork> networks;
	WifiNetwork network;
	network.ssid = cred.ssid;
	network.password = cred.password;
	network.enabled = true;
	networks.push_back(network);
	return saveNetworks(networks);
}
