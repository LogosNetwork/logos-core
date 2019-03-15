//#include <iostream>
//#include <logos/blockstore.hpp>
//#include <logos/bootstrap/bootstrap_messages.hpp>
//#include <logos/bootstrap/microblock.hpp>
//#include <logos/bootstrap/attempt.hpp>
//#include <logos/bootstrap/bootstrap.hpp>
//
//
//
//void Populate(Store & store)
//{
//
//}

#include <logos/bootstrap/bootstrap_messages.hpp>

namespace Bootstrap
{
	//constexpr uint32_t PullResponseReserveSize = MessageHeader::WireSize + sizeof(PullResponseStatus);
	using LeadingFieldsStream =
			boost::iostreams::stream_buffer<boost::iostreams::basic_array_sink<uint8_t>>;
	//return total message size including header
	uint32_t PullResponseSerializedLeadingFields(ConsensusType ct,
			PullResponseStatus status,
			uint32_t block_size,
			std::vector<uint8_t> & buf)
	{
		//uint32_t PullResponseReserveSize = MessageHeader::WireSize + sizeof(PullResponseStatus);
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
}
