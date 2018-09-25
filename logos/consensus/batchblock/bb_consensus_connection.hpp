#pragma once

#include <logos/consensus/consensus_connection.hpp>

class RequestPromoter;

class BBConsensusConnection : public ConsensusConnection<ConsensusType::BatchStateBlock>
{

    using Connection = ConsensusConnection<ConsensusType::BatchStateBlock>;

public:

    BBConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                          PrimaryDelegate * primary,
                          RequestPromoter * promoter,
                          PersistenceManager & persistence_manager,
                          MessageValidator & validator,
                          const DelegateIdentities & ids);

    void OnPostCommit(const PrePrepare & message) override;

private:

    RequestPromoter * _promoter;
};
