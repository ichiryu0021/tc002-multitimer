#include "timer/TimeMath.h"

#include <stdio.h>

namespace {

const long long kSecondsPerDay = 86400LL;
const long long kSecondsPerHour = 3600LL;
const int kMaxTzOffsetMinutes = 840;   // UTC+14:00
const int kMinTzOffsetMinutes = -720;  // UTC-12:00
const int kMinYear = 2000;
const int kMaxYear = 2099;

bool isDigitChar(char c) {
	return c >= '0' && c <= '9';
}

int digitValue(char c) {
	return c - '0';
}

// 向下取整除法（b > 0）：C++ 的 / 对负数向 0 截断，负 epoch 必须向下取整。
long long floorDiv(long long a, long long b) {
	long long q = a / b;
	if ((a % b) != 0 && a < 0) {
		--q;
	}
	return q;
}

void appendTwoDigits(std::string& out, int value) {
	out.push_back(static_cast<char>('0' + (value / 10) % 10));
	out.push_back(static_cast<char>('0' + value % 10));
}

bool isValidTimeOfDay(int hour, int minute, int second) {
	if (hour < 0 || hour > 23) {
		return false;
	}
	if (minute < 0 || minute > 59) {
		return false;
	}
	return second >= 0 && second <= 59;
}

// 通用公历日期校验（供 epoch 换算/1970 前后使用）。
// 应用层“目标日期 2000..2099”由 isValidDate 单独限制。
bool isCalendarDateValid(int year, int month, int day) {
	if (year < 1 || year > 9999) {
		return false;
	}
	if (month < 1 || month > 12) {
		return false;
	}
	return day >= 1 && day <= daysInMonth(year, month);
}

}  // namespace

bool isLeapYear(int year) {
	return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
	if (month < 1 || month > 12) {
		return 0;
	}
	static const int kMonthDays[12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	if (month == 2 && isLeapYear(year)) {
		return 29;
	}
	return kMonthDays[month - 1];
}

// Howard Hinnant 的 days_from_civil（公历，支持 1970 之前）。
long long daysFromCivil(int year, int month, int day) {
	int y = year;
	y -= (month <= 2) ? 1 : 0;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);  // [0, 399]
	const unsigned doy = static_cast<unsigned>(
			(153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
	const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
	return static_cast<long long>(era) * 146097LL +
			static_cast<long long>(doe) - 719468LL;
}

// Howard Hinnant 的 civil_from_days。
void civilFromDays(long long days, int& year, int& month, int& day) {
	const long long z = days + 719468LL;
	const long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
	const unsigned doe = static_cast<unsigned>(z - era * 146097LL);
	const unsigned yoe =
			(doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
	const int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
	const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
	const unsigned mp = (5U * doy + 2U) / 153U;
	const unsigned d = doy - (153U * mp + 2U) / 5U + 1U;
	const unsigned m = (mp < 10U) ? mp + 3U : mp - 9U;
	year = y + ((m <= 2U) ? 1 : 0);
	month = static_cast<int>(m);
	day = static_cast<int>(d);
}

bool isValidTzOffsetMinutes(int minutes) {
	return minutes >= kMinTzOffsetMinutes && minutes <= kMaxTzOffsetMinutes;
}

bool isValidDate(int year, int month, int day) {
	if (year < kMinYear || year > kMaxYear) {
		return false;
	}
	if (month < 1 || month > 12) {
		return false;
	}
	return day >= 1 && day <= daysInMonth(year, month);
}

bool isValidDateTime(const LocalDateTime& value) {
	return isValidDate(value.year, value.month, value.day) &&
			isValidTimeOfDay(value.hour, value.minute, value.second);
}

bool localDateTimeToEpoch(const LocalDateTime& local, int tzOffsetMinutes,
		long long& epochOut) {
	// 只用通用公历校验：本函数是纯换算工具，支持 1970 前后；
	// “目标日期 2000..2099”由 isValidDate / TimerEngine::targetEpoch 约束。
	if (!isCalendarDateValid(local.year, local.month, local.day) ||
			!isValidTimeOfDay(local.hour, local.minute, local.second) ||
			!isValidTzOffsetMinutes(tzOffsetMinutes)) {
		return false;
	}
	const long long days = daysFromCivil(local.year, local.month, local.day);
	const long long epoch = days * kSecondsPerDay +
			static_cast<long long>(local.hour) * kSecondsPerHour +
			static_cast<long long>(local.minute) * 60LL +
			static_cast<long long>(local.second) -
			static_cast<long long>(tzOffsetMinutes) * 60LL;
	epochOut = epoch;
	return true;
}

bool epochToLocalDateTime(long long epoch, int tzOffsetMinutes,
		LocalDateTime& localOut) {
	if (!isValidTzOffsetMinutes(tzOffsetMinutes)) {
		return false;
	}
	const long long localSeconds =
			epoch + static_cast<long long>(tzOffsetMinutes) * 60LL;
	const long long days = floorDiv(localSeconds, kSecondsPerDay);
	const long long secondOfDay = localSeconds - days * kSecondsPerDay;

	LocalDateTime value;
	civilFromDays(days, value.year, value.month, value.day);
	value.hour = static_cast<int>(secondOfDay / kSecondsPerHour);
	value.minute = static_cast<int>((secondOfDay % kSecondsPerHour) / 60LL);
	value.second = static_cast<int>(secondOfDay % 60LL);
	localOut = value;
	return true;
}

int weekdayFromEpoch(long long epoch) {
	// 1970-01-01 为周四：返回 1=周一 .. 7=周日
	const long long days = floorDiv(epoch, kSecondsPerDay);
	long long index = (days + 3) % 7;  // 0=周一
	if (index < 0) {
		index += 7;
	}
	return static_cast<int>(index) + 1;
}

bool parseDate(const std::string& text, int& year, int& month, int& day) {
	if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
		return false;
	}
	static const size_t kDigitPositions[8] = { 0, 1, 2, 3, 5, 6, 8, 9 };
	for (size_t i = 0; i < 8; ++i) {
		if (!isDigitChar(text[kDigitPositions[i]])) {
			return false;
		}
	}
	const int y = digitValue(text[0]) * 1000 + digitValue(text[1]) * 100 +
			digitValue(text[2]) * 10 + digitValue(text[3]);
	const int m = digitValue(text[5]) * 10 + digitValue(text[6]);
	const int d = digitValue(text[8]) * 10 + digitValue(text[9]);
	if (!isValidDate(y, m, d)) {
		return false;
	}
	year = y;
	month = m;
	day = d;
	return true;
}

bool parseTimeHMS(const std::string& text, int& hour, int& minute,
		int& second) {
	if (text.size() != 8 || text[2] != ':' || text[5] != ':') {
		return false;
	}
	static const size_t kDigitPositions[6] = { 0, 1, 3, 4, 6, 7 };
	for (size_t i = 0; i < 6; ++i) {
		if (!isDigitChar(text[kDigitPositions[i]])) {
			return false;
		}
	}
	const int h = digitValue(text[0]) * 10 + digitValue(text[1]);
	const int m = digitValue(text[3]) * 10 + digitValue(text[4]);
	const int s = digitValue(text[6]) * 10 + digitValue(text[7]);
	if (h > 23 || m > 59 || s > 59) {
		return false;
	}
	hour = h;
	minute = m;
	second = s;
	return true;
}

bool parseDateTime(const std::string& text, LocalDateTime& out) {
	if (text.size() != 19 || text[10] != ' ') {
		return false;
	}
	LocalDateTime value;
	if (!parseDate(text.substr(0, 10), value.year, value.month, value.day)) {
		return false;
	}
	if (!parseTimeHMS(text.substr(11), value.hour, value.minute,
				value.second)) {
		return false;
	}
	out = value;
	return true;
}

std::string formatDate(const LocalDateTime& value) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d", value.year, value.month,
			value.day);
	return std::string(buf);
}

std::string formatTimeHMS(const LocalDateTime& value) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d", value.hour, value.minute,
			value.second);
	return std::string(buf);
}

std::string formatDateTime(const LocalDateTime& value) {
	return formatDate(value) + " " + formatTimeHMS(value);
}

std::string formatEpochLocalTime(long long epoch, int tzOffsetMinutes) {
	LocalDateTime local;
	if (!epochToLocalDateTime(epoch, tzOffsetMinutes, local)) {
		// 仅当 tzOffsetMinutes 越界时发生；调用方应已校验配置
		return std::string();
	}
	return formatTimeHMS(local);
}

std::string formatCountdown(long long seconds, bool& useSmallFont) {
	if (seconds < 0) {
		seconds = 0;
	}
	long long hour = 0;
	long long minute = 0;
	long long second = 0;
	std::string text;
	if (seconds < 100LL * kSecondsPerHour) {
		// < 100h：HH:MM:SS（小时 00..99）
		useSmallFont = false;
		hour = seconds / kSecondsPerHour;
		minute = (seconds % kSecondsPerHour) / 60LL;
		second = seconds % 60LL;
		appendTwoDigits(text, static_cast<int>(hour));
		text.push_back(':');
		appendTwoDigits(text, static_cast<int>(minute));
		text.push_back(':');
		appendTwoDigits(text, static_cast<int>(second));
		return text;
	}
	// >= 100h：DdHH:MM:SS（天数不补零，时分秒两位）
	useSmallFont = true;
	const long long days = seconds / kSecondsPerDay;
	const long long rest = seconds % kSecondsPerDay;
	hour = rest / kSecondsPerHour;
	minute = (rest % kSecondsPerHour) / 60LL;
	second = rest % 60LL;

	char dayBuf[24];
	snprintf(dayBuf, sizeof(dayBuf), "%lld", days);
	text += dayBuf;
	text.push_back('d');
	appendTwoDigits(text, static_cast<int>(hour));
	text.push_back(':');
	appendTwoDigits(text, static_cast<int>(minute));
	text.push_back(':');
	appendTwoDigits(text, static_cast<int>(second));
	return text;
}

std::string formatDurationHMS(long long seconds) {
	if (seconds < 0) {
		seconds = 0;
	}
	char buf[40];
	snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
			seconds / kSecondsPerHour,
			(seconds % kSecondsPerHour) / 60LL, seconds % 60LL);
	return std::string(buf);
}
