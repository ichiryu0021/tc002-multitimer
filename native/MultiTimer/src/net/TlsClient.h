// 设备端最小 HTTPS 客户端：dlopen 系统 OpenSSL 1.1（Z21 的 /lib/libssl.so.1.1），
// 用 /res/ui/cacert.pem 做证书链 + 主机名校验，发 HTTP/1.0 GET 并读到 EOF。
//
// 为什么是 dlopen：
// - FlyThings Z21 工具链的 sysroot 没有 OpenSSL 头文件和 libssl.so/.a；
// - 修改 .cproject 链接参数被明确禁止；官方固件本来就在运行时加载这两个库；
// - 链接参数已含 -ldl，dlsym 不需要改工程。
//
// 线程安全：函数指针表只在第一次调用时（std::call_once + mutex）初始化，之后只读；
// 每次 get() 创建独立的 SSL_CTX/SSL 和 TCP socket，不共享任何可变连接状态。
#ifndef NET_TLSCLIENT_H_
#define NET_TLSCLIENT_H_

#include <string>

// 一次 HTTPS GET 的结果。statusCode==0 表示传输/TLS 失败，error 说明原因。
struct TlsHttpResponse {
	int statusCode;
	std::string body;
	std::string error;

	TlsHttpResponse();
};

class TlsClient {
public:
	// 向 https://host/path 发 GET。
	// - path 必须以 '/' 开头；host/path 中出现 CR/LF 会被拒绝（防注入）。
	// - timeoutMs 覆盖 DNS 之后的总耗时（连接 + 握手 + 读写），默认 4000 ms。
	// - 成功（TCP+TLS+HTTP 解析都完成）返回 true，即使 HTTP 状态不是 200；
	//   失败返回 false 并填 response.error。
	static bool get(const std::string& host, const std::string& path,
			TlsHttpResponse& response, int timeoutMs = 4000);

	// 检查 libssl/libcrypto 能否加载、关键符号是否齐全、OPENSSL_init_ssl 是否成功。
	// 失败时 error 给出具体库名/符号名。
	static bool available(std::string& error);

private:
	TlsClient();
};

#endif  // NET_TLSCLIENT_H_
