#pragma once

#include "ItemTableRow.h"

// 서버가 아는 모든 아이템의 정적 데이터 테이블 — 유일한 조회 지점.
// 실제 행(row) 목록은 ItemTable.cpp 안에 숨겨져 있다 — 이 헤더는 조회 함수만
// 내보내서, 다른 파일이 원본 배열을 직접 순회/수정하지 못하게 막는다.
// (배열을 직접 노출하면 언젠가 누군가 정렬을 바꾸거나 항목을 지워서
// id 매핑이 깨질 수 있다.)
namespace ItemTable
{
	// 존재하지 않는 id면 nullptr를 반환한다.
	// 클라이언트가 패킷으로 보낸 id는 반드시 이 함수를 거쳐 검증한 뒤에만 사용한다.
	[[nodiscard]] const ItemTableRow* Find(ItemTableId id) noexcept;
}
