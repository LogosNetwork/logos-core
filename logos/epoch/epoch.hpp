//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains declaration of the Epoch
///
//===----------------------------------------------------------------------===//
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/lib/numbers.hpp>

/// An election result entry, i.e. a delegate with it stake, and the votes
/// it received
struct Delegate 
{
    logos::account  account;
    uint64_t        vote;
    uint64_t        stake;
};

/// A epoch block is proposed after every epoch to summarize the epoch. 
/// It includes the summary of all the successful delegate consensus sessions.
struct Epoch : MessageHeader<MessageType::Pre_Prepare, ConsensusType::Epoch>
{
    using BlockHash = logos::block_hash;
public:
    Epoch() : MessageHeader(0), account(0), epochNumber(0), previous(0),
        microBlockTip(0), transactionFeePool(0)
    {
        delegates = {0};
        signature = {0};
    }
    ~Epoch() {}
    /// Calculate epoch's block hash
    BlockHash Hash() const {
        return ::Hash([&](function<void(const void *data,size_t)> cb)mutable->void {
            cb(&timestamp, sizeof(timestamp));
            cb(&epochNumber, sizeof(epochNumber));
            cb(account.bytes.data(), sizeof(account));
            cb(previous.bytes.data(), sizeof(previous));
            cb(microBlockTip.bytes.data(), sizeof(microBlockTip));
            cb(delegates.data(), NUM_DELEGATES * sizeof(Delegate));
            cb(&transactionFeePool, sizeof(transactionFeePool));
        });
    }
    BlockHash hash() { return Hash(); }
    static const size_t                     HASHABLE_BYTES; /// hashable bytes of the epoch - used in signing
    logos::account                          account; /// account address of the epoch's proposer
    uint                                    epochNumber; /// epoch number
    BlockHash                               previous; /// previous epoch number
    BlockHash                               microBlockTip; /// microblock tip of this epoch
    std::array<Delegate, NUM_DELEGATES>     delegates; /// delegate'ls list
    uint64_t                                transactionFeePool; /// this epoch's transaction fee pool
    Signature                               signature; /// signature of hashable bytes
};