/// @file
/// This file contains persistence header files. It should be included when persistence manager declaration
/// is needed.

#pragma once

// must be first
#include <logos/consensus/persistence/persistence_manager.hpp>
// specializations to follow
#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
