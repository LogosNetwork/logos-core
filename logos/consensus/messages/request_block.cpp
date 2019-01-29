#include <logos/consensus/messages/request_block.hpp>

RequestBlock::RequestBlock(bool & error, logos::stream & stream, bool with_requests)
    : PrePrepareCommon(error, stream)
{
    if(error)
    {
        return;
    }

    uint16_t size;
    error = logos::read(stream, size);
    if(error)
    {
        return;
    }

    if((error = (size > CONSENSUS_BATCH_SIZE)))
    {
        return;
    }

    hashes.assign(size, BlockHash());
    for(uint64_t i = 0; i < size; ++i)
    {
        error = logos::read(stream, hashes[i]);
        if(error)
        {
            return;
        }
    }

    if( with_requests )
    {
        for(uint64_t i = 0; i < size; ++i)
        {
            auto val = RequestPtr(new Send(error, stream));
            if(error)
            {
                return;
            }

            requests.push_back(val);
        }
    }
}

bool RequestBlock::AddRequest(const Send &request)
{
    // TODO: What if the requests.size() is greater
    //       than CONSENSUS_BATCH_SIZE?
    //
    if(requests.size() >= CONSENSUS_BATCH_SIZE)
    {
        return false;
    }

    requests.push_back(RequestPtr(new Send(request)));
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
        // TODO: GetHash
        //
        requests[i]->Hash().Hash(hash);
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
        // TODO: GetHash
        //
        s += logos::write(stream, requests[i]->Hash());
    }

    if(with_requests)
    {
        for(uint64_t i = 0; i < size; ++i)
        {
            s += requests[i]->Serialize(stream);
        }
    }

    return s;
}
