#pragma once

struct ContentID
{
protected:
    union
    {
        struct
        {
            uint16_t  m_Category;
            uint16_t  m_KIND;
            uint32_t  m_Serial;
        };
        uint64_t  m_ContentID;
    };

public:
    constexpr ContentID() noexcept
        : m_Category(0), m_KIND(0), m_Serial(0)
    {
    }

    constexpr ContentID(const decltype(m_ContentID)& initContentID) noexcept
        : m_ContentID(initContentID)
    {
    }

    template <typename T, typename U>
    constexpr ContentID(const T& initCategory, const U& initKind, const decltype(m_Serial)& initSerial) noexcept
        : m_Category(static_cast<uint16_t>(initCategory))
        , m_KIND(static_cast<uint16_t>(initKind))
        , m_Serial(initSerial)
    {
    }

    template <typename T, typename U> requires(std::is_enum_v<T>&& std::is_enum_v<U>)
        constexpr ContentID(const T& initCategory, const U& initKind) noexcept
        : m_Category(static_cast<uint16_t>(initCategory))
        , m_KIND(static_cast<uint16_t>(initKind))
        , m_Serial(0)
    {
    }

    template <typename _Ty> requires(std::is_enum_v<_Ty>)
        constexpr ContentID(const _Ty& initCategory, const decltype(m_Serial)& initSerial) noexcept
        : m_Category(static_cast<uint16_t>(initCategory))
        , m_KIND(0)
        , m_Serial(initSerial)
    {
    }

    template <typename _Ty>
        requires std::is_enum_v<_Ty>
    constexpr ContentID(const _Ty& initCategory) noexcept
        : m_Category(static_cast<uint16_t>(initCategory))
        , m_KIND(0)
        , m_Serial(0)
    {
    }

    ////////////////////////////////////////////////////////////
    // getters
    ////////////////////////////////////////////////////////////

    [[nodiscard]] constexpr uint16_t GetCategory() const noexcept { return m_Category; }
    [[nodiscard]] constexpr uint16_t GetKIND() const noexcept { return m_KIND; }
    [[nodiscard]] constexpr uint32_t GetSerial() const noexcept { return m_Serial; }
    [[nodiscard]] constexpr uint64_t GetContentID() const noexcept { return m_ContentID; }

    template <typename _Ty> requires(std::is_enum_v<_Ty>)
        constexpr auto GetCategory() const noexcept
    {
        return static_cast<_Ty>(m_Category);
    }

    ////////////////////////////////////////////////////////////
        // setters
        ////////////////////////////////////////////////////////////

    template <typename _Ty> requires(std::is_enum_v<_Ty> || std::is_arithmetic_v<_Ty>)
        constexpr auto SetCategory(const _Ty& initCategory) noexcept
    {
        if constexpr (std::is_enum_v<_Ty>)
            m_Category = static_cast<uint16_t>(initCategory);
        else if constexpr (std::is_arithmetic_v<_Ty>)
            m_Category = static_cast<uint16_t>(initCategory);
    }

    template <typename _Ty> requires(std::is_enum_v<_Ty>)
        constexpr auto GetKIND() const noexcept
    {
        return static_cast<_Ty>(m_KIND);
    }

    template <typename _Ty> requires(std::is_enum_v<_Ty> || std::is_arithmetic_v<_Ty>)
        constexpr auto SetKIND(const _Ty& initKIND) noexcept
    {
        if constexpr (std::is_enum_v<_Ty>)
            m_KIND = static_cast<uint16_t>(initKIND);
        else if constexpr (std::is_arithmetic_v<_Ty>)
            m_KIND = static_cast<uint16_t>(initKIND);
    }

    constexpr void SetSerial(const decltype(m_Serial)& o) noexcept
    {
        m_Serial = o;
    }

    constexpr void SetContentID(const decltype(m_ContentID)& initContentID) noexcept
    {
        m_ContentID = initContentID;
    }

    template <typename T, typename U>
    constexpr auto SetContentID(const T& initCategory, const U& initKind, const decltype(m_Serial)& initSerial) noexcept
    {
        SetCategory(initCategory);
        SetKIND(initKind);
        SetSerial(initSerial);
    }

    ////////////////////////////////////////////////////////////
    // operators
    ////////////////////////////////////////////////////////////

    constexpr operator uint64_t() const noexcept { return m_ContentID; }
    constexpr bool operator==(const ContentID& o) const noexcept { return (m_ContentID == o.m_ContentID); }
    constexpr bool operator!=(const ContentID& o) const noexcept { return !(*this == o); }
    constexpr bool operator<(const ContentID& o) const noexcept { return (m_ContentID < o.m_ContentID); }

    ////////////////////////////////////////////////////////////

    static ContentID npos;
};

namespace std
{
    template <>
    struct hash<ContentID>
    {
        size_t operator()(const ContentID& v) const noexcept
        {
            return std::hash<uint64_t>()(v.GetContentID());
        }
    };
}
