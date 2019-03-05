#include <logos/consensus/messages/request_block.hpp>

#include <logos/request/utility.hpp>

RequestBlock::RequestBlock(bool & error, logos::stream & stream, bool with_requests)
    : PrePrepareCommon(error, stream)
{
    Log log;
    if(error)
    {
        LOG_FATAL(log) << "RequestBlock - error in prepreparecommon";
        return;
    }

    uint16_t size;
    error = logos::read(stream, size);
    if(error)
    {
        LOG_FATAL(log) << "RequestBlock - error reading size";
        return;
    }

    if((error = (size > CONSENSUS_BATCH_SIZE)))
    {
        LOG_FATAL(log) << "RequestBlock - size is too great";
        return;
    }

    hashes.assign(size, BlockHash());
    for(uint64_t i = 0; i < size; ++i)
    {
        error = logos::read(stream, hashes[i]);
        if(error)
        {
            LOG_FATAL(log) << "RequestBlock - error reading hash : " << i;
            return;
        }
    }

    if( with_requests )
    {
        for(uint64_t i = 0; i < size; ++i)
        {
            auto val = DeserializeRequest(error, stream);
            if(error)
            {
                LOG_FATAL(log) << "RequestBlock - error deserializing request: "
                    << i;
                return;
            }

            requests.push_back(val);
        }
    }
}

bool RequestBlock::AddRequest(RequestPtr request)
{
    // TODO: What if the requests.size() is greater
    //       than CONSENSUS_BATCH_SIZE?
    //
    if(requests.size() >= CONSENSUS_BATCH_SIZE)
    {
        return false;
    }

    requests.push_back(request);
    return true;
}

/// Add the data members to a hash context
/// @param hash the hash context
void RequestBlock::Hash(blake2b_state & hash) const
{
    PrePrepareCommon::Hash(hash);

    uint16_t size = requests.size();
    blake2b_update(&hash, &size, sizeof(size));

    for(uint16_t i = 0; i < size; ++i)
    {
        requests[i]->GetHash().Hash(hash);
    }
}

void RequestBlock::SerializeJson(boost::property_tree::ptree & tree) const
{
    PrePrepareCommon::SerializeJson(tree);

    tree.put("type", "RequestBlock");
    tree.put("request_count", std::to_string(requests.size()));

    boost::property_tree::ptree request_tree;
    for(uint64_t i = 0; i < requests.size(); ++i)
    {
        boost::property_tree::ptree txn_content = requests[i]->SerializeJson();
        request_tree.push_back(std::make_pair("", txn_content));
    }
    tree.add_child("requests", request_tree);
}

uint32_t RequestBlock::Serialize(logos::stream & stream, bool with_requests) const
{
    uint16_t size = uint16_t(requests.size());

    auto s = PrePrepareCommon::Serialize(stream);
    s += logos::write(stream, size);

    for(uint64_t i = 0; i < size; ++i)
    {
        s += logos::write(stream, requests[i]->GetHash());
    }

    if(with_requests)
    {
        for(uint64_t i = 0; i < size; ++i)
        {
            s += requests[i]->ToStream(stream);
        }
    }

    return s;
}
