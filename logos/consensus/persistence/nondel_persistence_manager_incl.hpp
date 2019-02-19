/// @file
/// This file contains persistence header files. It should be included when persistence manager declaration
/// is needed.

#pragma once

// must be first
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
// specializations to follow
#include <logos/consensus/persistence/request/nondel_request_persistence.hpp>
#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>
#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>