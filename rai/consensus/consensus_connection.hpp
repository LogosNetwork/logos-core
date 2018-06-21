#pragma once

#include <rai/consensus/messages/messages.hpp>
#include <rai/consensus/primary_delegate.hpp>
#include <rai/consensus/consensus_state.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

namespace rai
{
    class alarm;
}

class ConsensusConnection
{

    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Socket   = boost::asio::ip::tcp::socket;
    using Log      = boost::log::sources::logger_mt;

public:

	ConsensusConnection(boost::asio::io_service & service,
	                    rai::alarm & alarm,
	                    Log & log,
	                    const Endpoint & endpoint,
	                    PrimaryDelegate * primary);

	ConsensusConnection(std::shared_ptr<Socket> socket,
                        rai::alarm & alarm,
                        Log & log,
                        const Endpoint & endpoint,
                        PrimaryDelegate * primary);

	void Send(const void * data, size_t size);

    template<typename Type>
    void Send(const Type & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }

private:

    static constexpr uint8_t  CONNECT_RETRY_DELAY = 5;
	static constexpr uint16_t BUFFER_SIZE         = 512;

    using ReceiveBuffer = std::array<uint8_t, BUFFER_SIZE>;

    void Connect();
    void Read();

    void OnConnect(boost::system::error_code const & ec);
    void OnData(boost::system::error_code const & ec, size_t size);
    void OnMessage(boost::system::error_code const & ec, size_t size);

    void OnConsensusMessage(const PrePrepareMessage & message);
    void OnConsensusMessage(const PrepareMessage & message);
    void OnConsensusMessage(const PostPrepareMessage & message);
    void OnConsensusMessage(const CommitMessage & message);
    void OnConsensusMessage(const PostCommitMessage & message);

    template<typename Msg>
    bool Validate(const Msg & msg);

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);

    ReceiveBuffer           receive_buffer_;
    std::shared_ptr<Socket> socket_;
    Endpoint                endpoint_;
    rai::alarm &            alarm_;
    Log &                   log_;
    PrimaryDelegate *       primary_;
    ConsensusState          state_     = ConsensusState::VOID;
    bool                    connected_ = false;
};



