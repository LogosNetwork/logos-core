//===-- logos/consensus/batchblock_consensus_manager.hpp - BatchBlockConsensusManager class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains declaration of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
///
//===----------------------------------------------------------------------===//
#pragma once

#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/request_handler.hpp>

//!< BatckBlockConsensusManager handles specifics of BatchBlock consensus
class BatchBlockConsensusManager: public ConsensusManager<ConsensusType::BatchStateBlock>
{

        //!< Aliases
    using BlockBuffer = std::list<std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>>>;
public:

    //!< Class constructor
    /*!
      Is called by ConsensusContainer
        \param service reference to boost asio service
        \param store reference to blockstore
        \param alarm reference to alarm
        \param log reference to boost asio log
        \param config reference to ConsensusManagerConfig configuration
        \param key_store delegates public key store
        \param validator validator/signer of consensus messages
    */
    BatchBlockConsensusManager(Service & service, 
                               Store & store,
                               logos::alarm & alarm,
                               Log & log,
                                       const Config & config,
                             DelegateKeyStore & key_store,
                             MessageValidator & validator)
        : ConsensusManager<ConsensusType::BatchStateBlock>(service, store, alarm, log, config, key_store, validator)
    {
    }

    //!< Class destractor
  ~BatchBlockConsensusManager() {}

    //!< Handles benchmark request
    /*!
      Special benchmark request, for batchblock benchmark the received blocks are processed in batch
        \param block state block
        \param result result of the operation
    */
    void OnBenchmarkSendRequest(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>> block, logos::process_return & result) override;

    //!< Indicates that the buffering is complete
    /*!
      \param result result of the operation
    */
  void BufferComplete(logos::process_return & result);

protected:
    //! Commit to the store
    /*!
      Commits the block to the database
        \param block the batch block to commit to the database
        \param delegate_id delegate id
    */
    void ApplyUpdates(const PrePrepareMessage<ConsensusType::BatchStateBlock> &block, uint8_t delegate_id) override;

    //! Checks if consensus have to be initiated
    /*!
      The extended override does additional processing if _using_buffered_blocks is true
        \return true if ready false otherwise
    */
    bool ReadyForConsensusExt() override;

    //! Returns number of stored blocks
    /*!
        Benchmarking related
        \return number of stored blocks
    */
  uint64_t OnConsensusReachedStoredCount() override;

    //! Sends buffered blocks 
    /*!
        Benchmark related
        \return true if using buffered blocks
    */
  bool OnConsensusReachedExt() override;

    //! Validates state block
    /*!
        Validates state block
        \param result of the validation
        \return true if validated false otherwise
    */
    bool Validate(
        std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>> block, 
        logos::process_return & result) override;

    //!< Sends buffered blocks
    /*!
        Benchmark related
    */
    void SendBufferedBlocks();

    //!< Queues request message
    /*!
        Queues state block
    */
    void QueueRequest(
        std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>>) override;

    //!< Gets next available BatchStateBlock
    /*!
        \return reference to BatchStateBlock
    */
  PrePrepareMessage<ConsensusType::BatchStateBlock> & PrePrepareGetNext() override;

    //!< Pops the BatchStateBlock from the queue
  void PrePreparePopFront() override;

    //!< Checks if the BatchStateBlock queue is empty
    /*!
        \return true if empty false otherwise
    */
  bool PrePrepareQueueEmpty() override;

    //!< Checks if the BatchStateBlock queue is full
    /*!
        \return true if full false otherwise
    */
  bool PrePrepareQueueFull() override;

private:

    bool                   _using_buffered_blocks = false; //!< Flag to indicate if buffering is enabled, benchmark related
    BlockBuffer     _buffer; //!< Buffered state blocks
    RequestHandler    _handler; //!< Queue of batch state blocks
};
