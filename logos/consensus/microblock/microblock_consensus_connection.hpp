///
/// @file
/// This file contains declaration of the MicroBlockConsensusConnection class
/// which handles specifics of MicroBlock consensus
///
#pragma once

#include <logos/consensus/consensus_connection.hpp>

class ArchiverMicroBlockHandler;

class MicroBlockConsensusConnection :
        public ConsensusConnection<ConsensusType::MicroBlock>
{
public:
    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    MicroBlockConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                                  PrimaryDelegate & primary,
                                  RequestPromoter<ConsensusType::MicroBlock> & promoter,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
				  ArchiverMicroBlockHandler & handler,
				  p2p_interface & p2p)
	: ConsensusConnection<ConsensusType::MicroBlock>(iochannel, primary, promoter, validator, ids, p2p)
        , _microblock_handler(handler)
    {}
    ~MicroBlockConsensusConnection() {}

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool Validate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) override;

    bool IsPrePrepared(const logos::block_hash & hash) override;

private:
    ArchiverMicroBlockHandler &  _microblock_handler;
};
