#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/common.hpp>

AggSignature::AggSignature(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    unsigned long m;
    error = logos::read(stream, m);
    if(error)
    {
        return;
    }
    new (&map) ParicipationMap(le64toh(m));

    error = logos::read(stream, sig);
    if(error)
    {
        return;
    }
}


PrePrepareCommon::PrePrepareCommon(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, delegate);
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

    error = logos::read(stream, sequence);
    if(error)
    {
        return;
    }
    sequence = le32toh(sequence);

    error = logos::read(stream, timestamp);
    if(error)
    {
        return;
    }
    timestamp = le64toh(timestamp);

    error = logos::read(stream, previous);
    if(error)
    {
        return;
    }

    error = logos::read(stream, preprepare_sig);
    if(error)
    {
        return;
    }
}

BatchStateBlock::BatchStateBlock(bool & error, logos::stream & stream, bool with_state_block)
    : PrePrepareCommon(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, block_count);
    if(error || block_count > CONSENSUS_BATCH_SIZE)
    {
        return;
    }
    block_count = le16toh(block_count);

    for(uint64_t i = 0; i < block_count; ++i)
    {
        error = logos::read(stream, hashs[i]);
        if(error)
        {
            return;
        }
     }

    if( with_state_block )
    {
        for(uint64_t i = 0; i < block_count; ++i)
        {
            new(&blocks[i]) StateBlock(error, stream);
            if(error)
            {
                return;
            }
        }
    }
}

void BatchStateBlock::SerializeJson(boost::property_tree::ptree & batch_state_block) const
{
    PrePrepareCommon::SerializeJson(batch_state_block);
    batch_state_block.put("type", "BatchStateBlock");
    batch_state_block.put("block_count", std::to_string(block_count));

    boost::property_tree::ptree blocks_tree;
    for(uint64_t i = 0; i < block_count; ++i)
    {
        boost::property_tree::ptree txn_content;
        blocks[i].SerializeJson(txn_content, true, false);
        blocks_tree.push_back(std::make_pair("", txn_content));
    }
    batch_state_block.add_child("blocks", blocks_tree);
}

uint32_t BatchStateBlock::Serialize(logos::stream & stream, bool with_state_block) const
{
    uint16_t bc = htole16(block_count);

    auto s = PrePrepareCommon::Serialize(stream);
    s += logos::write(stream, bc);

    for(uint64_t i = 0; i < block_count; ++i)
    {
        s += logos::write(stream, hashs[i]);
    }

    if(with_state_block)
    {
        for(uint64_t i = 0; i < block_count; ++i)
        {
            s += blocks[i].Serialize(stream, false);
        }
    }

    return s;
}

const size_t ConnectedClientIds::STREAM_SIZE;

StateBlock::StateBlock (bool & error_a, boost::property_tree::ptree const & tree_a,
         bool with_batch_hash, bool with_work)
{
    if (!error_a)
    {
        try
        {
            size_t num_trans = 0;
            auto account_l (tree_a.get<std::string> ("account"));
            error_a = account.decode_account (account_l);
            if (!error_a)
            {
                auto previous_l (tree_a.get<std::string> ("previous"));
                error_a = previous.decode_hex (previous_l);
                if (!error_a)
                {
                    auto sequence_l (tree_a.get<std::string> ("sequence"));
                    sequence = std::stoul(sequence_l);
                    auto type_l (tree_a.get<std::string> ("transaction_type"));
                    type = StrToType(type_l);
                    auto fee_l (tree_a.get<std::string> ("transaction_fee", "0"));
                    error_a = transaction_fee.decode_dec (fee_l);
                    if (!error_a)
                    {
                        auto signature_l (tree_a.get<std::string> ("signature", "0"));
                        error_a = signature.decode_hex (signature_l);
                        if (!error_a)
                        {
                            if(with_work)
                            {
                                auto work_l (tree_a.get<std::string> ("work"));
                                error_a = logos::from_string_hex (work_l, work);
                            }
                            if (!error_a)
                            {
                                if(with_batch_hash)
                                {
                                    auto batch_hash_l (tree_a.get<std::string> ("batch_hash"));
                                    error_a = batch_hash.decode_hex (batch_hash_l);
                                    if (!error_a)
                                    {
                                        auto index_in_batch_hash_l (tree_a.get<std::string> ("index_in_batch"));
                                        auto index_in_batch_ul = std::stoul(index_in_batch_hash_l);
                                        error_a = index_in_batch_ul > CONSENSUS_BATCH_SIZE;
                                        if( ! error_a)
                                            index_in_batch = index_in_batch_ul;
                                    }
                                }
                                if (!error_a)
                                {
                                    auto trans_count_l (tree_a.get<std::string> ("number_transactions"));
                                    num_trans = std::stoul(trans_count_l);
                                    auto trans_tree = tree_a.get_child("transactions");
                                    for (const std::pair<std::string, boost::property_tree::ptree> &p : trans_tree)
                                    {
                                        auto amount_l (p.second.get<std::string> ("amount"));
                                        Amount tran_amount;
                                        error_a = tran_amount.decode_dec (amount_l);
                                        if (!error_a)
                                        {
                                            auto target_l (p.second.get<std::string> ("target"));
                                            AccountAddress tran_target;
                                            error_a = tran_target.decode_account (target_l);
                                            if(error_a)
                                            {
                                                break;
                                            }
                                            error_a = ! AddTransaction(tran_target, tran_amount);
                                            if(error_a)
                                            {
                                                break;
                                            }
                                            error_a = (GetNumTransactions() > num_trans);
                                            if(error_a)
                                            {
                                                break;
                                            }
                                        }
                                    }

                                }
                            }
                        }
                    }
                }
            }
        }
        catch (std::runtime_error const &)
        {
            error_a = true;
        }

        if (!error_a)
        {
            Hash();
        }
    }
}

void StateBlock::SerializeJson(boost::property_tree::ptree & tree,
     bool with_batch_hash, bool with_work) const
{
    tree.put("account", account.to_account());
    tree.put("previous", previous.to_string());
    tree.put("sequence", std::to_string(sequence));
    tree.put("transaction_type", TypeToStr(type));
    tree.put("transaction_fee", transaction_fee.to_string_dec());
    tree.put("signature", signature.to_string());
    if(with_work)
        tree.put("work", std::to_string(work));
    tree.put("number_transactions", std::to_string(trans.size()));

    boost::property_tree::ptree ptree_tran_list;
    for (const auto & t : trans) {
        boost::property_tree::ptree ptree_tran;
        ptree_tran.put("target", t.target.to_account());
        ptree_tran.put("amount", t.amount.to_string_dec());
        ptree_tran_list.push_back(std::make_pair("", ptree_tran));
    }
    tree.add_child("transactions", ptree_tran_list);

    tree.put("hash", degest.to_string());

    if(with_batch_hash)
    {
        tree.put("batch_hash", batch_hash.to_string());
        tree.put("index_in_batch", std::to_string(index_in_batch));
    }
}

std::string to_string (const std::vector<uint8_t> & buf)
{
    std::stringstream stream;
    for(size_t i = 0; i < buf.size(); ++i)
    {
        stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (uint)(buf[i]);
    }
    return stream.str ();
}
