#include "pch.h"
#include "Item.h"

#include "ConsumableItem.h"
#include "EquipmentItem.h"
#include "ItemTable.h"

void Item::InitInstance()
{
	GameObject::InitInstance();
}

Item::SharedPtr MakeNewItem(ItemTableId itemId, uint16_t count)
{
	const ItemTableRow* row = ItemTable::Find(itemId);
	if (row == nullptr)
		return nullptr;

	Item::SharedPtr item = (row->type == ItemType::eEquipment)
		? static_pointer_cast<Item>(std::make_shared<EquipmentItem>())
		: static_pointer_cast<Item>(std::make_shared<ConsumableItem>());

	item->InitInstance();
	item->SetItemTableId(itemId);
	item->SetCount(count);
	// SUBJECT_ADD_NFY 등 일반 Subject 표시 경로가 이름을 그대로 쓸 수 있도록
	// 항상 채워둔다(인벤토리 안에 있을 때는 안 쓰이지만, 필드에 떨어지면 바로 필요해진다).
	item->SetName(row->name);

	return item;
}
