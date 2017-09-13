#ifndef ANYSCALAR_H
#define ANYSCALAR_H

#if __cplusplus>=201103L
#  include <type_traits>
#endif

#include <ostream>
#include <exception>
#include <map>

#include <epicsAssert.h>

#include <pv/templateMeta.h>
#include <pv/typeCast.h>
#include <pv/pvIntrospect.h> /* for ScalarType enum */

namespace detail {

// special mangling for AnyScalar ctor to map from argument type to storage type.
// allow construction from constants.
template <typename T>
struct any_storage_type { typedef T type; };
template<> struct any_storage_type<int> { typedef epics::pvData::int32 type; };
template<> struct any_storage_type<unsigned> { typedef epics::pvData::uint32 type; };
template<> struct any_storage_type<char*> { typedef std::string type; };
template<> struct any_storage_type<const char*> { typedef std::string type; };

}// namespace detail

/** A type-safe variant union capable of holding
 *  any of the PVD scalar types
 */
class AnyScalar {
public:
    struct bad_cast : public std::exception {
#if __cplusplus>=201103L
        bad_cast() noexcept {}
        virtual ~bad_cast() noexcept {}
        virtual const char* what() const noexcept
#else
        bad_cast() throw() {}
        virtual ~bad_cast() throw() {}
        virtual const char* what() const throw()
#endif
        { return "bad_cast() type mis-match"; }
    };

private:
    typedef epics::pvData::ScalarType ScalarType;
    ScalarType _stype;

    // always reserve enough storage for std::string (assumed worst case)
#if __cplusplus>=201103L
    struct wrap_t {
        typename std::aligned_storage<sizeof(std::string), alignof(std::string)>::type blob[1];
    } _wrap;
#else
    struct wrap_t {
        union blob_t {
            char data[sizeof(std::string)];
            double align_f; // assume std::string alignment <= 8
        } blob[1];
    } _wrap;
#endif

    // assumed largest non-string type
    typedef double _largest_blob;

    template<typename T>
    inline T& _as() {
        return *reinterpret_cast<T*>(_wrap.blob);
    }
    template<typename T>
    inline const T& _as() const {
        return *reinterpret_cast<const T*>(_wrap.blob);
    }
public:
    AnyScalar() : _stype((ScalarType)-1) {}

    template<typename T>
    explicit AnyScalar(T v)
    {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        typedef typename detail::any_storage_type<T2>::type TT;

        STATIC_ASSERT(sizeof(TT)<=sizeof(_wrap.blob));

        new (_wrap.blob) TT(v);

        // this line fails to compile when type T can't be mapped to one of
        // the PVD scalar types.
        _stype = (ScalarType)epics::pvData::ScalarTypeID<TT>::value;
    }

    AnyScalar(const AnyScalar& o)
        :_stype(o._stype)
    {
        if(o._stype==epics::pvData::pvString) {
            new (_wrap.blob) std::string(o._as<std::string>());
        } else if(o._stype!=(ScalarType)-1) {
            memcpy(_wrap.blob, o._wrap.blob, sizeof(_largest_blob));
        }
    }

#if __cplusplus>=201103L
    AnyScalar(AnyScalar&& o)
        :_stype(o._stype)
    {
        typedef std::string string;
        if(o._stype==epics::pvData::pvString) {
            new (_wrap.blob) std::string();
            _as<std::string>() = std::move(o._as<std::string>());
            o._as<string>().~string();
        } else if(o._stype!=(ScalarType)-1) {
            memcpy(_wrap.blob, o._wrap.blob, sizeof(_largest_blob));
        }
        o._stype = (ScalarType)-1;
    }
#endif

    ~AnyScalar() {
        if(_stype==epics::pvData::pvString) {
            typedef std::string string;
            _as<string>().~string();
        }
        // other types need no cleanup
    }

    AnyScalar& operator=(const AnyScalar& o) {
        AnyScalar(o).swap(*this);
        return *this;
    }

    template<typename T>
    AnyScalar& operator=(T v) {
        AnyScalar(v).swap(*this);
        return *this;
    }

#if __cplusplus>=201103L
    AnyScalar& operator=(AnyScalar&& o) {
        if(_stype==epics::pvData::pvString) {
            typedef std::string string;
            _as<string>().~string();
        }
        _stype = (ScalarType)-1;
        swap(o);
        return *this;
    }
#endif

    void swap(AnyScalar& o) {
        typedef std::string string;
        switch((unsigned)_stype) {
        case -1:
            switch((unsigned)o._stype) {
            case -1:
                // nil <-> nil
                break;
            case epics::pvData::pvString:
                // nil <-> string
                new (_wrap.blob) std::string();
                _as<std::string>().swap(o._as<std::string>());
                o._as<std::string>().~string();
                break;
            default:
                // nil <-> non-string
                memcpy(_wrap.blob, o._wrap.blob, sizeof(_largest_blob));
                break;
            }
            break;
        case epics::pvData::pvString:
            switch((unsigned)o._stype) {
            case -1:
                // string <-> nil
                new (o._wrap.blob) std::string();
                _as<std::string>().swap(o._as<std::string>());
                _as<std::string>().~string();
                break;
            case epics::pvData::pvString:
                // string <-> string
                _as<std::string>().swap(o._as<std::string>());
                break;
            default: {
                // string <-> non-string
                std::string temp;
                temp.swap(_as<std::string>());

                _as<std::string>().~string();

                memcpy(_wrap.blob, o._wrap.blob, sizeof(_largest_blob));

                new (o._wrap.blob) std::string();
                temp.swap(o._as<std::string>());
            }
                break;
            }
            break;
        default:
            switch((unsigned)o._stype) {
            case -1:
                // non-string <-> nil
                memcpy(o._wrap.blob, _wrap.blob, sizeof(_largest_blob));
                break;
            case epics::pvData::pvString: {
                // non-string <-> string
                std::string temp;
                temp.swap(o._as<std::string>());

                o._as<std::string>().~string();

                memcpy(o._wrap.blob, _wrap.blob, sizeof(_largest_blob));

                new (_wrap.blob) std::string();
                temp.swap(_as<std::string>());
            }
                break;
            default:
                // non-string <-> non-string
                _largest_blob temp;
                memcpy(&temp, _wrap.blob, sizeof(_largest_blob));
                memcpy(_wrap.blob, o._wrap.blob, sizeof(_largest_blob));
                memcpy(o._wrap.blob, &temp, sizeof(_largest_blob));
                // std::swap(o._wrap.blob, _wrap.blob); // gcc <=4.3 doesn't like this
                break;
            }
            break;
        }
        std::swap(_stype, o._stype);
    }

    inline epics::pvData::ScalarType type() const {
        return _stype;
    }

    inline bool empty() const { return _stype==(ScalarType)-1; }

#if __cplusplus>=201103L
    explicit operator bool() const { return !empty(); }
#else
private:
    typedef void (AnyScalar::*bool_type)(AnyScalar&);
public:
    operator bool_type() const { return !empty() ? &AnyScalar::swap : 0; }
#endif

    /** Return reference to wrapped value */
    template<typename T>
    // T -> strip_const -> map to storage type -> add reference
    typename detail::any_storage_type<typename epics::pvData::meta::strip_const<T>::type>::type&
    ref() {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        typedef typename detail::any_storage_type<T2>::type TT;

        if(_stype!=(ScalarType)epics::pvData::ScalarTypeID<TT>::value)
            throw bad_cast();
        return _as<TT>();
    }

    template<typename T>
    // T -> strip_const -> map to storage type -> add const reference
    typename epics::pvData::meta::decorate_const<typename detail::any_storage_type<typename epics::pvData::meta::strip_const<T>::type>::type>::type&
    ref() const {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        typedef typename detail::any_storage_type<T2>::type TT;

        if(_stype!=(ScalarType)epics::pvData::ScalarTypeID<TT>::value)
            throw bad_cast();
        return _as<TT>();
    }

    /** copy out wrapped value, with a value conversion. */
    template<typename T>
    T as() const {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        typedef typename detail::any_storage_type<T2>::type TT;

        if(_stype==(ScalarType)-1)
            throw bad_cast();
        TT ret;
        epics::pvData::castUnsafeV(1, (ScalarType)epics::pvData::ScalarTypeID<T2>::value, &ret,
                                   _stype, _wrap.blob);
        return ret;
    }

private:
    friend std::ostream& operator<<(std::ostream& strm, const AnyScalar& v);
};

inline
std::ostream& operator<<(std::ostream& strm, const AnyScalar& v)
{
    switch(v.type()) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case epics::pvData::pv ## PVACODE: strm<<v._as<PVATYPE>(); break;
#define CASE_REAL_INT64
#define CASE_STRING
#include "pvatypemap.h"
#undef CASE
#undef CASE_REAL_INT64
#undef CASE_STRING
    default:
        strm<<"(nil)"; break;
    }
    return strm;
}

#endif // ANYSCALAR_H
