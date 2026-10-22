#include "net/BilibiliParse.h"

#include <limits.h>
#include <stdio.h>

#include <string>

#include "net/mini_json.h"

namespace {

std::string numberText(long long value) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%lld", value);
	return std::string(buf);
}

bool isAsciiSpace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trimAscii(const std::string& text) {
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && isAsciiSpace(text[begin])) {
		++begin;
	}
	while (end > begin && isAsciiSpace(text[end - 1])) {
		--end;
	}
	return text.substr(begin, end - begin);
}

std::string lowerAscii(const std::string& text) {
	std::string out = text;
	for (size_t i = 0; i < out.size(); ++i) {
		if (out[i] >= 'A' && out[i] <= 'Z') {
			out[i] = static_cast<char>(out[i] - 'A' + 'a');
		}
	}
	return out;
}

// 失败时统一清空结果并写 error，避免调用方看到半填充的数据。
bool failRoom(BilibiliRoomData& out, const std::string& error) {
	out.clear();
	out.ok = false;
	out.error = error;
	return false;
}

// JsonValue 的数字统一按 long long 取；非数字由调用方先判断。
bool requireNumber(const JsonValue& object, const char* key,
		const char* path, long long& value, std::string& error) {
	const JsonValue& field = object[key];
	if (!field.isNumber()) {
		error = std::string("missing numeric field ") + path;
		return false;
	}
	value = field.asInt64();
	return true;
}

bool requireString(const JsonValue& object, const char* key,
		const char* path, std::string& value, std::string& error) {
	const JsonValue& field = object[key];
	if (!field.isString()) {
		error = std::string("missing string field ") + path;
		return false;
	}
	value = field.asString();
	return true;
}

// B 站错误码：优先 msg，其次 message。
std::string apiMessage(const JsonValue& root) {
	std::string msg = root["msg"].asString();
	if (msg.empty()) {
		msg = root["message"].asString();
	}
	return msg;
}

void parseApiError(const JsonValue& root, long long code, std::string& error) {
	error = "api code " + numberText(code);
	const std::string msg = apiMessage(root);
	if (!msg.empty()) {
		error += ": ";
		error += msg;
	}
}

int hexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

bool parseSizeDecimal(const std::string& text, size_t& value) {
	if (text.empty()) {
		return false;
	}
	size_t result = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c < '0' || c > '9') {
			return false;
		}
		const size_t digit = static_cast<size_t>(c - '0');
		if (result > (static_cast<size_t>(-1) - digit) / 10U) {
			return false;
		}
		result = result * 10U + digit;
	}
	value = result;
	return true;
}

// 取一行（不含行尾）。返回 false 表示没有找到换行符。
bool nextLine(const std::string& raw, size_t& pos, std::string& line) {
	const size_t eol = raw.find('\n', pos);
	if (eol == std::string::npos) {
		return false;
	}
	line = raw.substr(pos, eol - pos);
	if (!line.empty() && line[line.size() - 1] == '\r') {
		line.erase(line.size() - 1);
	}
	pos = eol + 1;
	return true;
}

}  // namespace

BilibiliRoomData::BilibiliRoomData()
		: ok(false), online(0), likes(0), fans(0), liveStatus(0) {
}

void BilibiliRoomData::clear() {
	ok = false;
	online = 0;
	likes = 0;
	fans = 0;
	liveStatus = 0;
	title.clear();
	error.clear();
}

namespace bilibili {

bool isValidRoomId(const std::string& roomId) {
	if (roomId.empty() || roomId.size() > 20) {
		return false;
	}
	for (size_t i = 0; i < roomId.size(); ++i) {
		if (roomId[i] < '0' || roomId[i] > '9') {
			return false;
		}
	}
	return true;
}

bool parseRoomJson(const std::string& body, BilibiliRoomData& out) {
	out.clear();

	JsonValue root;
	std::string jsonError;
	if (!JsonValue::parse(body, root, jsonError)) {
		return failRoom(out, "json: " + jsonError);
	}
	if (!root.isObject()) {
		return failRoom(out, "json: root is not an object");
	}

	long long code = 0;
	std::string error;
	if (!requireNumber(root, "code", "code", code, error)) {
		return failRoom(out, "json: " + error);
	}
	if (code != 0) {
		parseApiError(root, code, error);
		return failRoom(out, error);
	}

	const JsonValue& data = root["data"];
	if (!data.isObject()) {
		return failRoom(out, "json: data is not an object");
	}

	const JsonValue& room = data["room_info"];
	if (!room.isObject()) {
		return failRoom(out, "json: missing data.room_info object");
	}

	BilibiliRoomData parsed;
	if (!requireString(room, "title", "data.room_info.title", parsed.title,
				error)) {
		return failRoom(out, "json: " + error);
	}
	long long liveStatus = 0;
	if (!requireNumber(room, "live_status", "data.room_info.live_status",
				liveStatus, error)) {
		return failRoom(out, "json: " + error);
	}
	if (!requireNumber(room, "online", "data.room_info.online", parsed.online,
				error)) {
		return failRoom(out, "json: " + error);
	}
	if (!requireNumber(data["like_info_v3"], "total_likes",
				"data.like_info_v3.total_likes", parsed.likes, error)) {
		return failRoom(out, "json: " + error);
	}
	if (!requireNumber(data["anchor_info"]["relation_info"], "attention",
				"data.anchor_info.relation_info.attention", parsed.fans,
				error)) {
		return failRoom(out, "json: " + error);
	}

	parsed.liveStatus = static_cast<int>(liveStatus);
	parsed.ok = true;
	parsed.error.clear();
	out = parsed;
	return true;
}

bool parseRoomInfoJson(const std::string& body, BilibiliRoomData& out) {
	out.clear();

	JsonValue root;
	std::string jsonError;
	if (!JsonValue::parse(body, root, jsonError)) {
		return failRoom(out, "json: " + jsonError);
	}
	if (!root.isObject()) {
		return failRoom(out, "json: root is not an object");
	}

	long long code = 0;
	std::string error;
	if (!requireNumber(root, "code", "code", code, error)) {
		return failRoom(out, "json: " + error);
	}
	if (code != 0) {
		parseApiError(root, code, error);
		return failRoom(out, error);
	}

	const JsonValue& data = root["data"];
	if (!data.isObject()) {
		return failRoom(out, "json: data is not an object");
	}

	BilibiliRoomData parsed;
	if (!requireString(data, "title", "data.title", parsed.title, error)) {
		return failRoom(out, "json: " + error);
	}
	long long liveStatus = 0;
	if (!requireNumber(data, "live_status", "data.live_status", liveStatus,
				error)) {
		return failRoom(out, "json: " + error);
	}
	if (!requireNumber(data, "online", "data.online", parsed.online, error)) {
		return failRoom(out, "json: " + error);
	}
	if (!requireNumber(data, "attention", "data.attention", parsed.fans,
				error)) {
		return failRoom(out, "json: " + error);
	}

	parsed.liveStatus = static_cast<int>(liveStatus);
	parsed.likes = 0;  // get_info 不提供本场点赞
	parsed.ok = true;
	parsed.error.clear();
	out = parsed;
	return true;
}

HttpResponse::HttpResponse() : statusCode(0) {
}

bool decodeChunked(const std::string& raw, std::string& out,
		std::string& error) {
	error.clear();
	size_t pos = 0;
	while (true) {
		std::string sizeLine;
		if (!nextLine(raw, pos, sizeLine)) {
			error = "chunked: truncated chunk size line";
			return false;
		}
		const size_t semicolon = sizeLine.find(';');  // chunk 扩展
		if (semicolon != std::string::npos) {
			sizeLine.erase(semicolon);
		}
		sizeLine = trimAscii(sizeLine);
		if (sizeLine.empty()) {
			error = "chunked: empty chunk size";
			return false;
		}

		unsigned long long chunkSize = 0;
		for (size_t i = 0; i < sizeLine.size(); ++i) {
			const int digit = hexValue(sizeLine[i]);
			if (digit < 0) {
				error = "chunked: invalid hex digit in chunk size";
				return false;
			}
			if (chunkSize >
					(ULLONG_MAX - static_cast<unsigned long long>(digit)) /
							16ULL) {
				error = "chunked: chunk size overflow";
				return false;
			}
			chunkSize = chunkSize * 16ULL +
					static_cast<unsigned long long>(digit);
		}

		if (chunkSize == 0) {
			return true;  // 结束块；trailer 一律忽略
		}
		if (chunkSize > static_cast<unsigned long long>(
								kMaxHttpResponseBytes) ||
				out.size() + static_cast<size_t>(chunkSize) >
						kMaxHttpResponseBytes) {
			error = "chunked: body exceeds 64 KiB limit";
			return false;
		}
		if (chunkSize > static_cast<unsigned long long>(raw.size() - pos)) {
			error = "chunked: truncated chunk data";
			return false;
		}

		out.append(raw, pos, static_cast<size_t>(chunkSize));
		pos += static_cast<size_t>(chunkSize);

		// 每个数据块后必须跟 CRLF（宽容裸 LF）。
		if (pos < raw.size() && raw[pos] == '\r') {
			++pos;
		}
		if (pos < raw.size() && raw[pos] == '\n') {
			++pos;
		} else {
			error = "chunked: missing CRLF after chunk data";
			return false;
		}
	}
}

bool parseHttpResponse(const std::string& raw, HttpResponse& out,
		std::string& error) {
	out = HttpResponse();
	error.clear();

	if (raw.empty()) {
		error = "http: empty response";
		return false;
	}

	size_t pos = 0;
	std::string statusLine;
	if (!nextLine(raw, pos, statusLine)) {
		error = "http: incomplete status line";
		return false;
	}
	if (statusLine.size() < 8 || statusLine.compare(0, 5, "HTTP/") != 0 ||
			statusLine[5] < '0' || statusLine[5] > '9') {
		error = "http: malformed status line";
		return false;
	}
	const size_t firstSpace = statusLine.find(' ');
	if (firstSpace == std::string::npos || firstSpace <= 5) {
		error = "http: malformed status line";
		return false;
	}
	size_t codePos = firstSpace + 1;
	while (codePos < statusLine.size() && statusLine[codePos] == ' ') {
		++codePos;
	}
	if (codePos + 3 > statusLine.size()) {
		error = "http: missing status code";
		return false;
	}
	if (codePos + 3 < statusLine.size() && statusLine[codePos + 3] != ' ') {
		error = "http: malformed status code";
		return false;
	}
	for (size_t i = 0; i < 3; ++i) {
		if (statusLine[codePos + i] < '0' || statusLine[codePos + i] > '9') {
			error = "http: malformed status code";
			return false;
		}
	}
	const int statusCode = (statusLine[codePos] - '0') * 100 +
			(statusLine[codePos + 1] - '0') * 10 +
			(statusLine[codePos + 2] - '0');
	if (statusCode < 100 || statusCode > 599) {
		error = "http: status code out of range";
		return false;
	}

	bool chunked = false;
	bool hasContentLength = false;
	size_t contentLength = 0;
	bool headersDone = false;
	while (pos < raw.size()) {
		std::string line;
		if (!nextLine(raw, pos, line)) {
			error = "http: incomplete headers";
			return false;
		}
		if (line.empty()) {
			headersDone = true;
			break;
		}
		const size_t colon = line.find(':');
		if (colon == std::string::npos) {
			error = "http: malformed header line";
			return false;
		}
		const std::string name = lowerAscii(trimAscii(line.substr(0, colon)));
		const std::string value = trimAscii(line.substr(colon + 1));
		if (name == "transfer-encoding") {
			size_t begin = 0;
			while (begin <= value.size()) {
				const size_t comma = value.find(',', begin);
				const size_t end = (comma == std::string::npos)
						? value.size()
						: comma;
				const std::string token =
						lowerAscii(trimAscii(value.substr(begin, end - begin)));
				if (token == "chunked") {
					chunked = true;
				}
				if (comma == std::string::npos) {
					break;
				}
				begin = comma + 1;
			}
		} else if (name == "content-length") {
			size_t parsedLength = 0;
			if (!parseSizeDecimal(value, parsedLength)) {
				error = "http: invalid content-length";
				return false;
			}
			if (hasContentLength && parsedLength != contentLength) {
				error = "http: conflicting content-length";
				return false;
			}
			hasContentLength = true;
			contentLength = parsedLength;
		}
	}
	if (!headersDone) {
		error = "http: incomplete headers";
		return false;
	}

	std::string body = raw.substr(pos);
	if (chunked) {
		std::string decoded;
		if (!decodeChunked(body, decoded, error)) {
			return false;
		}
		body.swap(decoded);
	} else if (hasContentLength) {
		if (contentLength > kMaxHttpResponseBytes) {
			error = "http: content-length exceeds 64 KiB limit";
			return false;
		}
		if (body.size() < contentLength) {
			error = "http: truncated body";
			return false;
		}
		body.resize(contentLength);
	} else if (body.size() > kMaxHttpResponseBytes) {
		error = "http: body exceeds 64 KiB limit";
		return false;
	}

	out.statusCode = statusCode;
	out.body.swap(body);
	return true;
}

}  // namespace bilibili
