#ifndef ANYSCALAR_H
#define ANYSCALAR_H

#include <ostream>
#include <exception>
#include <map>

#include <pv/templateMeta.h>
#include <pv/typeCast.h>
#include <pv/pvIntrospect.h> /* for ScalarType enum */

namespace detail {
template <typename T>
struct any_storage_type { typedef T type; };
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
        virtual const char* what() noexcept
#else
        bad_cast() throw() {}
        virtual ~bad_cast() throw() {}
        virtual const char* what() throw()
#endif
        { return "bad_cast() type mis-match"; }
    };

private:
    struct HolderBase {
        virtual ~HolderBase() {}
        virtual HolderBase* clone() =0;
        virtual epics::pvData::ScalarType type() =0;
        virtual void* ptr() =0;
        virtual void show(std::ostream&) =0;
    };

    template<typename T>
    struct Holder : public HolderBase {
        T held;

        Holder(typename epics::pvData::meta::arg_type<T>::type v,
               epics::pvData::ScalarType t = (epics::pvData::ScalarType)epics::pvData::ScalarTypeID<T>::value)
            :held(v)
        {}
        virtual ~Holder() {}

        virtual HolderBase* clone() {
            return new Holder(held);
        }

        virtual epics::pvData::ScalarType type() {
            return (epics::pvData::ScalarType)epics::pvData::ScalarTypeID<T>::value;
        }

        virtual void* ptr() {
            return static_cast<void*>(&held);
        }

        virtual void show(std::ostream& strm) {
            strm<<held;
        }
    };

    HolderBase *holder;
public:
    AnyScalar() :holder(0) {}

    template<typename T>
    explicit AnyScalar(T v)
        :holder(new Holder<typename epics::pvData::meta::strip_const<typename detail::any_storage_type<T>::type>::type>(v))
    {}

    AnyScalar(const AnyScalar& o)
        :holder(o.holder ? o.holder->clone() : 0)
    {}

#if __cplusplus>=201103L
    AnyScalar(AnyScalar&& o)
        :holder(o.holder)
    { o.holder = 0; }
#endif

    ~AnyScalar() { delete holder; }

    AnyScalar& operator=(const AnyScalar& o) {
        AnyScalar(o).swap(*this);
        return *this;
    }

    inline void swap(AnyScalar& o) {
        std::swap(holder, o.holder);
    }

    inline epics::pvData::ScalarType type() const {
        return holder ? holder->type() : ((epics::pvData::ScalarType)-1);
    }

    inline bool empty() const { return !holder; }

#if __cplusplus>=201103L
    explicit operator bool() const { return holder; }
#else
private:
    typedef void (AnyScalar::bool_type)(AnyScalar&);
public:
    operator bool_type() const { holder ? &AnyScalar::swap : 0 }
#endif

    /** Return reference to wrapped value */
    template<typename T>
    T&
    ref() {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        if(!holder || type()!=(epics::pvData::ScalarType)epics::pvData::ScalarTypeID<T2>::value)
            throw bad_cast();
        return *static_cast<T*>(holder->ptr());
    }

    template<typename T>
    typename epics::pvData::meta::decorate_const<T&>::type
    ref() const {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        if(!holder || type()!=(epics::pvData::ScalarType)epics::pvData::ScalarTypeID<T2>::value)
            throw bad_cast();
        return *static_cast<T*>(holder->ptr());
    }

    /** copy out wrapped value, with a value conversion. */
    template<typename T>
    T as() const {
        typedef typename epics::pvData::meta::strip_const<T>::type T2;
        if(!holder)
            throw bad_cast();
        T2 ret;
        epics::pvData::castUnsafeV(1, (epics::pvData::ScalarType)epics::pvData::ScalarTypeID<T2>::value, &ret,
                                   holder->type(), holder->ptr());
        return ret;
    }

private:
    friend std::ostream& operator<<(std::ostream& strm, const AnyScalar& v);
};

inline
std::ostream& operator<<(std::ostream& strm, const AnyScalar& v)
{
    if(v.holder)
        v.holder->show(strm);
    else
        strm<<"(nil)";
    return strm;
}

#endif // ANYSCALAR_H
