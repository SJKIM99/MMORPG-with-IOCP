#pragma once
#include "ContentID.h"

struct ObjID : public ContentID
{
    uint64_t	m_DatabaseID{};

public:
    ObjID() = default;
    ObjID(const ObjID&) = default;
    ObjID(const ContentID& initContentID) : ContentID(initContentID) {}
    ObjID(const decltype(m_ContentID)& initContentID, const decltype(m_DatabaseID)& initDatabaseID) : ContentID(initContentID), m_DatabaseID(initDatabaseID) {}

    template <typename _Ty> requires(std::is_enum_v<_Ty>)
        ObjID(const _Ty& initCategory, const decltype(m_DatabaseID)& initDatabaseID)
    {
        SetCategory(initCategory);
        SetDatabaseID(initDatabaseID);
    }

    [[nodiscard]] constexpr uint64_t GetDatabaseID() const noexcept { return m_DatabaseID; }
    constexpr void SetDatabaseID(const decltype(m_DatabaseID)& o) noexcept { m_DatabaseID = o; }

    [[nodiscard]] ObjID& GetObjID()        noexcept { return *this; }
    [[nodiscard]] const ObjID& GetObjID()  const noexcept { return *this; }

    auto SetObjID(const ContentID& clsContentID, const uint64_t& uDatabaseID)
    {
        SetContentID(clsContentID.GetContentID());
        SetDatabaseID(uDatabaseID);
    }
    auto SetObjID(const ObjID& o)
    {
        SetObjID(o.GetContentID(), o.GetDatabaseID());
    }

    ObjID& operator=(const ObjID&) = default;

    /////////////////////////////////////////////////////////////////////////////////////
    //  std::less<_Ty>
    constexpr bool operator<(const ObjID& o) const
    {
        if (m_ContentID != o.m_ContentID) return m_ContentID < o.m_ContentID;
        return (m_DatabaseID < o.m_DatabaseID);
    }
    constexpr bool operator==(const ObjID& o) const { return (m_ContentID == o.m_ContentID) && (m_DatabaseID == o.m_DatabaseID); }
    constexpr bool operator!=(const ObjID& o) const { return !(*this == o); }
    /////////////////////////////////////////////////////////////////////////////////////

    constexpr operator bool()
    {
        return !(*this == ObjID::npos);
    }


    static	ObjID	npos;	//	default (0,0)
};

namespace std
{
    template <typename _Ty>
    void hash_combine(size_t& seed, const _Ty& v)
    {
        std::hash<_Ty> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    template <>
    struct hash<ObjID>
    {
        size_t operator()(const ObjID& v) const
        {
            size_t	seed = 0;
            hash_combine(seed, v.GetContentID());
            hash_combine(seed, v.GetDatabaseID());
            return seed;
        }
    };
};