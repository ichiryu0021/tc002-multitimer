// 日历/时区/格式化工具：纯 C++11，可在 PC 上单测。
// 设备没有 tzdata，本地时间一律用“UTC + tzOffsetMinutes”计算。
#ifndef TIMER_TIMEMATH_H_
#define TIMER_TIMEMATH_H_

#include <stdint.h>
#include <string>

struct LocalDateTime {
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;

	LocalDateTime()
		: year(1970), month(1), day(1), hour(0), minute(0), second(0) {
	}
};

bool isLeapYear(int year);
int daysInMonth(int year, int month);

// 公历日期 -> 1970-01-01 起的天数（Howard Hinnant 算法，year 可 <1970）
long long daysFromCivil(int year, int month, int day);
// 天数 -> 公历日期
void civilFromDays(long long days, int& year, int& month, int& day);

bool isValidTzOffsetMinutes(int minutes);          // -720..840
bool isValidDate(int year, int month, int day);    // 2000..2099 + 日历合法
bool isValidDateTime(const LocalDateTime& value);

// 本地墙上时间 <=> UTC epoch（秒）。偏移按分钟，支持 +05:30/+05:45/+12:45。
bool localDateTimeToEpoch(const LocalDateTime& local, int tzOffsetMinutes,
		long long& epochOut);
bool epochToLocalDateTime(long long epoch, int tzOffsetMinutes,
		LocalDateTime& localOut);

// 1970-01-01 为周四；返回 1=周一 .. 7=周日
int weekdayFromEpoch(long long epoch);

// 严格解析："YYYY-MM-DD"、"HH:MM:SS"、"YYYY-MM-DD HH:MM:SS"
bool parseDate(const std::string& text, int& year, int& month, int& day);
bool parseTimeHMS(const std::string& text, int& hour, int& minute, int& second);
bool parseDateTime(const std::string& text, LocalDateTime& out);

std::string formatDate(const LocalDateTime& value);      // YYYY-MM-DD
std::string formatTimeHMS(const LocalDateTime& value);   // HH:MM:SS
std::string formatDateTime(const LocalDateTime& value);  // YYYY-MM-DD HH:MM:SS

// epoch + 时区 -> 本地 HH:MM:SS
std::string formatEpochLocalTime(long long epoch, int tzOffsetMinutes);

// 倒计时文本（秒）：
//   < 100h  -> "HH:MM:SS"（小时两位，最多 99）
//   >= 100h -> "DdHH:MM:SS"（天数不补零）
// 第二参数返回是否建议使用小号字体（scale=1）。
std::string formatCountdown(long long seconds, bool& useSmallFont);

// 调试/状态接口用的纯时长格式，始终 "HH:MM:SS"（小时可超过两位）
std::string formatDurationHMS(long long seconds);

#endif  // TIMER_TIMEMATH_H_
