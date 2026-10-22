// WiFi 凭据读取/保存：
//   1. /data/multitimer.wifi          本应用的网络列表（v2 JSON，可多个网络）
//   2. /data/misc/wifi/wpa_supplicant.conf  系统实际生效的 wpa_supplicant 配置
//   3. /data/setting.ini              官方固件设置（Ulanzi Studio / 设备设置界面写入）
//   4. /mnt/usb1/test.cfg             官方 SDK 示例约定的 U 盘配置
//   5. /mnt/extsd/test.cfg            TF 卡同名配置
// 应用列表按文件内顺序尝试；系统 wpa_supplicant 已保存的网络仍由系统优先自动连接。
// 不依赖任何平台 SDK 头，可单独编译。
#ifndef CONFIG_WIFISTORE_H_
#define CONFIG_WIFISTORE_H_

#include <string>
#include <vector>

struct WifiNetwork {
	std::string ssid;
	std::string password;
	bool enabled;

	WifiNetwork() : enabled(true) {}
};

struct WifiCredential {
	std::string ssid;
	std::string password;
	std::string source;  // 实际读取到的文件，便于日志排查

	bool valid() const { return !ssid.empty(); }
};

class WifiStore {
public:
	enum { kMaxNetworks = 8 };

	// 读取网络列表；/data/multitimer.wifi 优先，缺失/损坏时回退到单网络来源。
	// source 非空时回填实际来源路径。
	static bool loadNetworks(std::vector<WifiNetwork>& out,
			std::string* source = NULL);

	// 兼容旧调用：返回列表中第一个启用的网络。
	static bool load(WifiCredential& out);

	// 保存到 /data/multitimer.wifi（v2 JSON，先写 .tmp 再 rename）。
	static bool saveNetworks(const std::vector<WifiNetwork>& networks);

	// 兼容旧调用：保存单网络列表。
	static bool save(const WifiCredential& cred);

	// 供单测/调试：解析 INI / JSON 文本。
	static bool parseIni(const std::string& text, WifiCredential& out);
	static bool parseJson(const std::string& text, WifiCredential& out);
	static bool parseJsonNetworks(const std::string& text,
			std::vector<WifiNetwork>& out);

private:
	WifiStore();
};

#endif  // CONFIG_WIFISTORE_H_
