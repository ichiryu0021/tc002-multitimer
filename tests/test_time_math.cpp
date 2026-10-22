// Host tests for timer/TimeMath (pure C++ calendar/timezone/format helpers).
#include "test_framework.h"

#include <string>

#include "timer/TimeMath.h"

namespace {

LocalDateTime makeLocal(int year, int month, int day, int hour, int minute,
		int second) {
	LocalDateTime value;
	value.year = year;
	value.month = month;
	value.day = day;
	value.hour = hour;
	value.minute = minute;
	value.second = second;
	return value;
}

long long epochOf(const LocalDateTime& local, int tzOffsetMinutes) {
	long long epoch = 0;
	localDateTimeToEpoch(local, tzOffsetMinutes, epoch);
	return epoch;
}

void expectLocal(const LocalDateTime& actual, int year, int month, int day,
		int hour, int minute, int second) {
	TF_EQ(actual.year, year);
	TF_EQ(actual.month, month);
	TF_EQ(actual.day, day);
	TF_EQ(actual.hour, hour);
	TF_EQ(actual.minute, minute);
	TF_EQ(actual.second, second);
}

}  // namespace

TF_TEST(timemath_leap_year) {
	TF_CHECK(isLeapYear(2000));
	TF_CHECK(isLeapYear(2024));
	TF_CHECK(isLeapYear(2028));
	TF_CHECK(isLeapYear(1600));
	TF_CHECK(isLeapYear(2400));

	TF_CHECK(!isLeapYear(2100));
	TF_CHECK(!isLeapYear(1900));
	TF_CHECK(!isLeapYear(2023));
	TF_CHECK(!isLeapYear(2021));
	TF_CHECK(!isLeapYear(2025));
}

TF_TEST(timemath_days_in_month) {
	const int kCommonYear[12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	for (int month = 1; month <= 12; ++month) {
		TF_EQ(daysInMonth(2023, month), kCommonYear[month - 1]);
	}
	TF_EQ(daysInMonth(2024, 2), 29);
	TF_EQ(daysInMonth(2000, 2), 29);
	TF_EQ(daysInMonth(2100, 2), 28);
	TF_EQ(daysInMonth(1900, 2), 28);

	TF_EQ(daysInMonth(2024, 0), 0);
	TF_EQ(daysInMonth(2024, 13), 0);
	TF_EQ(daysInMonth(2024, -1), 0);
}

TF_TEST(timemath_days_from_civil_anchors) {
	TF_EQ(daysFromCivil(1970, 1, 1), 0LL);
	TF_EQ(daysFromCivil(1970, 1, 2), 1LL);
	TF_EQ(daysFromCivil(1970, 1, 31), 30LL);
	TF_EQ(daysFromCivil(1970, 2, 1), 31LL);
	TF_EQ(daysFromCivil(1969, 12, 31), -1LL);
	TF_EQ(daysFromCivil(1969, 1, 1), -365LL);
	TF_EQ(daysFromCivil(1900, 1, 1), -25567LL);
	TF_EQ(daysFromCivil(2000, 1, 1), 10957LL);
	TF_EQ(daysFromCivil(2024, 1, 1), 19723LL);
	TF_EQ(daysFromCivil(2026, 10, 22), 20748LL);
	TF_EQ(daysFromCivil(2026, 12, 31), 20818LL);
	TF_EQ(daysFromCivil(2027, 1, 1), 20819LL);
	TF_EQ(daysFromCivil(2028, 2, 29), 21243LL);
	TF_EQ(daysFromCivil(2038, 1, 19), 24855LL);
	TF_EQ(daysFromCivil(2100, 1, 1), 47482LL);
}

TF_TEST(timemath_civil_from_days_anchors) {
	int year = 0;
	int month = 0;
	int day = 0;

	civilFromDays(0, year, month, day);
	TF_EQ(year, 1970);
	TF_EQ(month, 1);
	TF_EQ(day, 1);

	civilFromDays(-1, year, month, day);
	TF_EQ(year, 1969);
	TF_EQ(month, 12);
	TF_EQ(day, 31);

	civilFromDays(20748, year, month, day);
	TF_EQ(year, 2026);
	TF_EQ(month, 10);
	TF_EQ(day, 22);

	civilFromDays(47482, year, month, day);
	TF_EQ(year, 2100);
	TF_EQ(month, 1);
	TF_EQ(day, 1);

	civilFromDays(24855, year, month, day);
	TF_EQ(year, 2038);
	TF_EQ(month, 1);
	TF_EQ(day, 19);

	civilFromDays(-25567, year, month, day);
	TF_EQ(year, 1900);
	TF_EQ(month, 1);
	TF_EQ(day, 1);

	civilFromDays(21243, year, month, day);
	TF_EQ(year, 2028);
	TF_EQ(month, 2);
	TF_EQ(day, 29);
}

TF_TEST(timemath_civil_round_trip_days) {
	for (long long days = -80000; days <= 80000; days += 997) {
		int year = 0;
		int month = 0;
		int day = 0;
		civilFromDays(days, year, month, day);
		TF_EQ(daysFromCivil(year, month, day), days);
	}
}

TF_TEST(timemath_civil_round_trip_months) {
	for (int year = 1600; year <= 2400; year += 7) {
		for (int month = 1; month <= 12; ++month) {
			const int lastDay = daysInMonth(year, month);
			int outYear = 0;
			int outMonth = 0;
			int outDay = 0;
			civilFromDays(daysFromCivil(year, month, lastDay), outYear,
					outMonth, outDay);
			TF_EQ(outYear, year);
			TF_EQ(outMonth, month);
			TF_EQ(outDay, lastDay);
		}
	}
}

TF_TEST(timemath_valid_tz_offset_minutes) {
	TF_CHECK(isValidTzOffsetMinutes(0));
	TF_CHECK(isValidTzOffsetMinutes(480));
	TF_CHECK(isValidTzOffsetMinutes(-300));
	TF_CHECK(isValidTzOffsetMinutes(330));   // +05:30
	TF_CHECK(isValidTzOffsetMinutes(345));   // +05:45
	TF_CHECK(isValidTzOffsetMinutes(765));   // +12:45
	TF_CHECK(isValidTzOffsetMinutes(-720));  // -12:00
	TF_CHECK(isValidTzOffsetMinutes(840));   // +14:00

	TF_CHECK(!isValidTzOffsetMinutes(-721));
	TF_CHECK(!isValidTzOffsetMinutes(841));
	TF_CHECK(!isValidTzOffsetMinutes(-1000));
	TF_CHECK(!isValidTzOffsetMinutes(1000));
}

TF_TEST(timemath_valid_date) {
	TF_CHECK(isValidDate(2000, 1, 1));
	TF_CHECK(isValidDate(2099, 12, 31));
	TF_CHECK(isValidDate(2024, 2, 29));
	TF_CHECK(isValidDate(2026, 10, 22));
	TF_CHECK(isValidDate(2026, 12, 31));

	TF_CHECK(!isValidDate(1999, 12, 31));
	TF_CHECK(!isValidDate(2100, 1, 1));
	TF_CHECK(!isValidDate(2100, 2, 29));
	TF_CHECK(!isValidDate(2023, 2, 29));
	TF_CHECK(!isValidDate(2023, 4, 31));
	TF_CHECK(!isValidDate(2023, 0, 10));
	TF_CHECK(!isValidDate(2023, 13, 10));
	TF_CHECK(!isValidDate(2023, 1, 0));
	TF_CHECK(!isValidDate(2023, 1, 32));
	TF_CHECK(!isValidDate(0, 1, 1));
	TF_CHECK(!isValidDate(-1, 1, 1));
}

TF_TEST(timemath_valid_date_time) {
	TF_CHECK(isValidDateTime(makeLocal(2026, 10, 22, 16, 38, 29)));
	TF_CHECK(isValidDateTime(makeLocal(2026, 10, 22, 23, 59, 59)));
	TF_CHECK(isValidDateTime(makeLocal(2026, 10, 22, 0, 0, 0)));

	TF_CHECK(!isValidDateTime(makeLocal(2026, 10, 22, 24, 0, 0)));
	TF_CHECK(!isValidDateTime(makeLocal(2026, 10, 22, 0, 60, 0)));
	TF_CHECK(!isValidDateTime(makeLocal(2026, 10, 22, 0, 0, 60)));
	TF_CHECK(!isValidDateTime(makeLocal(2026, 10, 22, -1, 0, 0)));
	TF_CHECK(!isValidDateTime(makeLocal(2023, 2, 29, 12, 0, 0)));
	TF_CHECK(!isValidDateTime(makeLocal(1999, 12, 31, 23, 59, 59)));
}

TF_TEST(timemath_local_date_time_to_epoch) {
	long long epoch = 0;

	TF_CHECK(localDateTimeToEpoch(makeLocal(1970, 1, 1, 0, 0, 0), 0, epoch));
	TF_EQ(epoch, 0LL);

	// Local midnight at UTC-5 is 05:00Z.
	TF_CHECK(localDateTimeToEpoch(makeLocal(1970, 1, 1, 0, 0, 0), -300,
			epoch));
	TF_EQ(epoch, 18000LL);

	// Local midnight at UTC+5:30 is 18:30Z of the previous day.
	TF_CHECK(localDateTimeToEpoch(makeLocal(1970, 1, 1, 0, 0, 0), 330,
			epoch));
	TF_EQ(epoch, -19800LL);

	// DESIGN sample: 2026-10-22 16:38:29 at UTC+8 == 1792658309.
	TF_CHECK(localDateTimeToEpoch(makeLocal(2026, 10, 22, 16, 38, 29), 480,
			epoch));
	TF_EQ(epoch, 1792658309LL);

	// Same wall time read as UTC.
	TF_CHECK(localDateTimeToEpoch(makeLocal(2026, 10, 22, 16, 38, 29), 0,
			epoch));
	TF_EQ(epoch, 1792687109LL);

	// 1969-12-31 23:59:59 UTC == -1.
	TF_CHECK(localDateTimeToEpoch(makeLocal(1969, 12, 31, 23, 59, 59), 0,
			epoch));
	TF_EQ(epoch, -1LL);

	// 1900-01-01 00:00:00 UTC is a pre-1970 anchor.
	TF_CHECK(localDateTimeToEpoch(makeLocal(1900, 1, 1, 0, 0, 0), 0, epoch));
	TF_EQ(epoch, -2208988800LL);

	// 2100-01-01 00:00:00 UTC (not a target date, but a valid conversion).
	TF_CHECK(localDateTimeToEpoch(makeLocal(2100, 1, 1, 0, 0, 0), 0, epoch));
	TF_EQ(epoch, 4102444800LL);

	// Generic calendar dates convert; the 2000..2099 target range is enforced
	// by isValidDate / TimerEngine::targetEpoch instead.
	TF_CHECK(!localDateTimeToEpoch(makeLocal(10000, 1, 1, 0, 0, 0), 0,
			epoch));
	TF_CHECK(!localDateTimeToEpoch(makeLocal(0, 1, 1, 0, 0, 0), 0, epoch));

	TF_CHECK(!localDateTimeToEpoch(makeLocal(2023, 2, 29, 0, 0, 0), 0,
			epoch));
	TF_CHECK(!localDateTimeToEpoch(makeLocal(2026, 10, 22, 24, 0, 0), 0,
			epoch));
	TF_CHECK(!localDateTimeToEpoch(makeLocal(2026, 10, 22, 0, 0, 0), 900,
			epoch));
	TF_CHECK(!localDateTimeToEpoch(makeLocal(2026, 10, 22, 0, 0, 0), -721,
			epoch));
}

TF_TEST(timemath_epoch_to_local_date_time) {
	LocalDateTime local;

	TF_CHECK(epochToLocalDateTime(0, 0, local));
	expectLocal(local, 1970, 1, 1, 0, 0, 0);

	// Negative local time: epoch 0 at UTC-5 is 1969-12-31 19:00.
	TF_CHECK(epochToLocalDateTime(0, -300, local));
	expectLocal(local, 1969, 12, 31, 19, 0, 0);

	TF_CHECK(epochToLocalDateTime(0, 330, local));
	expectLocal(local, 1970, 1, 1, 5, 30, 0);

	// Negative epoch must be floored, not truncated toward zero.
	TF_CHECK(epochToLocalDateTime(-1, 0, local));
	expectLocal(local, 1969, 12, 31, 23, 59, 59);

	TF_CHECK(epochToLocalDateTime(-1, 480, local));
	expectLocal(local, 1970, 1, 1, 7, 59, 59);

	TF_CHECK(epochToLocalDateTime(-86400, 0, local));
	expectLocal(local, 1969, 12, 31, 0, 0, 0);

	TF_CHECK(epochToLocalDateTime(-86401, 0, local));
	expectLocal(local, 1969, 12, 30, 23, 59, 59);

	TF_CHECK(epochToLocalDateTime(1792658309LL, 480, local));
	expectLocal(local, 2026, 10, 22, 16, 38, 29);

	TF_CHECK(epochToLocalDateTime(4102444800LL, 0, local));
	expectLocal(local, 2100, 1, 1, 0, 0, 0);

	TF_CHECK(epochToLocalDateTime(4102444800LL, 765, local));
	expectLocal(local, 2100, 1, 1, 12, 45, 0);

	TF_CHECK(!epochToLocalDateTime(0, 841, local));
	TF_CHECK(!epochToLocalDateTime(0, -721, local));
}

TF_TEST(timemath_epoch_round_trip_wall_clock) {
	const int tzValues[] = { -720, -300, -1, 0, 330, 345, 480, 765, 840 };
	const long long epochs[] = {
		-1LL, 0LL, -86400LL, 86400LL, 1792658309LL, 2147483647LL,
		4102444800LL
	};
	for (size_t i = 0; i < sizeof(tzValues) / sizeof(tzValues[0]); ++i) {
		for (size_t j = 0; j < sizeof(epochs) / sizeof(epochs[0]); ++j) {
			LocalDateTime local;
			TF_CHECK(epochToLocalDateTime(epochs[j], tzValues[i], local));
			long long back = 0;
			TF_CHECK(localDateTimeToEpoch(local, tzValues[i], back));
			TF_EQ(back, epochs[j]);
		}
	}
}

TF_TEST(timemath_weekday_from_epoch) {
	// 1970-01-01 was a Thursday; 1=Monday .. 7=Sunday.
	TF_EQ(weekdayFromEpoch(0), 4);
	TF_EQ(weekdayFromEpoch(86400), 5);
	TF_EQ(weekdayFromEpoch(3 * 86400), 7);
	TF_EQ(weekdayFromEpoch(-1), 3);
	TF_EQ(weekdayFromEpoch(-3 * 86400), 1);
	TF_EQ(weekdayFromEpoch(daysFromCivil(2026, 10, 22) * 86400), 4);
	TF_EQ(weekdayFromEpoch(daysFromCivil(2024, 1, 1) * 86400), 1);
	TF_EQ(weekdayFromEpoch(daysFromCivil(2100, 1, 1) * 86400), 5);
	TF_EQ(weekdayFromEpoch(daysFromCivil(1900, 1, 1) * 86400), 1);
}

TF_TEST(timemath_parse_date_ok) {
	int year = 0;
	int month = 0;
	int day = 0;

	TF_CHECK(parseDate("2026-10-23", year, month, day));
	TF_EQ(year, 2026);
	TF_EQ(month, 10);
	TF_EQ(day, 23);

	TF_CHECK(parseDate("2000-01-01", year, month, day));
	TF_EQ(year, 2000);
	TF_EQ(month, 1);
	TF_EQ(day, 1);

	TF_CHECK(parseDate("2099-12-31", year, month, day));
	TF_EQ(year, 2099);
	TF_EQ(month, 12);
	TF_EQ(day, 31);

	TF_CHECK(parseDate("2024-02-29", year, month, day));
	TF_EQ(year, 2024);
	TF_EQ(month, 2);
	TF_EQ(day, 29);
}

TF_TEST(timemath_parse_date_rejects) {
	int year = 0;
	int month = 0;
	int day = 0;
	const char* bad[] = {
		"", "2026-10-2", "2026-10-233", "26-10-23", "2026/10/23",
		"2026-1-23", "2026-10-3", " 2026-10-23", "2026-10-23 ",
		"2026-10-2a", "20a6-10-23", "2026-00-10", "2026-13-10",
		"2026-02-30", "2023-02-29", "2026-04-31", "2026-00-00",
		"1999-12-31", "2100-01-01", "0000-01-01", "2026-10-00",
		"2026-10-32"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TF_CHECK_MSG(!parseDate(bad[i], year, month, day),
				std::string("accepted invalid date: ") + bad[i]);
	}
}

TF_TEST(timemath_parse_time_ok) {
	int hour = 0;
	int minute = 0;
	int second = 0;

	TF_CHECK(parseTimeHMS("00:00:00", hour, minute, second));
	TF_EQ(hour, 0);
	TF_EQ(minute, 0);
	TF_EQ(second, 0);

	TF_CHECK(parseTimeHMS("23:59:59", hour, minute, second));
	TF_EQ(hour, 23);
	TF_EQ(minute, 59);
	TF_EQ(second, 59);

	TF_CHECK(parseTimeHMS("19:25:00", hour, minute, second));
	TF_EQ(hour, 19);
	TF_EQ(minute, 25);
	TF_EQ(second, 0);

	TF_CHECK(parseTimeHMS("05:30:45", hour, minute, second));
	TF_EQ(hour, 5);
	TF_EQ(minute, 30);
	TF_EQ(second, 45);
}

TF_TEST(timemath_parse_time_rejects) {
	int hour = 0;
	int minute = 0;
	int second = 0;
	const char* bad[] = {
		"", "19:25", "19:25:0", "19:25:000", "9:25:00", "019:25:00",
		"24:00:00", "23:60:00", "23:59:60", "-1:00:00", "19-25-00",
		"19:25:00 ", " 19:25:00", "ab:cd:ef", "19:2a:00", "19:25:0a",
		"19::25:00"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TF_CHECK_MSG(!parseTimeHMS(bad[i], hour, minute, second),
				std::string("accepted invalid time: ") + bad[i]);
	}
}

TF_TEST(timemath_parse_date_time) {
	LocalDateTime value;
	TF_CHECK(parseDateTime("2026-10-23 19:25:00", value));
	expectLocal(value, 2026, 10, 23, 19, 25, 0);

	TF_CHECK(parseDateTime("2000-01-01 00:00:00", value));
	expectLocal(value, 2000, 1, 1, 0, 0, 0);

	const char* bad[] = {
		"", "2026-10-23", "2026-10-23 19:25", "2026-10-23T19:25:00",
		"2026-10-23  19:25:00", "2026-10-23 19:25:0",
		"2026-10-23 19:25:000", "2026-10-23 24:25:00",
		"2026-02-30 10:00:00", "2026-10-23 19:25:00 ",
		" 2026-10-23 19:25:00", "2026/10/23 19:25:00",
		"2026-10-23 19:25:00x"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		TF_CHECK_MSG(!parseDateTime(bad[i], value),
				std::string("accepted invalid datetime: ") + bad[i]);
	}
}

TF_TEST(timemath_format_date_time) {
	TF_EQ(formatDate(makeLocal(2026, 1, 2, 3, 4, 5)), std::string("2026-01-02"));
	TF_EQ(formatDate(makeLocal(2000, 12, 31, 0, 0, 0)),
			std::string("2000-12-31"));
	TF_EQ(formatTimeHMS(makeLocal(2026, 10, 22, 3, 4, 5)),
			std::string("03:04:05"));
	TF_EQ(formatTimeHMS(makeLocal(2026, 10, 22, 23, 59, 59)),
			std::string("23:59:59"));
	TF_EQ(formatDateTime(makeLocal(2026, 10, 22, 16, 38, 29)),
			std::string("2026-10-22 16:38:29"));
	TF_EQ(formatDateTime(makeLocal(2000, 1, 1, 0, 0, 0)),
			std::string("2000-01-01 00:00:00"));
}

TF_TEST(timemath_format_epoch_local_time) {
	TF_EQ(formatEpochLocalTime(1792658309LL, 480), std::string("16:38:29"));
	TF_EQ(formatEpochLocalTime(1792658309LL, 330), std::string("14:08:29"));
	TF_EQ(formatEpochLocalTime(0, 0), std::string("00:00:00"));
	TF_EQ(formatEpochLocalTime(0, -300), std::string("19:00:00"));
	TF_EQ(formatEpochLocalTime(-1, 480), std::string("07:59:59"));
	TF_EQ(formatEpochLocalTime(0, 765), std::string("12:45:00"));
	TF_EQ(formatEpochLocalTime(4102444800LL, 0), std::string("00:00:00"));
	// Invalid timezone offset cannot be formatted.
	TF_EQ(formatEpochLocalTime(0, 900), std::string());
}

TF_TEST(timemath_format_countdown_under_100h) {
	bool small = true;
	TF_EQ(formatCountdown(0, small), std::string("00:00:00"));
	TF_EQ(small, false);

	TF_EQ(formatCountdown(5, small), std::string("00:00:05"));
	TF_EQ(formatCountdown(59, small), std::string("00:00:59"));
	TF_EQ(formatCountdown(60, small), std::string("00:01:00"));
	TF_EQ(formatCountdown(61, small), std::string("00:01:01"));
	TF_EQ(formatCountdown(599, small), std::string("00:09:59"));
	TF_EQ(formatCountdown(3599, small), std::string("00:59:59"));
	TF_EQ(formatCountdown(3600, small), std::string("01:00:00"));
	TF_EQ(formatCountdown(3661, small), std::string("01:01:01"));
	TF_EQ(formatCountdown(35999, small), std::string("09:59:59"));
	TF_EQ(formatCountdown(36000, small), std::string("10:00:00"));
	TF_EQ(formatCountdown(86399, small), std::string("23:59:59"));

	// 24h..99:59:59 still renders as HH:MM:SS with a two-digit hour.
	TF_EQ(formatCountdown(86400, small), std::string("24:00:00"));
	TF_EQ(small, false);
	TF_EQ(formatCountdown(86459, small), std::string("24:00:59"));
	TF_EQ(small, false);

	// 99:59:59 is the last value rendered as HH:MM:SS.
	TF_EQ(formatCountdown(359999, small), std::string("99:59:59"));
	TF_EQ(small, false);

	// Negative input clamps to zero.
	TF_EQ(formatCountdown(-1, small), std::string("00:00:00"));
	TF_EQ(small, false);
}

TF_TEST(timemath_format_countdown_from_100h) {
	bool small = false;

	// 100h exactly switches to day format and small font.
	TF_EQ(formatCountdown(360000, small), std::string("4d04:00:00"));
	TF_EQ(small, true);

	TF_EQ(formatCountdown(360001, small), std::string("4d04:00:01"));
	// Day format starts at 100h, so the smallest "Nd..." values are 4d/5d.
	TF_EQ(formatCountdown(4 * 86400 + 4 * 3600 + 5 * 60 + 6, small),
			std::string("4d04:05:06"));
	TF_EQ(formatCountdown(5 * 86400 + 12 * 3600 + 34 * 60 + 56, small),
			std::string("5d12:34:56"));
	TF_EQ(formatCountdown(99 * 86400 + 23 * 3600 + 59 * 60 + 59, small),
			std::string("99d23:59:59"));
	TF_EQ(formatCountdown(100 * 86400, small), std::string("100d00:00:00"));
	TF_EQ(formatCountdown(365LL * 86400LL, small),
			std::string("365d00:00:00"));
	TF_EQ(small, true);
}

TF_TEST(timemath_countdown_small_font_flag) {
	const long long samples[] = {
		0LL, 1LL, 359998LL, 359999LL, 360000LL, 360001LL, 86399LL,
		86400LL, 8640000LL
	};
	for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
		bool small = false;
		formatCountdown(samples[i], small);
		TF_EQ(small, samples[i] >= 100LL * 3600LL);
	}

	// The flag and the rendered shape must agree for arbitrary values.
	for (long long seconds = 0; seconds < 1000000; seconds += 7919) {
		bool small = false;
		const std::string text = formatCountdown(seconds, small);
		const bool hasDaySuffix = text.find('d') != std::string::npos;
		TF_EQ(small, hasDaySuffix);
		TF_EQ(small, seconds >= 360000);
	}
}

TF_TEST(timemath_format_duration_hms) {
	TF_EQ(formatDurationHMS(0), std::string("00:00:00"));
	TF_EQ(formatDurationHMS(1), std::string("00:00:01"));
	TF_EQ(formatDurationHMS(59), std::string("00:00:59"));
	TF_EQ(formatDurationHMS(60), std::string("00:01:00"));
	TF_EQ(formatDurationHMS(3599), std::string("00:59:59"));
	TF_EQ(formatDurationHMS(3600), std::string("01:00:00"));
	TF_EQ(formatDurationHMS(3661), std::string("01:01:01"));
	TF_EQ(formatDurationHMS(86399), std::string("23:59:59"));
	TF_EQ(formatDurationHMS(86400), std::string("24:00:00"));
	// Hours are not capped at two digits for pure durations.
	TF_EQ(formatDurationHMS(360000), std::string("100:00:00"));
	TF_EQ(formatDurationHMS(3600000), std::string("1000:00:00"));
	TF_EQ(formatDurationHMS(2147483647LL), std::string("596523:14:07"));
	TF_EQ(formatDurationHMS(-1), std::string("00:00:00"));
	TF_EQ(formatDurationHMS(-5000), std::string("00:00:00"));
}
