// 设备端最小明文 HTTP 客户端（bridge / 直播数据 / 任意 http:// GET）。
//
// 为什么不用 curl：FlyThings Z21 工具链没有 libcurl，也没有可用的 OpenSSL 头；
// bridge 是局域网内的 PC/NAS，明文 HTTP 足够（契约 §5 只接受 http://）。
//
// 特性：
// - 只接受 "http://host[:port]/path[?query]"；https 在解析阶段就拒绝；
// - POSIX socket + getaddrinfo + poll，连接/发送/接收都有总超时（timeoutMs）；
// - 发 HTTP/1.0 + `Connection: close`（bridge 实现简单，收完 EOF 即结束）；
// - 解析状态行/headers/body；支持 Transfer-Encoding: chunked 与 Content-Length；
// - 响应原始字节上限由调用方给出（live 用 16 KB），超限判失败；
// - 不使用异常，所有失败走返回值 + error 文本；不打印 URL（可能含 token）。
#ifndef NET_HTTPCLIENT_H_
#define NET_HTTPCLIENT_H_

#include <stddef.h>

#include <string>

// 一次 GET 的结果。
// statusCode==0 表示传输层失败（DNS/连接/超时/格式），error 说明原因；
// statusCode!=0 表示已经拿到完整 HTTP 响应（非 200 也返回 true，由调用方决定）。
struct HttpClientResponse {
	int statusCode;
	std::string body;
	std::string error;

	HttpClientResponse();
	void clear();
};

class HttpClient {
public:
	// 解析 URL；port 缺省 80，path 缺省 "/"。
	// 失败返回 false 并填 error（只接受 http://，不允许空白/控制字符/@userinfo）。
	static bool parseUrl(const std::string& url, std::string& host, int& port,
			std::string& path, std::string& error);

	// 阻塞 GET。timeoutMs 是 DNS 之后连接 + 发送 + 接收的总预算。
	// maxBodyBytes 是收到的原始响应字节上限（含 headers），超限返回 false。
	static bool get(const std::string& url, int timeoutMs, size_t maxBodyBytes,
			HttpClientResponse& out);

	// 解析原始响应（状态行 + headers + body）；支持 chunked 与 Content-Length。
	// 供主机单测/调试使用，不发起网络。
	static bool parseResponse(const std::string& raw, HttpClientResponse& out);

private:
	HttpClient();
};

#endif  // NET_HTTPCLIENT_H_
