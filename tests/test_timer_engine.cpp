// Host tests for timer/TimerEngine (pure C++ v2 target scheduling).
#include "test_framework.h"

#include <string>
#include <vector>

#include "timer/TimeMath.h"
#include "timer/TimerEngine.h"

namespace {

DateTarget makeTarget(int year, int month, int day, int hour, int minute,
		int second, bool enabled = true) {
	DateTarget target;
	target.year = year;
	target.month = month;
	target.day = day;
	target.hour = hour;
	target.minute = minute;
	target.second = second;
	target.enabled = enabled;
	return target;
}

long long epochOf(int year, int month, int day, int hour, int minute,
		int second, int tzOffsetMinutes) {
	LocalDateTime local;
	local.year = year;
	local.month = month;
	local.day = day;
	local.hour = hour;
	local.minute = minute;
	local.second = second;
	long long epoch = 0;
	if (!localDateTimeToEpoch(local, tzOffsetMinutes, epoch)) {
		return -1;
	}
	return epoch;
}

TimerConfig makeConfig(int tzOffsetMinutes, int warnSeconds,
		const std::vector<DateTarget>& targets) {
	TimerConfig config;
	config.tzOffsetMinutes = tzOffsetMinutes;
	config.warnSeconds = warnSeconds;
	config.targets = targets;
	return config;
}

TimerEngine engineWith(int tzOffsetMinutes, int warnSeconds,
		const std::vector<DateTarget>& targets) {
	TimerEngine engine;
	engine.setConfig(makeConfig(tzOffsetMinutes, warnSeconds, targets));
	return engine;
}

void expectClock(const Stage& stage) {
	TF_EQ(static_cast<int>(stage.kind), static_cast<int>(STAGE_CLOCK));
	TF_EQ(stage.targetIndex, -1);
	TF_EQ(stage.targetEpoch, 0LL);
	TF_EQ(stage.remainSeconds, 0LL);
	TF_EQ(stage.warning, false);
	TF_EQ(stage.smallFont, false);
}

void expectCountup(const Stage& stage, long long targetEpoch,
		long long elapsedSeconds) {
	TF_EQ(static_cast<int>(stage.kind), static_cast<int>(STAGE_COUNTUP));
	TF_EQ(stage.targetEpoch, targetEpoch);
	TF_EQ(stage.elapsedSeconds, elapsedSeconds);
	TF_EQ(stage.remainSeconds, 0LL);
	TF_EQ(stage.warning, false);
}

}  // namespace

TF_TEST(engine_default_construction_is_clock) {
	const TimerEngine engine;
	expectClock(engine.evaluate(0));
	expectClock(engine.evaluate(1792658309LL));

	TF_EQ(engine.config().tzOffsetMinutes, 480);
	TF_EQ(engine.config().warnSeconds, 10);
	TF_EQ(engine.config().colorClock, 0xFFFFFFu);
	TF_EQ(engine.config().colorCountdown, 0x00FF00u);
	TF_EQ(engine.config().colorWarning, 0xFF2020u);
	TF_CHECK(engine.config().targets.empty());
}

TF_TEST(engine_empty_targets_returns_clock) {
	const std::vector<DateTarget> targets;
	const TimerEngine engine = engineWith(480, 10, targets);
	expectClock(engine.evaluate(epochOf(2026, 10, 22, 16, 38, 29, 480)));
	expectClock(engine.evaluate(0));
	expectClock(engine.evaluate(4102444800LL));
}

TF_TEST(engine_disabled_targets_ignored) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0, false));
	targets.push_back(makeTarget(2026, 10, 24, 9, 0, 0, false));
	const TimerEngine engine = engineWith(480, 10, targets);

	expectClock(engine.evaluate(epochOf(2026, 10, 22, 12, 0, 0, 480)));
	// The raw configuration keeps the disabled targets.
	TF_EQ(static_cast<int>(engine.config().targets.size()), 2);
}

TF_TEST(engine_single_future_target) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long targetEpoch = epochOf(2026, 10, 23, 8, 0, 0, 480);
	TF_CHECK(targetEpoch > 0);

	const Stage stage = engine.evaluate(targetEpoch - 60);
	TF_EQ(static_cast<int>(stage.kind), static_cast<int>(STAGE_COUNTDOWN));
	TF_EQ(stage.targetIndex, 0);
	TF_EQ(stage.targetEpoch, targetEpoch);
	TF_EQ(stage.remainSeconds, 60LL);
	TF_EQ(stage.warning, false);
	TF_EQ(stage.smallFont, false);
}

TF_TEST(engine_exactly_at_target) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long targetEpoch = epochOf(2026, 10, 23, 8, 0, 0, 480);

	const Stage atTarget = engine.evaluate(targetEpoch);
	TF_EQ(static_cast<int>(atTarget.kind), static_cast<int>(STAGE_COUNTDOWN));
	TF_EQ(atTarget.targetIndex, 0);
	TF_EQ(atTarget.remainSeconds, 0LL);
	TF_EQ(atTarget.warning, true);  // 0 <= warnSeconds

	expectCountup(engine.evaluate(targetEpoch + 1), targetEpoch, 1);
	expectCountup(engine.evaluate(targetEpoch + 86400), targetEpoch, 86400);
}

TF_TEST(engine_past_targets_skipped) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 20, 8, 0, 0));
	targets.push_back(makeTarget(2026, 10, 21, 8, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long lastEpoch = epochOf(2026, 10, 21, 8, 0, 0, 480);
	const long long now = epochOf(2026, 10, 22, 12, 0, 0, 480);
	expectCountup(engine.evaluate(now), lastEpoch, now - lastEpoch);
}

TF_TEST(engine_multiple_targets_sorted_by_epoch) {
	// Config order differs from chronological order on purpose.
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 12, 0, 0));
	targets.push_back(makeTarget(2026, 10, 22, 18, 0, 0));
	targets.push_back(makeTarget(2026, 10, 24, 9, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);

	const long long epoch1 = epochOf(2026, 10, 22, 18, 0, 0, 480);
	const long long epoch2 = epochOf(2026, 10, 23, 12, 0, 0, 480);
	const long long epoch3 = epochOf(2026, 10, 24, 9, 0, 0, 480);

	// Before the earliest target: sorted index 0.
	const Stage first = engine.evaluate(epoch1 - 3600);
	TF_EQ(static_cast<int>(first.kind), static_cast<int>(STAGE_COUNTDOWN));
	TF_EQ(first.targetIndex, 0);
	TF_EQ(first.targetEpoch, epoch1);
	TF_EQ(first.remainSeconds, 3600LL);

	// After the first target: sorted index 1.
	const long long now2 = epoch1 + 60;
	const Stage second = engine.evaluate(now2);
	TF_EQ(second.targetIndex, 1);
	TF_EQ(second.targetEpoch, epoch2);
	TF_EQ(second.remainSeconds, epoch2 - now2);

	// After the second target: sorted index 2.
	const long long now3 = epoch2 + 7200;
	const Stage third = engine.evaluate(now3);
	TF_EQ(third.targetIndex, 2);
	TF_EQ(third.targetEpoch, epoch3);
	TF_EQ(third.remainSeconds, epoch3 - now3);

	expectCountup(engine.evaluate(epoch3 + 1), epoch3, 1);
}

TF_TEST(engine_sorted_index_not_config_index) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 24, 9, 0, 0));
	targets.push_back(makeTarget(2026, 10, 22, 18, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long early = epochOf(2026, 10, 22, 18, 0, 0, 480);

	const Stage stage = engine.evaluate(early - 30);
	TF_EQ(stage.targetIndex, 0);  // config index 1, sorted index 0
	TF_EQ(stage.targetEpoch, early);
	TF_EQ(stage.remainSeconds, 30LL);

	const Stage next = engine.evaluate(early + 1);
	TF_EQ(next.targetIndex, 1);
	TF_EQ(next.targetEpoch, epochOf(2026, 10, 24, 9, 0, 0, 480));
}

TF_TEST(engine_duplicate_epoch_deduplicated) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 22, 18, 0, 0));
	targets.push_back(makeTarget(2026, 10, 22, 18, 0, 0));  // duplicate
	targets.push_back(makeTarget(2026, 10, 23, 12, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long duplicateEpoch = epochOf(2026, 10, 22, 18, 0, 0, 480);
	const long long nextEpoch = epochOf(2026, 10, 23, 12, 0, 0, 480);

	const Stage before = engine.evaluate(duplicateEpoch - 10);
	TF_EQ(before.targetIndex, 0);
	TF_EQ(before.remainSeconds, 10LL);

	// The duplicate must collapse: the next unique target is index 1, not 2.
	const Stage after = engine.evaluate(duplicateEpoch + 1);
	TF_EQ(after.targetIndex, 1);
	TF_EQ(after.targetEpoch, nextEpoch);
	TF_EQ(after.remainSeconds, nextEpoch - duplicateEpoch - 1);
}

TF_TEST(engine_invalid_targets_dropped) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2023, 2, 29, 12, 0, 0));  // invalid date
	targets.push_back(makeTarget(2026, 10, 23, 24, 0, 0)); // invalid hour
	targets.push_back(makeTarget(2026, 10, 23, 8, 60, 0)); // invalid minute
	targets.push_back(makeTarget(1999, 12, 31, 23, 59, 59));  // out of range
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));  // only valid one
	targets.push_back(makeTarget(2026, 10, 24, 8, 0, 0, false));
	const TimerEngine engine = engineWith(480, 10, targets);
	TF_EQ(static_cast<int>(engine.config().targets.size()), 6);

	const long long validEpoch = epochOf(2026, 10, 23, 8, 0, 0, 480);
	const Stage stage = engine.evaluate(validEpoch - 5);
	TF_EQ(static_cast<int>(stage.kind), static_cast<int>(STAGE_COUNTDOWN));
	TF_EQ(stage.targetIndex, 0);
	TF_EQ(stage.targetEpoch, validEpoch);
	TF_EQ(stage.remainSeconds, 5LL);

	// A config whose every target is invalid behaves like an empty config.
	std::vector<DateTarget> onlyInvalid;
	onlyInvalid.push_back(makeTarget(2023, 2, 29, 12, 0, 0));
	onlyInvalid.push_back(makeTarget(2026, 10, 23, 24, 0, 0));
	const TimerEngine badEngine = engineWith(480, 10, onlyInvalid);
	expectClock(badEngine.evaluate(epochOf(2026, 10, 22, 12, 0, 0, 480)));
}

TF_TEST(engine_invalid_timezone_makes_targets_unusable) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	const TimerEngine engine = engineWith(900, 10, targets);
	expectClock(engine.evaluate(1792658309LL));
}

TF_TEST(engine_warning_boundaries) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	const long long targetEpoch = epochOf(2026, 10, 23, 8, 0, 0, 480);

	const TimerEngine warn10 = engineWith(480, 10, targets);
	TF_EQ(warn10.evaluate(targetEpoch - 11).warning, false);
	TF_EQ(warn10.evaluate(targetEpoch - 10).warning, true);
	TF_EQ(warn10.evaluate(targetEpoch - 9).warning, true);
	TF_EQ(warn10.evaluate(targetEpoch - 1).warning, true);
	TF_EQ(warn10.evaluate(targetEpoch).warning, true);

	const TimerEngine warn0 = engineWith(480, 0, targets);
	TF_EQ(warn0.evaluate(targetEpoch - 1).warning, false);
	TF_EQ(warn0.evaluate(targetEpoch).warning, false);
	TF_EQ(warn0.evaluate(targetEpoch - 3600).warning, false);

	const TimerEngine warn600 = engineWith(480, 600, targets);
	TF_EQ(warn600.evaluate(targetEpoch - 601).warning, false);
	TF_EQ(warn600.evaluate(targetEpoch - 600).warning, true);
	TF_EQ(warn600.evaluate(targetEpoch).warning, true);
}

TF_TEST(engine_small_font_boundary) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 11, 2, 0, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long targetEpoch = epochOf(2026, 11, 2, 0, 0, 0, 480);

	const Stage atThreshold = engine.evaluate(targetEpoch - 360000);
	TF_EQ(atThreshold.remainSeconds, 360000LL);
	TF_EQ(atThreshold.smallFont, true);
	TF_EQ(static_cast<int>(atThreshold.kind),
			static_cast<int>(STAGE_COUNTDOWN));

	const Stage belowThreshold = engine.evaluate(targetEpoch - 359999);
	TF_EQ(belowThreshold.remainSeconds, 359999LL);
	TF_EQ(belowThreshold.smallFont, false);

	const Stage farAway = engine.evaluate(0);
	TF_EQ(farAway.remainSeconds, targetEpoch);
	TF_EQ(farAway.smallFont, true);
}

TF_TEST(engine_crosses_day_month_year) {
	const long long dayEpoch = epochOf(2026, 10, 23, 0, 0, 0, 480);
	{
		std::vector<DateTarget> targets;
		targets.push_back(makeTarget(2026, 10, 23, 0, 0, 0));
		const TimerEngine engine = engineWith(480, 10, targets);
		const long long now = epochOf(2026, 10, 22, 23, 59, 59, 480);
		const Stage stage = engine.evaluate(now);
		TF_EQ(stage.remainSeconds, 1LL);
		TF_EQ(stage.targetEpoch, dayEpoch);
	}
	{
		std::vector<DateTarget> targets;
		targets.push_back(makeTarget(2026, 11, 1, 0, 0, 0));
		const TimerEngine engine = engineWith(480, 10, targets);
		const long long now = epochOf(2026, 10, 31, 23, 59, 59, 480);
		const Stage stage = engine.evaluate(now);
		TF_EQ(stage.remainSeconds, 1LL);
		TF_EQ(stage.targetEpoch, epochOf(2026, 11, 1, 0, 0, 0, 480));
	}
	{
		std::vector<DateTarget> targets;
		targets.push_back(makeTarget(2027, 1, 1, 0, 0, 0));
		const TimerEngine engine = engineWith(480, 10, targets);
		const long long now = epochOf(2026, 12, 31, 23, 59, 59, 480);
		const Stage stage = engine.evaluate(now);
		TF_EQ(stage.remainSeconds, 1LL);
		TF_EQ(stage.targetEpoch, epochOf(2027, 1, 1, 0, 0, 0, 480));
	}
	{
		std::vector<DateTarget> targets;
		targets.push_back(makeTarget(2028, 2, 29, 12, 0, 0));
		const TimerEngine engine = engineWith(480, 10, targets);
		const long long now = epochOf(2028, 2, 29, 11, 59, 59, 480);
		const Stage stage = engine.evaluate(now);
		TF_EQ(stage.remainSeconds, 1LL);
	}
}

TF_TEST(engine_timezone_change_recomputes) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	const TimerEngine plus8 = engineWith(480, 10, targets);
	const TimerEngine plus530 = engineWith(330, 10, targets);

	const long long epochPlus8 = epochOf(2026, 10, 23, 8, 0, 0, 480);
	const long long epochPlus530 = epochOf(2026, 10, 23, 8, 0, 0, 330);
	TF_EQ(epochPlus530 - epochPlus8, 9000LL);

	const long long now = 1792658309LL;
	const Stage stage8 = plus8.evaluate(now);
	const Stage stage530 = plus530.evaluate(now);
	TF_EQ(stage8.targetEpoch, epochPlus8);
	TF_EQ(stage530.targetEpoch, epochPlus530);
	TF_EQ(stage530.remainSeconds - stage8.remainSeconds, 9000LL);
	TF_EQ(stage8.remainSeconds, epochPlus8 - now);
}

TF_TEST(engine_negative_tz_target) {
	// 2026-10-22 20:00 at UTC-5 == 2026-10-23 01:00 UTC.
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 22, 20, 0, 0));
	const TimerEngine engine = engineWith(-300, 10, targets);
	const long long targetEpoch = epochOf(2026, 10, 22, 20, 0, 0, -300);
	TF_EQ(targetEpoch, epochOf(2026, 10, 23, 1, 0, 0, 0));

	const Stage stage = engine.evaluate(targetEpoch - 1800);
	TF_EQ(static_cast<int>(stage.kind), static_cast<int>(STAGE_COUNTDOWN));
	TF_EQ(stage.remainSeconds, 1800LL);
}

TF_TEST(engine_static_target_epoch) {
	long long epoch = 0;
	TF_CHECK(TimerEngine::targetEpoch(makeTarget(2026, 10, 23, 19, 25, 0),
			480, epoch));
	TF_EQ(epoch, epochOf(2026, 10, 23, 19, 25, 0, 480));

	TF_CHECK(!TimerEngine::targetEpoch(makeTarget(2023, 2, 29, 12, 0, 0),
			480, epoch));
	TF_CHECK(!TimerEngine::targetEpoch(makeTarget(2026, 10, 23, 24, 0, 0),
			480, epoch));
	TF_CHECK(!TimerEngine::targetEpoch(makeTarget(2026, 10, 23, 19, 25, 60),
			480, epoch));
	TF_CHECK(!TimerEngine::targetEpoch(makeTarget(2026, 10, 23, 19, 25, 0),
			900, epoch));
}

TF_TEST(engine_set_config_replaces_previous) {
	std::vector<DateTarget> first;
	first.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	TimerEngine engine = engineWith(480, 10, first);

	const long long firstEpoch = epochOf(2026, 10, 23, 8, 0, 0, 480);
	TF_EQ(engine.evaluate(firstEpoch - 10).targetEpoch, firstEpoch);

	engine.setConfig(TimerConfig());
	expectClock(engine.evaluate(firstEpoch - 10));

	std::vector<DateTarget> second;
	second.push_back(makeTarget(2026, 10, 25, 9, 0, 0));
	engine.setConfig(makeConfig(330, 20, second));
	const long long secondEpoch = epochOf(2026, 10, 25, 9, 0, 0, 330);
	const Stage stage = engine.evaluate(secondEpoch - 20);
	TF_EQ(stage.targetEpoch, secondEpoch);
	TF_EQ(stage.remainSeconds, 20LL);
	TF_EQ(stage.warning, true);
	TF_EQ(engine.config().tzOffsetMinutes, 330);
	TF_EQ(engine.config().warnSeconds, 20);
}

TF_TEST(engine_clock_stage_fields_are_zeroed) {
	std::vector<DateTarget> targets;
	targets.push_back(makeTarget(2026, 10, 23, 8, 0, 0));
	const TimerEngine engine = engineWith(480, 10, targets);
	const long long targetEpoch = epochOf(2026, 10, 23, 8, 0, 0, 480);
	expectCountup(engine.evaluate(targetEpoch + 1), targetEpoch, 1);
	expectCountup(engine.evaluate(targetEpoch + 100000), targetEpoch, 100000);
}
