// 配置持久化（v3）：
// - JSON 存于 /data/multitimer.conf（jffs2 持久分区，原子写）
// - `load()` 读取磁盘：v3 直接校验；v2/v1 自动迁移到 v3 并回填 migrated=true
// - `fromJson()` 只接受 v3（HTTP POST 用）；v2 schema 返回 false + 明确错误
// 本头不依赖任何平台 SDK 头。
#ifndef CONFIG_CONFIGSTORE_H_
#define CONFIG_CONFIGSTORE_H_

#include <string>

#include "net/mini_json.h"
#include "timer/TimerTypes.h"

class ConfigStore {
public:
	static const int kConfigVersion = 3;
	static const int kMaxTargets = 12;
	static const int kMaxPages = ::kMaxPageCount;
	static const int kMaxRoomsPerLivePage = ::kMaxRoomsPerLivePage;

	// 全新安装（无配置文件）的默认配置：4 页
	//   p0 clock / p1 clock_countdown / p2 countdown / p3 live（2 个空槽，source=off）
	// `load()` 无论成功失败都会先把 out 置为该默认值。
	static TimerConfig defaultConfig();

	// 读取并校验 /data/multitimer.conf。
	// nowUtc 用于 v1 迁移时确定“当前本地日期”；无效时间（<2023-11）时不迁移 points。
	// migrated 非空时回填是否发生了 v1/v2 -> v3 迁移。
	// 返回 true 表示磁盘配置可用；false 表示缺失/损坏/更高版本（out 保持默认）。
	static bool load(TimerConfig& out, long long nowUtc,
			bool* migrated = NULL);

	// 原子保存（先写 .tmp、fsync，再 rename）
	static bool save(const TimerConfig& config);

	// TimerConfig <-> v3 JSON（字段顺序稳定，2 空格缩进）
	static std::string toJson(const TimerConfig& config);
	static bool fromJson(const JsonValue& value, TimerConfig& out,
			std::string& err);

	// "#RRGGBB" -> 0xRRGGBB，非法返回 false
	static bool parseColor(const std::string& text, uint32_t& rgb);

private:
	ConfigStore();
};

#endif  // CONFIG_CONFIGSTORE_H_
