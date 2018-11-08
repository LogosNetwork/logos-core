#pragma once

#include <boost/log/sources/severity_feature.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>

using boost::log::sources::severity_logger;
using boost::log::trivial::severity_level;

using Log = severity_logger<severity_level>;

#define LOG_TRACE(logger) BOOST_LOG_SEV(logger, boost::log::trivial::trace)
#define LOG_DEBUG(logger) BOOST_LOG_SEV(logger, boost::log::trivial::debug)
#define LOG_INFO(logger)  BOOST_LOG_SEV(logger, boost::log::trivial::info)
#define LOG_WARN(logger)  BOOST_LOG_SEV(logger, boost::log::trivial::warning)
#define LOG_ERROR(logger) BOOST_LOG_SEV(logger, boost::log::trivial::error)
#define LOG_FATAL(logger) BOOST_LOG_SEV(logger, boost::log::trivial::fatal)
