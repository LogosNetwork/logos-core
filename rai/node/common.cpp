#include <rai/node/common.hpp>

#include <rai/node/wallet.hpp>
#include <rai/lib/work.hpp>

std::array <uint8_t, 2> constexpr rai::message::magic_number;
size_t constexpr rai::message::ipv4_only_position;
size_t constexpr rai::message::bootstrap_server_position;
std::bitset <16> constexpr rai::message::block_type_mask;

rai::message::message (rai::message_type type_a) :
version_max (0x05),
version_using (0x05),
version_min (0x01),
type (type_a)
{
}

rai::message::message (bool & error_a, rai::stream & stream_a)
{
	error_a = read_header (stream_a, version_max, version_using, version_min, type, extensions);
}

rai::block_type rai::message::block_type () const
{
	return static_cast <rai::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void rai::message::block_type_set (rai::block_type type_a)
{
	extensions &= ~rai::message::block_type_mask;
extensions |= std::bitset <16> (static_cast <unsigned long long> (type_a) << 8);
}

bool rai::message::ipv4_only ()
{
	return extensions.test (ipv4_only_position);
}

void rai::message::ipv4_only_set (bool value_a)
{
	extensions.set (ipv4_only_position, value_a);
}

void rai::message::write_header (rai::stream & stream_a)
{
	rai::write (stream_a, rai::message::magic_number);
	rai::write (stream_a, version_max);
	rai::write (stream_a, version_using);
	rai::write (stream_a, version_min);
	rai::write (stream_a, type);
	rai::write (stream_a, static_cast <uint16_t> (extensions.to_ullong ()));
}

bool rai::message::read_header (rai::stream & stream_a, uint8_t & version_max_a, uint8_t & version_using_a, uint8_t & version_min_a, rai::message_type & type_a, std::bitset <16> & extensions_a)
{
	std::array <uint8_t, 2> magic_number_l;
	auto result (rai::read (stream_a, magic_number_l));
	if (!result)
	{
		result = magic_number_l != magic_number;
		if (!result)
		{
			result = rai::read (stream_a, version_max_a);
			if (!result)
			{
				result = rai::read (stream_a, version_using_a);
				if (!result)
				{
					result = rai::read (stream_a, version_min_a);
					if (!result)
					{
						result = rai::read (stream_a, type_a);
						if (!result)
						{
							uint16_t extensions_l;
							result = rai::read (stream_a, extensions_l);
							if (!result)
							{
								extensions_a = extensions_l;
							}
						}
					}
				}
			}
		}
	}
	return result;
}

rai::message_parser::message_parser (rai::message_visitor & visitor_a, rai::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
error (false),
insufficient_work (false)
{
}

void rai::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	error = false;
	rai::bufferstream header_stream (buffer_a, size_a);
	uint8_t version_max;
	uint8_t version_using;
	uint8_t version_min;
	rai::message_type type;
	std::bitset <16> extensions;
	if (!rai::message::read_header (header_stream, version_max, version_using, version_min, type, extensions))
	{
		switch (type)
		{
			case rai::message_type::keepalive:
			{
				deserialize_keepalive (buffer_a, size_a);
				break;
			}
			case rai::message_type::publish:
			{
				deserialize_publish (buffer_a, size_a);
				break;
			}
			case rai::message_type::confirm_req:
			{
				deserialize_confirm_req (buffer_a, size_a);
				break;
			}
			case rai::message_type::confirm_ack:
			{
				deserialize_confirm_ack (buffer_a, size_a);
				break;
			}
			default:
			{
				error = true;
				break;
			}
		}
	}
	else
	{
		error = true;
	}
}

void rai::message_parser::deserialize_keepalive (uint8_t const * buffer_a, size_t size_a)
{
	rai::keepalive incoming;
	rai::bufferstream stream (buffer_a, size_a);
	auto error_l (incoming.deserialize (stream));
	if (!error_l && at_end (stream))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		error = true;
	}
}

void rai::message_parser::deserialize_publish (uint8_t const * buffer_a, size_t size_a)
{
	rai::publish incoming;
	rai::bufferstream stream (buffer_a, size_a);
	auto error_l (incoming.deserialize (stream));
	if (!error_l && at_end (stream))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			insufficient_work = true;
		}
	}
	else
	{
		error = true;
	}
}

void rai::message_parser::deserialize_confirm_req (uint8_t const * buffer_a, size_t size_a)
{
	rai::confirm_req incoming;
	rai::bufferstream stream (buffer_a, size_a);
	auto error_l (incoming.deserialize (stream));
	if (!error_l && at_end (stream))
	{
		if (!rai::work_validate (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			insufficient_work = true;
		}
	}
	else
	{
		error = true;
	}
}

void rai::message_parser::deserialize_confirm_ack (uint8_t const * buffer_a, size_t size_a)
{
	bool error_l;
	rai::bufferstream stream (buffer_a, size_a);
	rai::confirm_ack incoming (error_l, stream);
	if (!error_l && at_end (stream))
	{
		if (!rai::work_validate (*incoming.vote->block))
		{
			visitor.confirm_ack (incoming);
		}
		else
		{
			insufficient_work = true;
		}
	}
	else
	{
		error = true;
	}
}

bool rai::message_parser::at_end (rai::bufferstream & stream_a)
{
	uint8_t junk;
	auto end (rai::read (stream_a, junk));
	return end;
}

rai::keepalive::keepalive () :
message (rai::message_type::keepalive)
{
	rai::endpoint endpoint (boost::asio::ip::address_v6 {}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

void rai::keepalive::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void rai::keepalive::serialize (rai::stream & stream_a)
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

bool rai::keepalive::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (type == rai::message_type::keepalive);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		std::array <uint8_t, 16> address;
		uint16_t port;
		read (stream_a, address);
		read (stream_a, port);
		*i = rai::endpoint (boost::asio::ip::address_v6 (address), port);
	}
	return result;
}

bool rai::keepalive::operator == (rai::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

rai::publish::publish () :
message (rai::message_type::publish)
{
}

rai::publish::publish (std::shared_ptr <rai::block> block_a) :
message (rai::message_type::publish),
block (block_a)
{
	block_type_set (block->type ());
}

bool rai::publish::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (type == rai::message_type::publish);
	if (!result)
	{
		block = rai::deserialize_block (stream_a, block_type ());
		result = block == nullptr;
	}
	return result;
}

void rai::publish::serialize (rai::stream & stream_a)
{
	assert (block != nullptr);
	write_header (stream_a);
	block->serialize (stream_a);
}

void rai::publish::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool rai::publish::operator == (rai::publish const & other_a) const
{
	return *block == *other_a.block;
}

rai::confirm_req::confirm_req () :
message (rai::message_type::confirm_req)
{
}

rai::confirm_req::confirm_req (std::shared_ptr <rai::block> block_a) :
message (rai::message_type::confirm_req),
block (block_a)
{
	block_type_set (block->type ());
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (type == rai::message_type::confirm_req);
	if (!result)
	{
		block = rai::deserialize_block (stream_a, block_type ());
		result = block == nullptr;
	}
	return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
	assert (block != nullptr);
	write_header (stream_a);
	block->serialize (stream_a);
}

bool rai::confirm_req::operator == (rai::confirm_req const & other_a) const
{
	return *block == *other_a.block;
}

rai::confirm_ack::confirm_ack (bool & error_a, rai::stream & stream_a) :
message (error_a, stream_a),
vote (std::make_shared <rai::vote> (error_a, stream_a, block_type ()))
{
}

rai::confirm_ack::confirm_ack (std::shared_ptr <rai::vote> vote_a) :
message (rai::message_type::confirm_ack),
vote (vote_a)
{
	block_type_set (vote->block->type ());
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (type == rai::message_type::confirm_ack);
	if (!result)
	{
		result = read (stream_a, vote->account);
		if (!result)
		{
			result = read (stream_a, vote->signature);
			if (!result)
			{
				result = read (stream_a, vote->sequence);
				if (!result)
				{
					vote->block = rai::deserialize_block (stream_a, block_type ());
					result = vote->block == nullptr;
				}
			}
		}
	}
	return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
	assert (block_type () == rai::block_type::send || block_type () == rai::block_type::receive || block_type () == rai::block_type::open || block_type () == rai::block_type::change);
	write_header (stream_a);
	vote->serialize (stream_a, block_type ());
}

bool rai::confirm_ack::operator == (rai::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

rai::frontier_req::frontier_req () :
message (rai::message_type::frontier_req)
{
}

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::frontier_req == type);
	if (!result)
	{
		assert (type == rai::message_type::frontier_req);
		result = read (stream_a, start.bytes);
		if (!result)
		{
			result = read (stream_a, age);
			if (!result)
			{
				result = read (stream_a, count);
			}
		}
	}
	return result;
}

void rai::frontier_req::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

void rai::frontier_req::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool rai::frontier_req::operator == (rai::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

rai::bulk_pull::bulk_pull () :
message (rai::message_type::bulk_pull)
{
}

void rai::bulk_pull::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

bool rai::bulk_pull::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::bulk_pull == type);
	if (!result)
	{
		assert (type == rai::message_type::bulk_pull);
		result = read (stream_a, start);
		if (!result)
		{
			result = read (stream_a, end);
		}
	}
	return result;
}

void rai::bulk_pull::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
	write (stream_a, start);
	write (stream_a, end);
}

rai::bulk_push::bulk_push () :
message (rai::message_type::bulk_push)
{
}

bool rai::bulk_push::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::bulk_push == type);
	return result;
}

void rai::bulk_push::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
}

void rai::bulk_push::visit (rai::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}
