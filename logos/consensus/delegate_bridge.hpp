// @file
// Declares DelegateBridge
//

#pragma once

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
class DelegateBridge : public MessageParser
{
    using PrePrepare  = PrePrepareMessage<CT>;
    using Prepare     = PrepareMessage<CT>;
    using Commit      = CommitMessage<CT>;
    using PostPrepare = PostPrepareMessage<CT>;
    using PostCommit  = PostCommitMessage<CT>;
    using Rejection   = RejectionMessage<CT>;

public:
    DelegateBridge(std::shared_ptr<IOChannel>);
    virtual ~DelegateBridge() = default;

    bool OnMessageData(const uint8_t * data,
                       uint8_t version,
                       MessageType message_type,
                       ConsensusType consensus_type,
                       uint32_t payload_size) override;

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

    void LogMessageReceived(const std::string & msg_str, const std::string & hash_str);
    void LogMessageReceived(const std::string & msg_str);

    virtual uint8_t RemoteDelegateId() = 0;

private:

    std::shared_ptr<IOChannel>  _iochannel;
    Log                         _log;
};

