///
/// @file
/// This file contains declaration of the Epoch
///
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/lib/numbers.hpp>

static const uint GENESIS_EPOCH = 2;

/// An election result entry, i.e. a delegate with it stake, and the votes
/// it received
struct Delegate 
{
    logos::account  _account;
    uint64_t        _vote;
    uint64_t        _stake;
};

/// A epoch block is proposed after every epoch to summarize the epoch. 
/// It includes the summary of all the successful delegate consensus sessions.
struct Epoch : MessageHeader<MessageType::Pre_Prepare, ConsensusType::Epoch>
{
    using BlockHash = logos::block_hash;
    using HashCb    = function<void(const void *data,size_t)>;
public:
    /// Class constructor
    Epoch()
        : MessageHeader(0)
        , _account(0)
        , _epoch_number(0)
        , _micro_block_tip(0)
        , _transaction_fee_pool(0)
    {
        _delegates = {0};
        previous = 0;
        signature = {0};
    }
    ~Epoch() {}

    /// Calculate Proposer's  hash
    /// @param cb call back to add data to the hash
    void ProposerHash(HashCb cb) const
    {
        cb(&timestamp, sizeof(timestamp));
        cb(&_epoch_number, sizeof(_epoch_number));
        cb(_account.bytes.data(), sizeof(_account));
        cb(previous.bytes.data(), sizeof(previous));
        cb(_micro_block_tip.bytes.data(), sizeof(_micro_block_tip));
        cb(_delegates.data(), NUM_DELEGATES * sizeof(Delegate));
        cb(&_transaction_fee_pool, sizeof(_transaction_fee_pool));
    }

    /// Calculate epoch's block hash
    BlockHash Hash() const {
        return ::Hash([&](HashCb cb)mutable->void {
            ProposerHash(cb);
        });
    }
    /// Calculate epoch's block hash
    BlockHash hash() const
    {
        return ::Hash([&](HashCb cb)mutable->void {
            ProposerHash(cb);
        });
    }
    static const size_t                     HASHABLE_BYTES;        ///< hashable bytes of the epoch - used in signing
    logos::account                          _account;              ///< account address of the epoch's proposer
    uint                                    _epoch_number;         ///< epoch number
    BlockHash                               _micro_block_tip;      ///< microblock tip of this epoch
    std::array<Delegate, NUM_DELEGATES>     _delegates;            ///< delegate'ls list
    uint64_t                                _transaction_fee_pool; ///< this epoch's transaction fee pool
    logos::signature                        _signature;            ///< proposer's signature
    Signature                               signature;             ///< signature of hashable bytes
};