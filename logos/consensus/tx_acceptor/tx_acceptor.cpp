// @file
// This file contains implementation of TxAcceptor which receives transactions from
// a client and forwards them to Delegate. TxAcceptor mitigates risk of DDoS attack.
// A delegate can have multiple TxAcceptors.
//

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/tx_acceptor/tx_acceptor_channel.hpp>
#include <logos/consensus/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/consensus/tx_acceptor/tx_acceptor.hpp>
#include <logos/consensus/messages/state_block.hpp>
#include <logos/node/node.hpp>
#include "tx_message_header.hpp"

TxPeerManager::TxPeerManager(
    Service & service,
    const std::string & ip,
    const uint16_t port,
    TxAcceptor & tx_acceptor,
    Reader reader)
    : _service(service)
    , _endpoint(Endpoint(boost::asio::ip::make_address_v4(ip), port))
    , _peer_acceptor(service, _endpoint, *this)
    , _tx_acceptor(tx_acceptor)
    , _reader(reader)
{
    LOG_INFO(_log) << "TxPeerManager::TxPeerManager creating acceptor on " << _endpoint;
    _peer_acceptor.Start();
}

void
TxPeerManager::OnConnectionAccepted(const Endpoint endpoint, std::shared_ptr<Socket> socket)
{
    LOG_DEBUG(_log) << "TxPeerManager::OnConnectionAccepted, accepted from " << endpoint;
    (_tx_acceptor.*_reader)(socket);
}

TxAcceptor::TxAcceptor(Service &service,
                       std::shared_ptr<TxChannel> acceptor_channel,
                       logos::node_config & config)
    : _service(service)
    , _json_peer(service, config.tx_acceptor_config.acceptor_ip,
                 config.tx_acceptor_config.json_port, *this, &TxAcceptor::AsyncReadJson)
    , _bin_peer(service, config.tx_acceptor_config.acceptor_ip,
                config.tx_acceptor_config.bin_port, *this, &TxAcceptor::AsyncReadBin)
    , _acceptor_channel(acceptor_channel)
    , _config(config.tx_acceptor_config)
{
    LOG_INFO(_log) << "TxAcceptor::TxAcceptor creating delegate TxAcceptor";
}

TxAcceptor::TxAcceptor(Service &service,
                       logos::node_config & config)
        : _service(service)
        , _json_peer(service, config.tx_acceptor_config.acceptor_ip,
                     config.tx_acceptor_config.json_port, *this, &TxAcceptor::AsyncReadJson)
        , _bin_peer(service, config.tx_acceptor_config.acceptor_ip,
                    config.tx_acceptor_config.bin_port, *this, &TxAcceptor::AsyncReadBin)
        , _config(config.tx_acceptor_config)
{
    LOG_INFO(_log) << "TxAcceptor::TxAcceptor creating standalone TxAcceptor";
    _acceptor_channel = std::make_shared<TxAcceptorChannel>(_service, _config.acceptor_ip, _config.port);
}

void
TxAcceptor::RespondJson(std::shared_ptr<json_request> jrequest, const Ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    ostream.flush ();
    auto body (ostream.str ());
    jrequest->res.set ("Content-Type", "application/json");
    jrequest->res.set ("Access-Control-Allow-Origin", "*");
    jrequest->res.set ("Access-Control-Allow-Headers", "Accept, Accept-Language, Content-Language, Content-Type");
    jrequest->res.set ("Connection", "close");
    jrequest->res.result (boost::beast::http::status::ok);
    jrequest->res.body () = body;
    jrequest->res.version (jrequest->request.version());
    jrequest->res.prepare_payload ();
    boost::beast::http::async_write (*jrequest->socket, jrequest->res, [this, jrequest](Error const & ec, size_t) {
        if (ec)
        {
            LOG_ERROR(_log) << "RespondJson error: " << ec.message();
        }
    });
}

void
TxAcceptor::RespondJson(std::shared_ptr<json_request> jrequest, const std::string & key, const std::string & value)
{
    Ptree tree;
    tree.put(key, value);
    RespondJson(jrequest, tree);
}

std::shared_ptr<StateBlock>
TxAcceptor::ToStateBlock(const std::string&& block_text)
{
    Ptree pblock;
    std::stringstream block_stream(block_text);
    boost::property_tree::read_json(block_stream, pblock);
    bool error = false;
    auto block = std::make_shared<StateBlock>(error, pblock, false, true);
    if (error)
    {
        block = nullptr;
    }
    return block;
}

logos::process_result
TxAcceptor::ProcessBlock(std::shared_ptr<StateBlock> block, bool should_buffer)
{
    auto result = Validate(block);
    if (result !=logos::process_result::progress)
    {
        LOG_ERROR(_log) << "TxAcceptor::AsyncReadJson failed to validate transaction "
                        << ProcessResultToString(result);
        return result;
    }

    result = _acceptor_channel->OnSendRequest(block, should_buffer).code;
    LOG_DEBUG(_log) << "TxAcceptor::AsyncReadJson OnSendRequest result "
                    << ProcessResultToString(result);

    return result;
}

void
TxAcceptor::AsyncReadJson(std::shared_ptr<Socket> socket)
{
    auto request = std::make_shared<json_request>(socket);

    boost::beast::http::async_read(*socket, request->buffer, request->request,
            [this, request](const Error &ec, size_t size){
        if (request->request.method() != boost::beast::http::verb::post)
        {
            RespondJson(request, "error", "can only POST requests");
            return;
        }

        LOG_INFO(_log) << "TxAcceptor::AsyncReadJson received transaction";

        std::stringstream istream(request->request.body());
        Ptree request_tree;
        boost::property_tree::read_json(istream, request_tree);

        auto block = ToStateBlock(request_tree.get<std::string>("block"));
        if (block == nullptr)
        {
            LOG_DEBUG(_log) << "TxAcceptor::AsyncReadJson failed to deserialize transaction";
            RespondJson(request, "error", "Block is invalid");
            return;
        }

        auto result = ProcessBlock(block,
                request_tree.get_optional<std::string>("buffer").is_initialized());

        switch (result)
        {
            case logos::process_result::progress:
                RespondJson(request, "hash", block->GetHash().to_string());
                break;
            case logos::process_result::buffered:
            case logos::process_result::buffering_done:
            case logos::process_result::pending:
                RespondJson(request, "result", ProcessResultToString(result));
                break;
            default:
                RespondJson(request, "error", ProcessResultToString(result));
        }
    });
}

void
TxAcceptor::RespondBin(std::shared_ptr<Socket> socket, logos::process_result result, BlockHash hash)
{
    TxResponse response(result, hash);
    auto buf = std::make_shared<std::vector<uint8_t>>();
    response.Serialize(*buf);
    boost::asio::async_write(*socket, boost::asio::buffer(buf->data(), buf->size()),
            [this](const Error &ec, size_t size) {
        if (ec)
        {
            LOG_ERROR(_log) << "TxAcceptor::RespondBin error: " << ec.message();
        }
    });
}

void
TxAcceptor::AsyncReadBin(std::shared_ptr<Socket> socket)
{
    auto hdr_buf = std::shared_ptr<std::array<uint8_t, TxMessageHeader::MESSAGE_SIZE>>();

    boost::asio::async_read(*socket, boost::asio::buffer(hdr_buf->data(), hdr_buf->size()),
            [this, socket, hdr_buf](const Error &ec, size_t size){
        if (ec)
        {
            LOG_ERROR(_log) << "TxAcceptor::AsyncReadBin error: " << ec.message();
            return;
        }
        bool error = false;
        TxMessageHeader header(error, hdr_buf->data(), hdr_buf->size());
        if (error)
        {
            LOG_ERROR(_log) << "TxAcceptor::AsyncReadBin header deserialize error";
            RespondBin(socket, logos::process_result::invalid_request);
            return;
        }
        LOG_INFO(_log) << "TxReceiverChannel::AsyncReadBin received header";
        auto buf = std::make_shared<std::vector<uint8_t>>(header.payload_size);
        boost::asio::async_read(*socket, boost::asio::buffer(buf->data(), buf->size()),
                [this, socket, buf, header](const Error &ec, size_t size){
             if (ec)
             {
                 LOG_ERROR(_log) << "TxAcceptor::AsyncReadBin transaction read error: " << ec.message();
                 RespondBin(socket, logos::process_result::invalid_request);
                 return;
             }
             bool error;
             auto block = std::make_shared<StateBlock>(error, buf->data(), buf->size());
             if (error)
             {
                 LOG_ERROR(_log) << "TxAcceptor::AsyncReadBin transaction deserialize error";
                 RespondBin(socket, logos::process_result::invalid_request);
                 return;
             }

             auto result = ProcessBlock(block, header.mpf);
             BlockHash hash = 0;
             if (result == logos::process_result::progress)
             {
                 hash = block->GetHash();
             }
             RespondBin(socket, result, hash);
        });
    });
}

logos::process_result
TxAcceptor::Validate(const std::shared_ptr<StateBlock> & block)
{
    if (block->account.is_zero())
    {
        return logos::process_result::opened_burn_account;
    }

    if (block->transaction_fee.number() < PersistenceManager<BSBCT>::MIN_TRANSACTION_FEE)
    {
        return logos::process_result::insufficient_fee;
    }

    if (!block->VerifySignature(block->account))
    {
        return logos::process_result::bad_signature;
    }

    return logos::process_result::progress;
}
