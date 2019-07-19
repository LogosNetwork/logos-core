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

private:

    using Store         = logos::block_store;

public:

    ValidatorBuilder(Store & store);
    ~ValidatorBuilder() = default;

    /// Get MessageValidator for the given epoch, propagates DelegateKeyStore with
    /// public keys for the given epoch
    /// @param epoch_number epoch number [in]
    /// @returns MessageValidator shared pointer
    std::shared_ptr<MessageValidator> GetValidator(uint32_t epoch_number);

private:

    uint16_t const MAX_CACHED = 3;

//    struct pki {
//        std::shared_ptr<DelegateKeyStore> key_store;
//        std::shared_ptr<MessageValidator> validator;
//    };

    Store &                                     _store;
    static std::unordered_map<uint32_t, std::shared_ptr<MessageValidator>>    _epoch_pki;
    Log                                         _log;
    static std::shared_ptr<MessageValidator>    _cached_validator;
    static uint32_t                             _cached_epoch;
    static std::mutex                           _mutex;
};

