#include "net/BilibiliClient.h"

#include <string>

#include "base/log.h"
#include "net/TlsClient.h"

namespace {

const char* const kApiHost = "api.live.bilibili.com";
const char* const kH5RoomPath =
		"/xlive/web-room/v1/index/getH5InfoByRoom?room_id=";
const char* const kRoomInfoPath = "/room/v1/Room/get_info?room_id=";
const int kDefaultTimeoutMs = 4000;

bool requestRoom(const std::string& path, bool h5Endpoint, int timeoutMs,
		BilibiliRoomData& out, std::string& error) {
	TlsHttpResponse response;
	if (!TlsClient::get(kApiHost, path, response, timeoutMs)) {
		error = response.error;
		return false;
	}
	if (response.statusCode != 200) {
		error = "http status " + std::to_string(response.statusCode);
		return false;
	}
	const bool parsed = h5Endpoint
			? bilibili::parseRoomJson(response.body, out)
			: bilibili::parseRoomInfoJson(response.body, out);
	if (!parsed) {
		error = out.error;
		return false;
	}
	return true;
}

}  // namespace

// static
bool BilibiliClient::fetchRoom(const std::string& roomId,
		BilibiliRoomData& out, int timeoutMs) {
	out.clear();
	if (!bilibili::isValidRoomId(roomId)) {
		out.ok = false;
		out.error = "invalid room id (expected 1-20 ASCII digits)";
		return false;
	}
	if (timeoutMs <= 0) {
		timeoutMs = kDefaultTimeoutMs;
	}

	BilibiliRoomData primary;
	std::string primaryError;
	if (requestRoom(std::string(kH5RoomPath) + roomId, true, timeoutMs,
				primary, primaryError)) {
		out = primary;
		return true;
	}

	BilibiliRoomData fallback;
	std::string fallbackError;
	if (requestRoom(std::string(kRoomInfoPath) + roomId, false, timeoutMs,
				fallback, fallbackError)) {
		out = fallback;
		return true;
	}

	out.clear();
	out.ok = false;
	out.error = "getH5InfoByRoom: " + primaryError + "; get_info: " +
			fallbackError;
	LOGW_TRACE("BilibiliClient: room %s failed: %s", roomId.c_str(),
			out.error.c_str());
	return false;
}
