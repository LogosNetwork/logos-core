#pragma once

#include <logos/node/node.hpp>

namespace logos
{
class system
{
public:
    system (uint16_t, size_t);
    system (uint16_t, boost::filesystem::path &data_path);
    ~system ();
    void generate_activity (logos::node &, std::vector<logos::account> &);
    void generate_mass_activity (uint32_t, logos::node &);
    void generate_usage_traffic (uint32_t, uint32_t, size_t);
    void generate_usage_traffic (uint32_t, uint32_t);
    logos::account get_random_account (std::vector<logos::account> &);
    logos::uint128_t get_random_amount (MDB_txn *, logos::node &, logos::account const &);
    void generate_rollback (logos::node &, std::vector<logos::account> &);
    void generate_change_known (logos::node &, std::vector<logos::account> &);
    void generate_change_unknown (logos::node &, std::vector<logos::account> &);
    void generate_receive (logos::node &);
    void generate_send_new (logos::node &, std::vector<logos::account> &);
    void generate_send_existing (logos::node &, std::vector<logos::account> &);
    std::shared_ptr<logos::wallet> wallet (size_t);
    logos::account account (MDB_txn *, size_t);
    void poll ();
    void stop ();
    boost::asio::io_service service;
    logos::alarm alarm;
    std::vector<std::shared_ptr<logos::node>> nodes;
    logos::logging logging;
    logos::work_pool work;
};
class landing_store
{
public:
    landing_store ();
    landing_store (logos::account const &, logos::account const &, uint64_t, uint64_t);
    landing_store (bool &, std::istream &);
    logos::account source;
    logos::account destination;
    uint64_t start;
    uint64_t last;
    bool deserialize (std::istream &);
    void serialize (std::ostream &) const;
    bool operator== (logos::landing_store const &) const;
};
class landing
{
public:
    landing (logos::node &, std::shared_ptr<logos::wallet>, logos::landing_store &, boost::filesystem::path const &);
    void write_store ();
    logos::uint128_t distribution_amount (uint64_t);
    void distribute_one ();
    void distribute_ongoing ();
    boost::filesystem::path path;
    logos::landing_store & store;
    std::shared_ptr<logos::wallet> wallet;
    logos::node & node;
    static int constexpr interval_exponent = 10;
    static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 1024 seconds
    static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
}
