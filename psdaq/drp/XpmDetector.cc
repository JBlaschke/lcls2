
#include "XpmDetector.hh"
#include "AxisDriver.h"
#include <unistd.h>
#include "psalg/utils/SysLog.hh"
#include "psdaq/mmhw/TriggerEventManager.hh"

using namespace XtcData;
using json = nlohmann::json;
using logging = psalg::SysLog;

static void dmaReadRegister (int, uint32_t*, uint32_t*);
static void dmaWriteRegister(int, uint32_t*, uint32_t);
static bool lverbose = true;

namespace Drp {

XpmDetector::XpmDetector(Parameters* para, MemPool* pool) :
    Detector(para, pool)
{
}

json XpmDetector::connectionInfo()
{
    int fd = open(m_para->device.c_str(), O_RDWR);
    if (fd < 0) {
        logging::error("Error opening %s", m_para->device.c_str());
        return json();
    }

    Pds::Mmhw::TriggerEventManager* tem = new ((void*)0x00C20000) Pds::Mmhw::TriggerEventManager;

    uint32_t reg;
    dmaReadRegister(fd, &tem->xma().rxId, &reg);

    close(fd);
    // there is currently a failure mode where the register reads
    // back as zero (incorrectly). This is not the best longterm
    // fix, but throw here to highlight the problem. - cpo
    if (!reg) {
        const char msg[] = "XPM Remote link id register is zero\n";
        logging::error("%s", msg);
        throw msg;
    }
    int xpm  = (reg >> 20) & 0x0F;
    int port = (reg >>  0) & 0xFF;
    json info = {{"xpm_id", xpm}, {"xpm_port", port}};
    return info;
}

// setup up device to receive data over pgp
void XpmDetector::connect(const json& connect_json, const std::string& collectionId)
{
    logging::info("XpmDetector connect");
    // FIXME make configureable
    m_length = 100;
    std::map<std::string,std::string>::iterator it = m_para->kwargs.find("sim_length");
    if (it != m_para->kwargs.end())
        m_length = stoi(it->second);

    int links = m_para->laneMask;

    int fd = open(m_para->device.c_str(), O_RDWR);
    if (fd < 0) {
        logging::error("Error opening %s", m_para->device.c_str());
        return;
    }

    int readoutGroup = connect_json["body"]["drp"][collectionId]["det_info"]["readout"];

    Pds::Mmhw::TriggerEventManager* tem = new ((void*)0x00C20000) Pds::Mmhw::TriggerEventManager;
    for(unsigned i=0, l=links; l; i++) {
        Pds::Mmhw::TriggerEventBuffer& b = tem->det(i);
        if (l&(1<<i)) {
            dmaWriteRegister(fd, &b.enable, (1<<2)      );  // reset counters
            dmaWriteRegister(fd, &b.pauseThresh, 16     );
            dmaWriteRegister(fd, &b.group , readoutGroup);
            dmaWriteRegister(fd, &b.enable, 3           );  // enable
            l &= ~(1<<i);

            dmaWriteRegister(fd, 0x00a00000+4*(i&3), (1<<30));  // clear
            dmaWriteRegister(fd, 0x00a00000+4*(i&3), (m_length&0xffffff) | (1<<31));  // enable
          }
      }

    close(fd);
}

}

void dmaReadRegister (int fd, uint32_t* addr, uint32_t* valp)
{
  uintptr_t addri = (uintptr_t)addr;
  dmaReadRegister(fd, addri&0xffffffff, valp);
  if (lverbose)
    printf("[%08lx] = %08x\n",addri,*valp);
}

void dmaWriteRegister(int fd, uint32_t* addr, uint32_t val)
{
  uintptr_t addri = (uintptr_t)addr;
  dmaWriteRegister(fd, addri&0xffffffff, val);
  if (lverbose)
    printf("[%08lx] %08x\n",addri,val);
}
