#include <logos/consensus/consensus_p2p.hpp>
#include <logos/consensus/messages/util.hpp>

#define P2P_BATCH_VERSION	1

struct P2pBatchHeader {
    uint8_t version;
    MessageType type;
    ConsensusType consensus_type;
    uint8_t delegate_id;
    uint8_t padding;
};

template<ConsensusType CT>
ConsensusP2p<CT>::ConsensusP2p(Log & log,
			   p2p_interface & p2p,
			   uint8_t delegate_id,
			   std::function<bool (const Prequel &, MessageType, uint8_t)> Validate,
			   boost::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates)
    : _log(log)
    , _p2p(p2p)
    , _delegate_id(delegate_id)
    , _Validate(Validate)
    , _ApplyUpdates(ApplyUpdates)
{}

template<ConsensusType CT>
bool ConsensusP2p<CT>::AddMessageToBatch(const uint8_t *data, size_t size)
{
    size_t oldsize = _p2p_batch.size();

    _p2p_batch.resize(oldsize + size + 4);
    memcpy(&_p2p_batch[oldsize], &size, 4);
    memcpy(&_p2p_batch[oldsize + 4], data, size);

    BOOST_LOG(_log) << "Consensus2P2p<" << ConsensusToName(CT) <<
	"> - message of size " << size <<
	" and type " << (unsigned)_p2p_batch[oldsize + 5] <<
	" added to p2p batch.";

    return true;
}

template<ConsensusType CT>
void ConsensusP2p<CT>::CleanBatch()
{
    _p2p_batch.clear();
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::PropagateBatch()
{
    bool res = _p2p.PropagateMessage(&_p2p_batch[0], _p2p_batch.size());

    if (res) {
	BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
	    "> - p2p batch of size " << _p2p_batch.size() <<
	    " propagated.";
    } else {
	BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
	    "> - p2p batch not propagated.";
    }

    CleanBatch();

    return res;
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::ProcessOutputMessage(const uint8_t *data, size_t size, bool propagate)
{
    bool res = true;

    if (!_p2p_batch.size()) {
	const struct P2pBatchHeader head = {
	    .version = P2P_BATCH_VERSION,
	    .type = MessageType::Unknown,
	    .consensus_type = CT,
	    .delegate_id = _delegate_id,
	};
	res &= AddMessageToBatch((uint8_t *)&head, sizeof(head));
    }

    res &= AddMessageToBatch(data, size);

    if (propagate)
    {
	if (res)
	{
	    res = PropagateBatch();
	}
	else
	{
	    CleanBatch();
	}
    }

    return res;
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::ProcessInputMessage(const uint8_t * data, size_t size) {
    MessageType mtype = MessageType::Unknown;
    PrePrepareMessage<CT> *pre_mess = 0;
    uint8_t delegate_id;
    int mess_counter = 0;

    BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
		"> - batch of size " << size << " received from p2p.";

    while (size >= 4) {
	size_t msize = *(uint32_t *)data;
	data += 4;
	size -= 4;
	if (msize > size) {
	    size = 1;
	    break;
	}

	BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
		"> - message of size " << msize << " and type " <<
		(unsigned)data[1] << " extracted from p2p batch.";

	switch (mtype) {
	    case MessageType::Unknown:
		{
		    P2pBatchHeader *head = (P2pBatchHeader *)data;
		    if (msize != sizeof(P2pBatchHeader) || head->version != P2P_BATCH_VERSION
			    || head->type != mtype || head->consensus_type != CT) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error parsing p2p batch header";
			return false;
		    }
		    delegate_id = head->delegate_id;
		    mtype = MessageType::Pre_Prepare;
		}
		break;
	    case MessageType::Pre_Prepare:
		{
		    MessageHeader<MessageType::Pre_Prepare,CT> *head
				= (MessageHeader<MessageType::Pre_Prepare,CT>*)data;
		    if (head->type != mtype || head->consensus_type != CT) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error parsing p2p batch Pre_Prepare message";
			return false;
		    }
		    pre_mess = (PrePrepareMessage<CT> *)head;
		    if (!_Validate((const Prequel &)*head, MessageType::Pre_Prepare, delegate_id)) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error validation p2p batch Pre_Prepare message";
//			return false;
		    }
		    mtype = MessageType::Post_Prepare;
		}
		break;
	    case MessageType::Post_Prepare:
		{
		    MessageHeader<MessageType::Post_Prepare,CT> *head
				= (MessageHeader<MessageType::Post_Prepare,CT>*)data;
		    if (head->type != mtype || head->consensus_type != CT) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error parsing p2p batch Post_Prepare message";
			return false;
		    }
		    if (!_Validate((const Prequel &)*head, MessageType::Post_Prepare, delegate_id)) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error validation p2p batch Post_Prepare message";
//			return false;
		    }
		    mtype = MessageType::Post_Commit;
		}
		break;
	    case MessageType::Post_Commit:
		{
		    MessageHeader<MessageType::Post_Commit,CT> *head
				= (MessageHeader<MessageType::Post_Commit,CT>*)data;
		    if (head->type != mtype || head->consensus_type != CT) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error parsing p2p batch Post_Commit message";
			return false;
		    }
		    if (!_Validate((const Prequel &)*head, MessageType::Post_Commit, delegate_id)) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error validation p2p batch Post_Commit message";
//			return false;
		    }
		}
		break;
	    default:
		break;
	}

	data += msize;
	size -= msize;

	if (++mess_counter == 4) {
	    break;
	}
    }

    if (size || mess_counter != 4) {
	BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
		"> - error parsing p2p batch";
	return false;
    } else {
	_ApplyUpdates(*pre_mess, delegate_id);
	BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
		"> - PrePrepare message from delegate " << (unsigned)delegate_id <<
		" saved to storage.";
	return true;
    }
}

template class ConsensusP2p<ConsensusType::BatchStateBlock>;
template class ConsensusP2p<ConsensusType::MicroBlock>;
template class ConsensusP2p<ConsensusType::Epoch>;
