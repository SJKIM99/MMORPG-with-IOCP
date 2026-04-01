#include "pch.h"
#include "GameObject.h"

void GameObject::InitInstance()
{
	ResetParent();
	_initialized.store(true);
}

void GameObject::OnUpdate(const UpdateTimePoint& updateTime)
{
	(void)updateTime;
}

GameObject::Ptr GameObject::GetParent() const noexcept
{
	return _parent.lock();
}

void GameObject::SetParent(const Ptr& parent) noexcept
{
	_parent = parent;
}

void GameObject::ResetParent() noexcept
{
	_parent.reset();
}

bool GameObject::HasParent() const noexcept
{
	return _parent.expired() == false;
}
