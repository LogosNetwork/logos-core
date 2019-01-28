#pragma once

namespace request
{

namespace fields
{

// Request types
//
const char SEND[]              = "send";
const char CHANGE[]            = "change";
const char ISSUE_TOKENS[]      = "issue";
const char ISSUE_ADTL[]        = "issue_additional";
const char IMMUTE[]            = "immute";
const char REVOKE[]            = "revoke";
const char FREEZE[]            = "freeze";
const char SET_FEE[]           = "set_fee";
const char UPDATE_WHITELIST[]  = "update_whitelist";
const char UPDATE_INFO[]       = "update_issuer_info";
const char UPDATE_CONTROLLER[] = "update_controller";
const char BURN[]              = "burn";
const char DISTRIBUTE[]        = "distribute";
const char WITHDRAW[]          = "withdraw";
const char SEND_TOKENS[]       = "send_tokens";
const char ANNOUNCE_CANDIDACY[]  = "announce_candidacy";
const char RENOUNCE_CANDIDACY[]  = "renounce_candidacy";
const char ELECTION_VOTE[]       = "election_vote";
const char UNKNOWN[]           = "unknown";

// Request fields
//
const char TYPE[]           = "type";
const char PREVIOUS[]       = "previous";
const char NEXT[]           = "next";
const char TOKEN_ID[]       = "token_id";
const char ADMIN_ACCOUNT[]  = "admin_account";
const char ACCOUNT[]        = "account";
const char PRIVILEGES[]     = "privileges";
const char SOURCE[]         = "source";
const char DESTINATION[]    = "destination";
const char AMOUNT[]         = "amount";
const char SYMBOL[]         = "symbol";
const char NAME[]           = "name";
const char TOTAL_SUPPLY[]   = "total_supply";
const char SETTINGS[]       = "settings";
const char CONTROLLERS[]    = "controllers";
const char INFO[]           = "issuer_info";
const char NEW_INFO[]       = "new_info";
const char SETTING[]        = "setting";
const char VALUE[]          = "value";
const char ACTION[]         = "action";
const char FEE_TYPE[]       = "fee_type";
const char FEE_RATE[]       = "fee_rate";
const char CONTROLLER[]     = "controller";
const char TRANSACTIONS[]   = "transactions";
const char FEE[]            = "fee";
const char CLIENT[]         = "client";
const char REPRESENTATIVE[] = "representative";

// Token setting fields
//
const char ADD[]               = "add";
const char MODIFY_ADD[]        = "modify_add";
const char MODIFY_REVOKE[]     = "modify_revoke";
const char MODIFY_FREEZE[]     = "modify_freeze";
const char ADJUST_FEE[]        = "adjust_fee";
const char MODIFY_ADJUST_FEE[] = "modify_adjust_fee";
const char WHITELIST[]         = "whitelist";
const char MODIFY_WHITELIST[]  = "modify_whitelist";

// Token Fee type fields
//
const char PERCENTAGE[] = "percentage";
const char FLAT[]       = "flat";

// Controller action fields
//
const char REMOVE[] = "remove";

// Freeze action fields
//
const char UNFREEZE[] = "unfreeze";

// Controller privilege fields
//
const char CHANGE_ADD[]               = "change_add";
const char CHANGE_MODIFY_ADD[]        = "change_modify_add";
const char CHANGE_REVOKE[]            = "change_revoke";
const char CHANGE_MODIFY_REVOKE[]     = "change_modify_revoke";
const char CHANGE_FREEZE[]            = "change_freeze";
const char CHANGE_MODIFY_FREEZE[]     = "change_modify_freeze";
const char CHANGE_ADJUST_FEE[]        = "change_adjust_fee";
const char CHANGE_MODIFY_ADJUST_FEE[] = "change_modify_adjust_fee";
const char CHANGE_WHITELIST[]         = "change_whitelist";
const char CHANGE_MODIFY_WHITELIST[]  = "change_modify_whitelisting";
const char PROMOTE_CONTROLLER[]       = "promote_controller";
const char WITHDRAW_FEE[]             = "withdraw_fee";

} // namespace fields

} // namespace request

