#ifndef IOCSHELPER_H
#define IOCSHELPER_H

#include <string>

#include <iocsh.h>

namespace detail {

template<typename T>
struct getarg {};
template<> struct getarg<int> {
    static int op(const iocshArgBuf& a) { return a.ival; }
    enum { argtype = iocshArgInt };
};
template<> struct getarg<double> {
    static double op(const iocshArgBuf& a) { return a.dval; }
    enum { argtype = iocshArgDouble };
};
template<> struct getarg<char*> {
    static char* op(const iocshArgBuf& a) { return a.sval; }
    enum { argtype = iocshArgString };
};
template<> struct getarg<const char*> {
    static const char* op(const iocshArgBuf& a) { return a.sval; }
    enum { argtype = iocshArgString };
};


template<int N>
struct iocshFuncInfo{
    iocshFuncDef def;
    std::string name;
    iocshArg *argarr[N];
    iocshArg args[N];
    std::string argnames[N];
    iocshFuncInfo(const std::string& n) :name(n) {
        def.name = name.c_str();
        def.nargs = N;
        def.arg = (iocshArg**)&argarr;
        for(size_t i=0; i<N; i++)
            argarr[i] = &args[i];
    }
};

template<void (*fn)()>
static void call0(const iocshArgBuf *args)
{
    fn();
}

template<typename A, void (*fn)(A)>
static void call1(const iocshArgBuf *args)
{
    fn(getarg<A>::op(args[0]));
}

template<typename A, typename B, void (*fn)(A,B)>
static void call2(const iocshArgBuf *args)
{
    fn(getarg<A>::op(args[0]),
       getarg<B>::op(args[1]));
}
}


template<void (*fn)()>
void iocshRegister(const char *name)
{
    detail::iocshFuncInfo<0> *info = new detail::iocshFuncInfo<0>(name);
    iocshRegister(&info->def, &detail::call0<fn>);
}

template<typename A, void (*fn)(A)>
void iocshRegister(const char *name, const char *arg1name)
{
    detail::iocshFuncInfo<1> *info = new detail::iocshFuncInfo<1>(name);
    info->argnames[0] = arg1name;
    info->args[0].name = info->argnames[0].c_str();
    info->args[0].type = (iocshArgType)detail::getarg<A>::argtype;
    iocshRegister(&info->def, &detail::call1<A, fn>);
}

template<typename A, typename B, void (*fn)(A,B)>
void iocshRegister(const char *name,
                   const char *arg1name,
                   const char *arg2name)
{
    detail::iocshFuncInfo<2> *info = new detail::iocshFuncInfo<2>(name);
    info->argnames[0] = arg1name;
    info->argnames[1] = arg2name;
    info->args[0].name = info->argnames[0].c_str();
    info->args[1].name = info->argnames[1].c_str();
    info->args[0].type = (iocshArgType)detail::getarg<A>::argtype;
    info->args[1].type = (iocshArgType)detail::getarg<B>::argtype;
    iocshRegister(&info->def, &detail::call2<A, B, fn>);
}

#endif // IOCSHELPER_H
