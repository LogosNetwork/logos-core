// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <timedata.h>
#include <ctype.h>

void BCLog::Logger::EnableCategory(BCLog::LogFlags flag)
{
    m_categories |= flag;
}

bool BCLog::Logger::EnableCategory(const std::string& str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, str)) return false;
    EnableCategory(flag);
    return true;
}

void BCLog::Logger::DisableCategory(BCLog::LogFlags flag)
{
    m_categories &= ~flag;
}

bool BCLog::Logger::DisableCategory(const std::string& str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, str)) return false;
    DisableCategory(flag);
    return true;
}

bool BCLog::Logger::WillLogCategory(BCLog::LogFlags category) const
{
    return (m_categories.load(std::memory_order_relaxed) & category) != 0;
}

struct CLogCategoryDesc
{
    BCLog::LogFlags flag;
    std::string category;
};

const CLogCategoryDesc LogCategories[] =
{
    {BCLog::NONE, "0"},
    {BCLog::NONE, "none"},
    {BCLog::NET, "net"},
    {BCLog::ADDRMAN, "addrman"},
    {BCLog::ALL, "1"},
    {BCLog::ALL, "all"},
};

bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str)
{
    if (str == "") {
        flag = BCLog::ALL;
        return true;
    }
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        if (category_desc.category == str) {
            flag = category_desc.flag;
            return true;
        }
    }
    return false;
}

std::string ListLogCategories()
{
    std::string ret;
    int outcount = 0;
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != BCLog::NONE && category_desc.flag != BCLog::ALL) {
            if (outcount != 0) ret += ", ";
            ret += category_desc.category;
            outcount++;
        }
    }
    return ret;
}

void BCLog::Logger::LogPrintStr(boost::log::trivial::severity_level level, const std::string &str)
{
    std::string s = str;
    while (isspace(s[s.length() - 1])) s.pop_back();
    if(s.length()) BOOST_LOG_SEV(log, level) << "[p2p] " << s;
}
