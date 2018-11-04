#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/p2p/p2p.h>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

template<ConsensusType CT>
class ConsensusP2p
{
    using Log        = boost::log::sources::logger_mt;

public:

    ConsensusP2p(Log & log,
		 p2p_interface & p2p);

    bool AddMessageToBatch(const uint8_t *data, size_t size);
    void CleanBatch();
    bool PropagateBatch();
    bool ProcessOutputMessage(const uint8_t *data, size_t size, bool propagate);

    bool ValidateBatch(const uint8_t *data, size_t size);

    Log                         _log;
    p2p_interface &		_p2p;
    std::vector<uint8_t>	_p2p_batch;	// PrePrepare + PostPrepare + PostCommit
};

