///
/// @file
/// This file contains declaration of the Epoch
///
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/tip.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/lib/numbers.hpp>

#include <bls/bls.hpp>

static const uint GENESIS_EPOCH = 2;

/// An election result entry, i.e. a delegate with it stake, and the votes
/// it received
struct Delegate 
{
    AccountAddress      account;
    DelegatePubKey      bls_pub;
    Amount              vote;
    Amount              stake;
    bool                starting_term;

    Delegate()
    : account()
    , bls_pub()
    , vote(0)
    , stake(0)
    , starting_term(false)
    {}

    Delegate(AccountAddress const & account,
            DelegatePubKey const & bls_pub,
            Amount vote,
            Amount stake)
    : account(account)
    , bls_pub(bls_pub)
    , vote(vote)
    , stake(stake)
    , starting_term(false)
    {}

    void Hash(blake2b_state & hash) const
    {
        account.Hash(hash);
        bls_pub.Hash(hash);
        blake2b_update(&hash, vote.bytes.data(), vote.bytes.size());
        blake2b_update(&hash, stake.bytes.data(), stake.bytes.size());
    }

    uint32_t Serialize(logos::stream & stream) const
    {
        uint32_t s = logos::write(stream, account);
        s += logos::write(stream, bls_pub);
        s += logos::write(stream, vote);
        s += logos::write(stream, stake);
        s += logos::write(stream, starting_term);
        return s;
    }

    Delegate(bool & error, logos::stream & stream)
    {
        error = logos::read(stream, account);
        if(error)
        {
            return;
        }

        error = logos::read(stream, bls_pub);
        if(error)
        {
            return;
        }

        error = logos::read(stream, vote);
        if(error)
        {
            return;
        }

        error = logos::read(stream, stake);
        if(error)
        {
            return;
        }
        error = logos::read(stream, starting_term);
    }

    void SerializeJson(boost::property_tree::ptree & epoch_block) const
    {
        epoch_block.put("account", account.to_string());
        epoch_block.put("bls_pub", bls_pub.to_string());
        epoch_block.put("vote", vote.to_string());
        epoch_block.put("stake", stake.to_string());
        epoch_block.put("starting_term", starting_term);
    }

    //TODO: is this enough? do we need to use bls key too?
    bool operator==(const Delegate& other) const
    {
        return account == other.account
            && bls_pub == other.bls_pub
            && vote == other.vote
            && stake == other.stake
            && starting_term == other.starting_term;
    }

    bool operator!=(const Delegate& other) const
    {
        return !((*this) == other);
    }
};


namespace std
{

    template <>
    struct hash<Delegate>
    {
        //TODO: is this enough? do we need to hash bls key as well?
        size_t operator()(const Delegate& d) const
        {
            return hash<AccountAddress>()(d.account);
        }
    };

}

/// A epoch block is proposed after the last micro block.
/// Like micro blocks, epoch block is used for checkpointing and boostrapping.
/// In addition, it enables delegates transition and facilitates governance.
struct Epoch : PrePrepareCommon
{
    using Amount = logos::amount;

public:
    /// Class constructor
    Epoch()
        : PrePrepareCommon()
        , micro_block_tip()
        , transaction_fee_pool(0ull)
        , delegates()
        , is_extension(false)
    {}

    ~Epoch() {}

    Epoch(bool & error, logos::stream & stream, bool with_appendix)
        : PrePrepareCommon(error, stream)
    {
        if(error)
        {
            return;
        }

        new (&micro_block_tip) Tip(error, stream);
        if(error)
        {
            return;
        }

        error = logos::read(stream, transaction_fee_pool);
        if(error)
        {
            return;
        }

        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            new (&delegates[i]) Delegate(error, stream);
            if(error)
            {
                return;
            }
        }

        error = logos::read(stream, is_extension);
    }

    void Hash(blake2b_state & hash) const
    {
        PrePrepareCommon::Hash(hash, true);
        micro_block_tip.Hash(hash);
        blake2b_update(&hash, transaction_fee_pool.bytes.data(), transaction_fee_pool.bytes.size());

        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            delegates[i].Hash(hash);
        }
    }

    uint32_t Serialize(logos::stream & stream, bool with_appendix) const
    {
        auto s = PrePrepareCommon::Serialize(stream);
        s += micro_block_tip.Serialize(stream);
        s += logos::write(stream, transaction_fee_pool);
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            s += delegates[i].Serialize(stream);
        }
        s += logos::write(stream, is_extension);
        return s;
    }
    /// JSON representation of Epoch (primarily for RPC messages)
    std::string ToJson() const;
    void SerializeJson(boost::property_tree::ptree &) const;

    Tip	      micro_block_tip;          ///< microblock tip of this epoch
    Amount    transaction_fee_pool;     ///< this epoch's transaction fee pool
    Delegate  delegates[NUM_DELEGATES]; ///< delegate'ls list
    bool is_extension;
};
