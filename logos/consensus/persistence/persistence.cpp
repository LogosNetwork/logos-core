/// @file
/// This file implements base Persistence class

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/message_validator.hpp>

Persistence::Milliseconds constexpr Persistence::DEFAULT_CLOCK_DRIFT;
Persistence::Milliseconds constexpr Persistence::ZERO_CLOCK_DRIFT;
