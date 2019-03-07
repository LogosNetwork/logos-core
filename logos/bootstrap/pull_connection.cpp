#include <logos/bootstrap/pull_connection.hpp>

namespace Bootstrap
{

	bulk_pull_client::bulk_pull_client (std::shared_ptr<ISocket> connection, Puller & puller)
	: connection(connection)
	, puller(puller)
	, pull(puller.GetPull())
	{
		LOG_TRACE(log) << __func__;
	}

	bulk_pull_client::~bulk_pull_client ()
	{
		LOG_TRACE(log) << __func__;
	}

	void bulk_pull_client::run()
	{

	}

    void bulk_pull_client::receive_block ()
    {

    }

    bulk_pull_server::bulk_pull_server (std::shared_ptr<ISocket> server, PullPtr pull)
    : connection(server)
    , request(pull)
	{
		LOG_TRACE(log) << __func__;
	}

    bulk_pull_server::~bulk_pull_server()
	{
		LOG_TRACE(log) << __func__;
	}

    void bulk_pull_server::run()
    {

    }

    void bulk_pull_server::send_block ()
    {

    }

}
