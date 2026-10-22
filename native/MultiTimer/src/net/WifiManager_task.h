// WiFi 自举后台任务：后台线程开 WiFi -> 等待系统自动连接 -> 读取凭据兜底连接
// -> 取 IP -> SNTP 对时 -> 断线重连。任何失败都不影响倒计时主逻辑。
// 在线期间每 60 秒在独立后台线程尝试 SNTP（不阻塞 2 秒链路检查）。
// 本头不依赖任何平台 SDK 头。
#ifndef NET_WIFIMANAGER_TASK_H_
#define NET_WIFIMANAGER_TASK_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class WifiBootstrap {
public:
	typedef std::function<void(const std::string& ip)> OnConnectedCallback;

	static WifiBootstrap& getInstance();

	// 幂等：仅首次调用真正 detach 后台线程，不阻塞调用方
	void start();

	// WiFi 是否已连接
	bool isUp() const;

	// 已连接时返回 wlan0 的 IPv4，否则返回空串
	std::string ip() const;

	// 已连接时的 SSID，否则返回空串
	std::string ssid() const;

	// 请求断开并用已保存的凭据重连（HTTP 修改 WiFi 后调用）
	void reconnect();

	// 注册“首次连上”回调（在后台线程触发，回调内不得直接操作 UI，
	// 只允许做线程安全的状态写入）；须在 start 之前注册
	void setOnConnected(OnConnectedCallback cb);

private:
	WifiBootstrap();
	WifiBootstrap(const WifiBootstrap&);
	WifiBootstrap& operator=(const WifiBootstrap&);

	void run();
	void setIp(const std::string& ip);
	void setSsid(const std::string& ssid);
	std::string queryIp();
	void notifyConnected(const std::string& ip);
	bool waitForConnection(int seconds);
	bool connectWithSavedCredential();
	void syncTimeIfNeeded();
	// 在后台线程触发一次 SNTP；mTimeSyncInFlight 防止重入，不阻塞链路检查
	void maybeSyncTimeAsync();
	void onLinkUp();

	std::atomic<bool> mStarted;
	std::atomic<bool> mConnected;
	std::atomic<bool> mNotified;
	std::atomic<bool> mReconnectRequested;
	std::atomic<bool> mWifiReady;
	std::atomic<unsigned long> mLastTimeSync;  // 32 位 ARM 上避免 8 字节原子依赖 libatomic
	std::atomic<bool> mTimeSyncInFlight;
	mutable std::mutex mMutex;
	std::string mIp;
	std::string mSsid;
	std::mutex mCbMutex;
	OnConnectedCallback mOnConnected;
};

// 便捷入口
void startWifiBootstrap();
bool wifiIsUp();
std::string wifiIp();
std::string wifiSsid();

#endif  // NET_WIFIMANAGER_TASK_H_
