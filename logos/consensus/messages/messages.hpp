#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/receive_block.hpp>
#include <logos/consensus/messages/request_block.hpp>
#include <logos/consensus/messages/tip.hpp>
#include <logos/epoch/epoch_transition.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/log.hpp>
#include <logos/lib/trace.hpp>
#include <arpa/inet.h>


static constexpr size_t MAX_MSG_SIZE = 1024*1024;
// TODO: Update based on new request types
// The current largest message is a post-committed RequestBlock with 1500 Sends,
// each has 8 transactions. Its size is 850702;

// ConsensusBlock definitions
template<ConsensusType CT, typename E = void>
struct ConsensusBlock;

template<>
struct ConsensusBlock<ConsensusType::Request> : public RequestBlock
{
    ConsensusBlock() = default;
    ConsensusBlock(bool & error, logos::stream & stream, bool with_appendix)
        : RequestBlock(error, stream, with_appendix)
    {}
};
template<>
struct ConsensusBlock<ConsensusType::MicroBlock> : public MicroBlock
{
    ConsensusBlock() = default;
    ConsensusBlock(bool & error, logos::stream & stream, bool with_appendix)
        : MicroBlock(error, stream, with_appendix)
    {}
};
template<>
struct ConsensusBlock<ConsensusType::Epoch> : public Epoch
{
    ConsensusBlock() = default;
    ConsensusBlock(bool & error, logos::stream & stream, bool with_appendix)
        : Epoch(error, stream, with_appendix)
    {}
};

template<ConsensusType CT> struct PostCommittedBlock;

template<ConsensusType CT>
struct PrePrepareMessage : public MessagePrequel<MessageType::Pre_Prepare, CT>,
                           public ConsensusBlock<CT>
{
    PrePrepareMessage() = default;

    PrePrepareMessage(bool & error,
                      logos::stream & stream,
                      uint8_t version,
                      bool with_appendix = true)
        : MessagePrequel<MessageType::Pre_Prepare, CT>(version)
        , ConsensusBlock<CT>(error, stream, with_appendix)
    {}

    PrePrepareMessage(const PostCommittedBlock<CT> & block)
        : MessagePrequel<MessageType::Pre_Prepare, CT>(block.version)
        , ConsensusBlock<CT>(block)
    {}

    BlockHash Hash() const
    {
        return Blake2bHash<PrePrepareMessage<CT>>(*this);
    }

    void Hash(blake2b_state & hash) const
    {
        MessagePrequel<MessageType::Pre_Prepare, CT>::Hash(hash);
        ConsensusBlock<CT>::Hash(hash);
    }

    void Serialize(std::vector<uint8_t> & buf, bool with_appendix = true) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            MessagePrequel<MessageType::Pre_Prepare, CT>::payload_size =
                    Serialize(stream, with_appendix) - MessagePrequelSize;
        }

        HeaderStream header_stream(buf.data(), MessagePrequelSize);
        MessagePrequel<MessageType::Pre_Prepare, CT>::Serialize(header_stream);
    }

    uint32_t Serialize(logos::stream & stream, bool with_appendix) const
    {
        return MessagePrequel<MessageType::Pre_Prepare, CT>::Serialize(stream) +
                ConsensusBlock<CT>::Serialize(stream, with_appendix);
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        ConsensusBlock<CT>::SerializeJson(tree);
        tree.put("hash", Hash().to_string());
    }

    std::string ToJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }
};

template<ConsensusType CT>
struct PostCommittedBlock : public MessagePrequel<MessageType::Post_Committed_Block, CT>,
                            public ConsensusBlock<CT>
{
    PostCommittedBlock() = default;

    PostCommittedBlock(PrePrepareMessage<CT> & block,
                       AggSignature & post_prepare_sig,
                       AggSignature & post_commit_sig)
        : MessagePrequel<MessageType::Post_Committed_Block, CT>(block.version)
        , ConsensusBlock<CT>(block)
        , post_prepare_sig(post_prepare_sig)
        , post_commit_sig(post_commit_sig)
        , next()
    { }

    PostCommittedBlock(bool & error,
                       logos::stream & stream,
                       uint8_t version,
                       bool with_appendix,
                       bool with_next)
        : MessagePrequel<MessageType::Post_Committed_Block, CT>(version)
        , ConsensusBlock<CT>(error, stream, with_appendix)
    {
        if(error)
        {
            return;
        }

        post_prepare_sig = AggSignature(error, stream);
        if(error)
        {
            return;
        }

        post_commit_sig = AggSignature(error, stream);
        if(error)
        {
            return;
        }

        if(with_next)
        {
            error = logos::read(stream, next);
        }
    }

    PostCommittedBlock(bool & error, logos::mdb_val & mdbval)
    {
        logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
        MessagePrequel<MessageType::Post_Committed_Block, CT> prequel(error, stream);
        if(error)
            return;
        new (this) PostCommittedBlock(error, stream, prequel.version, false, true);
    }

    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            Serialize(stream, false, true);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }

    BlockHash Hash() const
    {
        return Blake2bHash<PostCommittedBlock<CT>>(*this);
    }

    void Hash(blake2b_state & hash) const
    {
        MessagePrequel<MessageType::Post_Committed_Block, CT>::Hash(hash);
        ConsensusBlock<CT>::Hash(hash);
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        ConsensusBlock<CT>::SerializeJson(tree);
        post_prepare_sig.SerializeJson(tree);
        post_commit_sig.SerializeJson(tree);
        tree.put("next", next.to_string());
        tree.put("hash", Hash().to_string());
    }

    std::string ToJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }

    uint32_t Serialize(logos::stream & stream, bool with_appendix, bool with_next) const
    {
        auto s = MessagePrequel<MessageType::Post_Committed_Block, CT>::Serialize(stream);
        s += ConsensusBlock<CT>::Serialize(stream, with_appendix);
        s += post_prepare_sig.Serialize(stream);
        s += post_commit_sig.Serialize(stream);
        if(with_next)
            s += logos::write(stream, next);

        return s;
    }

    void Serialize(std::vector<uint8_t> & buf, bool with_appendix, bool with_next) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            MessagePrequel<MessageType::Post_Committed_Block, CT>::payload_size =
                    Serialize(stream, with_appendix, with_next) - MessagePrequelSize;
        }

        HeaderStream header_stream(buf.data(), MessagePrequelSize);
        MessagePrequel<MessageType::Post_Committed_Block, CT>::Serialize(header_stream);
    }

    Tip CreateTip() const
    {
    	return Tip(ConsensusBlock<CT>::epoch_number, ConsensusBlock<CT>::sequence, Hash());
    }

    AggSignature post_prepare_sig;
    AggSignature post_commit_sig;
    BlockHash    next;
};

// This should only be called for the first request block in an epoch
void update_PostCommittedRequestBlock_prev_field(const logos::mdb_val & mdbval, logos::mdb_val & mdbval_buf, const BlockHash & prev);

// Prepare and Commit messages
//
template<MessageType MT, ConsensusType CT,
         typename E = void
         >
struct StandardPhaseMessage;

template<MessageType MT, ConsensusType CT>
struct StandardPhaseMessage<MT, CT, std::enable_if_t<
    MT == MessageType::Prepare ||
    MT == MessageType::Commit>> : MessagePrequel<MT, CT>
{
    StandardPhaseMessage(const BlockHash & preprepare_hash)
        : preprepare_hash(preprepare_hash)
    {}

    StandardPhaseMessage(bool & error, logos::stream & stream, uint8_t version)
        : MessagePrequel<MT, CT>(version)
    {
        error = logos::read(stream, preprepare_hash);
        if(error)
        {
            return;
        }

        error = logos::read(stream, signature);
    }

    void Serialize(std::vector<uint8_t> & buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            auto s = MessagePrequel<MT, CT>::Serialize(stream);
            s += logos::write(stream, preprepare_hash);
            s += logos::write(stream, signature);
            MessagePrequel<MT, CT>::payload_size = s
                    - MessagePrequelSize;
        }

        HeaderStream header_stream(buf.data(), MessagePrequelSize);
        MessagePrequel<MT, CT>::Serialize(header_stream);
    }

    BlockHash   preprepare_hash;
    DelegateSig signature;
};

template<MessageType MT, ConsensusType CT>
std::ostream& operator<<(std::ostream& os, const StandardPhaseMessage<MT, CT>& m);

// Post Prepare and Post Commit messages
//
template<MessageType MT, ConsensusType CT,
         typename Enable = void
         >
struct PostPhaseMessage;

template<MessageType MT, ConsensusType CT>
struct PostPhaseMessage<MT, CT, std::enable_if_t<
    MT == MessageType::Post_Prepare ||
    MT == MessageType::Post_Commit>> : MessagePrequel<MT, CT>
{

    PostPhaseMessage(const BlockHash &preprepare_hash, const AggSignature &signature)
        : preprepare_hash(preprepare_hash)
        , signature(signature)
    {}

    PostPhaseMessage(bool & error, logos::stream & stream, uint8_t version)
        : MessagePrequel<MT, CT>(version)
    {
        error = logos::read(stream, preprepare_hash);
        if(error)
        {
            return;
        }
        new (&signature) AggSignature(error, stream);
    }

    // to compute post_prepare_hash
    BlockHash ComputeHash() const
    {
        return Blake2bHash<PostPhaseMessage<MT, CT>>(*this);
    }

    void Hash(blake2b_state & hash) const
    {
        preprepare_hash.Hash(hash);
        signature.Hash(hash);
    }

    void Serialize(std::vector<uint8_t> & buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            auto p_size = MessagePrequel<MT, CT>::Serialize(stream);
            p_size += logos::write(stream, preprepare_hash);
            p_size += signature.Serialize(stream);
            MessagePrequel<MT, CT>::payload_size = p_size
                    - MessagePrequelSize;
        }

        HeaderStream header_stream(buf.data(), MessagePrequelSize);
        MessagePrequel<MT, CT>::Serialize(header_stream);
    }
    BlockHash    preprepare_hash;
    AggSignature signature;
};

// Key advertisement
//
struct KeyAdvertisement : MessagePrequel<MessageType::Key_Advert,
                                         ConsensusType::Any>
{
    KeyAdvertisement() = default;

    KeyAdvertisement(bool & error,
                     logos::stream & stream,
                     uint8_t version)
        : MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>(version)
    {
        error = logos::read(stream, public_key);
    }

    void Serialize(std::vector<uint8_t> & buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>::Serialize(stream);
            MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>::payload_size =
                    logos::write(stream, public_key);;
        }

        HeaderStream header_stream(buf.data(), MessagePrequelSize);
        MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>::Serialize(header_stream);
    }

    DelegatePubKey public_key;
};

struct ConnectedClientIds
{
    ConnectedClientIds() = default;

    ConnectedClientIds(uint32_t epoch_number,
                       uint8_t delegate_id,
                       EpochConnection connection,
                       const char *ip);

    ConnectedClientIds(bool & error, logos::stream & stream);

    static constexpr size_t StreamSize()
    {
        return sizeof(epoch_number) +
               sizeof(delegate_id) +
               sizeof(EpochConnection) +
               INET6_ADDRSTRLEN;
    }

    uint32_t Serialize(std::vector<uint8_t> & buf) const;

    uint32_t        epoch_number;
    uint8_t         delegate_id;
    EpochConnection connection;
    char            ip[INET6_ADDRSTRLEN];
};

struct HeartBeat : MessagePrequel<MessageType::Heart_Beat,
                                  ConsensusType::Any>
{
    HeartBeat() = default;

    HeartBeat(bool & error,
              logos::stream & stream,
              uint8_t version)
        : MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>(version)
    {
        error = logos::read(stream, is_request);
    }

    void Serialize(std::vector<uint8_t> & buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>::Serialize(stream);
            MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>::payload_size =
                    logos::write(stream, is_request);
        }

        HeaderStream header_stream(buf.data(), MessagePrequelSize);
        MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>::Serialize(header_stream);
    }

    uint8_t is_request = 1;
};

void UpdateNext(const logos::mdb_val &mdbval, logos::mdb_val &mdbval_buf, const BlockHash &next);

struct P2pConsensusHeader
{
    P2pConsensusHeader(uint32_t epoch, uint8_t src, uint8_t dest)
    : epoch_number(epoch)
    , src_delegate_id(src)
    , dest_delegate_id(dest) {}
    P2pConsensusHeader(bool & error, logos::stream & stream)
    {
        Deserialize(error, stream);
    }
    P2pConsensusHeader(bool & error, std::vector<uint8_t> &buf)
    {
        logos::vectorstream stream(buf);
        Deserialize(error, stream);
    }
    void Deserialize(bool &error, logos::stream &stream)
    {
        error = logos::read(stream, epoch_number) ||
                logos::read(stream, src_delegate_id);
                logos::read(stream, dest_delegate_id);
    }

    uint32_t Serialize(logos::vectorstream &stream)
    {
       return logos::write(stream, epoch_number) +
              logos::write(stream, src_delegate_id) +
              logos::write(stream, dest_delegate_id);
    }

    uint32_t Serialize(std::vector<uint8_t> &buf)
    {
        logos::vectorstream stream(buf);
        return Serialize(stream);
    }

    uint32_t    epoch_number = 0;
    uint8_t     src_delegate_id = 0;
    uint8_t     dest_delegate_id = 0;
    static constexpr size_t P2PHEADER_SIZE = sizeof(epoch_number) +
                sizeof(src_delegate_id) +
                sizeof(dest_delegate_id);
};

// Convenience aliases for message names.
//
template<ConsensusType CT>
using PrepareMessage = StandardPhaseMessage<MessageType::Prepare, CT>;

template<ConsensusType CT>
using CommitMessage = StandardPhaseMessage<MessageType::Commit, CT>;

template<ConsensusType CT>
using PostPrepareMessage = PostPhaseMessage<MessageType::Post_Prepare, CT>;

template<ConsensusType CT>
using PostCommitMessage = PostPhaseMessage<MessageType::Post_Commit, CT>;

// Delegate Message specializations. The underlying type can
// vary based on the consensus type.
//
template<ConsensusType CT>
struct DelegateMessage;

template<>
struct DelegateMessage<ConsensusType::Request> : Request
{};

template<>
struct DelegateMessage<ConsensusType::MicroBlock> : PrePrepareMessage<ConsensusType::MicroBlock>
{};

template<>
struct DelegateMessage<ConsensusType::Epoch> : PrePrepareMessage<ConsensusType::Epoch>
{};

using ApprovedRB = PostCommittedBlock<ConsensusType::Request>;
using ApprovedMB = PostCommittedBlock<ConsensusType::MicroBlock>;
using ApprovedEB = PostCommittedBlock<ConsensusType::Epoch>;
