#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/consensus/messages/util.hpp>

namespace Bootstrap
{

    MessageHeader::MessageHeader(uint8_t version,
            MessageType type,
            ConsensusType ct,
            uint32_t payload_size)
    : version(version)
    , type(type)
    , pull_response_ct(ct)
    , payload_size(payload_size)
    {}

    MessageHeader::MessageHeader(bool & error, logos::stream & stream)
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

    uint32_t MessageHeader::Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, version);
        s += logos::write(stream, type);
        s += logos::write(stream, pull_response_ct);
        s += logos::write(stream, mpf);
        s += logos::write(stream, payload_size);

        assert(s == WireSize);
        return s;
    }

    bool MessageHeader::Validate()
    {
        return version == logos_version &&
               (type == MessageType::TipRequest ||
                type == MessageType::PullRequest ||
                type == MessageType::TipResponse ||
                type == MessageType::PullResponse
                ) &&
               payload_size <= BootstrapBufSize - WireSize;
    }

    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////

    PullRequest::PullRequest(ConsensusType block_type,
        uint32_t epoch_num,
        const BlockHash &prev,
        const BlockHash &target)
    : block_type(block_type)
    , epoch_num(epoch_num)
    , prev_hash(prev)
    , target(target)
    { }

    PullRequest::PullRequest(bool & error, logos::stream & stream)
    {
        error = logos::read(stream, block_type);
        if(error)
        {
            return;
        }
        error = logos::read(stream, epoch_num);
        if(error)
        {
            return;
        }
        error = logos::read(stream, prev_hash);
        if(error)
        {
            return;
        }
        error = logos::read(stream, target);
    }

    uint32_t PullRequest::Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, block_type);
        s += logos::write(stream, epoch_num);
        s += logos::write(stream, prev_hash);
        s += logos::write(stream, target);

        assert(s == WireSize);
        return s;
    }

    bool PullRequest::operator==(const PullRequest & other) const{
        return block_type==other.block_type &&
                epoch_num==other.epoch_num &&
                prev_hash==other.prev_hash &&
                target==other.target;
    }

    std::string PullRequest::to_string() const{
        std::stringstream stream;
        stream << ConsensusToName(block_type) << ":" << epoch_num << ":"<< prev_hash.to_string() << ":"<< target.to_string() ;

        return stream.str ();
    }
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////

    using LeadingFieldsStream =
            boost::iostreams::stream_buffer<boost::iostreams::basic_array_sink<uint8_t>>;

    //return total message size including header
    uint32_t PullResponseSerializedLeadingFields(ConsensusType ct,
            PullResponseStatus status,
            uint32_t block_size,
            std::vector<uint8_t> & buf)
    {
        if(buf.size() < PullResponseReserveSize)
        {
            buf.resize(PullResponseReserveSize);
        }

        LeadingFieldsStream stream(buf.data(), PullResponseReserveSize);
        uint32_t payload_size = sizeof(status) + block_size;
        MessageHeader header(logos_version,
                MessageType::PullResponse,
                ct,
                payload_size);
        header.Serialize(stream);
        logos::write(stream, status);
        return MessageHeader::WireSize + payload_size;
    }

#ifdef BOOTSTRAP_PROGRESS
    std::atomic<unsigned> num_blocks_processed(1);
    unsigned get_block_progress()
    {
        unsigned b = num_blocks_processed;
        num_blocks_processed = 0;
        return b;
    }
    void block_progressed()
    {
        num_blocks_processed++;
    }
#endif
}
