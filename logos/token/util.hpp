#pragma once

#include <logos/token/common.hpp>

#include <string>

std::string TokenSettingName(TokenSetting setting);

TokenSetting GetTokenSetting(bool & error, std::string data);
std::string GetTokenSettingField(size_t pos);
std::string GetTokenSettingField(TokenSetting setting);

ControllerPrivilege GetControllerPrivilege(bool & error, std::string data);
std::string GetControllerPrivilegeField(size_t pos);
std::string GetControllerPrivilegeField(ControllerPrivilege setting);

TokenFeeType GetTokenFeeType(bool & error, std::string data);
std::string GetTokenFeeTypeField(TokenFeeType fee_type);

ControllerAction GetControllerAction(bool & error, std::string data);
std::string GetControllerActionField(ControllerAction action);

FreezeAction GetFreezeAction(bool & error, std::string data);
std::string GetFreezeActionField(FreezeAction action);