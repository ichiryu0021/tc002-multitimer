#ifndef TIMER_TIMERENGINE_H_
#define TIMER_TIMERENGINE_H_

#include <vector>

#include "timer/TimerTypes.h"

enum StageKind {
	STAGE_INVALID = 0,  // 仅由页面在时间未同步时使用；引擎不返回
	STAGE_CLOCK = 1,    // 无任何目标：显示正常时钟
	STAGE_COUNTDOWN = 2, // 有未来目标：倒计时到最近目标
	STAGE_COUNTUP = 3   // 没有未来目标、但至少有过一个目标：从最近过期目标正计时
};

struct Stage {
	StageKind kind;
	long long remainSeconds;  // COUNTDOWN: 到目标的剩余秒（>=0）
	long long elapsedSeconds; // COUNTUP: 从最近过期目标起经过的秒（>=0）
	long long targetEpoch;    // COUNTDOWN: 目标 epoch；COUNTUP: 最近过期目标 epoch
	int targetIndex;          // 启用目标按 epoch 排序后的下标；无目标 -1
	bool warning;             // COUNTDOWN 且剩余 <= warnSeconds
	bool smallFont;           // COUNTDOWN/COUNTUP 且时长 >= 100 小时

	Stage()
		: kind(STAGE_CLOCK),
		  remainSeconds(0),
		  elapsedSeconds(0),
		  targetEpoch(0),
		  targetIndex(-1),
		  warning(false),
		  smallFont(false) {
	}
};

class TimerEngine {
public:
	TimerEngine();

	void setConfig(const TimerConfig& config);
	const TimerConfig& config() const { return mConfig; }

	// nowEpoch：UTC 秒（time(NULL)）。每秒调用，实时按当前时钟重算。
	Stage evaluate(long long nowEpoch) const;

	// 把本地日期时间目标折算为 UTC epoch（失败返回 false）
	static bool targetEpoch(const DateTarget& target, int tzOffsetMinutes,
			long long& epochOut);

private:
	void rebuild();

	TimerConfig mConfig;
	// 已启用目标按 epoch 升序；mSourceIndex 对应 mConfig.targets 下标
	std::vector<long long> mEpochs;
	std::vector<int> mSourceIndex;
};

#endif  // TIMER_TIMERENGINE_H_
