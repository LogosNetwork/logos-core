#pragma once

#include <functional>
#include <boost/function.hpp>
#include <boost/bind.hpp>

#include <logos/lib/log.hpp>
#include <logos/p2p/p2p.h>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager_incl.hpp>

template<ConsensusType CT>
class ConsensusP2p
{

public:

    ConsensusP2p(p2p_interface & p2p,
		 uint8_t delegate_id,
		 std::function<bool (const Prequel &, MessageType, uint8_t, ValidationStatus *)> Validate = {},
		 boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates = {});

    bool AddMessageToBatch(const uint8_t *data, size_t size);
    void CleanBatch();
    bool PropagateBatch();
    bool ProcessOutputMessage(const uint8_t *data, size_t size, bool propagate);
    bool ProcessInputMessage(const uint8_t *data, size_t size);

    Log                         _log;
    p2p_interface &		_p2p;
    uint8_t			_delegate_id;
    boost::function<bool (const Prequel &, MessageType, uint8_t, ValidationStatus *)> _Validate;
    boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> _ApplyUpdates;
    std::vector<uint8_t>	_p2p_batch;	// PrePrepare + PostPrepare + PostCommit
};

template<ConsensusType CT>
class PersistenceP2p
{
public:
    PersistenceP2p(p2p_interface & p2p,
		   uint8_t delegate_id,
		   logos::block_store &store)
	: _persistence(store)
	, _p2p(p2p, delegate_id,
	    [this](const Prequel &message, MessageType mtype, uint8_t delegate_id, ValidationStatus * status) {
		return mtype == MessageType::Pre_Prepare  ? this->_persistence.Validate((PrePrepareMessage<CT> &)message, delegate_id, status)
		     : mtype == MessageType::Post_Prepare ? false /* todo */
		     : mtype == MessageType::Post_Commit  ? false /* todo */
		     : false;
	    },
	    boost::bind(&PersistenceManager<CT>::ApplyUpdates, &_persistence, _1, _2)
	)
    {}

    bool ProcessInputMessage(const void *data, size_t size)
    {
	return _p2p.ProcessInputMessage((const uint8_t *)data, size);
    }

private:
    NonDelPersistenceManager<CT> _persistence;
    ConsensusP2p<CT>		 _p2p;
};

class ContainerP2p
{
public:
    ContainerP2p(p2p_interface & p2p,
		 uint8_t delegate_id,
		 logos::block_store &store)
	: _p2p(p2p)
	, _batch(p2p, delegate_id, store)
	, _micro(p2p, delegate_id, store)
	, _epoch(p2p, delegate_id, store)
    {}

    bool ProcessInputMessage(const void *data, size_t size)
    {
	return _batch.ProcessInputMessage(data, size)
	    || _micro.ProcessInputMessage(data, size)
	    || _epoch.ProcessInputMessage(data, size);
    }

    p2p_interface &					_p2p;
private:
    PersistenceP2p<ConsensusType::BatchStateBlock>	_batch;
    PersistenceP2p<ConsensusType::MicroBlock>		_micro;
    PersistenceP2p<ConsensusType::Epoch>		_epoch;
};
