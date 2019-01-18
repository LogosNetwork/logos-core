// @file
// This file contains implementation of TxAcceptor which receives transactions from
// a client and forwards them to Delegate. TxAcceptor mitigates risk of DDoS attack.
// A delegate can have multiple TxAcceptors.
//

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/consensus/tx_acceptor/tx_acceptor.hpp>
#include <logos/consensus/messages/state_block.hpp>
#include <logos/node/node.hpp>

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
    , _json_peer(service, config.consensus_manager_config.local_address,
                 config.tx_acceptor_config.json_port, *this, &TxAcceptor::AsyncReadJson)
    , _bin_peer(service, config.consensus_manager_config.local_address,
                config.tx_acceptor_config.bin_port, *this, &TxAcceptor::AsyncReadBin)
    , _acceptor_channel(acceptor_channel)
    , _config(config.tx_acceptor_config)
{
}

TxAcceptor::TxAcceptor(Service &service,
                       logos::node_config & config)
        : _service(service)
        , _json_peer(service, config.consensus_manager_config.local_address,
                     config.tx_acceptor_config.json_port, *this, &TxAcceptor::AsyncReadJson)
        , _bin_peer(service, config.consensus_manager_config.local_address,
                    config.tx_acceptor_config.bin_port, *this, &TxAcceptor::AsyncReadBin)
        , _config(config.tx_acceptor_config)
{
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

        std::stringstream istream(request->request.body());
        Ptree request_tree;
        boost::property_tree::read_json(istream, request_tree);

        auto block = ToStateBlock(request_tree.get<std::string>("block"));
        if (block == nullptr)
        {
            RespondJson(request, "error", "Block is invalid");
            return;
        }

        // TODO the code below should be generalized to support json and binary
        // TODO : remove validation from the delegate
        auto result = Validate(block);
        if (result !=logos::process_result::progress)
        {
            RespondJson(request, "error", ProcessResultToString(result));
            return;
        }

        result = _acceptor_channel->OnSendRequest(block,
                request_tree.get_optional<std::string>("buffer").is_initialized()).code;

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
TxAcceptor::AsyncReadBin(std::shared_ptr<Socket> socket)
{
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
