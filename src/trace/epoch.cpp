/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip> // std::get_time()
// https://stackoverflow.com/questions/37552982/is-stdget-time-broken-in-g-and-clang
#include <array>
#include "epoch.hpp"

namespace dr_evt {

std::string to_string(const epoch_t& t)
{
    const tm* timeinfo = std::localtime(&t.first);
    char buffer[256] = {'\0'};
    strftime(buffer, 124, "%Y-%m-%d %H:%M:%S", timeinfo);
    char frac_buf[128] = {'\0'};
    sprintf(frac_buf, "%f", t.second);
    strncpy(buffer + strlen(buffer), frac_buf + 1, 127);
    return std::string(buffer);
}

std::ostream& operator<<(std::ostream& os, const epoch_t& t)
{
    const tm* timeinfo = std::localtime(&t.first);
    char buffer[256] = {'\0'};
    strftime(buffer, 124, "%Y-%m-%d %H:%M:%S", timeinfo);
    char frac_buf[128] = {'\0'};
    sprintf(frac_buf, "%f", t.second);
    strncpy(buffer + strlen(buffer), frac_buf + 1, 127);
    os << buffer;
    return os;
}

/**
 *  Check if the give string is timestamp
 */
bool is_timestamp(const std::string& time_str)
{
    std::istringstream iss {time_str};
    std::tm t {};
    t.tm_isdst = -1;

    iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S"); // extract it into a std::tm
    return !(iss.fail());
}

/**
 *  Return seconds (epoch) converted from the time string given as well as the
 *  fractional second.
 */
epoch_t convert_time(const std::string& time_str)
{
    std::istringstream iss {time_str};
    std::tm t {};
    t.tm_isdst = -1;

    iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S"); // extract it into a std::tm
    if (iss.fail()) {
        throw std::invalid_argument {"Failed to parse time string: " + time_str};
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

hour_bin_id_t get_hour_bin_id(const std::time_t t)
{
    const std::tm * tinfo = localtime(&t);
    if (tinfo == nullptr) {
        throw std::invalid_argument
            {"Unable to convert std::time_t to std::tm!"};
    }
    return static_cast<hour_bin_id_t>(tinfo->tm_wday * 24 + tinfo->tm_hour);
}

day_of_week weekday(const std::time_t t)
{
    const std::tm * tinfo = std::localtime(&t);
    if (tinfo == nullptr) {
        throw std::invalid_argument
            {"Unable to convert std::time_t to std::tm!"};
    }
    return static_cast<day_of_week>(tinfo->tm_wday);
}

day_of_week weekday(const epoch_t& e)
{
    return weekday(e.first);
}

std::time_t get_time_of_next_week_start(const std::time_t t)
{
    const std::tm* tinfo = std::localtime(&t);
    if (tinfo == nullptr) {
        throw std::invalid_argument
            {"Unable to convert std::time_t to std::tm!"};
    }

    std::tm nextweek = *tinfo;
    nextweek.tm_mday += (7 - (tinfo->tm_wday));
    nextweek.tm_sec = 0;
    nextweek.tm_min = 0;
    nextweek.tm_hour = 0;

    return std::mktime(&nextweek);
}

std::time_t get_time_of_next_week_start(const epoch_t& t)
{
    return get_time_of_next_week_start(t.first);
}

std::time_t get_time_of_cur_week_start(const std::time_t t)
{
    const std::tm* tinfo = std::localtime(&t);
    if (tinfo == nullptr) {
        throw std::invalid_argument
            {"Unable to convert std::time_t to std::tm!"};
    }

    std::tm nextweek = *tinfo;
    nextweek.tm_mday -= (tinfo->tm_wday);
    nextweek.tm_sec = 0;
    nextweek.tm_min = 0;
    nextweek.tm_hour = 0;

    return std::mktime(&nextweek);
}

std::time_t get_time_of_cur_week_start(const epoch_t& t)
{
    return get_time_of_cur_week_start(t.first);
}

void hour_boundaries_of_week(const std::time_t t,
                             std::array<std::time_t, 7*24+1>& bo)
{
    const std::tm* tinfo = std::localtime(&t);
    if (tinfo == nullptr) {
        throw std::invalid_argument
            {"Unable to convert std::time_t to std::tm!"};
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
            bo [i++] = std::mktime(&nextweek);
        }
        nextweek.tm_mday ++;
    }
    nextweek.tm_hour = 0;
    nextweek.tm_mday ++;
    bo [i] = std::mktime(&nextweek);
}

} // end of namespace dr_evt
