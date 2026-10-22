// 配网模式（SoftAP）：断网/需要改 WiFi 时长按中键进入。
// 热点名固定 TC002-Setup，密码固定 12345678；手机连上后访问热点 IP（80 端口）
// 会由 HttpServer 跳转到 :8080 配置页。
#ifndef NET_CONFIGMODE_H_
#define NET_CONFIGMODE_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class ConfigMode {
public:
	static ConfigMode& getInstance();

	// 打开 SoftAP；已打开时幂等。timeoutMs=0 使用默认 15 分钟。
	// 返回 false 表示 SoftAP 不可用。
	bool enter(int timeoutMs = 0);

	// 关闭 SoftAP；未打开时无操作。只能由 UI 线程调用。
	void exit();

	// 请求 UI 线程关闭 SoftAP（HTTP/后台线程安全）
	void requestExit();

	// 有网页活动时续期，避免配网过程中 15 分钟超时
	void touch();

	bool isActive() const;

	// 每秒调用：轮询 AP 状态/IP、超时自动退出、IP 就绪时回调
	void tick();

	std::string ssid() const;
	std::string ip() const;

	// 热点密码（固定值，文档与页面提示使用）
	static const char* password();

	// IP 就绪回调（在 UI 线程的 tick 中触发）
	void setOnReady(std::function<void(const std::string& ip)> callback);

private:
	ConfigMode();
	ConfigMode(const ConfigMode&);
	ConfigMode& operator=(const ConfigMode&);

	mutable std::mutex mMutex;
	std::atomic<bool> mActive;
	std::atomic<bool> mExitRequested;
	bool mAnnounced;
	long long mStartedMs;
	long long mTimeoutMs;
	std::string mSsid;
	std::string mIp;
	std::function<void(const std::string&)> mOnReady;
};

#endif  // NET_CONFIGMODE_H_
