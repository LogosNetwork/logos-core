#include <logos/lib/interface.h>
#include <logos/node/utility.hpp>
#include <logos/node/working.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <ed25519-donna/ed25519.h>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path logos::working_path ()
{
	auto result (logos::app_path ());
	switch (logos::logos_network)
	{
		case logos::logos_networks::logos_test_network:
			result /= "LogosTest";
			break;
		case logos::logos_networks::logos_beta_network:
			result /= "LogosBeta";
			break;
		case logos::logos_networks::logos_live_network:
			result /= "Logos";
			break;
	}
	return result;
}

boost::filesystem::path logos::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

std::vector<boost::filesystem::path> logos::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
	return all_unique_paths;
}

logos::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs)
{
	boost::system::error_code error;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error);
		if (!error)
		{
			auto status1 (mdb_env_create (&environment));
			assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, 1ULL * 1024 * 1024 * 1024 * 1024)); // 1 Terabyte
			assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR | MDB_NOTLS, 00600));
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

logos::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

logos::mdb_env::operator MDB_env * () const
{
	return environment;
}

logos::mdb_val::mdb_val () :
value ({ 0, nullptr })
{
}

logos::mdb_val::mdb_val (MDB_val const & value_a) :
value (value_a)
{
}

logos::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

logos::mdb_val::mdb_val (logos::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<logos::uint128_union *> (&val_a))
{
}

logos::mdb_val::mdb_val (logos::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<logos::uint256_union *> (&val_a))
{
}

void * logos::mdb_val::data () const
{
	return value.mv_data;
}

size_t logos::mdb_val::size () const
{
	return value.mv_size;
}

logos::uint256_union logos::mdb_val::uint256 () const
{
	logos::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

logos::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
};

logos::mdb_val::operator MDB_val const & () const
{
	return value;
}

logos::transaction::transaction (logos::mdb_env & environment_a, MDB_txn * parent_a, bool write) :
environment (environment_a)
{
	auto status (mdb_txn_begin (environment_a, parent_a, write ? 0 : MDB_RDONLY, &handle));
	assert (status == 0);
}

logos::transaction::~transaction ()
{
	auto status (mdb_txn_commit (handle));
	assert (status == 0);
}

logos::transaction::operator MDB_txn * () const
{
	return handle;
}

void logos::open_or_create (std::fstream & stream_a, std::string const & path_a)
{
	stream_a.open (path_a, std::ios_base::in);
	if (stream_a.fail ())
	{
		stream_a.open (path_a, std::ios_base::out);
	}
	stream_a.close ();
	stream_a.open (path_a, std::ios_base::in | std::ios_base::out);
}
