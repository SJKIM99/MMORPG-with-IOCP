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

	return item;
}
