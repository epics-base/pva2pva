#ifndef HELPER_H
#define HELPER_H

#include <memory>

#if __cplusplus>=201103L
# define AUTO_VAL(NAME,VAL) auto NAME = VAL
# define AUTO_REF(NAME,VAL) auto& NAME = VAL
# define FOREACH(IT,END,C) for(auto IT=(C).begin(), END=(C).end(); IT!=END; ++IT)
#elif defined(__GNUC__)
# define AUTO_VAL(NAME,VAL) __typeof__(VAL) NAME(VAL)
# define AUTO_REF(NAME,VAL) __typeof__(VAL)& NAME(VAL)
# define FOREACH(IT,END,C) for(__typeof__((C).begin()) IT=(C).begin(), END=(C).end(); IT!=END; ++IT)
#endif

namespace p2p {
#if __cplusplus>=201103L
template<typename T>
using auto_ptr = std::unique_ptr<T>;
#define PTRMOVE(AUTO) std::move(AUTO)
#else
using std::auto_ptr;
#define PTRMOVE(AUTO) (AUTO)
#endif
}

#endif // HELPER_H
