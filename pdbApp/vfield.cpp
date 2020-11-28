
#include <pv/qsrv.h>

#ifdef QSRV_HAS_VFIELD

const VFieldType vfSharedVector = {"epics::pvData::shared_vector<const void>"};

const VFieldType vfStructure = {"epics::pvData::StructureConstPtr"};
const VFieldType vfPVStructure = {"epics::pvData::PVStructurePtr"};

#endif
