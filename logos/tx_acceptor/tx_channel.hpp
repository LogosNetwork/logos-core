// @file
// This file declares TxChannel which provides interface to ConsensusContainer
// and is the base class for TxAcceptorForwardChannel and TxReceiverForwardChannel.
//

#pragma once
#include <logos/common.hpp>
#include <logos/consensus/messages/messages.hpp>

class DelegateIdentityManager;

class TxChannel
{
protected:
    using Responses = std::vector<std::pair<logos::process_result, BlockHash>>;
    using DM        = DelegateMessage<ConsensusType::Request>;
public:
    /// Class constructor
    TxChannel() = default;
    /// Class destructor
    virtual ~TxChannel() = default;
    /// Forwards transaction  for batch block consensus.
    /// Submits transactions to consensus logic.
    ///     @param block state_block containing the transaction [in]
    ///     @param should_buffer bool flag that, when set, will [in]
    ///                              cause the block to be buffered
    ///     @return process_return result of the operation
    virtual logos::process_return OnDelegateMessage(std::shared_ptr<DM> block,
                                                    bool should_buffer = false) = 0;
    /// Forwards transactions  for batch block consensus.
    /// Submits transactions to consensus logic. Optimized when TxAcceptor runs in delegate mode
    ///     @param blocks of transaction [in]
    ///     @return process_return result of the operation, in standalone returns either progress or initializing
    virtual Responses OnSendRequest(std::vector<std::shared_ptr<DM>> &blocks) = 0;
};

class TxChannelExt : public TxChannel {
public:
    TxChannelExt () = default;
    virtual ~TxChannelExt() = default;

    /// Get identity manager
    /// @returns identity manager reference
    virtual DelegateIdentityManager & GetIdentityManager() = 0;
};
