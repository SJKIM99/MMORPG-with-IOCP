#pragma once

#include "GameSession.h"

class GameObject : public GameSession, public enable_shared_from_this<GameObject>
{
public:
	using Ptr = shared_ptr<GameObject>;
	using WeakPtr = weak_ptr<GameObject>;
	using UpdateClock = chrono::steady_clock;
	using UpdateTimePoint = UpdateClock::time_point;

public:
	GameObject() = default;
	virtual ~GameObject() = default;

	virtual void InitInstance();
	virtual void OnUpdate(const UpdateTimePoint& updateTime);

	[[nodiscard]] Ptr GetParent() const noexcept;

	template<typename TObject>
	requires derived_from<TObject, GameObject>
	[[nodiscard]] shared_ptr<TObject> GetParent() const noexcept
	{
		return dynamic_pointer_cast<TObject>(_parent.lock());
	}

	void SetParent(const Ptr& parent) noexcept;
	void ResetParent() noexcept;

	[[nodiscard]] bool HasParent() const noexcept;
	[[nodiscard]] bool IsInitialized() const noexcept { return _initialized.load(); }

	template<typename TObject = GameObject>
	requires derived_from<TObject, GameObject>
	[[nodiscard]] shared_ptr<TObject> SharedFromThis()
	{
		return dynamic_pointer_cast<TObject>(weak_from_this().lock());
	}

	template<typename TObject = const GameObject>
	requires derived_from<remove_cvref_t<TObject>, GameObject>
	[[nodiscard]] shared_ptr<TObject> SharedFromThis() const
	{
		return dynamic_pointer_cast<TObject>(weak_from_this().lock());
	}

private:
	WeakPtr _parent;
	Atomic<bool> _initialized = false;
};
