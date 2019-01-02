#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <type_traits>

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cryptopp/osrng.h>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <logos/config.hpp>
#include <logos/lib/interface.h>
#include <logos/lib/numbers.hpp>

#include <logos/consensus/messages/common.hpp>

namespace logos
{
using bufferstream = boost::iostreams::stream_buffer<boost::iostreams::basic_array_source<uint8_t>>;
using vectorstream = boost::iostreams::stream_buffer<boost::iostreams::back_insert_device<std::vector<uint8_t>>>;
// OS-specific way of finding a path to a home directory.
boost::filesystem::path working_path ();
// Get a unique path within the home directory, used for testing.
// Any directories created at this location will be removed when a test finishes.
boost::filesystem::path unique_path ();
// Remove all unique tmp directories created by the process. The list of unique paths are returned.
std::vector<boost::filesystem::path> remove_temporary_directories ();
// C++ stream are absolutely horrible so I need this helper function to do the most basic operation of creating a file if it doesn't exist or truncating it.
void open_or_create (std::fstream &, std::string const &);
// Reads a json object from the stream and if was changed, write the object back to the stream
template <typename T>
bool fetch_object (T & object, std::iostream & stream_a)
{
    assert (stream_a.tellg () == std::streampos (0) || stream_a.tellg () == std::streampos (-1));
    assert (stream_a.tellp () == std::streampos (0) || stream_a.tellp () == std::streampos (-1));
    bool error (false);
    boost::property_tree::ptree tree;
    try
    {
        boost::property_tree::read_json (stream_a, tree);
    }
    catch (std::runtime_error const &)
    {
        auto pos (stream_a.tellg ());
        if (pos != std::streampos (0))
        {
            error = true;
        }
    }
    if (!error)
    {
        auto updated (false);
        error = object.deserialize_json (updated, tree);
    }
    return error;
}
// Reads a json object from the stream and if was changed, write the object back to the stream
template <typename T>
bool fetch_object (T & object, boost::filesystem::path const & path_a, std::fstream & stream_a)
{
    bool error (false);
    logos::open_or_create (stream_a, path_a.string ());
    if (!stream_a.fail ())
    {
        boost::property_tree::ptree tree;
        try
        {
            boost::property_tree::read_json (stream_a, tree);
        }
        catch (std::runtime_error const &)
        {
            auto pos (stream_a.tellg ());
            if (pos != std::streampos (0))
            {
                error = true;
            }
        }
        if (!error)
        {
            auto updated (false);
            error = object.deserialize_json (updated, tree);
            if (!error && updated)
            {
                stream_a.close ();
                stream_a.open (path_a.string (), std::ios_base::out | std::ios_base::trunc);
                try
                {
                    boost::property_tree::write_json (stream_a, tree);
                }
                catch (std::runtime_error const &)
                {
                    error = true;
                }
            }
        }
    }
    return error;
}

/**
 * RAII wrapper for MDB_env
 */
class mdb_env
{
public:
    mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128);
    ~mdb_env ();
    operator MDB_env * () const;
    MDB_env * environment;
};

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
    mdb_val ();
    mdb_val (MDB_val const &);
    mdb_val (size_t, void *);
    mdb_val (logos::uint128_union const &);
    // mdb_val (logos::uint256_union const &);
    mdb_val (uint8_t const & v);
    mdb_val (const BlockHash & v);
    void * data () const;
    size_t size () const;
    logos::uint256_union uint256 () const;
    operator MDB_val * () const;
    operator MDB_val const & () const;
    MDB_val value;
};

/**
 * RAII wrapper of MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class transaction
{
public:
    transaction (logos::mdb_env &, MDB_txn *, bool);
    ~transaction ();
    operator MDB_txn * () const;
    MDB_txn * handle;
    logos::mdb_env & environment;
};
}
