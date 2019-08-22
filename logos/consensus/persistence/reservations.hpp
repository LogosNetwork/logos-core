/// @file
/// This file declares/implements Account Reservations
#pragma once

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <unordered_map>

namespace
{

using logos::reservation_info;
using ReservationCache = std::unordered_map<AccountAddress, reservation_info>;

}

class Reservations
{
protected:

    using Store           = logos::block_store;
    using AccountPtr      = std::shared_ptr<logos::Account>;
    using TokenAccountPtr = std::shared_ptr<TokenAccount>;
    using LogosAccountPtr = std::shared_ptr<logos::account_info>;

public:

    explicit Reservations(Store & store)
        : _store(store)
    {}

    virtual ~Reservations() = default;
    virtual bool CanAcquire(const AccountAddress & account,
                            const BlockHash & hash,
                            bool allow_duplicates) {return true;}

    virtual void Release(const AccountAddress & account,
                         const BlockHash& hash);

    virtual void UpdateReservation(const BlockHash & hash,
                                   const AccountAddress & account) {}
protected:

    static ReservationCache _cache;
    Store &                 _store;
    Log                     _log;
};

class ConsensusReservations : public Reservations
{
protected:

public:
    explicit ConsensusReservations(Store & store)
        : Reservations(store)
    {}

    virtual ~ConsensusReservations() = default;

    //-------------------------------------------------------------------------
    // XXX - It is possible for a delegate D1 that has validated/Post-Comitted
    //       (but hasn't yet updated its database and cleared the reservation)
    //       a send request from account A1 to receive the subsequent request
    //       from account A1 as a backup delegate for a PrePrepare from another
    //       delegate D2. In this case D1 would reject a valid send transaction
    //       from A1 since A1 would appear to still be reserved. However, this
    //       is unlikely, as for this to occur, the Post-Commit would have to
    //       propagate to both D2 and to the client before D1 clears the
    //       reservation. When this occurs, D1 will attempt to Acquire an
    //       account that is already stored in the ConsensusReservations cache. However,
    //       this is not the only case in which a cached account will be
    //       acquired.
    //-------------------------------------------------------------------------
    bool CanAcquire(const AccountAddress & account,
                    const BlockHash & hash,
                    bool allow_duplicates) override;

    // Can only be called after checking CanAcquire to ensure we don't corrupt reservation
    void UpdateReservation(const BlockHash & hash,
                           const AccountAddress & account) override;

};
