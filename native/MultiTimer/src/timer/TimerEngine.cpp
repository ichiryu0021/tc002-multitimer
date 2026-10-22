#include "timer/TimerEngine.h"

#include <algorithm>
#include <vector>

#include "timer/TimeMath.h"

namespace {

const long long kSmallFontThresholdSeconds = 100LL * 3600LL;  // 100 小时

struct Candidate {
	long long epoch;
	int sourceIndex;
};

bool candidateLess(const Candidate& a, const Candidate& b) {
	if (a.epoch != b.epoch) {
		return a.epoch < b.epoch;
	}
	// 相同 epoch 时按配置顺序，便于“去重保留靠前者”
	return a.sourceIndex < b.sourceIndex;
}

}  // namespace

TimerEngine::TimerEngine() {
	setConfig(TimerConfig());
}

void TimerEngine::setConfig(const TimerConfig& config) {
	mConfig = config;
	rebuild();
}

void TimerEngine::rebuild() {
	mEpochs.clear();
	mSourceIndex.clear();

	std::vector<Candidate> candidates;
	candidates.reserve(mConfig.targets.size());
	for (size_t i = 0; i < mConfig.targets.size(); ++i) {
		const DateTarget& target = mConfig.targets[i];
		if (!target.enabled) {
			continue;  // 禁用目标不参与倒计时
		}
		long long epoch = 0;
		if (!TimerEngine::targetEpoch(target, mConfig.tzOffsetMinutes, epoch)) {
			continue;  // 非法日期/时间/时区：丢弃
		}
		Candidate candidate;
		candidate.epoch = epoch;
		candidate.sourceIndex = static_cast<int>(i);
		candidates.push_back(candidate);
	}

	std::sort(candidates.begin(), candidates.end(), candidateLess);

	for (size_t i = 0; i < candidates.size(); ++i) {
		// 相同 epoch 去重：排序后靠前者即配置中靠前者
		if (!mEpochs.empty() && mEpochs.back() == candidates[i].epoch) {
			continue;
		}
		mEpochs.push_back(candidates[i].epoch);
		mSourceIndex.push_back(candidates[i].sourceIndex);
	}
}

Stage TimerEngine::evaluate(long long nowEpoch) const {
	Stage stage;  // 默认 STAGE_CLOCK / index=-1 / targetEpoch=0

	for (size_t i = 0; i < mEpochs.size(); ++i) {
		const long long epoch = mEpochs[i];
		if (epoch < nowEpoch) {
			continue;  // 已过期：跳过
		}
		stage.kind = STAGE_COUNTDOWN;
		stage.targetEpoch = epoch;
		stage.targetIndex = static_cast<int>(i);  // 排序（去重）后的下标
		stage.remainSeconds = epoch - nowEpoch;   // >= 0，恰好在目标时为 0
		stage.warning = mConfig.warnSeconds > 0 &&
				stage.remainSeconds <= static_cast<long long>(mConfig.warnSeconds);
		stage.smallFont = stage.remainSeconds >= kSmallFontThresholdSeconds;
		return stage;
	}

	// 没有未来目标：如果至少配置过一个已过期目标，从最近那个目标正计时；
	// 完全没有任何目标才是 STAGE_CLOCK。
	if (!mEpochs.empty()) {
		const size_t last = mEpochs.size() - 1;
		stage.kind = STAGE_COUNTUP;
		stage.targetEpoch = mEpochs[last];
		stage.targetIndex = static_cast<int>(last);
		stage.elapsedSeconds = nowEpoch - mEpochs[last];
		if (stage.elapsedSeconds < 0) {
			stage.elapsedSeconds = 0;
		}
		stage.smallFont = stage.elapsedSeconds >= kSmallFontThresholdSeconds;
	}
	return stage;
}

// static
bool TimerEngine::targetEpoch(const DateTarget& target, int tzOffsetMinutes,
		long long& epochOut) {
	LocalDateTime local;
	local.year = target.year;
	local.month = target.month;
	local.day = target.day;
	local.hour = target.hour;
	local.minute = target.minute;
	local.second = target.second;
	// 目标日期限 2000..2099 且必须日历合法（DESIGN §3/§7）
	if (!isValidDateTime(local)) {
		return false;
	}
	return localDateTimeToEpoch(local, tzOffsetMinutes, epochOut);
}
