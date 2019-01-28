#include <logos/epoch/election_requests.hpp>

AnnounceCandidacy::AnnounceCandidacy(const BlockHash & previous) 
    : Request(RequestType::AnnounceCandidacy, previous)
{}  

AnnounceCandidacy::AnnounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    //ensure type is correct
    error = type == RequestType::AnnounceCandidacy;
}

AnnounceCandidacy::AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = type == RequestType::AnnounceCandidacy;
}

RenounceCandidacy::RenounceCandidacy(const BlockHash & previous) 
    : Request(RequestType::RenounceCandidacy, previous)
{}  

RenounceCandidacy::RenounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    //ensure type is correct
    error = type == RequestType::RenounceCandidacy;
}

RenounceCandidacy::RenounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = type == RequestType::RenounceCandidacy;
}

