#pragma once

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/messages/messages.hpp>

template<ConsensusType CT>
class NoneDelegatePersistence
{
public:
    using Store         = logos::block_store;
    using PrePerpare    = PrePrepareMessage<CT>;
    using ApprovedBlock = PostCommittedBlock<CT>;
    using PostPrepare   = PostPrepareMessage<CT>;

    NoneDelegatePersistence(Store &store)
    : _builder(store)
    {}

    bool Validate(const ApprovedBlock & block, ValidationStatus * status)
    {
        PrePerpare pre_prepare(block);
        BlockHash pre_prepare_hash(pre_prepare.Hash());
        auto validator(_builder.GetValidator(block.epoch_number));

        if(!validator->Validate(pre_prepare_hash, block.post_prepare_sig))
        {
            LOG_ERROR (_log) << __func__ << " bad post_prepare signature";
            UpdateStatusReason(status, process_result::bad_signature);
            return false;
        }

        PostPrepare post_prepare(pre_prepare_hash, block.post_prepare_sig);
        BlockHash post_prepare_hash(post_prepare.ComputeHash());
        if(!validator->Validate(post_prepare_hash, block.post_commit_sig))
        {
            LOG_ERROR (_log) << __func__ << " bad post_commit signature";
            UpdateStatusReason(status, process_result::bad_signature);
            return false;
        }

        return ValidatePreprepare(pre_prepare, status);
    }

    virtual ~NoneDelegatePersistence() = default;

protected:
    virtual bool ValidatePreprepare(const PrePerpare & block, ValidationStatus * status) = 0;
    Log _log;

private:
    ValidatorBuilder    _builder;
};
