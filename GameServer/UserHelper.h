#pragma once

#include "Core\Types.h"

class Subject;

namespace UserHelper
{
	void SendMovePacket(Subject& receiver, uint32 clientId);
	void SendAddPlayerPacket(Subject& receiver, uint32 clientId);
	void SendRemovePlayerPacket(Subject& receiver, uint32 clientId);
	void SendLoginSuccessPacket(Subject& receiver);
	void SendLoginFailPacket(Subject& receiver);
	void SendPlayerAttackToNpcPacket(Subject& receiver, uint32 clientId);
	void SendNpcDiePacket(Subject& receiver, uint32 clientId);
	void SendRespawnNpcPacket(Subject& receiver, uint32 clientId);
	void SendNpcAttackToPlayerPacket(Subject& receiver);
	void SendHealPacket(Subject& receiver);
	void SendPlayerDiePacket(Subject& receiver, uint32 clientId);
	void SendRespawnPlayerPacket(Subject& receiver, uint32 clientId);
	[[nodiscard]] bool FlushPlayerSave(uint32 clientId);
}
