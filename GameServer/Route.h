#pragma once

class GameSession;

namespace Route
{
	void Dispatch(const shared_ptr<GameSession>& session, const char* packet);
}
