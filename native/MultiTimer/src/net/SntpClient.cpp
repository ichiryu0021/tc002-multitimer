#include "net/SntpClient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <mutex>
#include <string>

namespace {

const unsigned long long kNtpEpochOffset = 2208988800ULL;  // 1900-01-01 -> 1970-01-01
const size_t kNtpPacketSize = 48;

const char* kServers[] = {
	"ntp.aliyun.com",
	"cn.pool.ntp.org",
	"pool.ntp.org",
	"time.cloudflare.com",
};

std::mutex gServerMutex;
std::string gCustomServer;

// "host" / "host:port" -> host + port；裸 IPv6 不支持。
bool splitServer(const std::string& server, std::string& host, std::string& port) {
	host = server;
	port = "123";
	const size_t colon = server.rfind(':');
	if (colon != std::string::npos) {
		if (server.find(':') != colon) {
			return false;
		}
		host = server.substr(0, colon);
		port = server.substr(colon + 1);
	}
	return !host.empty() && !port.empty();
}

// 读取大端 32 位
unsigned long readU32(const unsigned char* p) {
	return (static_cast<unsigned long>(p[0]) << 24) |
			(static_cast<unsigned long>(p[1]) << 16) |
			(static_cast<unsigned long>(p[2]) << 8) |
			static_cast<unsigned long>(p[3]);
}

bool queryHost(const std::string& host, const std::string& port, int timeoutMs,
		long long& epochOut) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	struct addrinfo* result = NULL;
	if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 ||
			result == NULL) {
		return false;
	}

	bool ok = false;
	for (struct addrinfo* ai = result; ai != NULL && !ok; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) {
			continue;
		}

		unsigned char request[kNtpPacketSize];
		memset(request, 0, sizeof(request));
		request[0] = 0x1B;  // LI=0, VN=3, Mode=3 (client)

		if (sendto(fd, request, sizeof(request), 0, ai->ai_addr,
				ai->ai_addrlen) != static_cast<ssize_t>(sizeof(request))) {
			close(fd);
			continue;
		}

		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;
		int pr = poll(&pfd, 1, timeoutMs);
		if (pr <= 0) {
			close(fd);
			continue;
		}

		unsigned char response[kNtpPacketSize];
		ssize_t n = recvfrom(fd, response, sizeof(response), 0, NULL, NULL);
		close(fd);
		if (n < static_cast<ssize_t>(kNtpPacketSize)) {
			continue;
		}
		// mode 4 = server；stratum 0 是 kiss-o'-death，不能采信
		if ((response[0] & 0x07) != 4 || response[1] == 0) {
			continue;
		}

		unsigned long secs = readU32(&response[40]);
		unsigned long frac = readU32(&response[44]);
		if (secs < kNtpEpochOffset) {
			continue;
		}
		long long epoch = static_cast<long long>(secs - kNtpEpochOffset);
		// 用小数部分四舍五入到秒，避免接近整秒时差 1 秒
		if (frac >= 0x80000000UL) {
			++epoch;
		}
		if (epoch < 1700000000LL) {  // 早于 2023-11，明显异常
			continue;
		}
		epochOut = epoch;
		ok = true;
	}

	freeaddrinfo(result);
	return ok;
}

}  // namespace

// static
void SntpClient::setServer(const std::string& server) {
	std::lock_guard<std::mutex> lock(gServerMutex);
	gCustomServer = server;
}

// static
std::string SntpClient::server() {
	std::lock_guard<std::mutex> lock(gServerMutex);
	return gCustomServer;
}

// static
bool SntpClient::query(long long& epochOut, int timeoutMs) {
	std::string custom;
	{
		std::lock_guard<std::mutex> lock(gServerMutex);
		custom = gCustomServer;
	}

	// 自定义服务器优先；失败后回退内置列表，避免用户填错后完全不对时。
	if (!custom.empty()) {
		std::string host;
		std::string port;
		if (splitServer(custom, host, port) &&
				queryHost(host, port, timeoutMs, epochOut)) {
			return true;
		}
	}

	for (size_t i = 0; i < sizeof(kServers) / sizeof(kServers[0]); ++i) {
		if (queryHost(kServers[i], "123", timeoutMs, epochOut)) {
			return true;
		}
	}
	return false;
}

// static
bool SntpClient::syncNow(int timeoutMs) {
	long long epoch = 0;
	if (!query(epoch, timeoutMs)) {
		return false;
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	long long now = static_cast<long long>(tv.tv_sec);
	long long delta = epoch - now;
	if (delta < 0) {
		delta = -delta;
	}
	if (delta <= 1) {
		return true;  // 已经足够准确
	}

	tv.tv_sec = static_cast<time_t>(epoch);
	tv.tv_usec = 0;
	return settimeofday(&tv, NULL) == 0;
}
