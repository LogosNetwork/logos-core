// @file
// Declares DelegateBridge
//

#pragma once

#include <logos/consensus/p2p/consensus_p2p_bridge.hpp>
#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>

class IOChannel;

/// BackupDelegate's Interface to ConsensusNetIO.
class MessageParser
{
public:
  MessageParser() = default;
  virtual ~MessageParser() = default;

  // return true iff data is good
  virtual void OnMessage(std::shared_ptr<MessageBase>, MessageType, bool) = 0;
};

template<ConsensusType CT>
class DelegateBridge : public ConsensusP2pBridge<CT>,
                       public MessageParser
{
    using PrePrepare  = PrePrepareMessage<CT>;
    using Prepare     = PrepareMessage<CT>;
    using Commit      = CommitMessage<CT>;
    using PostPrepare = PostPrepareMessage<CT>;
    using PostCommit  = PostCommitMessage<CT>;
    using Rejection   = RejectionMessage<CT>;
    using Service     = boost::asio::io_service;

public:
    DelegateBridge(Service &service, std::shared_ptr<IOChannel>, p2p_interface & p2p, uint8_t delegate_id);
    virtual ~DelegateBridge() = default;

    void OnMessage(std::shared_ptr<MessageBase> msg, MessageType message_type, bool is_p2p) override;

    void Send(const void * data, size_t size);

    void ResetConnectCount();

    bool PrimaryDirectlyConnected();

protected:
    // Messages received by backup delegates
    virtual void OnConsensusMessage(const PrePrepare & message) = 0;
    virtual void OnConsensusMessage(const PostPrepare & message) = 0;
    virtual void OnConsensusMessage(const PostCommit & message) = 0;

    // Messages received by primary delegates
    virtual void OnConsensusMessage(const Prepare & message) = 0;
    virtual void OnConsensusMessage(const Commit & message) = 0;
    virtual void OnConsensusMessage(const Rejection & message) = 0;

    virtual uint8_t RemoteDelegateId() = 0;

    bool SendP2p(const uint8_t *data, uint32_t size, MessageType message_type,
                 uint32_t epoch_number, uint8_t dest_delegate_id) override
    {
        // backup delegate replies via p2p if the message was received via p2p
        // p2p is disabled after the reply
        auto ret = ConsensusP2pBridge<CT>::SendP2p(data, size, message_type, epoch_number, dest_delegate_id);
        ConsensusP2pBridge<CT>::EnableP2p(false);
        return ret;
    }

private:

    std::weak_ptr<IOChannel>    _iochannel;
    Log                         _log;
};

