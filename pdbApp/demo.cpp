
#include <epicsMath.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <dbLink.h>
#include <recGbl.h>
#include <alarm.h>

#include <longinRecord.h>
#include <waveformRecord.h>
#include <menuFtype.h>

#include <epicsExport.h>

namespace {

// pi/180
static const double pi_180 = 0.017453292519943295;

int dummy;

long init_spin(waveformRecord *prec)
{
    if(prec->ftvl==menuFtypeDOUBLE)
        prec->dpvt = &dummy;
    return 0;
}

long process_spin(waveformRecord *prec)
{
    if(prec->dpvt != &dummy) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 0;
    }

    const double freq = 360.0*pi_180/100; // rad/sample
    double phase = 0;
    double *val = static_cast<double*>(prec->bptr);

    long ret = dbGetLink(&prec->inp, DBF_DOUBLE, &phase, 0, 0);
    if(ret) {
        (void)recGblSetSevr(prec, LINK_ALARM, INVALID_ALARM);
        return ret;
    }

    phase *= pi_180; // deg -> rad

    for(size_t i=0, N=prec->nelm; i<N; i++)
        val[i] = sin(freq*i+phase);

    prec->nord = prec->nelm;

#ifdef DBRutag
    prec->utag = (prec->utag+1u)&0x7fffffff;
#endif

    return 0;
}

long process_repeat(waveformRecord *prec)
{
    long ret;
    if(prec->ftvl!=menuFtypeULONG) {
        recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return S_db_badDbrtype;
    }

    epicsUInt32 iv=0;
    if(!!(ret = dbGetLink(&prec->inp, DBF_ULONG, &iv, NULL, NULL))) {
        recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return ret;
    }

    epicsUInt32 *val = (epicsUInt32*)prec->bptr;
    for(size_t i=0, N=prec->nelm; i<N; i++) {
        val[i] = iv;
    }

    prec->nord = iv;

    return 0;
}

long process_utag(longinRecord *prec)
{
    long status = dbGetLink(&prec->inp, DBR_LONG, &prec->val, 0, 0);
#ifdef DBRutag
    prec->utag = prec->val;
#else
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
#endif
    return status;
}

template<typename REC>
struct dset5
{
    long count;
    long (*report)(int);
    long (*init)(int);
    long (*init_record)(REC *);
    long (*get_ioint_info)(int, REC *, IOSCANPVT*);
    long (*process)(REC *);
};

dset5<waveformRecord> devWfPDBDemo = {5,0,0,&init_spin,0,&process_spin};
dset5<waveformRecord> devWfPDBDemoRepeat = {5,0,0,0,0,&process_repeat};
dset5<longinRecord> devLoPDBUTag = {5,0,0,0,0,&process_utag};

} // namespace

extern "C" {
epicsExportAddress(dset, devWfPDBDemo);
epicsExportAddress(dset, devWfPDBDemoRepeat);
epicsExportAddress(dset, devLoPDBUTag);
}
