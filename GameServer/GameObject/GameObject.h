#pragma once

#include "ObjID.h"
#include "GameSession.h"

class GameObject : public ObjID, public enable_shared_from_this<GameObject>
{
	weak_ptr<GameObject> m_parent;
	ObjID m_AccountID;
	ObjID m_OwnerID;

public:
	using SharedPtr = shared_ptr<GameObject>;
	using WeakPtr = weak_ptr<GameObject>;

public:
	GameObject() = default;
	explicit GameObject(const ObjID& objID);
	explicit GameObject(const ObjID& objID, const ObjID& ownerID);

	virtual ~GameObject() = default;

	[[nodiscard]] ObjID& GetAccountID()       noexcept { return m_AccountID; }
	[[nodiscard]] const ObjID& GetAccountID() const noexcept { return m_AccountID; }
	[[nodiscard]] ObjID& GetOwnerID()         noexcept { return m_OwnerID; }
	[[nodiscard]] const ObjID& GetOwnerID()   const noexcept { return m_OwnerID; }
	
	void    SetAccountID(const decltype(m_AccountID)& o) noexcept { m_AccountID = o; }
	void    SetOwnerID(const decltype(m_OwnerID)& o) noexcept { m_OwnerID = o; }

	template<typename TObject> requires derived_from<TObject, GameObject>
	[[nodiscard]] shared_ptr<TObject> GetParent() const noexcept
	{
		return dynamic_pointer_cast<TObject>(m_parent.lock());
	}

	void SetParent(const SharedPtr& parent) noexcept;

	template<typename TObject = GameObject> requires derived_from<TObject, GameObject>
	[[nodiscard]] shared_ptr<TObject> self()
	{
		return dynamic_pointer_cast<TObject>(shared_from_this());
	}

	virtual void InitInstance();
	virtual bool OnUpdate();
};
