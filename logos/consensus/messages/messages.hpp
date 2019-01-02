#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/state_block.hpp>
#include <logos/consensus/messages/batch_state_block.hpp>
#include <logos/epoch/epoch_transition.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/log.hpp>

#include <arpa/inet.h>

static constexpr size_t MAX_MSG_SIZE = 1024*1024;
//The current largest message is a post-committed BSB with 1500 StateBlock,
//each has 8 transactions. Its size is 850702;

//ConsensusBlock definitions
template<ConsensusType CT, typename E = void>
struct ConsensusBlock;

template<ConsensusType CT>
struct ConsensusBlock<CT,
    typename std::enable_if<
        CT == ConsensusType::BatchStateBlock>::type> : public BatchStateBlock
{
    ConsensusBlock() = default;
    ConsensusBlock(bool & error, logos::stream & stream, bool with_state_block)
    : BatchStateBlock(error, stream, with_state_block)
      {}

    auto GetRef()
    {
        return *this;
    }
};

template<ConsensusType CT>
struct ConsensusBlock<CT,
    typename std::enable_if<
        CT == ConsensusType::MicroBlock>::type> : public MicroBlock
{
    ConsensusBlock() = default;
    ConsensusBlock(bool & error, logos::stream & stream, bool with_state_block)
    : MicroBlock(error, stream, with_state_block)
      {}

    auto GetRef()
    {
        return *this;
    }
};

template<ConsensusType CT>
struct ConsensusBlock<CT,
    typename std::enable_if<
        CT == ConsensusType::Epoch>::type> : public Epoch
{
    ConsensusBlock() = default;
    ConsensusBlock(bool & error, logos::stream & stream, bool with_state_block)
    : Epoch(error, stream, with_state_block)
      {}

    auto GetRef()
    {
        return *this;
    }
};


template<ConsensusType CT>
struct PrePrepareMessage : public MessagePrequel<MessageType::Pre_Prepare, CT>, public ConsensusBlock<CT>
{
    PrePrepareMessage() = default;

    PrePrepareMessage(bool & error, logos::stream & stream, uint8_t version, bool with_appendix = true)
    : MessagePrequel<MessageType::Pre_Prepare, CT>(version)
    , ConsensusBlock<CT>(error, stream, with_appendix)
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

    void Serialize(std::vector<uint8_t> & t, bool with_appendix = true) const
    {
        {
            logos::vectorstream stream(t);
            MessagePrequel<MessageType::Pre_Prepare, CT>::payload_size = htole32(Serialize(stream, with_appendix));
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MessageType::Pre_Prepare, CT>::Serialize(header_stream);
        }
    }

    uint32_t Serialize(logos::stream & stream, bool with_appendix) const
    {
        return MessagePrequel<MessageType::Pre_Prepare, CT>::Serialize(stream) +
                ConsensusBlock<CT>::Serialize(stream, with_appendix);
    }

    //TODO do we need this?
    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        ConsensusBlock<CT>::SerializeJson(tree);
        tree.put("hash", Hash().to_string());
    }

    std::string SerializeJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }
};

template<ConsensusType CT>
struct PostCommittedBlock : public MessagePrequel<MessageType::Post_Committed_Block, CT>, public ConsensusBlock<CT>
{
    AggSignature    post_prepare_sig;
    AggSignature    post_commit_sig;
    BlockHash       next;

    PostCommittedBlock() = default;

    PostCommittedBlock(PrePrepareMessage<CT> & block,
            AggSignature & post_prepare_sig,
            AggSignature & post_commit_sig)
    : MessagePrequel<MessageType::Post_Committed_Block, CT>(block.version)
    , ConsensusBlock<CT>(block.ConsensusBlock<CT>::GetRef())
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
    , post_prepare_sig(error, stream)
    , post_commit_sig(error, stream)
    {
        if(error)
        {
            return;
        }

        if(with_next)
        {
            error = logos::read(stream, next);
            if(error)
            {
                return;
            }
        }
    }

    PostCommittedBlock(bool & error, logos::mdb_val & mdbval)
    {
        logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
        MessagePrequel<MessageType::Post_Committed_Block, CT> prequel(error, stream);
        new (this) PostCommittedBlock(error, stream, prequel.version, false, true);
    }

    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const
    {
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
        //TODO pre_prepare_hash == post_commit_hash
        //        post_prepare_sig.Hash(hash);
        //        post_commit_sig.Hash(hash);
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        ConsensusBlock<CT>::SerializeJson(tree);
        post_prepare_sig.SerializeJson(tree);
        post_commit_sig.SerializeJson(tree);
        tree.put("hash", Hash().to_string());
    }

    std::string SerializeJson() const
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

    void Serialize(std::vector<uint8_t> & t, bool with_appendix, bool with_next) const
    {
        {
            logos::vectorstream stream(t);
            MessagePrequel<MessageType::Post_Committed_Block, CT>::payload_size = htole32(Serialize(stream, with_appendix, with_next));
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MessageType::Post_Committed_Block, CT>::Serialize(header_stream);
        }
    }
};

// Prepare and Commit messages
//
template<MessageType MT, ConsensusType CT,
         typename E = void
         >
struct StandardPhaseMessage;

template<MessageType MT, ConsensusType CT>
struct StandardPhaseMessage<MT, CT, typename std::enable_if<
    MT == MessageType::Prepare ||
    MT == MessageType::Commit>::type> : MessagePrequel<MT, CT>
{
    StandardPhaseMessage(const BlockHash & preprepare_hash)
    : preprepare_hash(preprepare_hash)
      {}

    StandardPhaseMessage(bool & error, logos::stream & stream, uint8_t version)
    : MessagePrequel<MT, CT>(version)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, preprepare_hash);
        if(error)
        {
            return;
        }

        error = logos::read(stream, signature);
        if(error)
        {
            return;
        }
    }

    void Serialize(std::vector<uint8_t> & t) const
    {
        {
            logos::vectorstream stream(t);
            auto s = MessagePrequel<MT, CT>::Serialize(stream);
            s += logos::write(stream, preprepare_hash);
            s += logos::write(stream, signature);
            s = htole32(s);
            MessagePrequel<MT, CT>::payload_size = s;
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MT, CT>::Serialize(header_stream);
        }
    }
    BlockHash preprepare_hash;
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
struct PostPhaseMessage<MT, CT, typename std::enable_if<
    MT == MessageType::Post_Prepare ||
    MT == MessageType::Post_Commit>::type> : MessagePrequel<MT, CT>
{

    PostPhaseMessage(const BlockHash &preprepare_hash, const AggSignature &signature)
    : preprepare_hash(preprepare_hash)
    , signature(signature)
      {}

    PostPhaseMessage(bool & error, logos::stream & stream, uint8_t version)
    : MessagePrequel<MT, CT>(version)
    {
        if(error)
        {
            return;
        }

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

    void Serialize(std::vector<uint8_t> & t) const
    {
        {
            logos::vectorstream stream(t);
            auto p_size = MessagePrequel<MT, CT>::Serialize(stream);
            p_size += logos::write(stream, preprepare_hash);
            p_size += signature.Serialize(stream);
            p_size = htole32(p_size);
            MessagePrequel<MT, CT>::payload_size = p_size;
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MT, CT>::Serialize(header_stream);
        }
    }
    BlockHash       preprepare_hash;
    AggSignature    signature;
};

// Key advertisement
//
struct KeyAdvertisement : MessagePrequel<MessageType::Key_Advert,
                                         ConsensusType::Any>
{
    DelegatePubKey public_key;

    KeyAdvertisement() = default;

    KeyAdvertisement(bool & error, logos::stream & stream, uint8_t version)
    : MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>(version)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, public_key);
        if(error)
        {
            return;
        }
    }

    void Serialize(std::vector<uint8_t> & t) const
    {
        {
            logos::vectorstream stream(t);
            MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>::Serialize(stream);
            payload_size = htole32(logos::write(stream, public_key));
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MessageType::Key_Advert, ConsensusType::Any>::Serialize(header_stream);
        }
    }
};

struct ConnectedClientIds
{
    uint32_t epoch_number;
    uint8_t delegate_id;
    EpochConnection connection;
    char ip[INET6_ADDRSTRLEN];

    static constexpr size_t STREAM_SIZE  =
            sizeof(uint32_t) +
            sizeof(uint8_t) +
            sizeof(EpochConnection) +
            INET6_ADDRSTRLEN;

    ConnectedClientIds() = default;

    ConnectedClientIds(uint32_t epoch_number, uint8_t delegate_id,
            EpochConnection connection, const char *ip)
    : epoch_number(epoch_number)
    , delegate_id(delegate_id)
    , connection(connection)
    {
        memcpy(this->ip, ip, INET6_ADDRSTRLEN);
    }

    ConnectedClientIds(bool & error, logos::stream & stream)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, epoch_number);
        if(error)
        {
            return;
        }
        epoch_number = le32toh(epoch_number);

        error = logos::read(stream, delegate_id);
        if(error)
        {
            return;
        }

        error = logos::read(stream, connection);
        if(error)
        {
            return;
        }

        error = logos::read(stream, ip);
        if(error)
        {
            return;
        }
    }

    uint32_t Serialize(std::vector<uint8_t> & t) const
    {
        logos::vectorstream stream(t);

        auto s = logos::write(stream, htole32(epoch_number));
        s += logos::write(stream, delegate_id);
        s += logos::write(stream, connection);
        s += logos::write(stream, ip);

        assert(STREAM_SIZE == s);
        return s;
    }

};

struct HeartBeat : MessagePrequel<MessageType::Heart_Beat,
                                  ConsensusType::Any>
{
    uint8_t is_request = 1;

    HeartBeat() = default;

    HeartBeat(bool & error, logos::stream & stream, uint8_t version)
    : MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>(version)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, is_request);
        if(error)
        {
            return;
        }
    }

    void Serialize(std::vector<uint8_t> & t) const
    {
        {
            logos::vectorstream stream(t);
            MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>::Serialize(stream);
            payload_size = htole32(logos::write(stream, is_request));
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MessageType::Heart_Beat, ConsensusType::Any>::Serialize(header_stream);
        }
    }
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

//// Pre-Prepare Message definitions.
////
//template<ConsensusType CT, typename E = void>
//struct PrePrepareMessage;
//
//template<ConsensusType CT>
//struct PrePrepareMessage<CT,
//    typename std::enable_if<
//        CT == ConsensusType::BatchStateBlock>::type> : MessagePrequel<MessageType::Pre_Prepare, CT>, BatchStateBlock
//{};
//
//template<ConsensusType CT>
//struct PrePrepareMessage<CT,
//    typename std::enable_if<
//        CT == ConsensusType::MicroBlock>::type> : MessagePrequel<MessageType::Pre_Prepare, CT>, MicroBlock
//{};
//
//template<ConsensusType CT>
//struct PrePrepareMessage<CT,
//    typename std::enable_if<
//        CT == ConsensusType::Epoch>::type> : MessagePrequel<MessageType::Pre_Prepare, CT>, Epoch
//{};

// Request Message specializations. The underlying type can
// vary based on the consensus type.
//
template<ConsensusType CT, typename Type = void>
struct RequestMessage;

template<ConsensusType CT>
struct RequestMessage<CT,
    typename std::enable_if<
        CT == ConsensusType::BatchStateBlock>::type> : StateBlock
{};

template<ConsensusType CT>
struct RequestMessage<CT, 
	typename std::enable_if< 
		CT == ConsensusType::MicroBlock>::type> : PrePrepareMessage<ConsensusType::MicroBlock>
{};

template<ConsensusType CT>
struct RequestMessage<CT, 
	typename std::enable_if< 
		CT == ConsensusType::Epoch>::type> : PrePrepareMessage<ConsensusType::Epoch>
{};

using ApprovedBSB    = PostCommittedBlock<ConsensusType::BatchStateBlock>;
using ApprovedMB     = PostCommittedBlock<ConsensusType::MicroBlock>;
using ApprovedEB     = PostCommittedBlock<ConsensusType::Epoch>;
