/// @file
/// This file declares PersistenceManager class which handles validation and persistence of
/// consensus related objects
#pragma once

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/node/common.hpp>

class ReservationsProvider;

template<ConsensusType CT> class PersistenceManager {};
