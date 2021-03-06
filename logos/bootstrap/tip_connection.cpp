#include <logos/node/utility.hpp>

#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/tip_connection.hpp>
#include <logos/bootstrap/connection.hpp>


namespace Bootstrap
{
    TipClient::TipClient (std::shared_ptr<ISocket> connection, Store & store)
    : connection (connection)
    , request(TipSet::CreateTipSet(store))
    {
        LOG_TRACE(log) <<"TipClient::"<< __func__;
    }

    TipClient::~TipClient ()
    {
        LOG_TRACE(log) <<"TipClient::"<< __func__;
    }

    void TipClient::run ()
    {
        LOG_TRACE(log) << "TipClient::run";
        auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
        {
            logos::vectorstream stream (*send_buffer);
            MessageHeader header(logos_version, MessageType::TipRequest, ConsensusType::Any, TipSet::WireSize);
            header.Serialize(stream);
            request.Serialize(stream);
        }

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this, this_l, send_buffer](bool good)
                {
                    if (good)
                    {
                        receive_tips();
                    }
                    else
                    {
                        try {
                            LOG_TRACE(log) << "TipClient::run AsyncSend error";
                            promise.set_value(true); // Report the error to caller in bootstrap.cpp.
                        }
                        catch(const std::future_error &e)
                        {
                            LOG_TRACE(log) << "TipClient::run: error setting promise: " << e.what();
                        }
                    }
                });
    }

    void TipClient::receive_tips()
    {
        LOG_TRACE(log) <<"TipClient::"<< __func__;
        auto this_l = shared_from_this ();
        connection->AsyncReceive([this, this_l](bool good, MessageHeader header, uint8_t * buf)
            {
                bool error = false;
                if(good)
                {
                    logos::bufferstream stream (buf, header.payload_size);
                    new (&response) TipSet(error, stream);
                    if (!error)
                    {
                        LOG_TRACE(log) <<"TipClient::"<< __func__ << "tips parsed";

                        //TODO more validation of tips in bootstrap V2
                        if(response.eb.epoch != response.mb.epoch &&
                                response.eb.epoch+1 != response.mb.epoch)
                        {
                            LOG_INFO(log) << "TipClient::received_tips validation error";
                            error = true;
                        }
                        else
                        {
                            connection->Release();
                            connection = nullptr;
                            promise.set_value(false);
                        }
                    }
                } else {
                    LOG_INFO(log) << "TipClient::received_tips parse error";
                    error = true;
                }

                if(error)
                {
                    connection->OnNetworkError();
                    connection = nullptr;
                    promise.set_value(true);
                }
            });
    }

    /////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////

    // NOTE Server sends tips to the client, client decides what to do
    TipServer::TipServer (std::shared_ptr<ISocket> connection, TipSet & request, Store & store)
    : connection (connection)
    , request (request)
    , response (TipSet::CreateTipSet(store))
    {
        LOG_TRACE(log) <<"TipServer::"<<__func__;
    }

    TipServer::~TipServer()
    {
        LOG_TRACE(log) << "TipServer::"<<__func__;
    }

    void TipServer::send_tips()
    {
        LOG_TRACE(log) << "TipServer::send_tips";
        auto send_buffer(std::make_shared<std::vector<uint8_t>>());
        {
            logos::vectorstream stream(*send_buffer);
            MessageHeader header(logos_version, MessageType::TipResponse, ConsensusType::Any, TipSet::WireSize);
            header.Serialize(stream);
            response.Serialize(stream);
        }
#ifdef DUMP_BLOCK_DATA
        {
            std::stringstream stream;
            for(size_t i = 0; i < send_buffer->size(); ++i)
            {
                stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (unsigned int)((*send_buffer)[i]);
            }
            LOG_TRACE(log) << "TipServer::"<<__func__ <<" data:" << stream.str ();
        }
#endif

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this, this_l](bool good)
                {
                    if (good)
                    {
                        LOG_INFO (log) << "TipServer::send_tips Sending tips done";
                        connection->Release();
                    }
                    else
                    {
                        LOG_ERROR(log) << "TipServer::send_tips Error sending tips";
                        connection->OnNetworkError();
                    }
                });

        //TODO compare tips and trigger local bootstrap
        //connection->node->ongoing_bootstrap();
    }

}//namespace
