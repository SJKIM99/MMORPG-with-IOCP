#include "pch.h"
#include "Subject.h"

Subject::Subject()
{
	::memset(_name, 0, sizeof(_name));
}

void Subject::InitInstance()
{
	GameObject::InitInstance();
	ResetGameplayState();
}

void Subject::OnUpdate(const UpdateTimePoint& updateTime)
{
	GameObject::OnUpdate(updateTime);
}

void Subject::ResetGameplayState()
{
	_timerEpoch.fetch_add(1);
	if (_state != SOCKET_STATE::ST_FREE)
		_state = SOCKET_STATE::ST_ALLOC;
	_transform.Reset();
	_stat.Reset();
	_lastMoveTime   = 0;
	_lastAttackTime = 0;
	_viewList.clear();
	_active.store(false);
	_attack.store(false);
}
