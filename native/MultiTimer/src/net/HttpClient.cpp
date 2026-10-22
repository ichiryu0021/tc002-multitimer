#include "net/HttpClient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>

namespace {

// CLOCK_MONOTONIC 毫秒：HTTP 超时不受 SNTP 跳时影响
long long monotonicMillis() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return static_cast<long long>(ts.tv_sec) * 1000LL +
			static_cast<long long>(ts.tv_nsec) / 1000000LL;
}

// 距离截止时间还剩多少毫秒；<=0 表示已超时
int remainingMillis(long long deadlineMs) {
	const long long left = deadlineMs - monotonicMillis();
	if (left <= 0) {
		return 0;
	}
	return static_cast<int>(left > 60000LL ? 60000LL : left);
}

std::string lowerCopy(const std::string& text) {
	std::string out = text;
	for (size_t i = 0; i < out.size(); ++i) {
		if (out[i] >= 'A' && out[i] <= 'Z') {
			out[i] = static_cast<char>(out[i] - 'A' + 'a');
		}
	}
	return out;
}

std::string trimToken(const std::string& text) {
	size_t begin = 0;
	while (begin < text.size() &&
			(text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
		++begin;
	}
	size_t end = text.size();
	while (end > begin &&
			(text[end - 1] == ' ' || text[end - 1] == '\t' ||
					text[end - 1] == '\r')) {
		--end;
	}
	return text.substr(begin, end - begin);
}

bool isHostChar(char c) {
	const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	const bool digit = c >= '0' && c <= '9';
	return alpha || digit || c == '.' || c == '-' || c == '_';
}

bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

// 非阻塞 connect + poll，返回已连接（阻塞模式）的 fd；失败返回 -1
int connectTcp(const std::string& host, int port, long long deadlineMs,
		std::string& error) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char portText[8];
	snprintf(portText, sizeof(portText), "%d", port);

	struct addrinfo* result = NULL;
	const int rc = getaddrinfo(host.c_str(), portText, &hints, &result);
	if (rc != 0 || result == NULL) {
		error = std::string("dns failed: ") + gai_strerror(rc);
		return -1;
	}

	int fd = -1;
	for (struct addrinfo* ai = result; ai != NULL; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) {
			continue;
		}
		const int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
			close(fd);
			fd = -1;
			error = "fcntl failed";
			continue;
		}

		int connectResult = connect(fd, ai->ai_addr, ai->ai_addrlen);
		if (connectResult != 0 && errno != EINPROGRESS) {
			close(fd);
			fd = -1;
			error = std::string("connect failed: ") + strerror(errno);
			continue;
		}
		if (connectResult != 0) {
			const int timeout = remainingMillis(deadlineMs);
			if (timeout <= 0) {
				close(fd);
				fd = -1;
				error = "connect timeout";
				break;
			}
			struct pollfd pfd;
			pfd.fd = fd;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			const int polled = poll(&pfd, 1, timeout);
			if (polled == 0) {
				close(fd);
				fd = -1;
				error = "connect timeout";
				continue;
			}
			if (polled < 0) {
				close(fd);
				fd = -1;
				error = std::string("connect poll failed: ") + strerror(errno);
				continue;
			}
			int socketError = 0;
			socklen_t length = sizeof(socketError);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) !=
					0 || socketError != 0) {
				const int saved = socketError != 0 ? socketError : errno;
				close(fd);
				fd = -1;
				error = std::string("connect failed: ") + strerror(saved);
				continue;
			}
		}
		// 后续用 poll 控制收发超时，socket 恢复阻塞模式便于 send/recv 语义
		fcntl(fd, F_SETFL, flags);
		error.clear();
		break;
	}

	freeaddrinfo(result);
	return fd;
}

bool sendAll(int fd, const std::string& data, long long deadlineMs,
		std::string& error) {
	size_t sent = 0;
	while (sent < data.size()) {
		const int timeout = remainingMillis(deadlineMs);
		if (timeout <= 0) {
			error = "send timeout";
			return false;
		}
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLOUT;
		pfd.revents = 0;
		const int polled = poll(&pfd, 1, timeout);
		if (polled == 0) {
			error = "send timeout";
			return false;
		}
		if (polled < 0) {
			if (errno == EINTR) {
				continue;
			}
			error = std::string("send poll failed: ") + strerror(errno);
			return false;
		}
		const ssize_t written = send(fd, data.data() + sent,
				data.size() - sent, MSG_NOSIGNAL);
		if (written > 0) {
			sent += static_cast<size_t>(written);
			continue;
		}
		if (written < 0 &&
				(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
			continue;
		}
		error = std::string("send failed: ") + strerror(errno);
		return false;
	}
	return true;
}

// 读到 EOF（服务端 Connection: close）；超过 maxBytes 判失败
bool recvUntilClose(int fd, size_t maxBytes, long long deadlineMs,
		std::string& raw, std::string& error) {
	char buffer[2048];
	while (true) {
		const int timeout = remainingMillis(deadlineMs);
		if (timeout <= 0) {
			error = "recv timeout";
			return false;
		}
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		const int polled = poll(&pfd, 1, timeout);
		if (polled == 0) {
			error = "recv timeout";
			return false;
		}
		if (polled < 0) {
			if (errno == EINTR) {
				continue;
			}
			error = std::string("recv poll failed: ") + strerror(errno);
			return false;
		}
		const ssize_t readBytes = recv(fd, buffer, sizeof(buffer), 0);
		if (readBytes > 0) {
			raw.append(buffer, static_cast<size_t>(readBytes));
			if (raw.size() > maxBytes) {
				error = "response too large";
				return false;
			}
			continue;
		}
		if (readBytes == 0) {
			return true;
		}
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
			continue;
		}
		error = std::string("recv failed: ") + strerror(errno);
		return false;
	}
}

// 解 chunked body（支持 chunk 扩展与 trailer）；out 追加解码结果
bool decodeChunked(const std::string& raw, std::string& out,
		std::string& error) {
	size_t pos = 0;
	while (true) {
		const size_t lineEnd = raw.find('\n', pos);
		if (lineEnd == std::string::npos) {
			error = "chunked: missing size line";
			return false;
		}
		std::string line = raw.substr(pos, lineEnd - pos);
		if (!line.empty() && line[line.size() - 1] == '\r') {
			line.erase(line.size() - 1);
		}
		const size_t semicolon = line.find(';');
		if (semicolon != std::string::npos) {
			line = line.substr(0, semicolon);
		}
		line = trimToken(line);
		if (line.empty()) {
			error = "chunked: empty size line";
			return false;
		}
		char* end = NULL;
		const unsigned long size = strtoul(line.c_str(), &end, 16);
		if (end == line.c_str() || (end != NULL && *end != '\0')) {
			error = "chunked: invalid size";
			return false;
		}
		pos = lineEnd + 1;
		if (size == 0) {
			// 末块：忽略 trailer（如果有）
			return true;
		}
		if (pos + size > raw.size()) {
			error = "chunked: truncated body";
			return false;
		}
		out.append(raw, pos, size);
		pos += size;
		if (pos + 2 <= raw.size() && raw[pos] == '\r' && raw[pos + 1] == '\n') {
			pos += 2;
		} else if (pos < raw.size() && raw[pos] == '\n') {
			pos += 1;
		} else {
			error = "chunked: missing CRLF";
			return false;
		}
	}
}

bool containsToken(const std::string& value, const std::string& token) {
	return lowerCopy(value).find(token) != std::string::npos;
}

}  // namespace

HttpClientResponse::HttpClientResponse() : statusCode(0) {
}

void HttpClientResponse::clear() {
	statusCode = 0;
	body.clear();
	error.clear();
}

HttpClient::HttpClient() {
}

// static
bool HttpClient::parseUrl(const std::string& url, std::string& host, int& port,
		std::string& path, std::string& error) {
	host.clear();
	path.clear();
	port = 80;
	error.clear();

	static const char* kScheme = "http://";
	const size_t schemeLength = 7;
	if (url.size() <= schemeLength ||
			url.compare(0, schemeLength, kScheme) != 0) {
		error = "only http:// is supported";
		return false;
	}
	for (size_t i = 0; i < url.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(url[i]);
		if (c <= 0x20 || c == 0x7F) {
			error = "url contains whitespace/control characters";
			return false;
		}
	}

	size_t pos = schemeLength;
	const size_t hostEnd = url.find_first_of(":/?", pos);
	const size_t end = (hostEnd == std::string::npos) ? url.size() : hostEnd;
	host = url.substr(pos, end - pos);
	if (host.empty()) {
		error = "missing host";
		return false;
	}
	if (host.size() > 253) {
		error = "host too long";
		return false;
	}
	for (size_t i = 0; i < host.size(); ++i) {
		if (!isHostChar(host[i])) {
			error = "invalid host character";
			return false;
		}
	}
	pos = end;

	if (pos < url.size() && url[pos] == ':') {
		++pos;
		const size_t portStart = pos;
		while (pos < url.size() && isDigit(url[pos])) {
			++pos;
		}
		if (pos == portStart || pos - portStart > 5) {
			error = "invalid port";
			return false;
		}
		port = atoi(url.substr(portStart, pos - portStart).c_str());
		if (port < 1 || port > 65535) {
			error = "port out of range";
			return false;
		}
	}

	if (pos == url.size()) {
		path = "/";
	} else if (url[pos] == '/') {
		path = url.substr(pos);
	} else if (url[pos] == '?') {
		path = "/" + url.substr(pos);
	} else {
		error = "invalid url path";
		return false;
	}
	if (path.size() > 512) {
		error = "url path too long";
		return false;
	}
	for (size_t i = 0; i < path.size(); ++i) {
		if (path[i] == '\r' || path[i] == '\n') {
			error = "url path contains CR/LF";
			return false;
		}
	}
	return true;
}

// static
bool HttpClient::parseResponse(const std::string& raw,
		HttpClientResponse& out) {
	out.clear();

	size_t headerEnd = raw.find("\r\n\r\n");
	size_t separatorLength = 4;
	if (headerEnd == std::string::npos) {
		headerEnd = raw.find("\n\n");
		separatorLength = 2;
	}
	if (headerEnd == std::string::npos) {
		out.error = "malformed http response (no header end)";
		return false;
	}
	const std::string head = raw.substr(0, headerEnd);
	const std::string body = raw.substr(headerEnd + separatorLength);

	const size_t firstLineEnd = head.find('\n');
	const std::string statusLine = trimToken(
			firstLineEnd == std::string::npos ? head
					: head.substr(0, firstLineEnd));
	if (statusLine.compare(0, 5, "HTTP/") != 0) {
		out.error = "malformed http status line";
		return false;
	}
	const size_t firstSpace = statusLine.find(' ');
	if (firstSpace == std::string::npos || firstSpace + 3 >= statusLine.size()) {
		out.error = "malformed http status line";
		return false;
	}
	const std::string codeText = statusLine.substr(firstSpace + 1, 3);
	if (!isDigit(codeText[0]) || !isDigit(codeText[1]) ||
			!isDigit(codeText[2])) {
		out.error = "malformed http status code";
		return false;
	}
	out.statusCode = atoi(codeText.c_str());

	std::map<std::string, std::string> headers;
	size_t pos = (firstLineEnd == std::string::npos) ? head.size()
			: firstLineEnd + 1;
	while (pos < head.size()) {
		size_t lineEnd = head.find('\n', pos);
		if (lineEnd == std::string::npos) {
			lineEnd = head.size();
		}
		const std::string line = head.substr(pos, lineEnd - pos);
		pos = lineEnd + 1;
		const size_t colon = line.find(':');
		if (colon == std::string::npos) {
			continue;
		}
		const std::string key = lowerCopy(trimToken(line.substr(0, colon)));
		const std::string value = trimToken(line.substr(colon + 1));
		if (!key.empty() && headers.find(key) == headers.end()) {
			headers[key] = value;
		}
	}

	std::map<std::string, std::string>::const_iterator transfer =
			headers.find("transfer-encoding");
	const bool chunked = transfer != headers.end() &&
			containsToken(transfer->second, "chunked");
	if (chunked) {
		std::string decoded;
		std::string error;
		if (!decodeChunked(body, decoded, error)) {
			out.error = error;
			out.statusCode = 0;
			return false;
		}
		out.body.swap(decoded);
		return true;
	}

	std::map<std::string, std::string>::const_iterator length =
			headers.find("content-length");
	if (length != headers.end()) {
		char* end = NULL;
		const unsigned long expected =
				strtoul(length->second.c_str(), &end, 10);
		if (end == length->second.c_str() ||
				(end != NULL && *end != '\0')) {
			out.error = "invalid content-length";
			out.statusCode = 0;
			return false;
		}
		if (body.size() < expected) {
			out.error = "short http body";
			out.statusCode = 0;
			return false;
		}
		out.body = body.substr(0, expected);
		return true;
	}

	// HTTP/1.0 无长度：body 即剩余字节（服务端 Connection: close）
	out.body = body;
	return true;
}

// static
bool HttpClient::get(const std::string& url, int timeoutMs,
		size_t maxBodyBytes, HttpClientResponse& out) {
	out.clear();

	std::string host;
	std::string path;
	std::string error;
	int port = 80;
	if (!parseUrl(url, host, port, path, error)) {
		out.error = error;
		return false;
	}
	if (timeoutMs < 100) {
		timeoutMs = 100;
	}
	const long long deadline = monotonicMillis() + timeoutMs;

	const int fd = connectTcp(host, port, deadline, error);
	if (fd < 0) {
		out.error = error.empty() ? "connect failed" : error;
		return false;
	}

	std::string request = "GET " + path + " HTTP/1.0\r\n";
	request += "Host: " + host;
	if (port != 80) {
		char portText[8];
		snprintf(portText, sizeof(portText), ":%d", port);
		request += portText;
	}
	request += "\r\n";
	request += "Accept: application/json\r\n";
	request += "User-Agent: MultiTimer/3\r\n";
	request += "Connection: close\r\n\r\n";

	if (!sendAll(fd, request, deadline, error)) {
		close(fd);
		out.error = error;
		return false;
	}

	std::string raw;
	if (!recvUntilClose(fd, maxBodyBytes, deadline, raw, error)) {
		close(fd);
		out.error = error;
		return false;
	}
	close(fd);

	if (!parseResponse(raw, out)) {
		return false;
	}
	return true;
}
