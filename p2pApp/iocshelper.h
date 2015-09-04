#ifndef IOCSHELPER_H
#define IOCSHELPER_H

#include <string>

#include <iocsh.h>

namespace detail {

template<typename T>
struct getarg {};
template<> struct getarg<int> {
    static int op(const iocshArgBuf& a) { return a.ival; }
};
template<> struct getarg<double> {
    static double op(const iocshArgBuf& a) { return a.dval; }
};
template<> struct getarg<char*> {
    static char* op(const iocshArgBuf& a) { return a.sval; }
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


template<typename T, void (*fn)(T)>
static void call1(const iocshArgBuf *args)
{
    fn(getarg<T>::op(args[0]));
}
}


template<void (*fn)()>
void iocshRegister0(const char *name)
{
    detail::iocshFuncInfo<0> *info = new detail::iocshFuncInfo<0>(name);
    iocshRegister(&info->def, &detail::call0<fn>);
}

template<typename T, void (*fn)(T)>
void iocshRegister1(const char *name, const char *arg1name)
{
    detail::iocshFuncInfo<1> *info = new detail::iocshFuncInfo<1>(name);
    info->argnames[0] = arg1name;
    info->args[0].name = info->argnames[0].c_str();
    iocshRegister(&info->def, &detail::call1<T, fn>);
}

#endif // IOCSHELPER_H
