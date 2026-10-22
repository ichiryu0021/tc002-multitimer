// 极简 HTTP 配置服务（0.0.0.0:8080，无鉴权/无 TLS，仅限可信局域网）：
//   GET  /                内嵌手机编辑页（gzip 字节，浏览器自动解压）
//   GET  /index.html      同上
//   GET  /api/config      查询 v3 配置 + device + ui（当前页）+ live 摘要
//   POST /api/config      只接受 v3；校验/保存并热应用 TimerConfig
//   POST /api/page        {"id":"p1"} / {"index":1} / {"reset":true} 切页
//   POST /api/time        以 {"epoch":<utc秒>} 校准系统时间并持久化
//   POST /api/wifi        保存 WiFi 凭据并异步重连
//   GET  /api/wifi*       读取 WiFi 列表/状态（v2.1.2 行为不变）
//   GET  /api/status      通过 TimerPage::getStatus 读取实时显示状态
//   GET  /api/live/status 直播数据源/房间状态（LiveService）
//   POST /api/live/refresh {"page":N} 触发一次异步刷新（可省略 page）
//   POST /api/live        push 模式推数据（body 同 bridge 响应格式）
//   OPTIONS *             CORS 预检
//   其余路径              404
// 请求体上限 16 KB；规范化配置上限 12 KB（超限 413）。
// 本头不依赖任何平台 SDK 头。
#ifndef NET_HTTPSERVER_H_
#define NET_HTTPSERVER_H_

#include <functional>

#include "timer/TimerEngine.h"

class HttpServer {
public:
	typedef std::function<void(const TimerConfig&)> ApplyCallback;

	// 启动监听（内部 detach accept 线程与连接线程），不阻塞调用方；
	// 端口绑定失败返回 false
	static bool start(int port, ApplyCallback applyCallback);

private:
	HttpServer();
};

#endif  // NET_HTTPSERVER_H_
