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

// A Cobalt day-index assigns to each date of the calendar on or after
// Junuary 1, 1970 a non-negative integer index with
// January 1, 1970 being assigned the index 0. For example
//
// Calendar Date            Cobalt day-index
// ----------              ----------------
// January 1, 1970          0
// January 2, 1970          1
// February 1, 1970         31
// October 18, 2016         17,092
//
// Notice that a day-index does not represent a fixed 24-hour period of real
// time because the day with day-index n has a different meaning in
// different time-zones. Thus there is no well-defined mapping from an
// instant of time to a day index.
//
// For the purpose of aggregating observations for analysis and reporting,
// Cobalt uses three types of epochs: Day, Week, and Month. A week epoch
// is a squence of 7 days from a Sunday to a Saturday. A month epoch is a
// sequence of days in a calendar month.
//
// Each week and month epoch containing days on or after January 1, 1970 is
// also given a zero-based index as follows:
//
// Calendar Week                   Cobalt week epoch index
// ----------------------------    -----------------------
// Thu 1970-1-1  - Sat 1970-1-3     0
// Sun 1970-1-4  - Sat 1970-1-10    1
// Sun 1970-1-11 - Sun 1970-1-17    2
// Sun 1970-1-18 - Sun 1970-1-24    3
// etc.
//
//
// Calendar Month     Cobalt month epoch index
// --------------     -----------------------
// January, 1970      0
// February, 1970     1
// March, 1970        2
// etc.

#ifndef COBALT_SRC_LIB_UTIL_DATETIME_UTIL_H_
#define COBALT_SRC_LIB_UTIL_DATETIME_UTIL_H_

#include <atomic>
#include <chrono>
#include <cstdint>

#include "src/registry/cobalt_registry.pb.h"
#include "src/registry/metric_definition.pb.h"

namespace cobalt {
namespace util {

// Unix seconds differ from physical seconds on a day in which there is
// a leap second.
static const uint32_t kNumUnixSecondsPerDay = 60L * 60L * 24L;

static const uint32_t kNumHoursPerDay = 24L;

static const uint32_t kInvalidIndex = UINT32_MAX;

static const uint32_t kDefaultCalendarYear = 1970;

// A CalendarDate represents a day on the calendar using normal human-readable
// indexing. Just as with day-index there is no well-defined mapping from an
// instant of time to a CalendarDate because this depends on the timezone.
struct CalendarDate {
  // A number from 1 to 31.
  uint32_t day_of_month = 1;

  // 1 = January, 2 = February, ..., 12 = December
  uint32_t month = 1;

  // Calendar year e.g. 2016.
  uint32_t year = kDefaultCalendarYear;

  bool operator==(const CalendarDate& other) const {
    return (other.day_of_month == day_of_month && other.month == month && other.year == year);
  }
};

// Returns the hour index relative to the beginning of the Unix epoch of the hour containing |time|
// in |time_zone|, or returns UINT32_MAX if |time_zone| is not valid.
uint32_t TimeToHourIndex(time_t time, MetricDefinition::TimeZonePolicy time_zone);

// Returns the day index corresponding to |time| in the given |time_zone|
// or UINT32_MAX if |time_zone| is not valid. |time| must be a Unix timestamp,
// that is a number of Unix seconds since the Unix epoch.
uint32_t TimeToDayIndex(time_t time, MetricDefinition::TimeZonePolicy time_zone);

// Returns the Unix time for midnight of the day with the given |day_index|
// in UTC.
time_t MidnightUtcFromDayIndex(uint32_t day_index);

// Converts the given CalendarDate to a Cobalt Day Index. If the fields of
// calendar_date do not make sense as a real day of the calendar
// (for example if month=13) then the result is undefined. If the
// specified date is prior to January 1, 1970 or not before the year 10,000
// then the result is undefined.
uint32_t CalendarDateToDayIndex(const CalendarDate& calendar_date);

// Converts the given day_index to a CalendarDate, necessarily on or
// after January 1, 1970.
CalendarDate DayIndexToCalendarDate(uint32_t day_index);

// Given a day_index returns the index of the Cobalt week epoch
// containing that date.
uint32_t DayIndexToWeekIndex(uint32_t day_index);

// Given an hour_index, returns the index of the Cobalt day containing that hour.
uint32_t HourIndexToDayIndex(uint32_t hour_index);

// Given a CalendarDate returns the index of the Cobalt week epoch
// containing that date. If the fields of calendar_date do not make sense as a
// real day of the calendar (for example if month=13) then the result is
// undefined. If the specified date is prior to January 1, 1970 then the result
// is undefined.
uint32_t CalendarDateToWeekIndex(const CalendarDate& calendar_date);

// Given the index of a Cobalt week epoch returns the CalendarDate for the first
// day of that week epoch. In all cases except week_index=0 the
// returned date will be a Sunday. If week_index=0 the returned date will
// be day zero: Thu 1970-1-1.
CalendarDate WeekIndexToCalendarDate(uint32_t week_index);

// Given a day_index returns the index of the Cobalt month epoch
// containing that date.
uint32_t DayIndexToMonthIndex(uint32_t day_index);

// Given a CalendarDate returns the index of the Cobalt month epoch
// containing that date. If the fields of
// calendar_date do not make sense as a real day of the calendar (for example if
// month=13) then the result is undefined. If the specified date is prior to
// January 1, 1970 then the result is undefined.
uint32_t CalendarDateToMonthIndex(const CalendarDate& calendar_date);

// Given the index of a Cobalt month epoch returns the CalendarDate for the
// first day of that month epoch.
CalendarDate MonthIndexToCalendarDate(uint32_t month_index);

// Returns the the given time as a number of seconds since the Unix epoch.
int64_t ToUnixSeconds(std::chrono::system_clock::time_point t);

// Returns the given number of seconds since the Unix epoch as a time_point.
std::chrono::system_clock::time_point FromUnixSeconds(int64_t seconds);

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_UTIL_DATETIME_UTIL_H_
