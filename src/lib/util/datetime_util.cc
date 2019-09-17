// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/lib/util/datetime_util.h"

#include <ctime>

namespace cobalt::util {

namespace {

// The algorithm in this file was copied from
// http://howardhinnant.github.io/date_algorithms.html
// See that site for further explanation.

// Recall that a year is a leap year if it is a multiple of 4 that is not
// a multiple of 100 unless it is a multiple of 400. Thus the pattern of
// leap years is periodic with period 400 years.

// There are 146097 days in every 400 year era:
// 146097 = 365 * 400 + 100 - 3. Of the integers in the range [0, 399]
// there are 100 multiples of 4 but 3 of those are multiples of 100
// and not multiples of 400, namely 100, 200 and 300.
constexpr uint32_t kNumDaysPerEra = 146097;

// The algorithm below uses an epoch of March 1, year 0
// whereas our API uses an epoch of January 1, 1970.
// kEpochOffset is the number of days from 0-3-1 to 1970-1-1
// proof:
// The number of days from 0-3-1 to 2000-3-1 is 5*kNumDaysPerEra
// The number of days from 1970-3-1 to 2000-3-1 is 30*365 + 8
// because the following years were leap years: '72 '76 '80 '84 '88 '92 '96 2000
// The number of days from 1970-1-1 to 1970-3-1 is 59.
constexpr uint32_t kEpochOffset = kNumDaysPerEra * 5L - 30L * 365L - 8L - 59L;

using TimeInfo = struct tm;

constexpr uint32_t kEpochYearZero = 1970;
constexpr uint32_t kTimeInfoYearZero = 1900;
constexpr uint32_t kMonthsPerYear = 12;
constexpr uint32_t kDaysPerWeek = 7;
constexpr uint32_t kMaxDaysPerMonth = 31;
constexpr uint32_t kMaxCalendarYear = 10000;

CalendarDate TimeInfoToCalendarDate(const TimeInfo& time_info) {
  CalendarDate calendar_date;
  calendar_date.day_of_month = time_info.tm_mday;
  calendar_date.month = time_info.tm_mon + 1;
  calendar_date.year = time_info.tm_year + kTimeInfoYearZero;
  return calendar_date;
}

}  // namespace

template <typename T>
inline uint32_t TimeToHourIndex(time_t time, typename T::TimeZonePolicy time_zone) {
  TimeInfo time_info;
  switch (time_zone) {
    case T::LOCAL:
      localtime_r(&time, &time_info);
      break;
    case T::UTC:
      gmtime_r(&time, &time_info);
      break;
    default:
      return UINT32_MAX;
  }
  return time_info.tm_hour;
}

uint32_t TimeToHourIndex(time_t time, MetricDefinition::TimeZonePolicy time_zone) {
  return TimeToHourIndex<MetricDefinition>(time, time_zone);
}

template <typename T>
inline uint32_t TimeToDayIndex(time_t time, typename T::TimeZonePolicy time_zone) {
  TimeInfo time_info;
  switch (time_zone) {
    case T::LOCAL:
      localtime_r(&time, &time_info);
      break;
    case T::UTC:
      gmtime_r(&time, &time_info);
      break;
    default:
      return UINT32_MAX;
  }
  return CalendarDateToDayIndex(TimeInfoToCalendarDate(time_info));
}

uint32_t TimeToDayIndex(time_t time, MetricDefinition::TimeZonePolicy time_zone) {
  return TimeToDayIndex<MetricDefinition>(time, time_zone);
}

uint32_t CalendarDateToDayIndex(const CalendarDate& calendar_date) {
  // This implementation was copied from
  // http://howardhinnant.github.io/date_algorithms.html#days_from_civil.
  // This same code is also used in the function DayOrdinal() in
  // Google's civil_time.cc.
  //
  // There is an ostensibly simpler but less portable solution to this
  // problem presented in CalendarDateToDayIndexAltImpl() in
  // datetime_util_test.cc.

  if (calendar_date.year < kEpochYearZero || calendar_date.year >= kMaxCalendarYear ||
      calendar_date.month < 1 || calendar_date.month > kMonthsPerYear ||
      calendar_date.day_of_month < 1 || calendar_date.day_of_month > kMaxDaysPerMonth) {
    return kInvalidIndex;
  }

  // This algorithm counts years as beginning on March 1. Convert to that now.
  // This trick has the advantage that a leap day is the last day of the year.
  const uint32_t year = calendar_date.year - (calendar_date.month <= 2 ? 1 : 0);

  // Which 400-year era is it?
  const uint32_t era = year / 400;

  // The year of the era is the year mod 400.
  const uint32_t yoe = year - era * 400;

  // Now we compute the day of the year, where March 1 is day number 1.
  // The main idea is the following trick which uses integer division to
  // capture the set of months that have 30 instead of 31 days. Let n be a month
  // number where n = 1 means March, n = 2 means April, etc. Notice that
  // For n = 1...10, using integer division,  we have that
  // (3n+2)/5 = 1, 1, 2, 2, 3, 4, 4, 5, 5, 6. It follows that the expression
  // (153*n + 2)/5 yields the number of days from March 1 through the end
  // of month number n. To see this note that (153*n + 2)/5 = 30n + (3n+2)/5.
  // Plugging in to the formula we have
  // n=1 (March)  -> 30 +   1
  // n=2 (April)  -> 30*2 + 1
  // n=3 (May)    -> 30*3 + 2
  // n=4 (June)   -> 30*4 + 2
  // n=5 (July)   -> 30*5 + 3
  // n=6 (August) -> 30*6 + 4
  // etc.
  const uint32_t doy = (153 * (calendar_date.month + (calendar_date.month > 2 ? -3 : 9)) + 2) / 5 +
                       calendar_date.day_of_month - 1;

  // Now we compute the day of the era. This is relatively easy using
  // the formula for leap years described at the top of this file.
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

  // shift epoch from 0-03-01 to 1970-01-01
  return era * kNumDaysPerEra + doe - kEpochOffset;
}

time_t MidnightUtcFromDayIndex(uint32_t day_index) { return day_index * kNumUnixSecondsPerDay; }

CalendarDate DayIndexToCalendarDate(uint32_t day_index) {
  // This function is an inverse to CalendarDateToDayIndex.
  // But because gmtime_r is a standard function unlike timegm, we
  // use the more straightforward implementation in this direction.
  time_t unix_time = day_index * kNumUnixSecondsPerDay;
  TimeInfo time_info;
  gmtime_r(&unix_time, &time_info);
  return TimeInfoToCalendarDate(time_info);
}

uint32_t DayIndexToWeekIndex(uint32_t day_index) {
  // Day zero was a Thursday which is 4 days after Sunday.
  return (day_index + 4) / kDaysPerWeek;
}

uint32_t CalendarDateToWeekIndex(const CalendarDate& calendar_date) {
  return DayIndexToWeekIndex(CalendarDateToDayIndex(calendar_date));
}

CalendarDate WeekIndexToCalendarDate(uint32_t week_index) {
  // Day zero was a Thursday which is 4 days after Sunday.
  return DayIndexToCalendarDate(week_index * kDaysPerWeek - (week_index > 0 ? 4 : 0));
}

uint32_t DayIndexToMonthIndex(uint32_t day_index) {
  return CalendarDateToMonthIndex(DayIndexToCalendarDate(day_index));
}

uint32_t CalendarDateToMonthIndex(const CalendarDate& calendar_date) {
  if (calendar_date.year < kEpochYearZero || calendar_date.month < 1 ||
      calendar_date.month > kMonthsPerYear) {
    return UINT32_MAX;
  }
  return kMonthsPerYear * (calendar_date.year - kEpochYearZero) + calendar_date.month - 1;
}

CalendarDate MonthIndexToCalendarDate(uint32_t month_index) {
  CalendarDate calendar_date;
  calendar_date.day_of_month = 1;
  calendar_date.month = (month_index % kMonthsPerYear) + 1;
  calendar_date.year = month_index / kMonthsPerYear + kEpochYearZero;
  return calendar_date;
}

// Returns the the given time as a number of seconds since the Unix epoch.
int64_t ToUnixSeconds(std::chrono::system_clock::time_point t) {
  return (std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch())).count();
}

// Returns the given number of seconds since the Unix epoch as a time_point.
std::chrono::system_clock::time_point FromUnixSeconds(int64_t seconds) {
  return std::chrono::system_clock::time_point(
      std::chrono::system_clock::duration(std::chrono::seconds(seconds)));
}

}  // namespace cobalt::util
