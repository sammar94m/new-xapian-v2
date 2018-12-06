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

namespace kdmt
{
//    using std::experimental::string_view;

typedef std::string string_view;


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
    inline const char* getRawStr(const char* str) { return str; }
    inline const char* getRawStr(const std::string& str) { return str.data(); }

    //
    // Helper function for swapping bytes, turning the big endian order in the string into
    // a proper little endian number. For now, only GCC is supported due to the use of intrinsics.
    //
    inline void flipBytes(uint16_t& val) { val = __builtin_bswap16(val); }
     inline void  flipBytes(uint32_t& val) { val = __builtin_bswap32(val); }
     inline void  flipBytes(uint64_t& val) { val = __builtin_bswap64(val); }
     inline void  flipBytes(kdmt128_t& val) {
        val.lsbs = __builtin_bswap64(val.lsbs);
        val.msbs = __builtin_bswap64(val.msbs);
    }

    //
    // Helper function that turns the string's keydomet into a number.
    // TODO - Can be optimized, e.g., by using a cast when the string's buffer is known to be large enough
    // (namely, SSO is used; zero padding still required based on actual string size).
    //
    template<typename KeyDometT, typename StrImp>
    KeyDometT strToPrefix(const StrImp& str)
    {
        const char* cstr = getRawStr(str);
        KeyDometT trg;
        strncpy((char*)&trg, cstr, sizeof(trg)); // short strings are padded with zeros
        flipBytes(trg);
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
        KeyDomet(KeyDomet& other) : val(other.val)
        {}

        KeyDomet(KeyDomet&&)  = default;
        KeyDomet()  = default;

        KeyDomet& operator=(KeyDomet&&)  = default;
        KeyDomet& operator=(const KeyDomet&)  = default;

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
            constexpr KeyDometType LastByteMask = 0xFF;
            return (val & LastByteMask) == 0;
        }

    private:

        KeyDometType val; // non-const to allow move assignment (useful in vector resizing and algorithms)

    };


    template<class StrImp_, KeyDometSize Size_>
    class KeyDometStr
    {

    public:

        using StrImp = StrImp_;
        static constexpr KeyDometSize Size = Size_;

        explicit KeyDometStr(const StrImp_& other) :
                prefix{other}, str{other}
        {}
        explicit KeyDometStr() :
                prefix{}, str{}
        {}

        KeyDometStr(const KeyDometStr& other) :
                prefix{other.prefix}, str{other.str}
        {}

        KeyDometStr(KeyDometStr& other) :
                prefix{other.prefix}, str{other.str}
        {}

        KeyDometStr(KeyDometStr&&)  = default;

        KeyDometStr& operator=(KeyDometStr&&)  = default;
        KeyDometStr& operator=(const KeyDometStr&)  = default;

        template<typename OtherImp, KeyDometSize OtherSize>
        friend class KeyDometStr;

        template<typename Imp>
        int compare(const KeyDometStr<Imp, Size_>& other) const
        {

        	//std::cout<<"+++++ comparing keydomet"<<std::endl;

        	if (this->prefix != other.prefix)
            {
                return diffAsOneOrMinusOne(this->prefix, other.prefix);
            }
            else
            {
                if (this->prefix.stringShorterThanKeydomet())
                {
                    return 0;
                }
                return strcmp(getRawStr(str) + sizeof(Size_), getRawStr(other.str) + sizeof(Size_));
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

        const StrImp_& getStr() const
        {
            return str;
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

    inline std::ostream& operator<<(std::ostream& os, const kdmt128_t& val)
    {
        os << val.msbs << val.lsbs;
        return os;
    }

    template<typename StrImp, KeyDometSize Size>
    inline std::ostream& operator<<(std::ostream& os, const KeyDometStr<StrImp, Size>& hk)
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

}

#endif //KEYDOMET_KEYDOMET_H
