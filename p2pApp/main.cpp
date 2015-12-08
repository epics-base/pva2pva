
#include <iocsh.h>
#include <epicsExit.h>
#include <libComRegister.h>

#define epicsExportSharedSymbols
#include "pva2pva.h"

int main(int argc, char *argv[])
{
    libComRegister(); // non-IOC related iocsh functions
    registerGWClientIocsh();
    registerGWServerIocsh();
    if(argc>1)
        iocsh(argv[1]);
    int ret = iocsh(NULL);
    gwServerShutdown();
    gwClientShutdown();
    epicsExit(ret);
    return ret;
}
