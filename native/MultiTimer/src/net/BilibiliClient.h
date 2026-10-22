// B 站直播数据直连入口（设备端 HTTPS + CA 校验），供 LiveService 调用。
//
// 主端点：https://api.live.bilibili.com/xlive/web-room/v1/index/getH5InfoByRoom
//         （一次拿全 title/live_status/online/likes/fans）
// 兜底：  https://api.live.bilibili.com/room/v1/Room/get_info
//         （主端点失败或字段缺失时；没有 likes，likes=0）
//
// 线程安全：fetchRoom 可在任意线程调用；每次请求独立 SSL_CTX/SSL/socket，
// 不共享可变全局。不要在 UI 线程里同步调用（设备网络最坏数百 ms~数秒）。
#ifndef NET_BILIBILICLIENT_H_
#define NET_BILIBILICLIENT_H_

#include <string>

#include "net/BilibiliParse.h"

class BilibiliClient {
public:
	// roomId：1..20 位 ASCII 数字（短号或真号）；空/非法立即失败，不发请求。
	// out：成功时 ok=true 且 error 为空；失败时 ok=false、error 含端点与原因。
	// 数值 0 是有效值（未开播房间 online/likes 可以为 0）。
	// timeoutMs：单次 HTTP 请求总预算，默认 4000 ms；主端点失败后会再请求兜底端点。
	static bool fetchRoom(const std::string& roomId, BilibiliRoomData& out,
			int timeoutMs = 4000);

private:
	BilibiliClient();
};

#endif  // NET_BILIBILICLIENT_H_
