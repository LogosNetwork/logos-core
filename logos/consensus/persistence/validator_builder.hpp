/// @file
/// This file contains declaration of the ValidatorBuilder which handles DelegateKeyStore and Validator
/// for non-delegate, TODO : THIS MUST BE UPDATED ONCE THERE IS A TRUE PKI HANDLING

#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/trace.hpp>
#include <logos/lib/log.hpp>

#include <unordered_map>
#include <memory>

/// Handles DelegateKeyStore and MessageValidator instantiation for the given epoch.
class ValidatorBuilder {

    using Store     = logos::block_store;
    using BlockHash = logos::block_hash;

public:

    ValidatorBuilder(Store & store);
    ~ValidatorBuilder() = default;

    /// Get MessageValidator for the given epoch, propagates DelegateKeyStore with
    /// public keys for the given epoch
    /// @param epoch_number epoch number [in]
    /// @returns MessageValidator shared pointer
    std::shared_ptr<MessageValidator> GetValidator(uint16_t epoch_number);

private:

    uint16_t const MAX_CACHED = 3;

    struct pki {
        std::shared_ptr<DelegateKeyStore> key_store;
        std::shared_ptr<MessageValidator> validator;
    };

    Store &                             _store;
    std::unordered_map<uint16_t, pki>   _epoch_pki;
    Log                                 _log;
    std::shared_ptr<MessageValidator>   _cached_validator = nullptr;
    uint16_t                            _cached_epoch = 0;
};

