// @file
// This file declares TxChannel which provides interface to ConsensusContainer
// and is the base class for TxAcceptorForwardChannel and TxReceiverForwardChannel.
//

#pragma once
#include <logos/common.hpp>
#include <logos/consensus/messages/state_block.hpp>

class TxChannel
{
public:
    /// Class constructor
    TxChannel() = default;
    /// Class destructor
    virtual ~TxChannel() = default;
    /// Forwards transaction  for batch block consensus.
    /// Submits transactions to consensus logic.
    ///     @param[in] block state_block containing the transaction
    ///     @param[in] should_buffer bool flag that, when set, will
    ///                              cause the block to be buffered
    ///     @return process_return result of the operation
    virtual logos::process_return OnSendRequest(std::shared_ptr<StateBlock> block,
                                                bool should_buffer = false) = 0;
};
