///
/// @file
/// This file contains declaration of the MicroBlockConsensusConnection class
/// which handles specifics of MicroBlock consensus
///
#pragma once

#include <logos/consensus/consensus_connection.hpp>
#include <logos/microblock/microblock_handler.hpp>

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
    MicroBlockConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
                                  PrimaryDelegate * primary,
                                  DelegateKeyStore & key_store,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  MicroBlockHandler & microblock_handler)
        : ConsensusConnection<ConsensusType::MicroBlock>(iochannel, primary, key_store, validator, ids)
        , _microblock_handler(microblock_handler)
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

private:
    MicroBlockHandler &  _microblock_handler;
};
