/// @file
/// This file contains the declaration of the MicroBlockHandler classe, which is used
/// in the Microblock processing
#pragma once
#include <logos/consensus/message_validator.hpp>
#include <logos/microblock/microblock.hpp>

static const uint16_t MICROBLOCK_PROPOSAL_TIME = 1200; // 20 minutes
static const uint16_t MICROBLOCK_CUTOFF_TIME = 600; // 10 minutes

namespace logos {
    class block_store;
    class alarm;
}

using BlockStore = logos::block_store;

/// Handle MicroBlock processing
class MicroBlockHandler : public std::enable_shared_from_this<MicroBlockHandler> {
public:
    /// Class constructor
    /// @param s logos::alarm reference
    /// @param s logos::block_store reference
    /// @param n number of delegates
    /// @param i microblock process period interval
    MicroBlockHandler(logos::alarm &a,
                      BlockStore &s,
                      uint8_t delegate_id,
                      MessageValidator & validator)
        : _alarm(a)
        , _store(s)
        , _interval_cutoff(std::chrono::seconds(MICROBLOCK_CUTOFF_TIME))
        , _interval_proposal(std::chrono::seconds(MICROBLOCK_PROPOSAL_TIME))
        , _delegate_id(delegate_id)
        , _validator(validator)
        {}

    /// Class distructor
    virtual ~MicroBlockHandler() {}

    /// Start periodic microblock processing
    /// called by node::start
    /// @param callback function that takes as the argument generated microblock
    void Start(
        std::function<void(std::shared_ptr<MicroBlock>)>
    );

    /// Build the block, called periodically by node
    /// Could be called by any delegate
    /// @param block to build [in|out]
    /// @returns true on success
    bool BuildMicroBlock(MicroBlock &block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to verify [in]
    /// @return true if verified (TBD, perhaps enum to address all possible failure scenarios
    /// 1. exists and matches; 2. doesn't exist but all data matches 3. doesn't exist and there is
    /// a different block matching the same parent. 4. doesn't exist and there is no parent that this
    /// block references, the block # is ahead of the current block #.)
    bool VerifyMicroBlock(MicroBlock &block);

private:
    logos::alarm &       _alarm;            ///< alarm
    BlockStore &         _store; 		    ///< reference to the block store
    std::chrono::seconds _interval_cutoff;  ///< microblock inclusion cutoff time
    std::chrono::seconds _interval_proposal;///< microblock proposal time
    uint8_t              _delegate_id;      ///< local delegate id
    MessageValidator &   _validator;        ///< validator used for message signing
};
