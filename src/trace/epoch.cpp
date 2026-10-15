/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file epoch.cpp
 * @brief Timestamp parsing, formatting, and calendar calculations.
 */

#include <cstring>
#include <ctime>
#include <iomanip> // std::get_time()
#include <sstream>
// https://stackoverflow.com/questions/37552982/is-stdget-time-broken-in-g-and-clang
#include "epoch.hpp"
#include <array>

namespace dr_evt {

std::string to_string(const epoch_t &t) {
  const tm *timeinfo = std::localtime(&t.first);
  if (!timeinfo) {
    return "Invalid time";
  }
  char buffer[256] = {'\0'};
  strftime(buffer, 124, "%Y-%m-%d %H:%M:%S", timeinfo);
  char frac_buf[128] = {'\0'};
  sprintf(frac_buf, "%f", t.second);
  strncpy(buffer + strlen(buffer), frac_buf + 1, 127);
  return std::string(buffer);
}

std::string format_sim_time(sim_time_t t, bool msec) {
  std::ostringstream oss;
  if (msec) {
    oss << std::fixed << std::setprecision(3) << t;
  } else {
    oss << static_cast<int64_t>(t);
  }
  return oss.str();
}

std::ostream &operator<<(std::ostream &os, const epoch_t &t) {
  const tm *timeinfo = std::localtime(&t.first);
  if (!timeinfo) {
    os << "Invalid time";
    return os;
  }
  char buffer[256] = {'\0'};
  strftime(buffer, 124, "%Y-%m-%d %H:%M:%S", timeinfo);
  char frac_buf[128] = {'\0'};
  sprintf(frac_buf, "%f", t.second);
  strncpy(buffer + strlen(buffer), frac_buf + 1, 127);
  os << buffer;
  return os;
}

/*
 *  Check if the give string is timestamp
 */
bool is_timestamp(const std::string &time_str) {
  std::string normalized = time_str;
  if (normalized.size() > 10 && normalized[10] == 'T') {
    normalized[10] = ' ';
  }
  std::istringstream iss{normalized};
  std::tm t{};
  t.tm_isdst = -1;

  iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S"); // extract it into a std::tm
  return !(iss.fail());
}

/*
 *  Return seconds (epoch) converted from the time string given as well as the
 *  fractional second.
 */
epoch_t convert_time(const std::string &time_str) {
  // Accept both the historical "YYYY-MM-DD HH:MM:SS" spelling and the ISO
  // 8601 date/time separator. Offset-bearing values are handled separately
  // by parse_time_with_timezone().
  std::string normalized = time_str;
  if (normalized.size() > 10 && normalized[10] == 'T') {
    normalized[10] = ' ';
  }
  std::istringstream iss{normalized};
  std::tm t{};
  t.tm_isdst = -1;

  iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S"); // extract it into a std::tm
  if (iss.fail()) {
    throw std::invalid_argument{"Failed to parse time string: " + time_str};
  }

  // Find the beginning of the fractional second
  float frac = 0.0;
  const auto i_frac = time_str.find_last_of(".");
  if (i_frac != std::string::npos) {
    auto frac_str = "0." + time_str.substr(i_frac + 1, std::string::npos);
    frac = static_cast<float>(std::atof(frac_str.c_str()));
  }

  return std::make_pair(std::mktime(&t), frac);
}

/** @brief Parse a numeric timezone offset into signed seconds.
 * @param[in] tz_str Offset in `Z` or signed `HH:MM` form.
 * @return Signed offset from UTC in seconds. */
static int parse_timezone_offset(const std::string &tz_str) {
  if (tz_str.empty() || tz_str == "Z") {
    return 0; // UTC
  }

  // Format: ±HH:MM
  if (tz_str.length() < 5) {
    throw std::invalid_argument{"Invalid timezone format: " + tz_str};
  }

  int sign = (tz_str[0] == '-') ? -1 : 1;
  int hours = std::stoi(tz_str.substr(1, 2));
  int minutes = std::stoi(tz_str.substr(4, 2));

  return sign * (hours * 3600 + minutes * 60);
}

std::pair<epoch_t, std::string>
parse_time_with_timezone(const std::string &time_str) {
  // Find timezone offset (format: ±HH:MM or Z)
  std::string tz_offset = "+00:00";
  std::string time_part = time_str;
  bool has_explicit_offset = false;

  // Look for 'Z' (UTC) or ±HH:MM at the end
  size_t tz_pos = time_str.find_last_of("+-Z");
  if (tz_pos != std::string::npos && tz_pos > 10) {
    // Found timezone
    has_explicit_offset = true;
    tz_offset = time_str.substr(tz_pos);
    time_part = time_str.substr(0, tz_pos);

    if (tz_offset == "Z") {
      tz_offset = "+00:00";
    }
  }

  // Replace 'T' with space for parsing
  size_t t_pos = time_part.find('T');
  if (t_pos != std::string::npos) {
    time_part[t_pos] = ' ';
  }

  // Parse the date/time part
  std::istringstream iss{time_part};
  std::tm t{};
  t.tm_isdst = -1;

  iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
  if (iss.fail()) {
    throw std::invalid_argument{"Failed to parse time string: " + time_str};
  }

  // Parse fractional seconds
  float frac = 0.0;
  size_t frac_pos = time_part.find('.');
  if (frac_pos != std::string::npos) {
    std::string frac_str = "0." + time_part.substr(frac_pos + 1);
    frac = static_cast<float>(std::atof(frac_str.c_str()));
  }

  std::time_t utc_time;
  if (has_explicit_offset) {
    // The string states its own offset, so the wall-clock fields are
    // relative to that offset and the process timezone must not take
    // part. Convert the fields as UTC and subtract the stated offset.
    // Using mktime() here applied the process timezone a second time,
    // shifting every offset-bearing timestamp by the local UTC offset
    // (8 hours with the default America/Los_Angeles in January).
    utc_time = timegm(&t) - parse_timezone_offset(tz_offset);
  } else {
    // No offset in the string: interpret the wall-clock fields in the
    // process timezone, which Data_Columns::init() sets from --timezone.
    utc_time = std::mktime(&t);
  }

  return std::make_pair(std::make_pair(utc_time, frac), tz_offset);
}

std::string to_local_time_string(const epoch_t &t,
                                 const std::string &tz_offset) {
  // Convert UTC epoch to local time using timezone offset
  int offset_seconds = parse_timezone_offset(tz_offset);
  std::time_t local_time = t.first + offset_seconds;

  std::tm *tm_local = std::gmtime(&local_time);
  if (!tm_local) {
    return "Invalid time";
  }

  char buffer[64];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_local);

  // Add fractional seconds if present
  if (t.second > 0.0) {
    char frac_buffer[16];
    std::snprintf(frac_buffer, sizeof(frac_buffer), ".%.3f", t.second);
    return std::string(buffer) + frac_buffer;
  }

  return std::string(buffer);
}

hour_bin_id_t get_hour_bin_id(const std::time_t t) {
  const std::tm *tinfo = localtime(&t);
  if (tinfo == nullptr) {
    throw std::invalid_argument{"Unable to convert std::time_t to std::tm!"};
  }
  return static_cast<hour_bin_id_t>(tinfo->tm_wday * 24 + tinfo->tm_hour);
}

/** @brief Return the local-calendar weekday for an epoch timestamp.
 * @param[in] t Integral epoch timestamp.
 * @return Corresponding day_of_week value. */
day_of_week weekday(const std::time_t t) {
  const std::tm *tinfo = std::localtime(&t);
  if (tinfo == nullptr) {
    throw std::invalid_argument{"Unable to convert std::time_t to std::tm!"};
  }
  return static_cast<day_of_week>(tinfo->tm_wday);
}

day_of_week weekday(const epoch_t &e) { return weekday(e.first); }

std::time_t get_time_of_next_week_start(const std::time_t t) {
  const std::tm *tinfo = std::localtime(&t);
  if (tinfo == nullptr) {
    throw std::invalid_argument{"Unable to convert std::time_t to std::tm!"};
  }

  std::tm nextweek = *tinfo;
  nextweek.tm_mday += (7 - (tinfo->tm_wday));
  nextweek.tm_sec = 0;
  nextweek.tm_min = 0;
  nextweek.tm_hour = 0;

  return std::mktime(&nextweek);
}

std::time_t get_time_of_next_week_start(const epoch_t &t) {
  return get_time_of_next_week_start(t.first);
}

std::time_t get_time_of_cur_week_start(const std::time_t t) {
  const std::tm *tinfo = std::localtime(&t);
  if (tinfo == nullptr) {
    throw std::invalid_argument{"Unable to convert std::time_t to std::tm!"};
  }

  std::tm nextweek = *tinfo;
  nextweek.tm_mday -= (tinfo->tm_wday);
  nextweek.tm_sec = 0;
  nextweek.tm_min = 0;
  nextweek.tm_hour = 0;

  return std::mktime(&nextweek);
}

std::time_t get_time_of_cur_week_start(const epoch_t &t) {
  return get_time_of_cur_week_start(t.first);
}

void hour_boundaries_of_week(const std::time_t t,
                             std::array<std::time_t, 7 * 24 + 1> &bo) {
  const std::tm *tinfo = std::localtime(&t);
  if (tinfo == nullptr) {
    throw std::invalid_argument{"Unable to convert std::time_t to std::tm!"};
  }

  std::tm nextweek = *tinfo;
  nextweek.tm_mday -= (tinfo->tm_wday);
  nextweek.tm_sec = 0;
  nextweek.tm_min = 0;
  nextweek.tm_hour = 0;

  size_t i = 0ul;

  for (int day = 0; day < 7; ++day) {
    for (int hr = 0; hr < 24; ++hr) {
      nextweek.tm_hour = hr;
      bo[i++] = std::mktime(&nextweek);
    }
    nextweek.tm_mday++;
  }
  nextweek.tm_hour = 0;
  nextweek.tm_mday++;
  bo[i] = std::mktime(&nextweek);
}

} // end of namespace dr_evt
