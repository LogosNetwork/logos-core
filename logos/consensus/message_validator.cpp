#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/message_validator.hpp>
#include <string>

MessageValidator::MessageValidator(DelegateKeyStore & key_store)
    : _keys(key_store)
{
}

DelegatePubKey MessageValidator::GetPublicKey()
{
    return DelegateIdentityManager::BlsPublicKey();
}

void MessageValidator::Sign(const BlockHash & hash, DelegateSig & sig)
{
    DelegateIdentityManager::Sign(hash, sig);
}
