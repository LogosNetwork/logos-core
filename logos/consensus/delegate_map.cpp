#include <logos/consensus/delegate_map.hpp>

std::shared_ptr<DelegateMap> DelegateMap::instance = nullptr;
std::mutex DelegateMap::mutex;
