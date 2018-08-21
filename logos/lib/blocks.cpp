#include <logos/lib/blocks.hpp>

#include <boost/endian/conversion.hpp>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, logos::block const & second)
{
    static_assert (std::is_base_of<logos::block, T>::value, "Input parameter is not a block type");
    return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}
}

std::string logos::to_string_hex (uint64_t value_a)
{
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
    stream << value_a;
    return stream.str ();
}

bool logos::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
    auto error (value_a.empty ());
    if (!error)
    {
        error = value_a.size () > 16;
        if (!error)
        {
            std::stringstream stream (value_a);
            stream << std::hex << std::noshowbase;
            try
            {
                uint64_t number_l;
                stream >> number_l;
                target_a = number_l;
                if (!stream.eof ())
                {
                    error = true;
                }
            }
            catch (std::runtime_error &)
            {
                error = true;
            }
        }
    }
    return error;
}

std::string logos::block::to_json ()
{
    std::string result;
    serialize_json (result);
    return result;
}

logos::block_hash logos::block::hash () const
{
    logos::uint256_union result;
    blake2b_state hash_l;
    auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
    assert (status == 0);
    hash (hash_l);
    status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
    assert (status == 0);
    return result;
}

logos::state_hashables::state_hashables (logos::account const & account_a, logos::block_hash const & previous_a, logos::account const & representative_a, logos::amount const & amount_a, logos::uint256_union const & link_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
amount (amount_a),
link (link_a)
{
}

logos::state_hashables::state_hashables (bool & error_a, logos::stream & stream_a)
{
    error_a = logos::read (stream_a, account);
    if (!error_a)
    {
        error_a = logos::read (stream_a, previous);
        if (!error_a)
        {
            error_a = logos::read (stream_a, representative);
            if (!error_a)
            {
                error_a = logos::read (stream_a, amount);
                if (!error_a)
                {
                    error_a = logos::read (stream_a, link);
                }
            }
        }
    }
}

logos::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
    try
    {
        auto account_l (tree_a.get<std::string> ("account"));
        auto previous_l (tree_a.get<std::string> ("previous"));
        auto representative_l (tree_a.get<std::string> ("representative"));
        auto balance_l (tree_a.get<std::string> ("amount"));
        auto link_l (tree_a.get<std::string> ("link"));
        error_a = account.decode_account (account_l);
        if (!error_a)
        {
            error_a = previous.decode_hex (previous_l);
            if (!error_a)
            {
                error_a = representative.decode_account (representative_l);
                if (!error_a)
                {
                    error_a = amount.decode_dec (balance_l);
                    if (!error_a)
                    {
                        error_a = link.decode_account (link_l) && link.decode_hex (link_l);
                    }
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        error_a = true;
    }
}

void logos::state_hashables::hash (blake2b_state & hash_a) const
{
    blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
    blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
    blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
    blake2b_update (&hash_a, amount.bytes.data (), sizeof (amount.bytes));
    blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

logos::state_block::state_block (logos::account const & account_a, logos::block_hash const & previous_a, logos::account const & representative_a, logos::amount const & amount_a, logos::uint256_union const & link_a, logos::raw_key const & prv_a, logos::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, amount_a, link_a),
signature (logos::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

logos::state_block::state_block (bool & error_a, logos::stream & stream_a) :
hashables (error_a, stream_a)
{
    if (!error_a)
    {
        error_a = logos::read (stream_a, signature);
        if (!error_a)
        {
            error_a = logos::read (stream_a, work);
            boost::endian::big_to_native_inplace (work);
        }
    }
}

logos::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
    if (!error_a)
    {
        try
        {
            auto type_l (tree_a.get<std::string> ("type"));
            auto signature_l (tree_a.get<std::string> ("signature"));
            auto work_l (tree_a.get<std::string> ("work"));
            error_a = type_l != "state";
            if (!error_a)
            {
                error_a = logos::from_string_hex (work_l, work);
                if (!error_a)
                {
                    error_a = signature.decode_hex (signature_l);
                }
            }
        }
        catch (std::runtime_error const &)
        {
            error_a = true;
        }
    }
}

void logos::state_block::hash (blake2b_state & hash_a) const
{
    logos::uint256_union preamble (static_cast<uint64_t> (logos::block_type::state));
    blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
    hashables.hash (hash_a);
}

uint64_t logos::state_block::block_work () const
{
    return work;
}

void logos::state_block::block_work_set (uint64_t work_a)
{
    work = work_a;
}

logos::block_hash logos::state_block::previous () const
{
    return hashables.previous;
}

void logos::state_block::serialize (logos::stream & stream_a) const
{
    write (stream_a, hashables.account);
    write (stream_a, hashables.previous);
    write (stream_a, hashables.representative);
    write (stream_a, hashables.amount);
    write (stream_a, hashables.link);
    write (stream_a, signature);
    write (stream_a, boost::endian::native_to_big (work));
}

void logos::state_block::serialize_json (std::string & string_a) const
{
    boost::property_tree::ptree tree = serialize_json ();
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
}

boost::property_tree::ptree logos::state_block::serialize_json () const
{
    boost::property_tree::ptree tree;

    tree.put ("type", "state");
    tree.put ("account", hashables.account.to_account ());
    tree.put ("previous", hashables.previous.to_string ());
    tree.put ("representative", representative ().to_account ());
    tree.put ("amount", hashables.amount.to_string_dec ());
    tree.put ("link", hashables.link.to_string ());
    tree.put ("link_as_account", hashables.link.to_account ());
    std::string signature_l;
    signature.encode_hex (signature_l);
    tree.put ("signature", signature_l);
    tree.put ("work", logos::to_string_hex (work));

    return tree;
}

bool logos::state_block::deserialize (logos::stream & stream_a)
{
    auto error (read (stream_a, hashables.account));
    if (!error)
    {
        error = read (stream_a, hashables.previous);
        if (!error)
        {
            error = read (stream_a, hashables.representative);
            if (!error)
            {
                error = read (stream_a, hashables.amount);
                if (!error)
                {
                    error = read (stream_a, hashables.link);
                    if (!error)
                    {
                        error = read (stream_a, signature);
                        if (!error)
                        {
                            error = read (stream_a, work);
                            boost::endian::big_to_native_inplace (work);
                        }
                    }
                }
            }
        }
    }
    return error;
}

bool logos::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto error (false);
    try
    {
        assert (tree_a.get<std::string> ("type") == "state");
        auto account_l (tree_a.get<std::string> ("account"));
        auto previous_l (tree_a.get<std::string> ("previous"));
        auto representative_l (tree_a.get<std::string> ("representative"));
        auto balance_l (tree_a.get<std::string> ("balance"));
        auto link_l (tree_a.get<std::string> ("link"));
        auto work_l (tree_a.get<std::string> ("work"));
        auto signature_l (tree_a.get<std::string> ("signature"));
        error = hashables.account.decode_account (account_l);
        if (!error)
        {
            error = hashables.previous.decode_hex (previous_l);
            if (!error)
            {
                error = hashables.representative.decode_account (representative_l);
                if (!error)
                {
                    error = hashables.amount.decode_dec (balance_l);
                    if (!error)
                    {
                        error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
                        if (!error)
                        {
                            error = logos::from_string_hex (work_l, work);
                            if (!error)
                            {
                                error = signature.decode_hex (signature_l);
                            }
                        }
                    }
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        error = true;
    }
    return error;
}

void logos::state_block::visit (logos::block_visitor & visitor_a) const
{
    visitor_a.state_block (*this);
}

logos::block_type logos::state_block::type () const
{
    return logos::block_type::state;
}

bool logos::state_block::operator== (logos::block const & other_a) const
{
    return blocks_equal (*this, other_a);
}

bool logos::state_block::operator== (logos::state_block const & other_a) const
{
    return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.amount == other_a.hashables.amount && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool logos::state_block::valid_predecessor (logos::block const & block_a) const
{
    return true;
}

logos::block_hash logos::state_block::source () const
{
    return 0;
}

logos::block_hash logos::state_block::root () const
{
    return !hashables.previous.is_zero () ? hashables.previous : hashables.account;
}

logos::account logos::state_block::representative () const
{
    return hashables.representative;
}

logos::signature logos::state_block::block_signature () const
{
    return signature;
}

void logos::state_block::signature_set (logos::uint512_union const & signature_a)
{
    signature = signature_a;
}

std::unique_ptr<logos::block> logos::deserialize_block_json (boost::property_tree::ptree const & tree_a)
{
    std::unique_ptr<logos::block> result;
    try
    {
        auto type (tree_a.get<std::string> ("type"));
        if (type == "state")
        {
            bool error;
            std::unique_ptr<logos::state_block> obj (new logos::state_block (error, tree_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
    }
    catch (std::runtime_error const &)
    {
    }
    return result;
}

std::unique_ptr<logos::block> logos::deserialize_block (logos::stream & stream_a)
{
    logos::block_type type;
    auto error (read (stream_a, type));
    std::unique_ptr<logos::block> result;
    if (!error)
    {
        result = logos::deserialize_block (stream_a, type);
    }
    return result;
}

std::unique_ptr<logos::block> logos::deserialize_block (logos::stream & stream_a, logos::block_type type_a)
{
    std::unique_ptr<logos::block> result;
    switch (type_a)
    {
        case logos::block_type::state:
        {
            bool error;
            std::unique_ptr<logos::state_block> obj (new logos::state_block (error, stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
            break;
        }
        default:
            assert (false);
            break;
    }
    return result;
}

