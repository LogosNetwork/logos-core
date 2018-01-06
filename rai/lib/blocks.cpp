#include <rai/lib/blocks.hpp>

std::string rai::to_string_hex (uint64_t value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
	stream << value_a;
	return stream.str ();
}

bool rai::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
	auto result (value_a.empty ());
	if (!result)
	{
		result = value_a.size () > 16;
		if (!result)
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
					result = true;
				}
			}
			catch (std::runtime_error &)
			{
				result = true;
			}
		}
	}
	return result;
}

std::string rai::block::to_json ()
{
	std::string result;
	serialize_json (result);
	return result;
}

rai::block_hash rai::block::hash () const
{
	rai::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}

void rai::send_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void rai::send_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t rai::send_block::block_work () const
{
	return work;
}

void rai::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

rai::send_hashables::send_hashables (rai::block_hash const & previous_a, rai::account const & destination_a, rai::amount const & balance_a) :
previous (previous_a),
destination (destination_a),
balance (balance_a)
{
}

rai::send_hashables::send_hashables (bool & error_a, rai::stream & stream_a)
{
	error_a = rai::read (stream_a, previous.bytes);
	if (!error_a)
	{
		error_a = rai::read (stream_a, destination.bytes);
		if (!error_a)
		{
			error_a = rai::read (stream_a, balance.bytes);
		}
	}
}

rai::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void rai::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	assert (status == 0);
}

void rai::send_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

void rai::send_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "send");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	tree.put ("destination", hashables.destination.to_account ());
	std::string balance;
	hashables.balance.encode_hex (balance);
	tree.put ("balance", balance);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", rai::to_string_hex (work));
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool rai::send_block::deserialize (rai::stream & stream_a)
{
	auto result (false);
	result = read (stream_a, hashables.previous.bytes);
	if (!result)
	{
		result = read (stream_a, hashables.destination.bytes);
		if (!result)
		{
			result = read (stream_a, hashables.balance.bytes);
			if (!result)
			{
				result = read (stream_a, signature.bytes);
				if (!result)
				{
					result = read (stream_a, work);
				}
			}
		}
	}
	return result;
}

bool rai::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		result = hashables.previous.decode_hex (previous_l);
		if (!result)
		{
			result = hashables.destination.decode_account (destination_l);
			if (!result)
			{
				result = hashables.balance.decode_hex (balance_l);
				if (!result)
				{
					result = rai::from_string_hex (work_l, work);
					if (!result)
					{
						result = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

rai::send_block::send_block (rai::block_hash const & previous_a, rai::account const & destination_a, rai::amount const & balance_a, rai::raw_key const & prv_a, rai::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, destination_a, balance_a),
signature (rai::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

rai::send_block::send_block (bool & error_a, rai::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = rai::read (stream_a, signature.bytes);
		if (!error_a)
		{
			error_a = rai::read (stream_a, work);
		}
	}
}

rai::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = rai::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

bool rai::send_block::operator== (rai::block const & other_a) const
{
	auto other_l (dynamic_cast<rai::send_block const *> (&other_a));
	auto result (other_l != nullptr);
	if (result)
	{
		result = *this == *other_l;
	}
	return result;
}

rai::block_type rai::send_block::type () const
{
	return rai::block_type::send;
}

bool rai::send_block::operator== (rai::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

rai::block_hash rai::send_block::previous () const
{
	return hashables.previous;
}

rai::block_hash rai::send_block::source () const
{
	return 0;
}

rai::block_hash rai::send_block::root () const
{
	return hashables.previous;
}

rai::account rai::send_block::representative () const
{
	return 0;
}

void rai::send_block::signature_set (rai::uint512_union const & signature_a)
{
	signature = signature_a;
}

rai::open_hashables::open_hashables (rai::block_hash const & source_a, rai::account const & representative_a, rai::account const & account_a) :
source (source_a),
representative (representative_a),
account (account_a)
{
}

rai::open_hashables::open_hashables (bool & error_a, rai::stream & stream_a)
{
	error_a = rai::read (stream_a, source.bytes);
	if (!error_a)
	{
		error_a = rai::read (stream_a, representative.bytes);
		if (!error_a)
		{
			error_a = rai::read (stream_a, account.bytes);
		}
	}
}

rai::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void rai::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

rai::open_block::open_block (rai::block_hash const & source_a, rai::account const & representative_a, rai::account const & account_a, rai::raw_key const & prv_a, rai::public_key const & pub_a, uint64_t work_a) :
hashables (source_a, representative_a, account_a),
signature (rai::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
	assert (!representative_a.is_zero ());
}

rai::open_block::open_block (rai::block_hash const & source_a, rai::account const & representative_a, rai::account const & account_a, std::nullptr_t) :
hashables (source_a, representative_a, account_a),
work (0)
{
	signature.clear ();
}

rai::open_block::open_block (bool & error_a, rai::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = rai::read (stream_a, signature);
		if (!error_a)
		{
			error_a = rai::read (stream_a, work);
		}
	}
}

rai::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = rai::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void rai::open_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t rai::open_block::block_work () const
{
	return work;
}

void rai::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

rai::block_hash rai::open_block::previous () const
{
	rai::block_hash result (0);
	return result;
}

void rai::open_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

void rai::open_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", rai::to_string_hex (work));
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool rai::open_block::deserialize (rai::stream & stream_a)
{
	auto result (read (stream_a, hashables.source));
	if (!result)
	{
		result = read (stream_a, hashables.representative);
		if (!result)
		{
			result = read (stream_a, hashables.account);
			if (!result)
			{
				result = read (stream_a, signature);
				if (!result)
				{
					result = read (stream_a, work);
				}
			}
		}
	}
	return result;
}

bool rai::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		result = hashables.source.decode_hex (source_l);
		if (!result)
		{
			result = hashables.representative.decode_hex (representative_l);
			if (!result)
			{
				result = hashables.account.decode_hex (account_l);
				if (!result)
				{
					result = rai::from_string_hex (work_l, work);
					if (!result)
					{
						result = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

void rai::open_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

rai::block_type rai::open_block::type () const
{
	return rai::block_type::open;
}

bool rai::open_block::operator== (rai::block const & other_a) const
{
	auto other_l (dynamic_cast<rai::open_block const *> (&other_a));
	auto result (other_l != nullptr);
	if (result)
	{
		result = *this == *other_l;
	}
	return result;
}

bool rai::open_block::operator== (rai::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

rai::block_hash rai::open_block::source () const
{
	return hashables.source;
}

rai::block_hash rai::open_block::root () const
{
	return hashables.account;
}

rai::account rai::open_block::representative () const
{
	return hashables.representative;
}

void rai::open_block::signature_set (rai::uint512_union const & signature_a)
{
	signature = signature_a;
}

rai::change_hashables::change_hashables (rai::block_hash const & previous_a, rai::account const & representative_a) :
previous (previous_a),
representative (representative_a)
{
}

rai::change_hashables::change_hashables (bool & error_a, rai::stream & stream_a)
{
	error_a = rai::read (stream_a, previous);
	if (!error_a)
	{
		error_a = rai::read (stream_a, representative);
	}
}

rai::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void rai::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

rai::change_block::change_block (rai::block_hash const & previous_a, rai::account const & representative_a, rai::raw_key const & prv_a, rai::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, representative_a),
signature (rai::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

rai::change_block::change_block (bool & error_a, rai::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = rai::read (stream_a, signature);
		if (!error_a)
		{
			error_a = rai::read (stream_a, work);
		}
	}
}

rai::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = rai::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void rai::change_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t rai::change_block::block_work () const
{
	return work;
}

void rai::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

rai::block_hash rai::change_block::previous () const
{
	return hashables.previous;
}

void rai::change_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

void rai::change_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("work", rai::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

bool rai::change_block::deserialize (rai::stream & stream_a)
{
	auto result (read (stream_a, hashables.previous));
	if (!result)
	{
		result = read (stream_a, hashables.representative);
		if (!result)
		{
			result = read (stream_a, signature);
			if (!result)
			{
				result = read (stream_a, work);
			}
		}
	}
	return result;
}

bool rai::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		result = hashables.previous.decode_hex (previous_l);
		if (!result)
		{
			result = hashables.representative.decode_hex (representative_l);
			if (!result)
			{
				result = rai::from_string_hex (work_l, work);
				if (!result)
				{
					result = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

void rai::change_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

rai::block_type rai::change_block::type () const
{
	return rai::block_type::change;
}

bool rai::change_block::operator== (rai::block const & other_a) const
{
	auto other_l (dynamic_cast<rai::change_block const *> (&other_a));
	auto result (other_l != nullptr);
	if (result)
	{
		result = *this == *other_l;
	}
	return result;
}

bool rai::change_block::operator== (rai::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

rai::block_hash rai::change_block::source () const
{
	return 0;
}

rai::block_hash rai::change_block::root () const
{
	return hashables.previous;
}

rai::account rai::change_block::representative () const
{
	return hashables.representative;
}

void rai::change_block::signature_set (rai::uint512_union const & signature_a)
{
	signature = signature_a;
}

std::unique_ptr<rai::block> rai::deserialize_block_json (boost::property_tree::ptree const & tree_a)
{
	std::unique_ptr<rai::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		if (type == "receive")
		{
			bool error;
			std::unique_ptr<rai::receive_block> obj (new rai::receive_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "send")
		{
			bool error;
			std::unique_ptr<rai::send_block> obj (new rai::send_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "open")
		{
			bool error;
			std::unique_ptr<rai::open_block> obj (new rai::open_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "change")
		{
			bool error;
			std::unique_ptr<rai::change_block> obj (new rai::change_block (error, tree_a));
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

std::unique_ptr<rai::block> rai::deserialize_block (rai::stream & stream_a)
{
	rai::block_type type;
	auto error (read (stream_a, type));
	std::unique_ptr<rai::block> result;
	if (!error)
	{
		result = rai::deserialize_block (stream_a, type);
	}
	return result;
}

std::unique_ptr<rai::block> rai::deserialize_block (rai::stream & stream_a, rai::block_type type_a)
{
	std::unique_ptr<rai::block> result;
	switch (type_a)
	{
		case rai::block_type::receive:
		{
			bool error;
			std::unique_ptr<rai::receive_block> obj (new rai::receive_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case rai::block_type::send:
		{
			bool error;
			std::unique_ptr<rai::send_block> obj (new rai::send_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case rai::block_type::open:
		{
			bool error;
			std::unique_ptr<rai::open_block> obj (new rai::open_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case rai::block_type::change:
		{
			bool error;
			std::unique_ptr<rai::change_block> obj (new rai::change_block (error, stream_a));
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

void rai::receive_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

bool rai::receive_block::operator== (rai::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

bool rai::receive_block::deserialize (rai::stream & stream_a)
{
	auto result (false);
	result = read (stream_a, hashables.previous.bytes);
	if (!result)
	{
		result = read (stream_a, hashables.source.bytes);
		if (!result)
		{
			result = read (stream_a, signature.bytes);
			if (!result)
			{
				result = read (stream_a, work);
			}
		}
	}
	return result;
}

bool rai::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		result = hashables.previous.decode_hex (previous_l);
		if (!result)
		{
			result = hashables.source.decode_hex (source_l);
			if (!result)
			{
				result = rai::from_string_hex (work_l, work);
				if (!result)
				{
					result = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

void rai::receive_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

void rai::receive_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", rai::to_string_hex (work));
	tree.put ("signature", signature_l);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

rai::receive_block::receive_block (rai::block_hash const & previous_a, rai::block_hash const & source_a, rai::raw_key const & prv_a, rai::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, source_a),
signature (rai::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

rai::receive_block::receive_block (bool & error_a, rai::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		error_a = rai::read (stream_a, signature);
		if (!error_a)
		{
			error_a = rai::read (stream_a, work);
		}
	}
}

rai::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = rai::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void rai::receive_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t rai::receive_block::block_work () const
{
	return work;
}

void rai::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool rai::receive_block::operator== (rai::block const & other_a) const
{
	auto other_l (dynamic_cast<rai::receive_block const *> (&other_a));
	auto result (other_l != nullptr);
	if (result)
	{
		result = *this == *other_l;
	}
	return result;
}

rai::block_hash rai::receive_block::previous () const
{
	return hashables.previous;
}

rai::block_hash rai::receive_block::source () const
{
	return hashables.source;
}

rai::block_hash rai::receive_block::root () const
{
	return hashables.previous;
}

rai::account rai::receive_block::representative () const
{
	return 0;
}

void rai::receive_block::signature_set (rai::uint512_union const & signature_a)
{
	signature = signature_a;
}

rai::block_type rai::receive_block::type () const
{
	return rai::block_type::receive;
}

rai::receive_hashables::receive_hashables (rai::block_hash const & previous_a, rai::block_hash const & source_a) :
previous (previous_a),
source (source_a)
{
}

rai::receive_hashables::receive_hashables (bool & error_a, rai::stream & stream_a)
{
	error_a = rai::read (stream_a, previous.bytes);
	if (!error_a)
	{
		error_a = rai::read (stream_a, source.bytes);
	}
}

rai::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void rai::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}
