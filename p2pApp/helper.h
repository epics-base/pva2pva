#ifndef HELPER_H
#define HELPER_H

#if __cplusplus>=201103L
# define AUTO_VAL(NAME,VAL) auto NAME = VAL
# define AUTO_REF(NAME,VAL) auto& NAME = VAL
# define FOREACH(IT,END,C) for(auto IT=(C).begin(), END=(C).end(); IT!=END; ++IT)
#elif defined(__GNUC__)
# define AUTO_VAL(NAME,VAL) __typeof__(VAL) NAME(VAL)
# define AUTO_REF(NAME,VAL) __typeof__(VAL)& NAME(VAL)
# define FOREACH(IT,END,C) for(__typeof__((C).begin()) IT=(C).begin(), END=(C).end(); IT!=END; ++IT)
#else
# error Require C++11 or G++
#endif

#endif // HELPER_H
