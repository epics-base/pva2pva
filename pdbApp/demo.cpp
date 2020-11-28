
#define USE_TYPED_DSET

#include <epicsMath.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <recGbl.h>
#include <alarm.h>

#include <waveformRecord.h>
#include <ndainRecord.h>
#include <menuFtype.h>
#include <errlog.h>

#include <epicsExport.h>

namespace {

namespace pvd = epics::pvData;

// 2*pi
const double two_pi = 6.283185307179586;
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

    return 0;
}

struct imgPvt {
    double phase;
};

#ifdef QSRV_HAS_VFIELD

long init_img(dbCommon *pcom)
{
    ndainRecord *prec = (ndainRecord*)pcom;
    try {
        imgPvt * pvt = new imgPvt;
        pvt->phase = 0.0;
        prec->dpvt = pvt;

        return 0;
    }catch(std::exception& e){
        errlogPrintf("%s: init_img error: %s\n", prec->name, e.what());
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "DEMO ERR: %s", e.what());
        return -1;
    }
}

long process_img(ndainRecord *prec)
{
    if(!prec->dpvt)
        return -1;

    imgPvt *pvt = (imgPvt*)prec->dpvt;
    try {
        pvd::shared_vector<pvd::uint8> pixels(prec->w * prec->h);

        // place blob center offset from image center by 1/4th
        double centerX = prec->w/2 + cos(pvt->phase)*prec->w/4.0,
               centerY = prec->h/2 + sin(pvt->phase)*prec->h/4.0;
        double sigma = (prec->w < prec->h ? prec->w : prec->h)/4.0;
        sigma = 2.0*sigma*sigma;

        for(epicsUInt32 h=0u; h<prec->h; h++) {
            for(epicsUInt32 w=0u; w<prec->w; w++) {
                double distX = w - centerX,
                       distY = h - centerY,
                       dist2  = distX*distX + distY*distY;

                pixels[h*prec->w + w] = (pvd::uint8)( 255.0 * exp(-dist2/sigma) );
            }
        }

        for(epicsUInt32 w=0u; w<prec->w; w++) {
            pixels[prec->h/2*prec->w + w] = 255u;
        }

        prec->val = pvd::static_shared_vector_cast<const void>(pvd::freeze(pixels));

        pvt->phase = fmod(pvt->phase + two_pi/60.0, two_pi);

        return 0;
    }catch(std::exception& e){
        errlogPrintf("%s: process_img error: %s\n", prec->name, e.what());
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "DEMO ERR: %s", e.what());
        return -1;
    }
}

#endif // QSRV_HAS_VFIELD

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

#ifdef QSRV_HAS_VFIELD
ndaindset devNDAIPDBDemo = {
    {
        5, 0, 0,
        &init_img,
        0,
    },
    &process_img,
};
#endif

} // namespace

extern "C" {
epicsExportAddress(dset, devWfPDBDemo);
#ifdef QSRV_HAS_VFIELD
epicsExportAddress(dset, devNDAIPDBDemo);
#endif
}
