#pragma once

namespace request
{

namespace fields
{

// Request types
//
const char SEND[]               = "send";
const char CHANGE[]             = "change";
const char ISSUANCE[]           = "issuance";
const char ISSUE_ADDITIONAL[]   = "issue_additional";
const char CHANGE_SETTING[]     = "change_setting";
const char IMMUTE_SETTING[]     = "immute_setting";
const char REVOKE[]             = "revoke";
const char ADJUST_USER_STATUS[] = "adjust_user_status";
const char ADJUST_FEE[]         = "adjust_fee";
const char UPDATE_ISSUER_INFO[] = "update_issuer_info";
const char UPDATE_CONTROLLER[]  = "update_controller";
const char BURN[]               = "burn";
const char DISTRIBUTE[]         = "distribute";
const char WITHDRAW_FEE[]       = "withdraw_fee";
const char WITHDRAW_LOGOS[]     = "withdraw_logos";
const char TOKEN_SEND[]         = "token_send";
const char ANNOUNCE_CANDIDACY[] = "announce_candidacy";
const char RENOUNCE_CANDIDACY[] = "renounce_candidacy";
const char ELECTION_VOTE[]      = "election_vote";
const char START_REPRESENTING[] = "start_representing";
const char STOP_REPRESENTING[]  = "stop_representing";
const char UNKNOWN[]            = "unknown";

// Request fields
//
const char HASH[]            = "hash";
const char TYPE[]            = "type";
const char ORIGIN[]          = "origin";
const char SIGNATURE[]       = "signature";
const char PREVIOUS[]        = "previous";
const char NEXT[]            = "next";
const char FEE[]             = "fee";
const char SEQUENCE[]        = "sequence";
const char TOKEN_ID[]        = "token_id";
const char ACCOUNT[]         = "account";
const char PRIVILEGES[]      = "privileges";
const char SOURCE[]          = "source";
const char TRANSACTION[]     = "transaction";
const char DESTINATION[]     = "destination";
const char AMOUNT[]          = "amount";
const char SYMBOL[]          = "symbol";
const char NAME[]            = "name";
const char TOTAL_SUPPLY[]    = "total_supply";
const char SETTINGS[]        = "settings";
const char CONTROLLERS[]     = "controllers";
const char ISSUER_INFO[]     = "issuer_info";
const char NEW_INFO[]        = "new_info";
const char SETTING[]         = "setting";
const char VALUE[]           = "value";
const char ACTION[]          = "action";
const char FEE_TYPE[]        = "fee_type";
const char FEE_RATE[]        = "fee_rate";
const char CONTROLLER[]      = "controller";
const char TRANSACTIONS[]    = "transactions";
const char CLIENT[]          = "client";
const char REPRESENTATIVE[]  = "representative";
const char TOKEN_FEE[]       = "token_fee";
const char PRIVATE_KEY[]     = "private_key";
const char PUBLIC_KEY[]      = "public_key";
const char WALLET[]          = "wallet";
const char FROZEN[]          = "frozen";
const char UNFROZEN[]        = "unfrozen";
const char WHITELISTED[]     = "whitelisted";
const char NOT_WHITELISTED[] = "not_whitelisted";
const char STATUS[]          = "status";
const char WORK[]            = "work";
const char VOTES[]           = "votes";
const char REQUEST[]         = "request";
const char STAKE[]           = "stake";
const char EPOCH_NUM[]       = "epoch_num";
const char BLS_KEY[]         = "bls_key"; 

// Token setting fields
//
const char MODIFY_ISSUANCE[]   = "modify_issuance";
const char MODIFY_REVOKE[]     = "modify_revoke";
const char MODIFY_FREEZE[]     = "modify_freeze";
const char MODIFY_ADJUST_FEE[] = "modify_adjust_fee";
const char WHITELIST[]         = "whitelist";
const char MODIFY_WHITELIST[]  = "modify_whitelist";

// Token Fee type fields
//
const char PERCENTAGE[] = "percentage";
const char FLAT[]       = "flat";

// Controller action fields
//
const char ADD[]    = "add";
const char REMOVE[] = "remove";

// Controller privilege fields
//
const char CHANGE_ISSUANCE[]          = "change_issuance";
const char CHANGE_MODIFY_ISSUANCE[]   = "change_modify_issuance";
const char CHANGE_REVOKE[]            = "change_revoke";
const char CHANGE_MODIFY_REVOKE[]     = "change_modify_revoke";
const char CHANGE_FREEZE[]            = "change_freeze";
const char CHANGE_MODIFY_FREEZE[]     = "change_modify_freeze";
const char CHANGE_ADJUST_FEE[]        = "change_adjust_fee";
const char CHANGE_MODIFY_ADJUST_FEE[] = "change_modify_adjust_fee";
const char CHANGE_WHITELIST[]         = "change_whitelist";
const char CHANGE_MODIFY_WHITELIST[]  = "change_modify_whitelist";
const char FREEZE[]                   = "freeze";

} // namespace fields

} // namespace request
