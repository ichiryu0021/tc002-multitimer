// 极简 SNTP 客户端：设备没有 RTC 时，开机后靠 WiFi 对时。
// 支持用户在网页配置自定义服务器（host 或 host:port）；空则使用内置列表。
// 不依赖平台 SDK 头，使用 POSIX socket；在后台线程调用，失败不影响主逻辑。
#ifndef NET_SNTPCLIENT_H_
#define NET_SNTPCLIENT_H_

#include <string>

class SntpClient {
public:
	// 设置/读取自定义服务器；空串表示只用内置列表。
	// setServer 可由 HTTP/配置线程调用，内部加锁。
	static void setServer(const std::string& server);
	static std::string server();

	// 先尝试自定义服务器，再回退内置服务器；成功设置系统时间返回 true。
	// timeoutMs 为单个服务器的总超时（DNS + 收发）。
	static bool syncNow(int timeoutMs = 2500);

	// 仅查询，不设置系统时间；epochOut 为 Unix 秒。
	static bool query(long long& epochOut, int timeoutMs = 2500);

private:
	SntpClient();
};

#endif  // NET_SNTPCLIENT_H_
