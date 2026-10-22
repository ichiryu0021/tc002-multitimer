// Host tests for net/BilibiliParse + net/BilibiliClient (pure C++11, no network).
//
// The platform TLS code (TlsClient.cpp / BilibiliClient.cpp) is intentionally
// not compiled here: it needs POSIX sockets and dlopen. These tests cover the
// logic that decides success/failure and fills BilibiliRoomData, plus HTTP
// status/chunked parsing, using fixtures shaped like the real measured payloads
// (docs/RESEARCH-live-apis.md §2).
#include "test_framework.h"

#include <string>
#include <vector>

#include "net/BilibiliClient.h"
#include "net/BilibiliParse.h"

namespace {

std::string h5RoomJson() {
	return R"({
		"code": 0, "message": "OK", "ttl": 1,
		"data": {
			"room_info": {
				"uid": 50329118, "room_id": 7734200,
				"title": "【预告】24号1点全球总决赛瑞士轮",
				"live_status": 1, "online": 198882,
				"area_name": "游戏赛事"
			},
			"anchor_info": {
				"base_info": {"uname": "哔哩哔哩英雄联盟赛事"},
				"relation_info": {"attention": 16896337}
			},
			"watched_show": {"num": 198882, "text_large": "19.8万人气"},
			"like_info_v3": {"total_likes": 4044047}
		}
	})";
}

std::string infoRoomJson() {
	return R"({
		"code": 0, "msg": "ok", "message": "ok",
		"data": {
			"uid": 50329118, "room_id": 7734200, "short_id": 6,
			"attention": 16896337, "online": 198882,
			"live_status": 1,
			"title": "【预告】24号1点全球总决赛瑞士轮",
			"area_name": "游戏赛事", "live_time": "2026-10-15 15:50:15"
		}
	})";
}

bool contains(const std::string& text, const std::string& needle) {
	return text.find(needle) != std::string::npos;
}

}  // namespace

TF_TEST(bilibili_room_id_validation) {
	TF_CHECK(bilibili::isValidRoomId("6"));   // 短号
	TF_CHECK(bilibili::isValidRoomId("21452505"));
	TF_CHECK(bilibili::isValidRoomId("0006"));
	TF_CHECK(bilibili::isValidRoomId("12345678901234567890"));  // 20 digits

	TF_CHECK(!bilibili::isValidRoomId(""));
	TF_CHECK(!bilibili::isValidRoomId("123456789012345678901"));  // 21 digits
	TF_CHECK(!bilibili::isValidRoomId(" 6"));
	TF_CHECK(!bilibili::isValidRoomId("6 "));
	TF_CHECK(!bilibili::isValidRoomId("-1"));
	TF_CHECK(!bilibili::isValidRoomId("1.5"));
	TF_CHECK(!bilibili::isValidRoomId("12a"));
	TF_CHECK(!bilibili::isValidRoomId("abc"));
	TF_CHECK(!bilibili::isValidRoomId("6\r\nHost: evil"));
	TF_CHECK(!bilibili::isValidRoomId("\xEF\xBC\x96"));  // 全角数字 6
}

TF_TEST(bilibili_room_data_defaults) {
	BilibiliRoomData data;
	TF_CHECK(!data.ok);
	TF_EQ(data.online, 0LL);
	TF_EQ(data.likes, 0LL);
	TF_EQ(data.fans, 0LL);
	TF_EQ(data.liveStatus, 0);
	TF_EQ(data.title, std::string());
	TF_EQ(data.error, std::string());

	data.ok = true;
	data.online = 5;
	data.error = "old";
	data.clear();
	TF_CHECK(!data.ok);
	TF_EQ(data.online, 0LL);
	TF_EQ(data.error, std::string());
}

TF_TEST(bilibili_h5_parse_ok) {
	BilibiliRoomData data;
	TF_CHECK(bilibili::parseRoomJson(h5RoomJson(), data));
	TF_CHECK(data.ok);
	TF_EQ(data.online, 198882LL);
	TF_EQ(data.likes, 4044047LL);
	TF_EQ(data.fans, 16896337LL);
	TF_EQ(data.liveStatus, 1);
	TF_EQ(data.title, std::string("【预告】24号1点全球总决赛瑞士轮"));
	TF_EQ(data.error, std::string());
}

TF_TEST(bilibili_h5_parse_zero_values_are_valid) {
	// 未开播房间：online/likes 都是 0，但请求成功，不能把 0 判成失败。
	const std::string body = R"({
		"code": 0,
		"data": {
			"room_info": {"title": "未开播", "live_status": 0, "online": 0},
			"anchor_info": {"relation_info": {"attention": 123}},
			"like_info_v3": {"total_likes": 0}
		}
	})";
	BilibiliRoomData data;
	data.online = 999;  // 确认 parse 会覆盖旧值
	TF_CHECK(bilibili::parseRoomJson(body, data));
	TF_CHECK(data.ok);
	TF_EQ(data.online, 0LL);
	TF_EQ(data.likes, 0LL);
	TF_EQ(data.fans, 123LL);
	TF_EQ(data.liveStatus, 0);
	TF_EQ(data.title, std::string("未开播"));
}

TF_TEST(bilibili_h5_parse_api_error) {
	BilibiliRoomData data;
	TF_CHECK(!bilibili::parseRoomJson(
			R"({"code":-400,"message":"请求错误","ttl":1})", data));
	TF_CHECK(!data.ok);
	TF_CHECK(contains(data.error, "-400"));
	TF_CHECK(contains(data.error, "请求错误"));

	BilibiliRoomData missing;
	TF_CHECK(!bilibili::parseRoomJson(
			R"({"code":19002000,"message":"房间不存在"})", missing));
	TF_CHECK(contains(missing.error, "19002000"));
}

TF_TEST(bilibili_h5_parse_missing_fields) {
	const char* const broken[] = {
		// 没有 data
		R"({"code":0,"message":"OK"})",
		// data 是数组（B 站错误形态）
		R"({"code":0,"data":[]})",
		// 没有 room_info
		R"({"code":0,"data":{"like_info_v3":{"total_likes":1},
			"anchor_info":{"relation_info":{"attention":2}}}})",
		// 没有 title
		R"({"code":0,"data":{"room_info":{"live_status":1,"online":2},
			"like_info_v3":{"total_likes":1},
			"anchor_info":{"relation_info":{"attention":2}}}})",
		// title 类型不对
		R"({"code":0,"data":{"room_info":{"title":123,"live_status":1,"online":2},
			"like_info_v3":{"total_likes":1},
			"anchor_info":{"relation_info":{"attention":2}}}})",
		// 没有 online
		R"({"code":0,"data":{"room_info":{"title":"t","live_status":1},
			"like_info_v3":{"total_likes":1},
			"anchor_info":{"relation_info":{"attention":2}}}})",
		// 没有 like_info_v3
		R"({"code":0,"data":{"room_info":{"title":"t","live_status":1,"online":2},
			"anchor_info":{"relation_info":{"attention":2}}}})",
		// 没有 total_likes
		R"({"code":0,"data":{"room_info":{"title":"t","live_status":1,"online":2},
			"like_info_v3":{},"anchor_info":{"relation_info":{"attention":2}}}})",
		// 没有 attention
		R"({"code":0,"data":{"room_info":{"title":"t","live_status":1,"online":2},
			"like_info_v3":{"total_likes":1},"anchor_info":{}}})",
		// 不是 JSON
		"not json at all",
		// 根不是对象
		"[1,2,3]",
	};
	for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); ++i) {
		BilibiliRoomData data;
		const bool parsed = bilibili::parseRoomJson(broken[i], data);
		TF_CHECK_MSG(!parsed, std::string("fixture ") + std::to_string(i) +
									  " should fail");
		TF_CHECK(!data.ok);
		TF_CHECK(!data.error.empty());
	}
}

TF_TEST(bilibili_info_parse_ok) {
	BilibiliRoomData data;
	TF_CHECK(bilibili::parseRoomInfoJson(infoRoomJson(), data));
	TF_CHECK(data.ok);
	TF_EQ(data.online, 198882LL);
	TF_EQ(data.likes, 0LL);  // get_info 没有点赞
	TF_EQ(data.fans, 16896337LL);
	TF_EQ(data.liveStatus, 1);
	TF_EQ(data.title, std::string("【预告】24号1点全球总决赛瑞士轮"));
}

TF_TEST(bilibili_info_parse_errors) {
	BilibiliRoomData notFound;
	TF_CHECK(!bilibili::parseRoomInfoJson(
			R"({"code":1,"msg":"未找到该房间","data":[]})", notFound));
	TF_CHECK(contains(notFound.error, "1"));
	TF_CHECK(contains(notFound.error, "未找到该房间"));

	BilibiliRoomData missingAttention;
	TF_CHECK(!bilibili::parseRoomInfoJson(
			R"({"code":0,"data":{"title":"t","live_status":1,"online":2}})",
			missingAttention));
	TF_CHECK(contains(missingAttention.error, "attention"));

	BilibiliRoomData missingOnline;
	TF_CHECK(!bilibili::parseRoomInfoJson(
			R"({"code":0,"data":{"title":"t","live_status":1,
			"attention":2}})", missingOnline));
	TF_CHECK(contains(missingOnline.error, "online"));
}

TF_TEST(bilibili_http_close_delimited) {
	const std::string raw =
			"HTTP/1.0 200 OK\r\n"
			"Content-Type: application/json; charset=utf-8\r\n"
			"Connection: close\r\n"
			"\r\n"
			R"({"code":0,"data":{"ok":true}})";
	bilibili::HttpResponse response;
	std::string error;
	TF_CHECK(bilibili::parseHttpResponse(raw, response, error));
	TF_EQ(error, std::string());
	TF_EQ(response.statusCode, 200);
	TF_EQ(response.body, std::string(R"({"code":0,"data":{"ok":true}})"));
}

TF_TEST(bilibili_http_chunked) {
	const std::string raw =
			"HTTP/1.1 200 OK\r\n"
			"Transfer-Encoding: chunked\r\n"
			"\r\n"
			"5;ext=1\r\nhello\r\n"
			"A\r\n0123456789\r\n"
			"0\r\n"
			"\r\n";
	bilibili::HttpResponse response;
	std::string error;
	TF_CHECK(bilibili::parseHttpResponse(raw, response, error));
	TF_EQ(response.statusCode, 200);
	TF_EQ(response.body, std::string("hello0123456789"));
}

TF_TEST(bilibili_http_chunked_empty_body) {
	const std::string raw =
			"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
	bilibili::HttpResponse response;
	std::string error;
	TF_CHECK(bilibili::parseHttpResponse(raw, response, error));
	TF_EQ(response.statusCode, 200);
	TF_EQ(response.body, std::string());
}

TF_TEST(bilibili_http_chunked_wins_over_content_length) {
	const std::string raw =
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 9999\r\n"
			"tRaNsFeR-EnCoDiNg: gzip, Chunked\r\n"
			"\r\n"
			"3\r\nabc\r\n0\r\n\r\n";
	bilibili::HttpResponse response;
	std::string error;
	TF_CHECK(bilibili::parseHttpResponse(raw, response, error));
	TF_EQ(response.statusCode, 200);
	TF_EQ(response.body, std::string("abc"));
}

TF_TEST(bilibili_http_status_and_content_length) {
	bilibili::HttpResponse notFound;
	std::string error;
	TF_CHECK(bilibili::parseHttpResponse(
			"HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nbad",
			notFound, error));
	TF_EQ(notFound.statusCode, 404);
	TF_EQ(notFound.body, std::string("bad"));

	// 多余的字节按 Content-Length 截断。
	bilibili::HttpResponse exact;
	TF_CHECK(bilibili::parseHttpResponse(
			"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhiEXTRA", exact,
			error));
	TF_EQ(exact.body, std::string("hi"));
}

TF_TEST(bilibili_http_lf_only_headers) {
	const std::string raw =
			"HTTP/1.1 200 OK\nContent-Length: 2\n\nhi";
	bilibili::HttpResponse response;
	std::string error;
	TF_CHECK(bilibili::parseHttpResponse(raw, response, error));
	TF_EQ(response.statusCode, 200);
	TF_EQ(response.body, std::string("hi"));
}

TF_TEST(bilibili_http_malformed) {
	const char* const broken[] = {
		"",
		"garbage\r\n\r\n",
		"HTTP/1.1 abc OK\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc",
		"HTTP/1.1 200 OK\r\nContent-Length: 2",  // 没有空行
		"HTTP/1.1 200 OK\r\nBadHeaderWithoutColon\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\nabc",
	};
	for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); ++i) {
		bilibili::HttpResponse response;
		std::string error;
		const bool parsed = bilibili::parseHttpResponse(broken[i], response,
				error);
		TF_CHECK_MSG(!parsed, std::string("fixture ") + std::to_string(i) +
									  " should fail");
		TF_CHECK(!error.empty());
	}
}

TF_TEST(bilibili_http_truncated_chunked) {
	const char* const broken[] = {
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nA\r\nshort",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
	};
	for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); ++i) {
		bilibili::HttpResponse response;
		std::string error;
		const bool parsed = bilibili::parseHttpResponse(broken[i], response,
				error);
		TF_CHECK_MSG(!parsed, std::string("fixture ") + std::to_string(i) +
									  " should fail");
		TF_CHECK(contains(error, "chunked"));
	}
}

TF_TEST(bilibili_http_body_limit) {
	// 64 KiB 上限：Content-Length 超限直接失败，不应尝试分配/截取超大 body。
	const std::string raw =
			"HTTP/1.1 200 OK\r\nContent-Length: 65537\r\n\r\n";
	bilibili::HttpResponse response;
	std::string error;
	TF_CHECK(!bilibili::parseHttpResponse(raw, response, error));
	TF_CHECK(contains(error, "64 KiB"));
}
