// 直播单元格的纯格式化逻辑（契约 §3；纯 C++11，主机可测）。
//
// 只做字符串/颜色选择，不依赖 Surface / TomThumb / 平台 SDK；
// 渲染器负责把结果画到 52x16 的 Surface 上。
#ifndef LIVE_LIVEFORMAT_H_
#define LIVE_LIVEFORMAT_H_

#include <stdint.h>

#include <string>

#include "live/LiveTypes.h"
#include "pages/PageTypes.h"

namespace LiveFormat {

// 指标值压缩（契约 §3：`1234` / `12.3w` / `9.9y`，<=6 字符）：
//   0..9999         十进制
//   1e4..1e7-1      万，1 位小数，小数位为 0 时省略：`1.2w` `12.3w` `100w` `999.9w`
//                   万进位到 1000w（7 字符）时升级为亿：`0.1y`
//   1e7..1e10-1     亿，1 位小数，小数位为 0 时省略：`0.1y` `1y` `12.3y` `100y`
//   >=1e10          上限截断 `999y`
// 负数 / 无数据返回 "--"（无数据请用 formatValue(LiveMetricValue)）。
std::string formatCount(long long value);

// valid=false -> "--"；否则同 formatCount。
std::string formatValue(const LiveMetricValue& value);

// status 指标：0 -> "OFF"，非 0 -> "LIVE"，无值 -> "--"。
std::string statusText(const LiveMetricValue& value);

// 指标选择：online/likes/fans -> 压缩数字；status -> OFF/LIVE；
// none/title/inherit -> "--"（v3.0 屏幕不显示标题，契约 §2）。
std::string formatMetric(const LiveMetricValue& value, LiveMetric metric);

// 平台单字母（B/D/K/H/Y/C）。
const char* platformLetter(LivePlatform platform);

// 平台颜色（契约 §3：B #FF6699、D #FFFFFF、K #FF9900；
// H/Y/C 为扩展位，取设计稿颜色）。
uint32_t platformColor(LivePlatform platform);

// 标签规范化：转大写、只保留 [A-Z0-9._-]、截断到 3 字符；空/全非法 -> ""。
// 空字符串表示“由 defaultLabel 自动生成”。
std::string normalizeLabel(const std::string& raw);

// 默认标签：平台字母 + 房间号尾部最多 2 个合法字符（大写），总长 <=3。
// 例：bilibili/21452505 -> "B05"；kuaishou/abc -> "KBC"；room 空 -> "B"。
std::string defaultLabel(LivePlatform platform, const std::string& room);

// 实际标签：room.label 非空则规范化使用，否则 defaultLabel。
std::string labelFor(const LiveRoomConfig& room);

// 实际指标：room.metric 为 INHERIT 时用 page.metric；仍为 INHERIT 时回退 online。
LiveMetric effectiveMetric(const PageConfig& page, const LiveRoomConfig& room);

// 从快照里取该指标的值（没有对应字段时返回 invalid）。
LiveMetricValue selectMetricValue(const LiveRoomData& room, LiveMetric metric);

// 该槽位是否“已配置”（enabled 且 room 非空）；未配置槽位留黑、不轮询。
bool isCellConfigured(const LiveRoomConfig& room);

}  // namespace LiveFormat

#endif  // LIVE_LIVEFORMAT_H_
