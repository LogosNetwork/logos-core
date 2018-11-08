#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/log.hpp>
#include <logos/p2p/p2p.h>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <functional>

template<ConsensusType CT>
class ConsensusP2p
{

public:

    ConsensusP2p(p2p_interface & p2p,
		 uint8_t delegate_id,
		 std::function<bool (const Prequel &, MessageType, uint8_t)> Validate,
		 boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates);

    bool AddMessageToBatch(const uint8_t *data, size_t size);
    void CleanBatch();
    bool PropagateBatch();
    bool ProcessOutputMessage(const uint8_t *data, size_t size, bool propagate);
    bool ProcessInputMessage(const uint8_t *data, size_t size);

    Log                         _log;
    p2p_interface &		_p2p;
    uint8_t			_delegate_id;
    boost::function<bool (const Prequel &, MessageType, uint8_t)> _Validate;
    boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> _ApplyUpdates;
    std::vector<uint8_t>	_p2p_batch;	// PrePrepare + PostPrepare + PostCommit
};

