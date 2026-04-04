#pragma once

#include "Transform.h"
#include "Stat.h"
#include "GameObject.h"

class Subject : public GameObject
{
public:
	Subject();
	virtual ~Subject() = default;

	void InitInstance() override;
	void OnUpdate(const UpdateTimePoint& updateTime) override;
	virtual void ResetGameplayState();

	[[nodiscard]] uint64 GetTimerEpoch() const { return _timerEpoch.load(); }

public:
	Transform            _transform;
	Stat                 _stat;
	char                 _name[NAME_SIZE]{};
	Atomic<uint64>       _timerEpoch    = 1;
	unordered_set<uint32> _viewList;
	uint32               _lastMoveTime   = 0;
	uint32               _lastAttackTime = 0;
	Atomic<bool>         _active         = false;
	Atomic<bool>         _attack         = false;
};
