#pragma once

#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/batchblock_consensus_connection.hpp>
#include <logos/consensus/request_handler.hpp>

class BatchBlockConsensusManager: public ConsensusManager<ConsensusType::BatchStateBlock>
{

    using BlockBuffer      	  = std::list<std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>>>;
public:

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

  ~BatchBlockConsensusManager() {}

	virtual void OnBenchmarkSendRequest(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>>, logos::process_return & ) override;

  void BufferComplete(logos::process_return & result);

protected:
	virtual void ApplyUpdates(const PrePrepareMessage<ConsensusType::BatchStateBlock> &, uint8_t delegate_id) override;

	virtual bool ReadyForConsensus() override;

  virtual uint64_t OnConsensusReached_StoredCount() override;
  virtual bool OnConsensusReached_Ext() override;

	virtual bool Validate(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>> block, logos::process_return & result) override;

	void SendBufferedBlocks();

	virtual void QueueRequest(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>>) override;
  virtual PrePrepareMessage<ConsensusType::BatchStateBlock> & PrePrepare_GetNext() override;
  virtual void PrePrepare_PopFront() override;
  virtual bool PrePrepare_QueueEmpty() override;
  virtual bool PrePrepare_QueueFull() override;

private:

	bool 			      _using_buffered_blocks = false;
	BlockBuffer     _buffer;
	RequestHandler	_handler;
};
