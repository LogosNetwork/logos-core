#include <logos/consensus/consensus_p2p.hpp>
#include <logos/consensus/messages/util.hpp>

template<ConsensusType CT>
ConsensusP2p<CT>::ConsensusP2p(Log & log,
			   p2p_interface & p2p)
    : _log(log)
    , _p2p(p2p)
{}

template<ConsensusType CT>
bool ConsensusP2p<CT>::AddMessageToBatch(const uint8_t *data, size_t size)
{
    size_t oldsize = _p2p_batch.size();
    _p2p_batch.resize(oldsize + size + 4);
    memcpy(&_p2p_batch[oldsize], &size, 4);
    memcpy(&_p2p_batch[oldsize + 4], data, size);

    BOOST_LOG(_log) << "Consensus2P2p - message of size " << size << " and type " <<
	(unsigned)_p2p_batch[oldsize + 5] << " added to p2p batch.";

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
	BOOST_LOG(_log) << "ConsensusP2p - p2p batch of size " <<
	    _p2p_batch.size() << " propagated.";
    } else {
	BOOST_LOG(_log) << "ConsensusP2p - p2p batch not propagated.";
    }

    CleanBatch();

    return res;
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::ProcessOutputMessage(const uint8_t *data, size_t size, bool propagate)
{
    bool res = AddMessageToBatch(data, size);

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
bool ConsensusP2p<CT>::ValidateBatch(const uint8_t * data, size_t size) {
    MessageType mtype = MessageType::Pre_Prepare;

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
	    case MessageType::Pre_Prepare:
		{
		    MessageHeader<MessageType::Pre_Prepare,CT> *head
				= (MessageHeader<MessageType::Pre_Prepare,CT>*)data;
		    if (head->type != mtype || head->consensus_type != CT) {
			BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
			    "> - error parsing p2p batch Pre_Prepare message";
			return false;
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
		    mtype = MessageType::Pre_Prepare;
		}
		break;
	    default:
		break;
	}

	data += msize;
	size -= msize;

	if (mtype == MessageType::Pre_Prepare) {
	    break;
	}
    }

    if (size) {
	BOOST_LOG(_log) << "ConsensusP2p<" << ConsensusToName(CT) <<
		"> - error parsing p2p batch";
	return false;
    }

    return true;
}

template class ConsensusP2p<ConsensusType::BatchStateBlock>;
template class ConsensusP2p<ConsensusType::MicroBlock>;
template class ConsensusP2p<ConsensusType::Epoch>;
