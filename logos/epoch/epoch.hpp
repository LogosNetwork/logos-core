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
    logos::account      account;
    uint64_t            vote;
    uint64_t            stake;
};

/// A epoch block is proposed after the last micro block.
/// Like micro blocks, epoch block is used for checkpointing and boostrapping.
/// In addition, it enables delegates transition and facilitates governance.
struct Epoch : MessageHeader<MessageType::Pre_Prepare, ConsensusType::Epoch>
{
    using BlockHash = logos::block_hash;
    using HashCb    = function<void(const void *data,size_t)>;
public:
    /// Class constructor
    Epoch()
        : MessageHeader(0)
        , account(0)
        , epoch_number(0)
        , micro_block_tip(0)
        , transaction_fee_pool(0)
        , delegates{0}
        , next(0)
    {
        signature.fill(0);
        previous = 0;
    }
    ~Epoch() {}

    /// Calculate Proposer's  hash
    /// @param cb call back to add data to the hash
    void ProposerHash(HashCb cb) const
    {
        cb(previous.bytes.data(), sizeof(previous));
        //cb(&timestamp, sizeof(timestamp));
        cb(&epoch_number, sizeof(epoch_number));
        //cb(_account.bytes.data(), sizeof(_account));
        cb(micro_block_tip.bytes.data(), sizeof(micro_block_tip));
        cb(&transaction_fee_pool, sizeof(transaction_fee_pool));
        cb(delegates, NUM_DELEGATES * sizeof(Delegate));
    }

    /// Calculate epoch's block hash
    BlockHash Hash() const {
        return merkle::Hash([&](HashCb cb)mutable->void {
            ProposerHash(cb);
        });
    }
    /// Calculate epoch's block hash
    BlockHash hash() const
    {
        return merkle::Hash([&](HashCb cb)mutable->void {
            ProposerHash(cb);
        });
    }

    /// JSON representation of Epoch (primarily for RPC messages)
    std::string SerializeJson() const;
    void SerializeJson(boost::property_tree::ptree &) const;

    static const size_t     HASHABLE_BYTES;              ///< hashable bytes of the epoch - used in signing
    logos::account          account;                     ///< account address of the epoch's proposer
    uint32_t                epoch_number;                ///< epoch number
    uint32_t                padding;                     ///< padding
    BlockHash               micro_block_tip;             ///< microblock tip of this epoch
    uint64_t                transaction_fee_pool;        ///< this epoch's transaction fee pool
    Delegate                delegates[NUM_DELEGATES];    ///< delegate'ls list
    BlockHash               next;                        ///< next block's reference
    Signature               signature;                   ///< signature of hashable bytes
};
