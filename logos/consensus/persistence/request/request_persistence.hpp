/// @file
/// This file contains declaration of Request related validation/persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

const ConsensusType R = ConsensusType::Request;

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

template<>
class PersistenceManager<R> : public Persistence
{

    friend class RequestConsensusManager;

protected:

    using Message         = DelegateMessage<R>;
    using PrePrepare      = PrePrepareMessage<R>;
    using ReservationsPtr = std::shared_ptr<Reservations>;
    using RequestPtr      = std::shared_ptr<const Request>;

    public:

    PersistenceManager(
            Store & store,
            ReservationsPtr reservations,
            Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    virtual void ApplyUpdates(const ApprovedRB & message, uint8_t delegate_id);

    void StoreRequestBlock(const ApprovedRB & message,
                           MDB_txn * transaction,
                           uint8_t delegate_id);

    virtual bool BlockExists(const ApprovedRB & message);

    bool ValidateRequest(
            RequestPtr request,
            uint32_t cur_epoch_num,
            logos::process_return & result,
            bool allow_duplicates = true,
            bool prelim = false);

    virtual bool ValidateSingleRequest(
            RequestPtr request,
            uint32_t cur_epoch_num,
            logos::process_return & result,
            bool allow_duplicates = true);

    bool ValidateAndUpdate(
            RequestPtr request,
            uint32_t cur_epoch_num,
            logos::process_return & result,
            bool allow_duplicates = true);

    bool ValidateBatch(const PrePrepare & message, RejectionMap & rejection_map);

    bool Validate(const PrePrepare & message, ValidationStatus * status = nullptr);


    void ApplyRequest(const StartRepresenting& request, logos::account_info& info, MDB_txn* txn);
    void ApplyRequest(const StopRepresenting& request, logos::account_info& info, MDB_txn* txn);
    void ApplyRequest(const ElectionVote& request, MDB_txn* txn);
    void ApplyRequest(const AnnounceCandidacy& request, logos::account_info& info, MDB_txn* txn);
    void ApplyRequest(const RenounceCandidacy& request, logos::account_info& info, MDB_txn* txn);
    void ApplyRequest(const Proxy& request, logos::account_info& info, MDB_txn* txn);
    void ApplyRequest(const Stake& request, logos::account_info& info, MDB_txn* txn);
    void ApplyRequest(const Unstake& request, logos::account_info& info, MDB_txn* txn);

    bool ValidateRequest(
            const ElectionVote& request,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result); 

    bool ValidateRequest(
            const AnnounceCandidacy& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool ValidateRequest(
            const RenounceCandidacy& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool ValidateRequest(
            const StartRepresenting& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool ValidateRequest(
            const StopRepresenting& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool ValidateRequest(
            const Proxy& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool ValidateRequest(
            const Stake& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool ValidateRequest(
            const Unstake& request,
            logos::account_info const & info,
            uint32_t cur_epoch_num,
            MDB_txn* txn,
            logos::process_return& result);

    bool IsDeadPeriod(uint32_t cur_epoch_num, MDB_txn* txn);

    static constexpr uint32_t  RESERVATION_PERIOD  = 2;
    static constexpr uint128_t MIN_TRANSACTION_FEE = 0x21e19e0c9bab2400000_cppui128; // 10^22

private:

    bool ValidateTokenAdminRequest(
            RequestPtr request,
            logos::process_return & result,
            std::shared_ptr<logos::Account> info);

    bool ValidateTokenTransfer(
            RequestPtr request,
            logos::process_return & result,
            std::shared_ptr<logos::Account> info,
            const Amount & token_total);

    void ApplyRequestBlock(const ApprovedRB & message,
            MDB_txn * transaction);
    void ApplyRequest(RequestPtr request,
            uint64_t timestamp,
            uint32_t cur_epoch_num,
            MDB_txn * transaction);

    template<typename SendType>
    void ApplySend(
            std::shared_ptr<const SendType> request,
            uint64_t timestamp,
            MDB_txn *transaction,
            uint32_t const & epoch_num,
            BlockHash token_id = 0);

    template<typename AmountType>
    void ApplySend(
            const Transaction<AmountType> &send,
            uint64_t timestamp,
            MDB_txn *transaction,
            const BlockHash &request_hash,
            const BlockHash &token_id,
            const AccountAddress& origin,
            uint32_t const & epoch_num,
            uint16_t transaction_index = 0);

    void PlaceReceive(
            ReceiveBlock & receive,
            uint64_t timestamp,
            MDB_txn * transaction);

    Log               _log;
    ReservationsPtr   _reservations;
    static std::mutex _write_mutex;
};
