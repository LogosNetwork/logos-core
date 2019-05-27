#pragma once

#include <queue>

#include <logos/consensus/messages/messages.hpp>

class BlockWriteQueue
{
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

private:
    std::queue<EBPtr>   ebs;
    std::queue<MBPtr>   mbs;
    std::queue<RBPtr>   rbs;
};
