#pragma once

#include "Item.h"

class User;

// 필드(바닥)에 떨어진 아이템의 생성/습득/소멸을 다룬다. 별도의 "필드 아이템"
// 클래스는 없다 — Item 인스턴스가 GameObjectManager 등록 여부만 바뀌며 인벤토리와
// 필드를 오간다(Item.h 상단 주석 참고).
namespace ItemHelper
{
	// item(Inventory::TryExtractItem 등으로 이미 어느 Inventory에도 속하지 않게 된
	// 상태여야 함, 즉 IsOnGround()==true)을 (x, y) 위치에 배치한다. GameObjectManager
	// 등록, Sector 배치, 주변 알림, 30초 뒤 자동 소멸 예약까지 전부 이 함수가 한다.
	// 반드시 이 item을 소유할(=그 Zone의) 스레드 위에서 호출해야 한다 — 호출자가
	// 이미 자기 Zone 스레드 위에서 실행 중이라는 전제(Route::Dispatch 경유)로 설계됨.
	void SpawnFieldItem(const Item::SharedPtr& item, short x, short y);

	// player가 지금 서 있는 칸(GetX()/GetY())에 필드 아이템이 있으면 주워서
	// player의 인벤토리에 넣는다. 결과(성공/실패/인벤토리 꽉 참)는 이 함수 안에서
	// 전부 패킷으로 통지하므로 반환값은 없다.
	//
	// 중요(동시 습득 방지): 이 함수는 "칸에 있는 아이템을 찾는다 -> 인벤토리에
	// 넣어본다 -> 성공했을 때만 필드에서 지운다" 순서로, 도중에 다른 스레드가
	// 끼어들 수 있는 지점(await/enqueue 등)이 전혀 없다. 같은 칸에 두 플레이어가
	// 서 있어도 그 둘은 반드시 같은 Zone(=같은 스레드)에 속하므로, 두 번째
	// 요청은 첫 번째 요청이 완전히 끝난 뒤에야 처리된다 — 그래서 같은 아이템을
	// 두 명이 동시에 집는 상황 자체가 발생할 수 없다(복사 불가능).
	void TryPickupAt(const std::shared_ptr<User>& player);

	// TimerThread가 30초 뒤 호출하는 소멸 콜백. 그 사이에 이미 주워졌다면(더
	// 이상 GameObjectManager에 없다면) 조용히 아무 것도 하지 않는다.
	void HandleDespawn(const ObjID& itemId);
}
