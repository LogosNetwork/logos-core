
#include <logos/node/common.hpp>

#include <logos/lib/work.hpp>
#include <logos/node/wallet.hpp>

std::array<uint8_t, 2> constexpr logos::message::magic_number;
size_t constexpr logos::message::ipv4_only_position;
size_t constexpr logos::message::bootstrap_server_position;
std::bitset<16> constexpr logos::message::block_type_mask;

logos::message::message (logos::message_type type_a) :
version_max (logos::protocol_version),
version_using (logos::protocol_version),
version_min (logos::protocol_version_min),
type (type_a)
{
}

logos::message::message (bool & error_a, logos::stream & stream_a)
{
    error_a = read_header (stream_a, version_max, version_using, version_min, type, extensions);
}

logos::block_type logos::message::block_type () const
{
    return static_cast<logos::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void logos::message::block_type_set (logos::block_type type_a)
{
    extensions &= ~logos::message::block_type_mask;
    extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

bool logos::message::ipv4_only ()
{
    return extensions.test (ipv4_only_position);
}

void logos::message::ipv4_only_set (bool value_a)
{
    extensions.set (ipv4_only_position, value_a);
}

void logos::message::write_header (logos::stream & stream_a)
{
    logos::write (stream_a, logos::message::magic_number);
    logos::write (stream_a, version_max);
    logos::write (stream_a, version_using);
    logos::write (stream_a, version_min);
    logos::write (stream_a, type);
    logos::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool logos::message::read_header (logos::stream & stream_a, uint8_t & version_max_a, uint8_t & version_using_a, uint8_t & version_min_a, logos::message_type & type_a, std::bitset<16> & extensions_a)
{
    uint16_t extensions_l;
    std::array<uint8_t, 2> magic_number_l;
    auto result (logos::read (stream_a, magic_number_l));
    result = result || magic_number_l != magic_number;
    result = result || logos::read (stream_a, version_max_a);
    result = result || logos::read (stream_a, version_using_a);
    result = result || logos::read (stream_a, version_min_a);
    result = result || logos::read (stream_a, type_a);
    result = result || logos::read (stream_a, extensions_l);
    if (!result)
    {
        extensions_a = extensions_l;
    }
    return result;
}

logos::message_parser::message_parser (logos::message_visitor & visitor_a, logos::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
status (parse_status::success)
{
}

void logos::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
    status = parse_status::success;
    logos::bufferstream header_stream (buffer_a, size_a);
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
    logos::message_type type;
    std::bitset<16> extensions;
    if (!logos::message::read_header (header_stream, version_max, version_using, version_min, type, extensions))
    {
        switch (type)
        {
            case logos::message_type::keepalive:
            {
                deserialize_keepalive (buffer_a, size_a);
                break;
            }

            default:
            {
                status = parse_status::invalid_message_type;
                break;
            }
        }
    }
    else
    {
        status = parse_status::invalid_header;
    }
}

void logos::message_parser::deserialize_keepalive (uint8_t const * buffer_a, size_t size_a)
{
    logos::keepalive incoming;
    logos::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        visitor.keepalive (incoming);
    }
    else
    {
        status = parse_status::invalid_keepalive_message;
    }
}



bool logos::message_parser::at_end (logos::bufferstream & stream_a)
{
    uint8_t junk;
    auto end (logos::read (stream_a, junk));
    return end;
}

logos::keepalive::keepalive () :
message (logos::message_type::keepalive)
{
    logos::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
    {
        *i = endpoint;
    }
}

void logos::keepalive::visit (logos::message_visitor & visitor_a) const
{
    visitor_a.keepalive (*this);
}

void logos::keepalive::serialize (logos::stream & stream_a)
{
    write_header (stream_a);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        assert (i->address ().is_v6 ());
        auto bytes (i->address ().to_v6 ().to_bytes ());
        write (stream_a, bytes);
        write (stream_a, i->port ());
    }
}

bool logos::keepalive::deserialize (logos::stream & stream_a)
{
    auto error (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!error);
    assert (type == logos::message_type::keepalive);
    for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
    {
        std::array<uint8_t, 16> address;
        uint16_t port;
        if (!read (stream_a, address) && !read (stream_a, port))
        {
            *i = logos::endpoint (boost::asio::ip::address_v6 (address), port);
        }
        else
        {
            error = true;
        }
    }
    return error;
}

bool logos::keepalive::operator== (logos::keepalive const & other_a) const
{
    return peers == other_a.peers;
}

logos::frontier_req::frontier_req () :
message (logos::message_type::frontier_req)
{
}

bool logos::frontier_req::deserialize (logos::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
    assert (logos::message_type::frontier_req == type);

    return result;
}

void logos::frontier_req::serialize (logos::stream & stream_a)
{
    write_header (stream_a);
}

void logos::frontier_req::visit (logos::message_visitor & visitor_a) const
{
    visitor_a.frontier_req (*this);
}

bool logos::frontier_req::operator== (logos::frontier_req const & other_a) const
{
    return true;//TODO all fields removed.start == other_a.start && age == other_a.age && count == other_a.count;
}

logos::bulk_pull::bulk_pull () :
message (logos::message_type::bulk_pull)
{
}

void logos::bulk_pull::visit (logos::message_visitor & visitor_a) const
{
    visitor_a.bulk_pull (*this);
}

bool logos::bulk_pull::deserialize (logos::stream & stream_a) // Implemented these for bootstrapping.
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
    if (!result)
    {
        assert (((logos::message_type::bulk_pull == type) || 
                (logos::message_type::batch_blocks_pull == type)));
        //assert (type == logos::message_type::bulk_pull);
        result = read (stream_a, start);
        if (!result)
        {
            result = read (stream_a, end);
            if(!result) {
                result = read(stream_a,timestamp_start);
                if(!result) {
                    result = read(stream_a,timestamp_end);
                    if(!result) {
                        result = read(stream_a,delegate_id);
                        if(!result) {
                            result = read(stream_a,seq_start);
                            if(!result) {
                                result = read(stream_a,seq_end);
                                if(!result) {
                                    result = read(stream_a,e_start);
                                    if(!result) {
                                        result = read(stream_a,e_end);
                                        if(!result) {
                                            result = read(stream_a,m_start);
                                            if(!result) {
                                                result = read(stream_a,m_end);
                                                if(!result) {
                                                    result = read(stream_a,b_start);
                                                    if(!result) {
                                                        result = read(stream_a,b_end); //RGD Yeah, I know...
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
            }
        }
    }
#ifdef _DEBUG
    std::cout << "logos::bulk_pull:: result: " << result << std::endl;
#endif
    return result;
}

void logos::bulk_pull::serialize (logos::stream & stream_a)
{
    write_header (stream_a);
    write (stream_a, start);
    write (stream_a, end);
    write (stream_a, timestamp_start);
    write (stream_a, timestamp_end);
    write (stream_a, delegate_id);
    write (stream_a, seq_start);
    write (stream_a, seq_end);
    write (stream_a, e_start);
    write (stream_a, e_end);
    write (stream_a, m_start);
    write (stream_a, m_end);
    write (stream_a, b_start);
    write (stream_a, b_end);
}

logos::bulk_pull_blocks::bulk_pull_blocks () :
message (logos::message_type::bulk_pull_blocks)
{
}

void logos::bulk_pull_blocks::visit (logos::message_visitor & visitor_a) const
{
//    visitor_a.bulk_pull_blocks (*this);
}

bool logos::bulk_pull_blocks::deserialize (logos::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
    assert (logos::message_type::bulk_pull_blocks == type);
    if (!result)
    {
        assert (type == logos::message_type::bulk_pull_blocks);
        result = read (stream_a, min_hash);
        if (!result)
        {
            result = read (stream_a, max_hash);
        }

        if (!result)
        {
            result = read (stream_a, mode);
        }

        if (!result)
        {
            result = read (stream_a, max_count);
        }
    }
    return result;
}

void logos::bulk_pull_blocks::serialize (logos::stream & stream_a)
{
    write_header (stream_a);
    write (stream_a, min_hash);
    write (stream_a, max_hash);
    write (stream_a, mode);
    write (stream_a, max_count);
}

logos::bulk_push::bulk_push () :
message (logos::message_type::bulk_push)
{
}

bool logos::bulk_push::deserialize (logos::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
    assert (logos::message_type::bulk_push == type);
    return result;
}

void logos::bulk_push::serialize (logos::stream & stream_a)
{
    write_header (stream_a);
}

void logos::bulk_push::visit (logos::message_visitor & visitor_a) const
{
//    visitor_a.bulk_push (*this);
}

logos::message_visitor::~message_visitor ()
{
}
