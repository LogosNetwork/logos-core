//===-- logos/consensus/epoch/epoch_consensus_manager.hpp - ConsensusManager class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains declaration of the EpochConsensusManager class, which
/// handles specifics of Epoch consensus
///
//===----------------------------------------------------------------------===//
#pragma once


#include <logos/consensus/consensus_manager.hpp>

//!< EpochConsensusManager handles specifics of Epoch consensus
class EpochConsensusManager: public ConsensusManager<ConsensusType::Epoch>
{
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
	EpochConsensusManager(Service & service,
	                           Store & store,
	                           logos::alarm & alarm,
	                           Log & log,
					           const Config & config,
                               DelegateKeyStore & key_store,
                               MessageValidator & validator)
		: ConsensusManager<ConsensusType::Epoch>(
			service, store, alarm, log, config, key_store, validator)
	{
		queue = 1;
	}

    //!< Class destractor
    ~EpochConsensusManager() {}

    //!< Handles benchmark request
	/*!
	  Special benchmark request
		\param block state block
		\param result result of the operation
	*/
    void OnBenchmarkSendRequest(
		std::shared_ptr<RequestMessage<ConsensusType::Epoch>>, 
		logos::process_return & ) override;

protected:
    //! Commit to the store
	/*!
	  Commits the block to the database
		\param block the micro block to commit to the database
		\param delegate_id delegate id
	*/
	void ApplyUpdates(
		const PrePrepareMessage<ConsensusType::Epoch> &, 
		uint8_t delegate_id) override;

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
		std::shared_ptr<RequestMessage<ConsensusType::Epoch>> block, 
		logos::process_return & result) override;

    //!< Queues request message
	/*!
		Queues micro block
	*/
	void QueueRequest(std::shared_ptr<RequestMessage<ConsensusType::Epoch>>) override;

    //!< Gets next available Epoch
	/*!
		\return reference to Epoch
	*/
    PrePrepareMessage<ConsensusType::Epoch> & PrePrepareGetNext() override;

    //!< Pops the Epoch from the queue
    void PrePreparePopFront() override;

    //!< Checks if the Epoch queue is empty
	/*!
		\return true if empty false otherwise
	*/
    bool PrePrepareQueueEmpty() override;

    //!< Checks if the Epoch queue is full
	/*!
		\return true if full false otherwise
	*/
    bool PrePrepareQueueFull() override;

private:
    std::shared_ptr<PrePrepareMessage<ConsensusType::Epoch>>  _cur_epoch; //!< Currently handled epoch
	int queue;
};