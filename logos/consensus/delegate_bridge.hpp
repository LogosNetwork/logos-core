// @file
// Declares DelegateBridge
//

#pragma once

#include <logos/consensus/p2p/consensus_p2p_bridge.hpp>
#include <logos/consensus/consensus_msg_sink.hpp>
#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>

class IOChannel;

/// BackupDelegate's Interface to ConsensusNetIO.
class MessageParser
{
public:

  virtual ~MessageParser() {}

  // return true iff data is good
  virtual bool OnMessageData(const uint8_t * data,
          uint8_t version,
          MessageType message_type,
          ConsensusType consensus_type,
          uint32_t payload_size) = 0;
};

template<ConsensusType CT>
class DelegateBridge : public ConsensusMsgSink,
                       public ConsensusP2pBridge<CT>
{
    using PrePrepare  = PrePrepareMessage<CT>;
    using Prepare     = PrepareMessage<CT>;
    using Commit      = CommitMessage<CT>;
    using PostPrepare = PostPrepareMessage<CT>;
    using PostCommit  = PostCommitMessage<CT>;
    using Rejection   = RejectionMessage<CT>;

public:
    DelegateBridge(Service &service, std::shared_ptr<IOChannel>, p2p_interface & p2p, uint8_t delegate_id);
    virtual ~DelegateBridge() = default;

    void OnMessage(std::shared_ptr<MessageBase> msg, MessageType message_type, bool is_p2p) override;
    std::shared_ptr<MessageBase> Parse(const uint8_t * data, uint8_t version, MessageType message_type,
                                       ConsensusType consensus_type, uint32_t payload_size) override;

    void Send(const void * data, size_t size);

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

    bool SendP2p(const uint8_t *data, uint32_t size, uint32_t epoch_number, uint8_t dest_delegate_id) override
    {
        // backup delegate replies via p2p if the message was received via p2p
        // p2p is disabled after the reply
        auto ret = ConsensusP2pBridge<CT>::SendP2p(data, size, epoch_number, dest_delegate_id);
        ConsensusP2pBridge<CT>::EnableP2p(false);
        return ret;
    }

private:

    std::shared_ptr<IOChannel>  _iochannel;
    Log                         _log;
};

