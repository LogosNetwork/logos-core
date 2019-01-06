/// @file
/// This file contains declaration of the ValidatorBuilder which handles DelegateKeyStore and Validator
/// for non-delegate, TODO : THIS MUST BE UPDATED ONCE THERE IS A TRUE PKI HANDLING

#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/node/delegate_identity_manager.hpp>

std::unordered_map<uint16_t, ValidatorBuilder::pki> ValidatorBuilder::_epoch_pki;
uint16_t ValidatorBuilder::_cached_epoch = 0;
std::shared_ptr<MessageValidator> ValidatorBuilder::_cached_validator = nullptr;

ValidatorBuilder::ValidatorBuilder(ValidatorBuilder::Store &store)
    : _store(store)
{}

std::shared_ptr<MessageValidator>
ValidatorBuilder::GetValidator(uint16_t epoch_number)
{
    std::shared_ptr<MessageValidator> validator = nullptr;
    BlockHash   hash;
    ApprovedEB  epoch;

    // delegate's epoch block for requested epoch
    epoch_number -= 2;

    if (_cached_validator != nullptr && _cached_epoch == epoch_number)
    {
        LOG_DEBUG(_log) << "ValidatorBuilder::GetValidator using cached validator for epoch block " << epoch_number;
        return _cached_validator;
    }

    auto k = _epoch_pki.find(epoch_number);

    if (k == _epoch_pki.end())
    {
        if (_store.epoch_tip_get(hash))
        {
            LOG_FATAL(_log) << "ValidatorBuilder::GetValidator failed to get epoch tip";
            trace_and_halt();
        }

        bool res;
        for (res = _store.epoch_get(hash, epoch); !res && epoch_number < epoch.epoch_number;
                hash = epoch.previous, res = _store.epoch_get(hash, epoch))
        {
        }

        if (res)
        {
            LOG_FATAL(_log) << "ValidatorBuilder::GetValidator failed to get epoch "
                            << hash.to_string();
            trace_and_halt();
        }

        if (epoch_number == epoch.epoch_number)
        {
            auto key_store = std::make_shared<DelegateKeyStore>();
            validator = std::make_shared<MessageValidator>(*key_store,
                logos::genesis_delegates[DelegateIdentityManager::_global_delegate_idx].bls_key);
            uint8_t id = 0;
            for (auto delegate : epoch.delegates)
            {
                key_store->OnPublicKey(id++, delegate.bls_pub);
            }
            _epoch_pki[epoch_number] = {key_store, validator};
            if (_epoch_pki.size() > MAX_CACHED)
            {
                uint16_t min = UINT16_MAX;
                for (auto it = _epoch_pki.begin(); it != _epoch_pki.end(); ++it)
                {
                    if (it->first < min)
                    {
                        min = it->first;
                    }
                }
                _epoch_pki.erase(min);
            }
        }
        else if (epoch_number > epoch.epoch_number)
        {
            LOG_FATAL(_log) << "ValidatorBuilder::GetValidator invalid requested epoch " << epoch_number
                            << " tip's epoch " << epoch.epoch_number;
            trace_and_halt();
        }
    }
    else
    {
        validator = k->second.validator;
    }

    LOG_DEBUG(_log) << "ValidatorBuilder::GetValidator cached validator for epoch block " << epoch_number;

    _cached_epoch = epoch_number;
    _cached_validator = validator;

    return validator;
}
