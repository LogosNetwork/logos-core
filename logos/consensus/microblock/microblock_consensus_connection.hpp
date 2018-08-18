//===-- logos/consensus/microblock_consensus_connection.hpp - MicroBlockConsensusConnection class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains declaration of the MicroBlockConsensusConnection class, which
/// handles specifics of MicroBlock consensus
///
//===----------------------------------------------------------------------===//
#pragma once

#include <logos/consensus/consensus_connection.hpp>

//!< MicroBlockConsensusConnection handles specifics of MicroBlock consensus
class MicroBlockConsensusConnection : public ConsensusConnection<ConsensusType::MicroBlock>
{
public:
    //!
	/*!
	  Handles specifis of MicroBlockConsensus
	  \param iochannel pointer to iochannel for this delegeate
	  \param alarm reference to alarm
	  \param primary pointer to PrimaryDelegate
	  \param persistence_manager reference to persistence manager to handle the database
	  \param key_store delegates public key store
	  \param validator validator/signer of consensus messages
	  \param ids identity of the connected delegate
	*/
    MicroBlockConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
	                    logos::alarm & alarm,
	                    PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
                        DelegateKeyStore & key_store,
	                    MessageValidator & validator,
	                    const DelegateIdentities & ids) 
        : ConsensusConnection<ConsensusType::MicroBlock>(iochannel,alarm,primary,persistence_manager, key_store, validator, ids) {} 
protected:

    //!
	/*!
	  Validate BatchStateBlock message
	  \param message message to validate
	  \return true if validated false otherwise
	*/
    virtual bool Validate(const PrePrepareMessage<ConsensusType::MicroBlock> & message) override;

    //! BatchBlockConsensusConnection parameterized by consensus type 
    /*!
      Allows creation of a set of dependent classes in a type safe and polimorphic way,
      i.e. specific type of ConsensusManager can only create corresponding instance of
      ConsensusConnection
    */
    virtual void ApplyUpdates(const PrePrepareMessage<ConsensusType::MicroBlock> & block, uint8_t delegate_id) override;
};

//! MicroBlockConsensusConnection parameterized by consensus type 
/*!
  Allows creation of a set of dependent classes in a type safe and polimorphic way,
  i.e. specific type of ConsensusManager can only create corresponding instance of
  ConsensusConnection
*/
template<ConsensusType consensus_type>
struct ConsensusConnectionT<consensus_type, typename std::enable_if< consensus_type == ConsensusType::MicroBlock>::type> : MicroBlockConsensusConnection
{
    ConsensusConnectionT(std::shared_ptr<IIOChannel> iochannel,
	                    logos::alarm & alarm,
                        PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
                        DelegateKeyStore & key_store,
	                    MessageValidator & validator,
	                    const DelegateIdentities & ids) 
                        : 
                        MicroBlockConsensusConnection(iochannel, alarm, primary,
                                                      persistence_manager, key_store, validator, ids) {}
};
