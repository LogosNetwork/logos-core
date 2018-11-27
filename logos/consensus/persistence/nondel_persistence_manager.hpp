/// @file
/// This file declares non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/node/common.hpp>

#include <unordered_map>

class MessageValidator;

template<ConsensusType CT>
class NonDelPersistenceManager {

protected:

    using Store         = logos::block_store;
    using PrePrepare    = PrePrepareMessage<CT>;
    using PostCommit    = PostCommitMessage<CT>;
    using Milliseconds  = std::chrono::milliseconds;

public:
    PersistenceManager(MessageValidator & validator,
                       Store & store,
                       Milliseconds clock_drift = Persistence::DEFAULT_CLOCK_DRIFT);

    /// Message validation
    /// @param message to validate [in]
    /// @param remote_delegate_id remote delegate id [in]
    /// @param status result of the validation, optional [in|out]
    /// @returns true if validated
    bool Validate(const PrePrepare & message, uint8_t remote_delegate_id, ValidationStatus * status = nullptr);

    bool Validate(const PostCommit & message, uint8_t remote_delegate_id);

    /// Save message to the database
    /// @param message to save [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id);
};
