#include "net/TlsClient.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "net/BilibiliParse.h"

namespace {

// 设备内置 CA 包（145 张 Mozilla 根证书，RESEARCH §5.3）。不许把证书内容拷进仓库。
const char* const kCaFile = "/res/ui/cacert.pem";
const char* const kHttpsPort = "443";

// OpenSSL 常量（1.1.0，取自 openssl/ssl.h；不引入头文件）。
const int kSslVerifyPeer = 0x01;
const long kSslCtrlSetTlsextHostname = 55;
const long kTlsextNametypeHostName = 0;
const int kSslErrorSsl = 1;
const int kSslErrorWantRead = 2;
const int kSslErrorWantWrite = 3;
const int kSslErrorSyscall = 5;
const int kSslErrorZeroReturn = 6;
// SSL_CTX_ctrl 命令号（OpenSSL 1.1.0 ssl.h）
const long kSslCtrlSetMinProtoVersion = 123;
const long kSslCtrlSetMaxProtoVersion = 124;
const long kTls12Version = 0x0303L;  // TLS1_2_VERSION

// libssl 是不透明结构体；本文件只通过函数指针操作。
struct ssl_ctx_st;
struct ssl_st;
struct ssl_method_st;
typedef ssl_ctx_st SSL_CTX;
typedef ssl_st SSL;
typedef ssl_method_st SSL_METHOD;

// 运行时函数表。std::call_once 初始化后只读；不保存任何连接/证书状态。
struct OpenSslApi {
	bool ready;
	std::string error;
	void* sslLib;
	void* cryptoLib;

	int (*OPENSSL_init_ssl)(uint64_t options, const void* settings);
	const SSL_METHOD* (*TLS_client_method)();
	SSL_CTX* (*SSL_CTX_new)(const SSL_METHOD* method);
	void (*SSL_CTX_free)(SSL_CTX* ctx);
	int (*SSL_CTX_load_verify_locations)(SSL_CTX* ctx, const char* caFile,
			const char* caPath);
	void (*SSL_CTX_set_verify)(SSL_CTX* ctx, int mode,
			void (*callback)(int, void*));
	long (*SSL_CTX_ctrl)(SSL_CTX* ctx, int command, long argument, void* pointer);
	int (*SSL_CTX_set_cipher_list)(SSL_CTX* ctx, const char* list);
	SSL* (*SSL_new)(SSL_CTX* ctx);
	void (*SSL_free)(SSL* ssl);
	int (*SSL_set_fd)(SSL* ssl, int fd);
	int (*SSL_set1_host)(SSL* ssl, const char* host);
	long (*SSL_ctrl)(SSL* ssl, int command, long argument, void* pointer);
	int (*SSL_connect)(SSL* ssl);
	int (*SSL_read)(SSL* ssl, void* buffer, int length);
	int (*SSL_write)(SSL* ssl, const void* buffer, int length);
	int (*SSL_get_error)(const SSL* ssl, int result);
	int (*SSL_shutdown)(SSL* ssl);
	long (*SSL_get_verify_result)(const SSL* ssl);
	// 可选诊断符号；缺失不影响功能
	unsigned long (*ERR_get_error)();
	void (*ERR_error_string_n)(unsigned long code, char* buffer, size_t len);
};

OpenSslApi gApi;
std::once_flag gInitOnce;

template <typename Function>
bool loadSymbol(void* handle, const char* name, Function& function,
		std::string& error) {
	dlerror();
	void* symbol = dlsym(handle, name);
	const char* dlsymError = dlerror();
	if (dlsymError != NULL || symbol == NULL) {
		error = std::string("dlsym(") + name + ") failed";
		if (dlsymError != NULL) {
			error += ": ";
			error += dlsymError;
		}
		return false;
	}
	function = reinterpret_cast<Function>(symbol);
	return true;
}

void* openLibrary(const std::vector<std::string>& names,
		const char* label, std::string& error) {
	std::string lastError;
	for (size_t i = 0; i < names.size(); ++i) {
		dlerror();
		// RTLD_GLOBAL：让 libssl 的 NEEDED libcrypto 绑定到我们先加载的
		// 那一个（/res/lib 里随镜像携带的 1.1.1），而不是系统里无 TLS 的 1.1.0i。
		void* handle = dlopen(names[i].c_str(), RTLD_NOW | RTLD_GLOBAL);
		if (handle != NULL) {
			return handle;
		}
		const char* dlError = dlerror();
		lastError = names[i];
		if (dlError != NULL) {
			lastError += ": ";
			lastError += dlError;
		}
	}
	error = std::string("dlopen ") + label + " failed: " + lastError;
	return NULL;
}

void initApi() {
	OpenSslApi& api = gApi;
	api.ready = false;

	// 优先随镜像携带的 OpenSSL 1.1.1（armhf）；系统 /lib/libssl.so.1.1
	// 是 1.1.0i 且所有 TLS client method 被编译掉，不能用于直连。
	std::vector<std::string> cryptoLibs;
	std::vector<std::string> sslLibs;
	const char* envDir = getenv("MT_TLS_LIB_DIR");
	if (envDir != NULL && envDir[0] != '\0') {
		cryptoLibs.push_back(std::string(envDir) + "/libcrypto.so.1.1");
		sslLibs.push_back(std::string(envDir) + "/libssl.so.1.1");
	}
	cryptoLibs.push_back("/res/lib/libcrypto.so.1.1");
	cryptoLibs.push_back("libcrypto.so.1.1");
	cryptoLibs.push_back("libcrypto.so");
	sslLibs.push_back("/res/lib/libssl.so.1.1");
	sslLibs.push_back("libssl.so.1.1");
	sslLibs.push_back("libssl.so");

	api.cryptoLib = openLibrary(cryptoLibs, "libcrypto", api.error);
	if (api.cryptoLib == NULL) {
		return;
	}
	api.sslLib = openLibrary(sslLibs, "libssl", api.error);
	if (api.sslLib == NULL) {
		return;
	}

	std::string symbolError;
	if (!loadSymbol(api.sslLib, "OPENSSL_init_ssl", api.OPENSSL_init_ssl,
				symbolError) ||
			!loadSymbol(api.sslLib, "TLS_client_method", api.TLS_client_method,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_CTX_new", api.SSL_CTX_new,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_CTX_free", api.SSL_CTX_free,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_CTX_load_verify_locations",
					api.SSL_CTX_load_verify_locations, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_CTX_set_verify", api.SSL_CTX_set_verify,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_CTX_ctrl", api.SSL_CTX_ctrl,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_CTX_set_cipher_list",
					api.SSL_CTX_set_cipher_list, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_new", api.SSL_new, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_free", api.SSL_free, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_set_fd", api.SSL_set_fd, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_set1_host", api.SSL_set1_host,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_ctrl", api.SSL_ctrl, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_connect", api.SSL_connect, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_read", api.SSL_read, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_write", api.SSL_write, symbolError) ||
			!loadSymbol(api.sslLib, "SSL_get_error", api.SSL_get_error,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_shutdown", api.SSL_shutdown,
					symbolError) ||
			!loadSymbol(api.sslLib, "SSL_get_verify_result",
					api.SSL_get_verify_result, symbolError)) {
		api.error = "libssl.so.1.1 missing symbol: " + symbolError;
		return;
	}

	if (api.OPENSSL_init_ssl(0, NULL) != 1) {
		api.error = "OPENSSL_init_ssl(0, NULL) failed";
		return;
	}
	// 诊断用可选符号：失败只影响错误字符串详细度
	api.ERR_get_error = reinterpret_cast<unsigned long (*)()>(
			dlsym(api.cryptoLib, "ERR_get_error"));
	api.ERR_error_string_n = reinterpret_cast<void (*)(unsigned long, char*,
			size_t)>(dlsym(api.cryptoLib, "ERR_error_string_n"));

	api.ready = true;
}

void ensureApi() {
	std::call_once(gInitOnce, initApi);
}

// 把 OpenSSL 错误队列转成可读字符串；仅诊断，失败返回空。
std::string opensslErrorDetail() {
	if (gApi.ERR_get_error == NULL) {
		return std::string();
	}
	std::string detail;
	for (int i = 0; i < 4; ++i) {
		const unsigned long code = gApi.ERR_get_error();
		if (code == 0) {
			break;
		}
		char buffer[256];
		buffer[0] = '\0';
		if (gApi.ERR_error_string_n != NULL) {
			gApi.ERR_error_string_n(code, buffer, sizeof(buffer));
		} else {
			snprintf(buffer, sizeof(buffer), "0x%08lx", code);
		}
		if (!detail.empty()) {
			detail += "; ";
		}
		detail += buffer;
	}
	return detail;
}

int remainingMs(const std::chrono::steady_clock::time_point& deadline) {
	const std::chrono::steady_clock::time_point now =
			std::chrono::steady_clock::now();
	if (now >= deadline) {
		return 0;
	}
	long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - now).count();
	if (ms <= 0) {
		return 1;
	}
	if (ms > 60000) {
		return 60000;
	}
	return static_cast<int>(ms);
}

bool waitWritable(int fd, int timeoutMs) {
	fd_set writable;
	FD_ZERO(&writable);
	FD_SET(fd, &writable);
	struct timeval timeout;
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;
	int result = 0;
	do {
		result = select(fd + 1, NULL, &writable, NULL, &timeout);
	} while (result < 0 && errno == EINTR);
	return result > 0;
}

bool setTimeouts(int fd, int timeoutMs) {
	struct timeval timeout;
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) !=
			0) {
		return false;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) !=
			0) {
		return false;
	}
	int noDelay = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
	return true;
}

bool connectTcp(const std::string& host,
		const std::chrono::steady_clock::time_point& deadline, int& fdOut,
		std::string& error) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	struct addrinfo* result = NULL;
	const int gai = getaddrinfo(host.c_str(), kHttpsPort, &hints, &result);
	if (gai != 0) {
		error = "getaddrinfo(" + host + ") failed: " + gai_strerror(gai);
		return false;
	}
	if (result == NULL) {
		error = "getaddrinfo(" + host + ") returned no address";
		return false;
	}

	int lastError = 0;
	for (struct addrinfo* ai = result; ai != NULL; ai = ai->ai_next) {
		if (remainingMs(deadline) <= 0) {
			lastError = ETIMEDOUT;
			break;
		}
		int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) {
			lastError = errno;
			continue;
		}
		const int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
			lastError = errno;
			close(fd);
			continue;
		}

		int connectResult = connect(fd, ai->ai_addr, ai->ai_addrlen);
		if (connectResult != 0 && errno != EINPROGRESS &&
				errno != EWOULDBLOCK) {
			lastError = errno;
			close(fd);
			continue;
		}
		if (connectResult != 0) {
			const int waitMs = remainingMs(deadline);
			if (waitMs <= 0 || !waitWritable(fd, waitMs)) {
				lastError = ETIMEDOUT;
				close(fd);
				continue;
			}
			int socketError = 0;
			socklen_t socketErrorLength = sizeof(socketError);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError,
						&socketErrorLength) != 0 ||
					socketError != 0) {
				lastError = (socketError != 0) ? socketError : errno;
				close(fd);
				continue;
			}
		}

		// 恢复阻塞模式，由 SO_RCVTIMEO/SO_SNDTIMEO 提供超时。
		if (fcntl(fd, F_SETFL, flags) < 0) {
			lastError = errno;
			close(fd);
			continue;
		}
		const int waitMs = remainingMs(deadline);
		if (waitMs <= 0) {
			lastError = ETIMEDOUT;
			close(fd);
			continue;
		}
		if (!setTimeouts(fd, waitMs)) {
			lastError = errno;
			close(fd);
			continue;
		}
		fdOut = fd;
		freeaddrinfo(result);
		return true;
	}

	freeaddrinfo(result);
	error = "tcp connect " + host + ":" + kHttpsPort + " failed";
	if (lastError != 0) {
		error += " (errno=";
		error += std::to_string(lastError);
		error += ")";
	}
	return false;
}

std::string buildRequest(const std::string& host, const std::string& path) {
	std::string request;
	request.reserve(160);
	request += "GET ";
	request += path;
	request += " HTTP/1.0\r\nHost: ";
	request += host;
	request += "\r\nAccept: application/json\r\n";
	request += "User-Agent: MultiTimerTC002/3.0\r\n";
	request += "Connection: close\r\n\r\n";
	return request;
}

bool hasHeaderInjection(const std::string& text) {
	return text.find('\r') != std::string::npos ||
			text.find('\n') != std::string::npos;
}

void releaseTls(SSL*& ssl, SSL_CTX*& ctx, int& fd) {
	if (ssl != NULL) {
		gApi.SSL_shutdown(ssl);
		gApi.SSL_free(ssl);
		ssl = NULL;
	}
	if (ctx != NULL) {
		gApi.SSL_CTX_free(ctx);
		ctx = NULL;
	}
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
}

// OpenSSL 的 socket BIO 用 write()/send()，不带 MSG_NOSIGNAL。对端提前关闭连接时
// 内核会发 SIGPIPE，默认动作是杀死整个进程。这里只在当前线程屏蔽，并在退出前
// 回收本线程产生的 SIGPIPE；若调用方本来就屏蔽了 SIGPIPE，则不改动其状态。
class SigPipeGuard {
public:
	SigPipeGuard() : mBlocked(false) {
		sigemptyset(&mSignalSet);
		sigaddset(&mSignalSet, SIGPIPE);
		if (pthread_sigmask(SIG_BLOCK, &mSignalSet, &mOldMask) != 0) {
			return;
		}
		if (sigismember(&mOldMask, SIGPIPE) == 0) {
			mBlocked = true;
		} else {
			// 调用方已屏蔽：恢复原掩码，并且不要消费它可能已有的 pending 信号。
			pthread_sigmask(SIG_SETMASK, &mOldMask, NULL);
		}
	}

	~SigPipeGuard() {
		if (!mBlocked) {
			return;
		}
		sigset_t pending;
		if (sigpending(&pending) == 0 &&
				sigismember(&pending, SIGPIPE) == 1) {
			struct timespec zero;
			zero.tv_sec = 0;
			zero.tv_nsec = 0;
			sigtimedwait(&mSignalSet, NULL, &zero);
		}
		pthread_sigmask(SIG_SETMASK, &mOldMask, NULL);
	}

private:
	sigset_t mSignalSet;
	sigset_t mOldMask;
	bool mBlocked;

	SigPipeGuard(const SigPipeGuard&);
	SigPipeGuard& operator=(const SigPipeGuard&);
};

}  // namespace

TlsHttpResponse::TlsHttpResponse() : statusCode(0) {
}

bool TlsClient::available(std::string& error) {
	ensureApi();
	if (!gApi.ready) {
		error = gApi.error;
		return false;
	}
	error.clear();
	return true;
}

bool TlsClient::get(const std::string& host, const std::string& path,
		TlsHttpResponse& response, int timeoutMs) {
	response = TlsHttpResponse();
	SigPipeGuard sigPipeGuard;

	if (host.empty() || path.empty() || path[0] != '/') {
		response.error = "invalid request target (empty host or path)";
		return false;
	}
	if (hasHeaderInjection(host) || hasHeaderInjection(path)) {
		response.error = "invalid request target (CR/LF)";
		return false;
	}
	if (timeoutMs <= 0) {
		timeoutMs = 4000;
	}

	std::string apiError;
	if (!available(apiError)) {
		response.error = "tls unavailable: " + apiError;
		return false;
	}

	const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMs);

	int fd = -1;
	std::string networkError;
	if (!connectTcp(host, deadline, fd, networkError)) {
		response.error = networkError;
		return false;
	}

	SSL_CTX* ctx = NULL;
	SSL* ssl = NULL;
	ctx = gApi.SSL_CTX_new(gApi.TLS_client_method());
	if (ctx == NULL) {
		response.error = "SSL_CTX_new failed";
		releaseTls(ssl, ctx, fd);
		return false;
	}
	// 设备 OpenSSL 1.1.0i 的默认协议集合在部分构建里为空/不兼容；
	// 明确固定 TLS1.2（B站/抖音/快手均支持），避免 NO_PROTOCOLS_AVAILABLE。
	gApi.SSL_CTX_ctrl(ctx, kSslCtrlSetMinProtoVersion, kTls12Version, NULL);
	gApi.SSL_CTX_ctrl(ctx, kSslCtrlSetMaxProtoVersion, kTls12Version, NULL);
	gApi.SSL_CTX_set_cipher_list(ctx,
			"ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:"
			"ECDHE-RSA-CHACHA20-POLY1305:ECDHE-RSA-AES128-SHA256:"
			"ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:"
			"HIGH:!aNULL:!MD5:!RC4");

	if (gApi.SSL_CTX_load_verify_locations(ctx, kCaFile, NULL) != 1) {
		response.error = std::string("SSL_CTX_load_verify_locations(") +
				kCaFile + ") failed";
		releaseTls(ssl, ctx, fd);
		return false;
	}
	gApi.SSL_CTX_set_verify(ctx, kSslVerifyPeer, NULL);

	ssl = gApi.SSL_new(ctx);
	if (ssl == NULL) {
		response.error = "SSL_new failed";
		releaseTls(ssl, ctx, fd);
		return false;
	}
	if (gApi.SSL_set_fd(ssl, fd) != 1) {
		response.error = "SSL_set_fd failed";
		releaseTls(ssl, ctx, fd);
		return false;
	}
	if (gApi.SSL_set1_host(ssl, host.c_str()) != 1) {
		response.error = "SSL_set1_host failed (hostname verification)";
		releaseTls(ssl, ctx, fd);
		return false;
	}
	if (gApi.SSL_ctrl(ssl, kSslCtrlSetTlsextHostname,
				kTlsextNametypeHostName,
				const_cast<char*>(host.c_str())) != 1) {
		response.error = "SNI setup failed (SSL_ctrl host_name)";
		releaseTls(ssl, ctx, fd);
		return false;
	}

	const int connectResult = gApi.SSL_connect(ssl);
	if (connectResult != 1) {
		const int sslError = gApi.SSL_get_error(ssl, connectResult);
		const long verifyResult = gApi.SSL_get_verify_result(ssl);
		if (verifyResult != 0) {
			response.error = "certificate verify failed (X509_V_ERR=" +
					std::to_string(static_cast<long long>(verifyResult)) + ")";
		} else {
			response.error = "SSL_connect failed (SSL_get_error=" +
					std::to_string(sslError);
			if (errno != 0) {
				response.error += ", errno=" + std::to_string(errno);
			}
			const std::string detail = opensslErrorDetail();
			if (!detail.empty()) {
				response.error += ", " + detail;
			}
			response.error += ")";
		}
		releaseTls(ssl, ctx, fd);
		return false;
	}

	const long verifyResult = gApi.SSL_get_verify_result(ssl);
	if (verifyResult != 0) {
		response.error = "certificate verify failed (X509_V_ERR=" +
				std::to_string(static_cast<long long>(verifyResult)) + ")";
		releaseTls(ssl, ctx, fd);
		return false;
	}

	const std::string request = buildRequest(host, path);
	size_t sent = 0;
	std::string ioError;
	while (sent < request.size()) {
		if (remainingMs(deadline) <= 0) {
			ioError = "write timeout";
			break;
		}
		const int written = gApi.SSL_write(ssl, request.data() + sent,
				static_cast<int>(request.size() - sent));
		if (written > 0) {
			sent += static_cast<size_t>(written);
			continue;
		}
		const int sslError = gApi.SSL_get_error(ssl, written);
		if ((sslError == kSslErrorWantRead || sslError == kSslErrorWantWrite) &&
				remainingMs(deadline) > 0) {
			continue;
		}
		ioError = "SSL_write failed (SSL_get_error=" +
				std::to_string(sslError);
		if (errno != 0) {
			ioError += ", errno=" + std::to_string(errno);
		}
		ioError += ")";
		break;
	}

	std::string raw;
	if (ioError.empty()) {
		char buffer[2048];
		while (true) {
			if (remainingMs(deadline) <= 0) {
				ioError = "read timeout";
				break;
			}
			const int received = gApi.SSL_read(ssl, buffer,
					static_cast<int>(sizeof(buffer)));
			if (received > 0) {
				if (raw.size() + static_cast<size_t>(received) >
						bilibili::kMaxHttpResponseBytes) {
					ioError = "response exceeds 64 KiB limit";
					break;
				}
				raw.append(buffer, static_cast<size_t>(received));
				continue;
			}
			const int sslError = gApi.SSL_get_error(ssl, received);
			if (sslError == kSslErrorZeroReturn) {
				break;  // close_notify：正常结束
			}
			if (sslError == kSslErrorWantRead || sslError == kSslErrorWantWrite) {
				if (remainingMs(deadline) <= 0) {
					ioError = "read timeout";
					break;
				}
				continue;
			}
			if (sslError == kSslErrorSyscall && received == 0) {
				break;  // 对端直接关 TCP（未发 close_notify），按 EOF 处理
			}
			if (sslError == kSslErrorSyscall &&
					(errno == EAGAIN || errno == EWOULDBLOCK)) {
				if (remainingMs(deadline) <= 0) {
					ioError = "read timeout";
					break;
				}
				continue;
			}
			if (sslError == kSslErrorSsl) {
				ioError = "SSL_read failed (SSL protocol error)";
			} else {
				ioError = "SSL_read failed (SSL_get_error=" +
						std::to_string(sslError);
				if (errno != 0) {
					ioError += ", errno=" + std::to_string(errno);
				}
				ioError += ")";
			}
			break;
		}
	}

	releaseTls(ssl, ctx, fd);

	if (raw.empty()) {
		response.error = ioError.empty() ? "empty response" : ioError;
		return false;
	}

	bilibili::HttpResponse parsed;
	std::string parseError;
	if (!bilibili::parseHttpResponse(raw, parsed, parseError)) {
		response.error = ioError.empty() ? parseError
										 : (ioError + "; " + parseError);
		return false;
	}
	// 只有“读完 body 但对端没关连接”的超时可以容忍；TLS 协议错误/
	// 超限/写失败都可能是被截断或损坏的响应，不能当成成功。
	if (!ioError.empty() && ioError != "read timeout") {
		response.error = ioError + " (incomplete response rejected)";
		return false;
	}
	response.statusCode = parsed.statusCode;
	response.body.swap(parsed.body);
	return true;
}
