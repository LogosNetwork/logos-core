#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/common.hpp>

#ifndef BOOST_LITTLE_ENDIAN
    static_assert(false, "Only LITTLE_ENDIAN machines are supported!");
#endif

constexpr size_t P2pConsensusHeader::SIZE;
constexpr size_t P2pHeader::SIZE;
constexpr size_t PrequelAddressAd::SIZE;
constexpr size_t CommonAddressAd::IP_LENGTH;
constexpr char CommonAddressAd::ipv6_prefix[];
constexpr size_t AddressAd::SIZE;
constexpr size_t AddressAdTxAcceptor::SIZE;

void UpdateNext(const logos::mdb_val &mdbval, logos::mdb_val &mdbval_buf, const BlockHash &next)
{
    if(mdbval.size() <= HASH_SIZE)
    {
        Log log;
        LOG_FATAL(log) << __func__ << " DB value too small";
        trace_and_halt();
    }

    // From LMDB:
    //    The memory pointed to by the returned values is owned by the database.
    //    The caller need not dispose of the memory, and may not modify it in any
    //    way. For values returned in a read-only transaction any modification
    //    attempts will cause a SIGSEGV.
    //    Values returned from the database are valid only until a subsequent
    //    update operation, or the end of the transaction.

    memcpy(mdbval_buf.data(), mdbval.data(), mdbval_buf.size() - HASH_SIZE);
    uint8_t * start_of_next = reinterpret_cast<uint8_t *>(mdbval_buf.data()) + mdbval_buf.size() - HASH_SIZE;
    const uint8_t * next_data = next.data();
    memcpy(start_of_next, next_data, HASH_SIZE);
}

void update_PostCommittedRequestBlock_prev_field(const logos::mdb_val & mdbval, logos::mdb_val & mdbval_buf, const BlockHash & prev)
{
    if(mdbval.size() <= HASH_SIZE)
    {
        Log log;
        LOG_FATAL(log) << __func__ << " DB value too small";
        trace_and_halt();
    }

    struct PrePrepareCommon *p_ppc = 0;
    auto pre_size (MessagePrequelSize
        + sizeof(p_ppc->primary_delegate) + sizeof(p_ppc->epoch_number)
        + sizeof(p_ppc->delegates_epoch_number)
        + sizeof(p_ppc->sequence) + sizeof(p_ppc->timestamp));
    memcpy(mdbval_buf.data(), mdbval.data(), pre_size);

    uint8_t * start_of_prev = reinterpret_cast<uint8_t *>(mdbval_buf.data()) + pre_size;
    const uint8_t * prev_data = prev.data();
    memcpy(start_of_prev, prev_data, HASH_SIZE);

    auto post_offset (pre_size + HASH_SIZE);
    memcpy(reinterpret_cast<uint8_t *>(mdbval_buf.data()) + post_offset,
           reinterpret_cast<uint8_t *>(mdbval.data()) + post_offset,
           mdbval_buf.size() - post_offset);
}
