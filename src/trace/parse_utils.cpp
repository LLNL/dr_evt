/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include "trace/parse_utils.hpp"

namespace dr_evt {

using std::string;

std::unordered_map<std::string, job_queue_t> str2jobq {
#if PBATCH_GROUP
    {"pbatch", pBatch},
    {"pbatch0", pBatch},
    {"pbatch1", pBatch},
    {"pbatch2", pBatch},
    {"pbatch3", pBatch},
#else
    {"pbatch", pBatch},
    {"pbatch0", pBatch0},
    {"pbatch1", pBatch1},
    {"pbatch2", pBatch2},
    {"pbatch3", pBatch3},
#endif
    {"pall", pAll},
    {"pdebug", pDebug},
    {"exempt", pExempt},
    {"expedite", pExpedite},
    {"pbb", pBb},
    {"pibm", pIbm},
    {"pnvidia", pNvidia},
    {"ptest", pTest},
    {"standby", standby},
    {"", pUnknown}
};

std::map<job_queue_t, std::string> jobq2str {
#if PBATCH_GROUP
    {pBatch, "pbatch"},
#else
    {pBatch, "pbatch"},
    {pBatch0, "pbatch0"},
    {pBatch1, "pbatch1"},
    {pBatch2, "pbatch2"},
    {pBatch3, "pbatch3"},
#endif
    {pAll, "pall"},
    {pDebug, "pdebug"},
    {pExempt, "pexempt"},
    {pExpedite, "pexpedite"},
    {pBb, "pbb"},
    {pIbm, "pibm"},
    {pNvidia, "pnvidia"},
    {pTest, "ptest"},
    {standby, "standby"},
    {pUnknown, ""}
};

void set_by(epoch_t& t, const std::string& str) {
    t = convert_time(str);
}

void set_by(unsigned& v, const std::string& str) {
    size_t pos;
    v = static_cast<unsigned>(stoi(str, &pos));
    if (pos != str.size()) {
        throw std::invalid_argument
            {"Failed to parse an unsigned integer! " + str};
    }
}

void set_by(double& v, const std::string& str) {
    size_t pos;
    v = stof(str, &pos);
    if (pos != str.size()) {
        throw std::invalid_argument
            {"Failed to parse a double precision number! " + str};
    }
}

void set_by(job_queue_t& q, const std::string& str) {
    std::unordered_map<std::string, job_queue_t>::const_iterator it
        = str2jobq.find(str);
    if (it == str2jobq.cend()) {
        if (str.compare("\"\"") == 0) {
            q = pUnknown;
            return;
        }
        throw std::invalid_argument
            {"Failed to recognize a job queue string! " + str};
    }
    q = it->second;
}

std::string to_string(const job_queue_t q)
{
    std::map<job_queue_t, std::string>::const_iterator it
        = jobq2str.find(q);
    if (it == jobq2str.cend()) {
        throw std::invalid_argument {"Failed to recognize a job queue type!"};
    }
    return it->second;
}

/**
 * Removes leading and trailing spaces from a string
 */
string trim(const string& str,
             const string& whitespace)
{
    const auto i_beg = str.find_first_not_of(whitespace);
    if (i_beg == string::npos)
        return ""; // no content

    const auto i_end = str.find_last_not_of(whitespace);
    const auto span = i_end - i_beg + 1;

    return str.substr(i_beg, span);
}

std::vector<substr_pos_t> comma_separate(const std::string& str)
{
    std::vector<substr_pos_t> ret;
    ret.reserve(str.size());
    size_t s = 0ul;

    for (size_t i = 0ul; i < str.size(); i++) {
        if (str[i] == ',') {
            ret.emplace_back(s, i-s);
            s = i+1;
        }
    }
    if (str.back() == ',') {
        ret.emplace_back(str.size(), 0ul);
    } else {
        ret.emplace_back(s, str.size() - s);
    }

    return ret;
}

/**
 *  Replace the comma, which is a delimiter, within a (double) quotation.
 *  Without this, parsing comma-sepated-value data may result in an error.
 *  Does not handle a case as "'...,..."' where quotation is done erroneously.
 */
void replace_comma_within_quotation(string& line)
{
    bool db_quote_open = false; // is double quotation open
    //bool quote_open = false; // is quotation open
    bool esc = false; // escape sequence in progress
    unsigned hash = 0u; // comment block begins
    // A comment block ends with ';'.
    // However, in some cases #include <blah.h> is shown as a non-comment
    // line. Fortunately, that will eventually ends with a C/C++ line that
    // ends with ';'

    for (auto& c: line) {
      #ifdef DEBUG
        cout << c;
      #endif // DEBUG
        if (c == '\\') {
            esc = (esc == false);
        } else {
            esc = false;

            if (c == '#') {
                //if (!quote_open && !db_quote_open) {
                if (!db_quote_open) {
                    hash ++;
                }
            } else if (c == ';') {
                //if (!quote_open && !db_quote_open) {
                if (!db_quote_open) {
                    hash = 0u;
                }
            } else {
                if (hash > 0) {
                    continue;
                }
                //if (c == '"' && !esc && !quote_open)  {
                if (c == '"' && !esc)  {
                    db_quote_open = (db_quote_open == false);
                  #ifdef DEBUG
                    if (!db_quote_open) cout << endl;
                  #endif // DEBUG
/*
                } else if (c == '\'' && !esc && !db_quote_open) {
                    quote_open = (quote_open == false);
                    // There are still errors like with record 940914, in which
                    // a quotation does not close in user_script field.
*/
                  #ifdef DEBUG
                      //if (!quote_open && !db_quote_open) cout << endl;
                      if (!db_quote_open) cout << endl;
                  #endif // DEBUG
                } else if (c == ',' && !esc) {
                    //c = (db_quote_open || quote_open)? '`' : ',';
                    c = (db_quote_open)? '`' : ',';
                  #ifdef DEBUG
                    if (c == ',') cout << endl;
                  #endif // DEBUG
                }
            }
        }
    }
  #ifdef DEBUG
    cout << endl << endl;
  #endif // DEBUG
}


/**
 *  Case-insensitive substring search
 */
bool search_ci(const string& str, const string& sub)
{
    auto it =
        std::search(str.cbegin(), str.cend(),
                     sub.cbegin(), sub.cend(),
                     [](const char c1, const char c2) {
                         return std::toupper(c1) == std::toupper(c2);
                     }
        );
    return (it != str.cend() );
}

} // end of namespace dr_evt
