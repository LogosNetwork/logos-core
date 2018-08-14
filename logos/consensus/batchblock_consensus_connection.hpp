#pragma once

#include <logos/consensus/consensus_connection.hpp>

class BatchBlockConsensusConnection : public ConsensusConnection<ConsensusType::BatchStateBlock>
{
public:
	BatchBlockConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
	                    logos::alarm & alarm,
	                    PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
	                    DelegateKeyStore & key_store,
	                    MessageValidator & validator,
	                    const DelegateIdentities & ids)
      : ConsensusConnection<ConsensusType::BatchStateBlock>(iochannel, alarm, primary, persistence_manager, key_store, validator, ids)
  {
  }

protected:
    virtual bool Validate(const PrePrepareMessage<ConsensusType::BatchStateBlock> & message) override;
};

template<ConsensusType consensus_type>
struct ConsensusConnectionT<consensus_type, typename std::enable_if< consensus_type == ConsensusType::BatchStateBlock>::type> : BatchBlockConsensusConnection
{
	ConsensusConnectionT(std::shared_ptr<IIOChannel> iochannel,
	                    logos::alarm & alarm,
	                    PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
	                    DelegateKeyStore & key_store,
	                    MessageValidator & validator,
	                    const DelegateIdentities & ids)
      : BatchBlockConsensusConnection(iochannel, alarm, primary, persistence_manager, key_store, validator, ids)
  {
  }
};
