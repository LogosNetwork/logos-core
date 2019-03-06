#pragma once

#include <logos/consensus/messages/common.hpp>

namespace Bootstrap {

    using Store = logos::block_store;

    constexpr uint32_t BootstrapBufExtra = 4096;
    //One page more than max consensus message size
    constexpr uint32_t BootstrapBufSize = MAX_MSG_SIZE + BootstrapBufExtra;

    enum class MessageType : uint8_t
    {
        TipRequest   = 0,
        TipResponse  = 1,
        PullRequest  = 2,
        PullResponse = 3,
        //PullEnd      = 4,
        // Invalid
        Unknown      = 0xff
    };

    struct MessageHeader
    {
        MessageHeader() = default;

        MessageHeader(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, version);
            if(error)
            {
                return;
            }
            error = logos::read(stream, type);
            if(error)
            {
                return;
            }
            error = logos::read(stream, pull_response_ct);
            if(error)
            {
                return;
            }
            error = logos::read(stream, mpf);
            if(error)
            {
                return;
            }
            error = logos::read(stream, payload_size);
            if(error)
            {
                return;
            }
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, version);
            s += logos::write(stream, type);
            s += logos::write(stream, pull_response_ct);
            s += logos::write(stream, mpf);
            s += logos::write(stream, payload_size);

            assert(s == WireSize);
            return s;
        }

        bool Validate()
        {
            return version != logos_version &&
                   (type == MessageType::TipRequest ||
                    type == MessageType::PullRequest ||
                    type == MessageType::TipResponse ||
                    type == MessageType::PullResponse
                    ) &&
                   payload_size <= BootstrapBufSize - WireSize;
        }

        uint8_t version = logos_version;
        MessageType type = MessageType::Unknown;
        ConsensusType pull_response_ct = ConsensusType::Any;
        uint8_t mpf = 0;
        mutable uint32_t payload_size = 0;

        static constexpr uint32_t WireSize = 8;
    };

    struct TipSet
    {
        struct Tip
        {
            uint32_t epoch;
            uint32_t sqn; //ignored for epoch blocks
            BlockHash digest;

            Tip() = default;

            Tip(uint32_t epoch, uint32_t sqn, const BlockHash & digest)
            : epoch(epoch)
            , sqn(sqn)
            , digest(digest)
            {}

            Tip(bool & error, logos::stream & stream)
            {
                error = logos::read(stream, epoch);
                if(error)
                {
                    return;
                }
                error = logos::read(stream, sqn);
                if(error)
                {
                    return;
                }
                error = logos::read(stream, digest);
            }

            uint32_t Serialize(logos::stream & stream) const
            {
                auto s = logos::write(stream, epoch);
                s += logos::write(stream, sqn);
                s += logos::write(stream, digest);

                assert(s == WireSize);
                return s;
            }

            static constexpr uint32_t WireSize = sizeof(epoch) + sizeof(sqn) + HASH_SIZE;
        };

        Tip eb;
        Tip mb;
        std::vector<Tip> bsb_vec;
        std::vector<Tip> bsb_vec_new_epoch;

        /// Class constructor
        TipSet() = default;
        TipSet(bool & error, logos::stream & stream)
        {
            new(&eb) Tip(error, stream);
            if (error) {
                return;
            }
            new(&mb) Tip(error, stream);
            if (error) {
                return;
            }
            for (uint8_t i = 0; i < NUM_DELEGATES; ++i) {
                bsb_vec.push_back(Tip(error, stream));
                if (error) {
                    return;
                }
            }

            uint8_t has_new_epoch = 0;
            error = logos::read(stream, has_new_epoch);
            if (error) {
                return;
            }
            if (has_new_epoch)
            {
                for (uint8_t i = 0; i < NUM_DELEGATES; ++i)
                {
                    bsb_vec_new_epoch.push_back(Tip(error, stream));
                    if (error) {
                        return;
                    }
                }
            }
        }

        bool HaveNewEpochTips() const
        {
            auto s = bsb_vec_new_epoch.size();
            assert(s == NUM_DELEGATES || s == 0);
            return s == NUM_DELEGATES;
        }

        /// Serialize
        /// write this object out as a stream.
        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = eb.Serialize(stream);
            s += mb.Serialize(stream);

            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
                s += bsb_vec[i].Serialize(stream);
            }

            uint8_t new_epoch_byte = HaveNewEpochTips() ? 1: 0;
            s += logos::write(stream, new_epoch_byte);
            if(new_epoch_byte)
            {
                for(uint i = 0; i < NUM_DELEGATES; ++i)
                {
                    s += bsb_vec_new_epoch[i].Serialize(stream);
                }
            }

            return s;
        }

        uint32_t GetWireSize() const
        {
            return eb.WireSize + mb.WireSize + Tip::WireSize * (HaveNewEpochTips()? 2 : 1);
        }

        /*
        //TODO
        /// ostream operator
        /// @param out ostream reference
        /// @param resp BatchBlock::tips_response (object to stream out)
        /// @returns ostream operator

        friend
        ostream& operator<<(ostream &out, BatchBlock::tips_response resp)
        {
            out << "block_type: tips_block timestamp_start: " << resp.timestamp_start
                << " timestamp_end: " << resp.timestamp_end
                << " delegate_id: "   << resp.delegate_id
                << " epoch_block_tip: [" << resp.epoch_block_tip.to_string() << "] "
                << " micro_block_tip: [" << resp.micro_block_tip.to_string() << "] "
                << " epoch_block_seq_number: " << resp.epoch_block_seq_number
                << " micro_block_seq_number: " << resp.micro_block_seq_number
                << "\n";
            for(int i = 0; i < NUMBER_DELEGATES; ++i) {
                out << " batch_block_tip: [" << resp.batch_block_tip[i].to_string() << "] "
                    << " batch_block_seq_number: " << resp.batch_block_seq_number[i] << "\n";
            }
            return out;
        }
    */
    };

/*
/// getBatchBlockTip
/// @param s Store reference
/// @param delegate int
/// @returns BlockHash (the tip we asked for)
    BlockHash getBatchBlockTip(Store &s, int delegate);

/// getBatchBlockSeqNr
/// @param s Store reference
/// @param delegate int
/// @returns uint64_t (the sequence number)
    uint64_t  getBatchBlockSeqNr(Store &s, int delegate);
*/

    struct PullRequest {
        ConsensusType block_type;
        BlockHash pos;
        uint32_t num_blocks;

        PullRequest(ConsensusType block_type,
            const BlockHash &pos,
            uint32_t num_blocks)
        : block_type(block_type)
        , pos(pos)
        , num_blocks(num_blocks)
        { }

        PullRequest(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, block_type);
            if(error)
            {
                return;
            }
            error = logos::read(stream, pos);
            if(error)
            {
                return;
            }
            error = logos::read(stream, num_blocks);
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, block_type);
            s += logos::write(stream, pos);
            s += logos::write(stream, num_blocks);

            assert(s == WireSize);
            return s;
        }

        static constexpr uint32_t WireSize = sizeof(block_type) + HASH_SIZE + sizeof(num_blocks);
    };

/*    struct PullEnd {
        BlockHash pos;

        PullEnd(const BlockHash &pos)
        : pos(pos)
        { }

        PullEnd(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, pos);
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, pos);

            assert(s == WireSize);
            return s;
        }

        static constexpr uint32_t WireSize = HASH_SIZE;
    };*/

    enum class PullResponseStatus
    {
        MoreBlock,
        LastBlock,
        NoBlock
    };

    template<ConsensusType CT>
    struct PullResponse
    {
        PullResponseStatus status;
        BlockHash pos;
        uint32_t block_number;
        shared_ptr<PostCommittedBlock<CT>> block;

        PullResponse(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, status);
            if(error)
            {
                return;
            }
            if(status == PullResponseStatus::NoBlock)
            {
                return;
            }
            error = logos::read(stream, pos);
            if(error)
            {
                return;
            }
            error = logos::read(stream, block_number);
            if(error)
            {
                return;
            }
            //TODO block
        }
    };

    /**
     * At the server side, to save a round of deserialization and then serialization,
     * we serialize the meta-data fields and memcpy the block directly to the buffer.
     */
    uint32_t PullResponseSerialize(
            PullResponseStatus status,
            const BlockHash &pos,
            uint32_t block_number,
            uint32_t header_reserve,
            const logos::mdb_val & block,
            std::vector<uint8_t> & buf)
    {
        //TODO
    }

} // namespace
