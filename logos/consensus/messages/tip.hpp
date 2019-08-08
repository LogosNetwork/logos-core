#pragma once

#include <blake2/blake2.h>

#include <logos/node/utility.hpp>
#include <logos/consensus/messages/byte_arrays.hpp>
#include <boost/iostreams/stream_buffer.hpp>

struct Tip
{
    uint32_t epoch = 0;
    uint32_t sqn = 0; //same as epoch for epoch_blocks
    BlockHash digest;

    Tip();
    Tip(uint32_t epoch, uint32_t sqn, const BlockHash & digest);
    Tip(bool & error, logos::stream & stream);
    Tip(bool & error, logos::mdb_val & mdbval);

    uint32_t Serialize(logos::stream & stream) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const;

    void Hash(blake2b_state & hash) const;
    bool operator<(const Tip & other) const;
    bool operator==(const Tip & other) const;
    bool operator!=(const Tip & other) const;
    void clear();
    std::string to_string () const;
    uint32_t n_th_block_in_epoch(uint32_t expected_epoch) const;
    static constexpr uint32_t WireSize = sizeof(epoch) + sizeof(sqn) + HASH_SIZE;
};

using BatchTips       = Tip[NUM_DELEGATES];
