#ifndef __SERIALIZABLE_H
#define __SERIALIZABLE_H

#include <map> // key - value pairs in serializable
#include <vector> // storing bytestreams as a vector
#include <utility> // pair to pair the types with the bytestreams
#include <stdint.h> // need fixed width types because we are byte counting
#include <endian.h> // useful byte swapping (can be reimplimented if need be)
#include <climits> // CHAR_BIT is defined here
#include <assert.h> // ensure we have a compatible system
#include <type_traits> // is_<type> and enable_if expansions

#include <iostream> // temporary for debugging output

namespace serial
{

enum Type { NONE, UINT8, INT8, UINT16, INT16, UINT32, INT32, UINT64, INT64, FLT32, FLT64, FLT128 };

typedef unsigned char uchar;
typedef std::vector<uchar> ByteVec;
typedef std::pair<Type, ByteVec> SerializedMember;

// now define some helper functions
template <typename T>
uchar _getSize()
{
    uchar raw = CHAR_BIT * sizeof(T);
    uchar remainder = raw % 8;

    return 0 == remainder ? raw : raw + 8 - remainder;
}

template <typename T>
Type _getTypeKeyInt()
{
    uchar size = _getSize<T>();
    switch (size)
    {
    case 8: if (std::is_signed<T>::value) return INT8; else return UINT8;
    case 16: if (std::is_signed<T>::value) return INT16; else return UINT16;
    case 32: if (std::is_signed<T>::value) return INT32; else return UINT32;
    case 64: if (std::is_signed<T>::value) return INT64; else return UINT64;
    default: return NONE;
    };

    return NONE;
}

template <typename T>
Type _getTypeKeyFloat()
{
    uchar size = _getSize<T>();
    switch (size)
    {
    case 32: return FLT32;
    case 64: return FLT64;
    case 128: return FLT128;
    default: return NONE;
    };

    return NONE;
}

template <typename T>
Type _getTypeKey()
{
    if (std::is_integral<T>::value) return _getTypeKeyInt<T>();
    else if (std::is_floating_point<T>::value) return _getTypeKeyFloat<T>();
    else return NONE;
}

template <typename T>
ByteVec _fillArrayInt(const T& input)
{
    uchar size = _getSize<T>();

    T le_input = input;
    // the htole functions from endian.h don't use CHAR_BIT
    // and implicitly byteswap using 8 bits per byte, hence
    // the fixed size function names. We can then safely use
    // them on any platform. If this is not true, we must
    // write our own byte swappers.
    switch (size)
    {
    case 8: break;
    case 16:
        le_input = static_cast<T>(htole16(input));
        break;
    case 32:
        le_input = static_cast<T>(htole32(input));
        break;
    case 64:
        le_input = static_cast<T>(htole64(input));
        break;
    default:
        break;
    };

    ByteVec val;
    for (uchar i = 0; i < size; i += 8)
        val.push_back(static_cast<uchar>(le_input >> i));

    return val;
}

template <typename T, typename O>
ByteVec _floatToInt(const T input)
{
    union
    {
        T in;
        O out;
    }   data;

    data.in = input;

    return _fillArrayInt(data.out);
}

template <typename T>
ByteVec _floatToInt128(const T& input)
{
    union
    {
        T in;
        int64_t out[2];
    }   data;

    data.in = input;

    ByteVec arr[2];
    arr[0] = _fillArrayInt(data.out[0]);
    arr[1] = _fillArrayInt(data.out[1]);

    // now assume overall little endian order
    arr[0].reserve(16);
    arr[0].insert(arr[0].end(), arr[1].begin(), arr[1].end());

    return arr[0];
}

template <typename T>
ByteVec _fillArrayFloat(const T input)
{
    uchar size = _getSize<T>();
    switch (size)
    {
    case 32: return _floatToInt<T, int32_t>(input);
    case 64: return _floatToInt<T, int64_t>(input);
    case 128: return _floatToInt128<T>(input);
    default: break;
    };

    return ByteVec(0);
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value, ByteVec>::type
_fillArray(const T input)
{
    return _fillArrayInt(input);
}

template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, ByteVec>::type
_fillArray(const T input)
{
    return _fillArrayFloat(input);
}

template <typename T>
typename std::enable_if<!std::is_integral<T>::value && !std::is_floating_point<T>::value, ByteVec>::type
_fillArray(const T input)
{
    return ByteVec(0);
}

ByteVec& _setSize(ByteVec& arr, const uint32_t size)
{
    ByteVec sizeArray = _fillArrayInt(size);
    arr.insert(arr.begin(), sizeArray.begin(), sizeArray.end());

    return arr;
}

template <typename T>
T _readArrayInt(const ByteVec& input)
{
    uchar size = _getSize<T>();
    if (size/8 != input.size())
        return static_cast<T>(0);

    T v = 0;
    for (uchar ii = 0; ii < size; ++ii)
        v |= static_cast<T>(input.at(i/8)) << i;

    switch (size)
    {
    case 8: break;
    case 16:
        v = static_cast<T>(le16toh(v));
        break;
    case 32:
        v = static_cast<T>(le32toh(v));
        break;
    case 64:
        v = static_cast<T>(le64toh(v));
        break;
    default: break;
    };

    return v;
}

template <typename T, typename O>
O _intToFloat(const ByteVec& input)
{
    T intVal = _readArrayInt<T>(input);

    union
    {
        T in;
        O out;
    }   data;

    data.in = intVal;

    return data.out;
}

template <typename O>
O _intToFloat128(const ByteVec& input)
{
    if (input.size() != 16)
        return static_cast<O>(0);

    int64_t arr[2];
    arr[0] = _readArrayInt<int64_t>(ByteVec(input.begin(), input.begin() + 8));
    arr[1] = _readArrayInt<int64_t>(ByteVec(input.begin()+ 8, input.end()));

    union
    {
        int64_t in[2];
        O out;
    }   data;

#if __BYTE_ORDER == __LITTLE_ENDIAN
    data.in[0] = arr[0];
    data.in[1] = arr[1];
#else
    data.in[0] = arr[1];
    data.in[1] = arr[0];
#endif

    return data.out;
}

template <typename T>
T _readArrayFloat(const ByteVec& input)
{
    uchar size = _getSize<T>();
    if (size/8 != input.size())
        return static_cast<T>(0);

    switch (size)
    {
    case 32: return _intToFloat<int32_t, T>(input);
    case 64: return _intToFloat<int64_t, T>(input);
    case 128: return _intToFloat128<T>(input);
    default: break;
    };

    return static_cast<T>(0);
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value, T>::type
_readArray(const ByteVec& input)
{
    return _readArrayInt(input);
}

template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, T>::type
_readArray(const ByteVec& input)
{
    return _readArrayFloat(input);
}

template <typename T>
typename std::enable_if<!std::is_integral<T>::value && !std::is_floating_point<T>::value, T>::type
_readArray(const ByteVec& input)
{
    return static_cast<T>(0);
}

uint32_t _getSize(const ByteVec& t)
{
    ByteVec sizeVec(t.begin(), t,begin() + 4);
    return _readArrayInt<uint32_t>(sizeVec);
}

class Serializable
{
public:
    Serializable()
    {
        assert(CHAR_BIT >= 8);
    }

    virtual ~Serializable() {}

    virtual Serializable* create() const = 0;

    SerializedMember& key(const std::string& k) { return _members[k]; }
    bool contains(const std::string& k) const { return _members.find(k) != members.end(); }

    template <typename T>
    friend SerializedMember& operator<<(SerializedMember& k, const T& v);

    template <typename T>
    friend const SerializedMember& operator>>(const SerializedMember& k, T& v);

private:
    std::map<std::string, SerializedMember> _members;
};

class Factory
{
public:
    Factory() {}

    virtual ~Factory()
    {
        for (SerializationMap::const_iterator it = _serializables.cbegin();
             it != _serializables.cend(); ++it)
            delete it->second;

        _serializables.clear();
    }

    void add(const std::string& cName, Serializable *const cInst)
    {
        if (cInst == NULL)
            return;

        SerializationMap::const_iterator it = _serializables.find(cName);

        if (it == _serializables.cend())
            _serializables[cName] = cInst;
    }

    Serializable* create(const std::string& cName) const
    {
        SerializationMap::const_iterator it = _serializables.find(cName);

        if (it == _serializables.cend())
            return NULL;

        return it->second->create();
    }

private:
    typedef std::map<std::string, Serializable *> SerializationMap;
    SerializationMap _serializables;
};

template <typename T>
struct has_iterator
{
private:
    template <typename C> static char test(typename C::iterator*);
    template <typename C> static int  test(...);
public:
    enum { value = sizeof(test<T>(0)) == sizeof(char) };
}

template <typename T>
struct OutStreamHelper
{
    static SerializedMember& streamOut(SerializedMember& k, const T& v)
    {
        ByteVec t = _fillArray(v);
        if (t.size() == 0)
            return k;

        _setSize(t, 1);

        k = std::make_pair(_getTypeKey<T>(), t);
        return k;
    }
};

template <typename T, typename Iter>
ByteVec fillOutputVector(Iter begin, Iter end)
{
    uint32_t size = std::distance(begin, end);

    ByteVec t;
    t.reserve(size*_getSize<T>()/8 + 4);

    for (Iter it = begin; it != end; ++it)
    {
        ByteVec member = _fillArray(*it);
        if (member.size() == 0)
        {
            --size;
            continue;
        }

        t.insert(t.end(), member.begin(), member.end());
    }

    _setSize(t, size);

    ByteVec(t).swap(t);
    return t;
}

template <typename T, uint32_t N>
struct OutStreamHelper<T[N]>
{
    static SerializedMember& streamOut(SerializedMember& k, const T v[])
    {
        ByteVec t = fillOutputVector<T>(v, v+N);

        k = std::make_pair(_getTypeKey<T>(), t);
        return k;
    }
};

template <typename T, uint32_t N>
struct OutStreamHelper<std::vector<T> >
{
    static SerializedMember& streamOut(SerializedMember& k, const std::vector<T>& v)
    {
        ByteVec t = fillOutputVector<T>(v.begin(), v.end());

        k = std::make_pair(_getTypeKey<T>(), t);
        return k;
    }
};

template <template <typename, typename, typename, typename> class Container,
          typename Key, typename T, typename C, typename A>
struct OutStreamHelper<Container<Key, T, C, A> >
{
    template <typename Q>
    static typename std::enable_if<has_iterator<Q>::value, SerializedMember&>::type
    streamOut(SerializedMember& k, const Q& v)
    {
        std::cout << "MAP" << std::endl;
        return k;
    }

    template <typename Q>
    static typename std::enable_if<!has_iterator<Q>::value, SerializedMember&>::type
    streamOut(SerializedMember& k, const Q& v)
    {
        std::cout << "NOT MAP" << std::endl;
        return k;
    }
};

// generic container
template <template <typename, typename...> class Container,
          typename T, typename... Add>
struct OutStreamHelper<Container<T, Add...> >
{
    static SerializedMember& streamOut(SerializedMember& k, const Container<T, Add...>& v)
    {
        std::cout << "CONTAINER" << std::endl;
    }
};

template <typename T>
SerializedMember& operator<<(SerializedMember& k, const T& v)
{
    return OutStreamHelper<T>::streamOut(k, v);
}

template <typename T>
struct InStreamHelper
{
    static const SerializedMember& streamIn(const SerializedMember& k, T& v)
    {
        if (k.first != _getTypeKey<T>() || k.second.size() < _getSize<T>()/8 + 4)
            return k;

        ByteVec copy(k.second.begin() + 4, k.second.begin() + 4 + _getSize<T>()/8);

        v = _readArray<T>(copy);

        return k;
    }
};

template <typename T, typename Iter>
Iter readByteVec(Iter begin, ByteVec input, size_t outsize = 0)
{
    uint32_t size = _getSize(input);
    uchar indSize = _getSize<T>()/8;
    if (input.size() < size*indSize + 4)
        return begin;

    size_t readSize = size;
    if (outsize > 0)
        readSize = std::min(readSize, outsize);

    for (size_t idx = 0; idx < readSize; ++idx)
    {
        ByteVec copy(input.begin() + 4 + idx*indSize,
                     input.begin() + 4 + (idx+1)*indSize);

        *(begin++) = _readArray<T>(copy);
    }

    return begin;
}

template <typename T, uint32_t N>
struct InStreamHelper<T[N]>
{
    static const SerializedMember& streamIn(const SerializedMember& k, T v[])
    {
        if (k.first != _getTypeKey<T>())
            return k;

        readByteVec<T>(v, k.second, N);

        return k;
    }
};

template <typename T>
struct InStreamHelper<std::vector<T> >
{
    static const SerializedMember& streamIn(const SerializedMember& k, std::vector<T>& v)
    {
        if (k.first != _getTypeKey<T>())
            return k;

        readByteVec<T>(std::back_inserter(v), k.second);

        return k;
    }
};

template <typename T>
const SerializedMember& operator>>(const SerializedMember& k, T& v)
{
    return InStreamHelper<T>::streamIn(k, v);
}

}; // namespace serial

#endif // __SERIALIZABLE_H
