// B 站直播接口的纯逻辑层：房间号校验、JSON 字段提取、HTTP 响应/chunked 解码。
//
// 本文件只依赖标准库与 net/mini_json，不包含任何平台头（网络/TLS 在
// TlsClient / BilibiliClient 里），因此可以直接在 PC 主机单测中编译运行。
//
// 端点与字段依据 docs/RESEARCH-live-apis.md §2（2026-10-22 实测）：
// - 主端点 getH5InfoByRoom：room_info.{title,live_status,online} +
//   like_info_v3.total_likes + anchor_info.relation_info.attention；
// - 兜底 get_info：data.{title,live_status,online,attention}（没有点赞）。
#ifndef NET_BILIBILIPARSE_H_
#define NET_BILIBILIPARSE_H_

#include <stddef.h>

#include <string>

// 单个 B 站直播间的抓取结果。数值 0 是有效值，失败只能看 ok/error。
struct BilibiliRoomData {
	bool ok;
	long long online;    // 人气值（不是并发人数）
	long long likes;     // 本场累计点赞；get_info 兜底时为 0
	long long fans;      // 主播粉丝数（attention）
	int liveStatus;      // 0=未开播，1=直播中
	std::string title;   // 直播间标题
	std::string error;   // ok=false 时的错误文本（ok=true 时为空）

	BilibiliRoomData();
	void clear();
};

namespace bilibili {

// 设备端 HTTP 响应硬上限（RESEARCH §5.1：B 站响应约 4 KB；64 KiB 足够宽松）。
const size_t kMaxHttpResponseBytes = 64U * 1024U;

// room_id 只接受 1..20 位 ASCII 数字（B 站短号或真号）。
// 空串、前后空格、符号、小数点、全角数字、超过 20 位一律拒绝。
bool isValidRoomId(const std::string& roomId);

// 解析 getH5InfoByRoom 的响应体。成功时填充 out 并返回 true；
// 失败时 out.ok=false、out.error 说明原因（含 B 站 code/msg 或缺失字段名）。
bool parseRoomJson(const std::string& body, BilibiliRoomData& out);

// 解析 room/v1/Room/get_info 的响应体（兜底端点，没有点赞字段）。
bool parseRoomInfoJson(const std::string& body, BilibiliRoomData& out);

// 原始 HTTP 响应（状态行 + headers + body）。
struct HttpResponse {
	int statusCode;
	std::string body;

	HttpResponse();
};

// 解析原始 HTTP 响应字节。支持：
// - CRLF 或裸 LF 行尾；
// - Transfer-Encoding: chunked（含 chunk 扩展、多段、trailer）；
// - Content-Length；
// - HTTP/1.0 无长度（读到 EOF，body 即剩余字节）。
// 只要状态行/headers 合法就返回 true（404 也算成功解析）；
// 传输/格式错误返回 false 并填 error。
bool parseHttpResponse(const std::string& raw, HttpResponse& out,
		std::string& error);

// 解 chunked body；成功返回 true。out 追加解码后的字节。
bool decodeChunked(const std::string& raw, std::string& out,
		std::string& error);

}  // namespace bilibili

#endif  // NET_BILIBILIPARSE_H_
