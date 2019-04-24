//
// Created by erangi on 7/4/17.
//

#ifndef KEYDOMET_KEYDOMET_H
#define KEYDOMET_KEYDOMET_H

#include <iostream>
#include <string>
#include <cstring>
#include <experimental/string_view>

//TODO optimize keydomet generation for SSO
//TODO consider implications of storage type when char type is signed vs. unsigned
//TODO make keydomet usage stats optional
//TODO measure memory footprint in term of cache lines on various benchmarks.
//     mostly, eval the effect of going from a 32B string object to 40B with keydomet, ruining cache line packing.
//TODO consider placing the keydomet in the SSO buffer when the string is long (and not using keydomet otherwise).
//     since keydomet isn't much better than SSO, the memory overhead reduction could outweigh not using keydomet on
//     short strings. should probably be a compile time optimization choice.


//typedef KeyDometStr<std::experimental::string_view, kdmt::KeyDometSize::SIZE_32BIT> kdmtStr;

namespace kdmt
{
    using std::experimental::string_view;

    enum class KeyDometSize : uint8_t
    {
        SIZE_16BIT  = 2,
        SIZE_32BIT  = 4,
        SIZE_64BIT  = 8,
        SIZE_128BIT = 16
    };

    template<KeyDometSize>
    struct KeyDometStorage; // Unexpected KeyDometSize value!

    template<> struct KeyDometStorage<KeyDometSize::SIZE_16BIT>  { using type = uint16_t; };
    template<> struct KeyDometStorage<KeyDometSize::SIZE_32BIT>  { using type = uint32_t; };
    template<> struct KeyDometStorage<KeyDometSize::SIZE_64BIT>  { using type = uint64_t; };

    struct kdmt128_t
    {
        using HalfType = typename KeyDometStorage<KeyDometSize::SIZE_64BIT>::type;
        HalfType msbs;
        HalfType lsbs;

        kdmt128_t() = default;
        constexpr explicit kdmt128_t(HalfType l) : msbs{0}, lsbs{l} {}
        constexpr kdmt128_t(HalfType m, HalfType l) : msbs{m}, lsbs{l} {}
        constexpr kdmt128_t operator&(const kdmt128_t& other) const
        {
            return {(msbs & other.msbs), (lsbs & other.lsbs)};
        }
        constexpr bool operator<(const kdmt128_t& other) const
        {
            return (msbs < other.msbs) || (msbs == other.msbs && lsbs < other.lsbs);
        }
        constexpr bool operator==(const kdmt128_t& other) const
        {
            return (msbs == other.msbs) && (lsbs == other.lsbs);
        }
        constexpr bool operator!=(const kdmt128_t& other) const
        {
            return (msbs != other.msbs) || (lsbs != other.lsbs);
        }
    };
    template<> struct KeyDometStorage<KeyDometSize::SIZE_128BIT> { using type = kdmt128_t; };

    //
    // Helper functions to provide access to the raw c-string array.
    // New string types should add an overload, which uses that type's API.
    //
    //const char* getRawStr(const char* str) { return str; }
    //const char* getRawStr(const std::string& str) { return str.data(); }
    //const char* getRawStr(const string_view& str) { return str.data(); }

    //
    // Helper function for swapping bytes, turning the big endian order in the string into
    // a proper little endian number. For now, only GCC is supported due to the use of intrinsics.
    //
    //void flipBytes(uint16_t& val) { val = __builtin_bswap16(val); }
    //void flipBytes(uint32_t& val) { val = __builtin_bswap32(val); }
    //void flipBytes(uint64_t& val) { val = __builtin_bswap64(val); }
    //void flipBytes(kdmt128_t& val) {
    //    val.lsbs = __builtin_bswap64(val.lsbs);
    //    val.msbs = __builtin_bswap64(val.msbs);
    //}

    //
    // Helper function that turns the string's keydomet into a number.
    // TODO - Can be optimized, e.g., by using a cast when the string's buffer is known to be large enough
    // (namely, SSO is used; zero padding still required based on actual string size).
    //
    template<typename KeyDometT, typename StrImp>
    KeyDometT strToPrefix(const StrImp& str)
    {
        const char* cstr = str.data();//getRawStr(str);
        KeyDometT trg;
        strncpy((char*)&trg, cstr, sizeof(trg)); // short strings are padded with zeros
        //flipBytes(trg);
	trg = __builtin_bswap64(trg);
        return trg;
    }

    template<KeyDometSize SIZE>
    class KeyDomet
    {

    public:

        using KeyDometType = typename KeyDometStorage<SIZE>::type;

        template<typename StrImp>
        explicit KeyDomet(const StrImp& str) : val{strToPrefix<KeyDometType>(str)}
        {
        }

        KeyDomet(const KeyDomet& other) : val(other.val)
        {}

        KeyDomet(KeyDomet&&) noexcept = default;

        KeyDomet& operator=(KeyDomet&&) noexcept = default;
        KeyDomet& operator=(const KeyDomet&) noexcept = default;

        KeyDometType getVal() const
        {
            return val;
        }

        bool operator<(const KeyDomet<SIZE>& other) const
        {
            return this->val < other.val;
        }

        bool operator==(const KeyDomet<SIZE>& other) const
        {
            return this->val == other.val;
        }

        bool operator!=(const KeyDomet<SIZE>& other) const
        {
            return this->val != other.val;
        }

        bool stringShorterThanKeydomet() const
        {
            // if the last byte/char of the keydomet is all zeros, the string must
            // have been shorter than the keydomet's capacity.
            // note: this won't always be correct when working with Unicode strings!
            constexpr KeyDometType LastByteMask{0xFFUL};
            return (val & LastByteMask) == KeyDometType{0UL};
        }

    private:

        KeyDometType val; // non-const to allow move assignment (useful in vector resizing and algorithms)

    };

    //size_t usedPrefix = 0;
    //size_t usedStr = 0;

    template<class StrImp_, KeyDometSize Size_>
    class KeyDometStr
    {

    public:

        using StrImp = StrImp_;
        static constexpr KeyDometSize Size = Size_;

        KeyDometStr(const StrImp_& other = "") : prefix{other}, str{other} {}

        KeyDometStr(const KeyDometStr& other) :
                prefix{other.prefix}, str{other.str}
        {}

        KeyDometStr(KeyDometStr&&) noexcept = default;

        KeyDometStr& operator=(KeyDometStr&&) = default;
        KeyDometStr& operator=(const KeyDometStr&) = default;

        template<typename OtherImp, KeyDometSize OtherSize>
        friend class KeyDometStr;

        template<typename Imp>
        int compare(const KeyDometStr<Imp, Size_>& other) const
        {
            if (this->prefix != other.prefix)
            {
                //++usedPrefix;
                return diffAsOneOrMinusOne(this->prefix, other.prefix);
            }
            else
            {
                if (this->prefix.stringShorterThanKeydomet())
                {
                    //++usedPrefix;
                    return 0;
                }
                //++usedStr;
                return strcmp(str.data() + sizeof(Size_), other.str.data() + sizeof(Size_));
            }
        }

        template<typename Imp>
        bool operator<(const KeyDometStr<Imp, Size_>& other) const
        {
            return compare(other) < 0;
        }

        template<typename Imp>
        bool operator<=(const KeyDometStr<Imp, Size_>& other) const
        {
            return compare(other) <= 0;
        }

        template<typename Imp>
        bool operator==(const KeyDometStr<Imp, Size_>& other) const
        {
            return compare(other) == 0;
        }

        const KeyDomet<Size_>& getPrefix() const
        {
            return prefix;
        }

        const string getStr() const
        {
            return str.to_string();
        }
	

	bool empty() const {
		return str.empty();
	}
    private:

        // using composition instead of inheritance (from StrImp) to make sure
        // the keydomet is at the beginning of the object rather than at the end
        KeyDomet<Size_> prefix;
        StrImp_ str;

        static int diffAsOneOrMinusOne(const KeyDomet<Size_>& v1, const KeyDomet<Size_>& v2)
        {
            // return -1 if v1 < v2 and 1 otherwise
            // this is done without conditional branches and without risking wrap-around
            return ((int)!(v1 < v2) << 1) - 1;
        }

    };
/*
    std::ostream& operator<<(std::ostream& os, const kdmt128_t& val)
    {
        os << val.msbs << val.lsbs;
        return os;
    }

    template<typename StrImp, KeyDometSize Size>
    std::ostream& operator<<(std::ostream& os, const KeyDometStr<StrImp, Size>& hk)
    {
        const StrImp& str = hk.getStr();
        os << str;
        return os;
    }

    //
    // Hasher allowing the use of the keydomet as a (poorly distributed) hash value
    //
    struct KeyDometHasher
    {
        template<typename StrImp, KeyDometSize Size>
        size_t operator()(const KeyDometStr<StrImp, Size>& k) const
        {
            return k.getPrefix().val;
        }
    };

    //
    // A comparator allowing bucket lookups in a hash table
    //
    struct KeyDometComparator
    {
        template<typename StrImp, KeyDometSize Size>
        bool operator()(const KeyDometStr<StrImp, Size>& lhs, const KeyDometStr<StrImp, Size>& rhs) const
        {
            return lhs == rhs;
        }
    };

    namespace imp
    {
        // Searching associative containers (namely, sorted rather than hashed) could only be done using
        // the exact key type till C++14. From C++14, any type that can be compared with the key type can
        // be used. For instance, a std::string_view can be used in order to find a std::string key.
        // this is very useful when a key is given as a std::string but a KeyDomet should be constructed
        // in order to do the lookup: instead of creating a KeyDomet that will own a copy of the std::string
        // (with all the mess involved with the creation of the copy - allocation etc.), a KeyDometView is
        // used, merely referencing the original std::string.
        // To use the above feature, the comparator used by the container must be transparent, namely have
        // a is_transparent member. The code below detects the presence of that member, and the result is
        // used to determine whether a KeyDomet or KeyDometView should be constructed for lookups.
        template<typename...>
        using VoidT = void;

        struct NonTransparent {};

        auto getComparator(...) -> NonTransparent;

        template<template<class, class...> class Container, class KD, class... Args>
        auto getComparator(const Container<KD, Args...>&) -> typename Container<KD, Args...>::key_compare;

        template<class Comparator, class = VoidT<>>
        struct HasTransparentFlagHelper : std::false_type {};

        template<class Comparator>
        struct HasTransparentFlagHelper<Comparator, VoidT<typename Comparator::is_transparent>> : std::true_type {};

        template<template<class, class...> class Container, class KD, class... Args>
        auto isTransparent(const Container<KD, Args...>& c) ->
        std::enable_if_t<HasTransparentFlagHelper<decltype(getComparator(c))>::value, std::true_type>;

        auto isTransparent(...) -> std::false_type;

        template<template<class, class...> class Container, KeyDometSize Size, class... Args>
        auto getFindKeyType(const Container<KeyDometStr<std::string, Size>, Args...>& s) ->
        // if container's comparator is transparent, use string_view-based key
        // else, use a full-blown string-based key
        std::conditional_t<
                decltype(isTransparent(s))::value,
                KeyDometStr<string_view, Size>,
                KeyDometStr<std::string, Size>
        >;
        //TODO make the above more generic - basic string type, view type and conversion function or so.
        // make string -> string_view the default and example implementation.
    }

    template<template<class, class...> class Container, class KD, class... Args>
    auto makeFindKey(const Container<KD, Args...>& s, const std::string& key)
    {
        // associative containers (maps, sets) can use a transparent comparator. such a comperator can
        // compare the internal key type with other types (as long as there's an appropriate operator).
        // this allows such containers to hold strings but search using string_views, saving the allocation.
        // *** Note: to make a container associative, define it with the less<> comparator:
        // *** using KeydometSet = std::set<KdmtStr, std::less<>>;
        using KeyDometStrType = std::remove_const_t<decltype(imp::getFindKeyType(s))>;
        return KeyDometStrType{key};
    }
*/
}


typedef kdmt::KeyDometStr<std::experimental::string_view, kdmt::KeyDometSize::SIZE_64BIT> kdmtStr;

#endif //KEYDOMET_KEYDOMET_H
