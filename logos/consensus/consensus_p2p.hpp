#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/p2p/p2p.h>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <functional>

template<ConsensusType CT>
class ConsensusP2p
{
    using Log        = boost::log::sources::logger_mt;

public:

    ConsensusP2p(Log & log,
		 p2p_interface & p2p,
		 uint8_t delegate_id,
		 boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates);

    bool AddMessageToBatch(const uint8_t *data, size_t size);
    void CleanBatch();
    bool PropagateBatch();
    bool ProcessOutputMessage(const uint8_t *data, size_t size, bool propagate);

    bool ValidateBatch(const uint8_t *data, size_t size);

    Log                         _log;
    p2p_interface &		_p2p;
    uint8_t			_delegate_id;
    boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> _ApplyUpdates;
    std::vector<uint8_t>	_p2p_batch;	// PrePrepare + PostPrepare + PostCommit
};

