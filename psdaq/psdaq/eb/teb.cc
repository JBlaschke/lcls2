#include "EbAppBase.hh"

#include "BatchManager.hh"
#include "EbEvent.hh"

#include "EbLfClient.hh"
#include "EbLfServer.hh"

#include "utilities.hh"

#include "psdaq/trigger/Trigger.hh"
#include "psdaq/trigger/utilities.hh"
#include "psdaq/service/MetricExporter.hh"
#include "psdaq/service/Collection.hh"
#include "psdaq/service/Dl.hh"
#include "psdaq/service/SysLog.hh"
#include "xtcdata/xtc/Dgram.hh"

#include <stdio.h>
#include <unistd.h>                     // For getopt()
#include <cstring>
#include <climits>
#include <csignal>
#include <bitset>
#include <atomic>
#include <vector>
#include <cassert>
#include <iostream>
#include <exception>
#include <algorithm>                    // For std::fill()
#include <set>                          // For multiset
#include <Python.h>

#include "rapidjson/document.h"

using namespace rapidjson;
using namespace XtcData;
using namespace Pds;
using namespace Pds::Trg;

using json = nlohmann::json;
using logging = Pds::SysLog;

static const int      core_0           = 18; // devXXX: 11, devXX:  7, accXX:  9
static const int      core_1           = 19; // devXXX: 12, devXX: 19, accXX: 21
static const size_t   header_size      = sizeof(Dgram);
static const size_t   input_extent     = 2; // Revisit: Number of "L3" input  data words
static const size_t   result_extent    = 2; // Revisit: Number of "L3" result data words
static const size_t   max_contrib_size = header_size + input_extent  * sizeof(uint32_t);
static const size_t   max_result_size  = header_size + result_extent * sizeof(uint32_t);

static struct sigaction      lIntAction;
static volatile sig_atomic_t lRunning = 1;

void sigHandler( int signal )
{
  static unsigned callCount(0);

  if (callCount == 0)
  {
    printf("\nShutting down\n");

    lRunning = 0;
  }

  if (callCount++)
  {
    fprintf(stderr, "Aborting on 2nd ^C...\n");
    ::abort();
  }
}


namespace Pds {
  namespace Eb {

    class Teb : public EbAppBase
    {
    public:
      Teb(const EbParams& prms, std::shared_ptr<MetricExporter>& exporter);
    public:
      int      connect(const EbParams&);
      int      configure(const EbParams&, Trigger* object, unsigned prescale);
      Trigger* trigger() const { return _trigger; }
      void     run();
    public:                         // For EventBuilder
      virtual
      void     process(EbEvent* event);
    private:
      void     _tryPost(const Dgram& dg);
      void     _post(const Batch&);
      uint64_t _receivers(const Dgram& ctrb) const;
    private:
      std::vector<EbLfLink*>       _l3Links;
      EbLfServer                   _mrqTransport;
      std::vector<EbLfLink*>       _mrqLinks;
      BatchManager                 _batMan;
      std::multiset<Batch*, Batch> _batchList;
      unsigned                     _id;
      const unsigned               _verbose;
    private:
      u64arr_t                     _rcvrs;
      //uint64_t                     _trimmed;
      Trigger*                     _trigger;
      unsigned                     _prescale;
    private:
      unsigned                     _wrtCounter;
    private:
      uint64_t                     _eventCount;
      uint64_t                     _batchCount;
      uint64_t                     _writeCount;
      uint64_t                     _monitorCount;
      uint64_t                     _prescaleCount;
    private:
      const EbParams&              _prms;
      EbLfClient                   _l3Transport;
    };
  };
};


using namespace Pds::Eb;

Teb::Teb(const EbParams& prms, std::shared_ptr<MetricExporter>& exporter) :
  EbAppBase     (prms, BATCH_DURATION, MAX_ENTRIES, MAX_BATCHES),
  _l3Links      (),
  _mrqTransport (prms.verbose),
  _mrqLinks     (),
  _batMan       (Trigger::size()), // Revisit: prms.maxResultSize),
  _id           (-1),
  _verbose      (prms.verbose),
  //_trimmed      (0),
  _trigger      (nullptr),
  _eventCount   (0),
  _batchCount   (0),
  _writeCount   (0),
  _monitorCount (0),
  _prescaleCount(0),
  _prms         (prms),
  _l3Transport  (prms.verbose)
{
  std::map<std::string, std::string> labels{{"partition", std::to_string(prms.partition)}};
  exporter->add("TEB_EvtRt",  labels, MetricType::Rate,    [&](){ return _eventCount;             });
  exporter->add("TEB_EvtCt",  labels, MetricType::Counter, [&](){ return _eventCount;             });
  exporter->add("TEB_BatCt",  labels, MetricType::Counter, [&](){ return _batchCount;             }); // Outbound
  exporter->add("TEB_BtAlCt", labels, MetricType::Counter, [&](){ return _batMan.batchAllocCnt(); });
  exporter->add("TEB_BtFrCt", labels, MetricType::Counter, [&](){ return _batMan.batchFreeCnt();  });
  exporter->add("TEB_BtWtg",  labels, MetricType::Gauge,   [&](){ return _batMan.batchWaiting();  });
  exporter->add("TEB_EpAlCt", labels, MetricType::Counter, [&](){ return  epochAllocCnt();        });
  exporter->add("TEB_EpFrCt", labels, MetricType::Counter, [&](){ return  epochFreeCnt();         });
  exporter->add("TEB_EvAlCt", labels, MetricType::Counter, [&](){ return  eventAllocCnt();        });
  exporter->add("TEB_EvFrCt", labels, MetricType::Counter, [&](){ return  eventFreeCnt();         });
  exporter->add("TEB_TxPdg",  labels, MetricType::Gauge,   [&](){ return _l3Transport.pending();  });
  exporter->add("TEB_RxPdg",  labels, MetricType::Gauge,   [&](){ return  rxPending();            });
  exporter->add("TEB_BtInCt", labels, MetricType::Counter, [&](){ return  bufferCnt();            }); // Inbound
  exporter->add("TEB_FxUpCt", labels, MetricType::Counter, [&](){ return  fixupCnt();             });
  exporter->add("TEB_ToEvCt", labels, MetricType::Counter, [&](){ return  tmoEvtCnt();            });
  exporter->add("TEB_WrtCt",  labels, MetricType::Counter, [&](){ return  _writeCount;            });
  exporter->add("TEB_MonCt",  labels, MetricType::Counter, [&](){ return  _monitorCount;          });
  exporter->add("TEB_PsclCt", labels, MetricType::Counter, [&](){ return  _prescaleCount;         });
}

int Teb::connect(const EbParams& prms)
{
  int rc;
  if ( (rc = EbAppBase::connect(prms)) )
    return rc;

  _id = prms.id;
  _l3Links.resize(prms.addrs.size());
  _rcvrs = prms.receivers;

  void*  region  = _batMan.batchRegion();
  size_t regSize = _batMan.batchRegionSize();

  for (unsigned i = 0; i < prms.addrs.size(); ++i)
  {
    const char*    addr = prms.addrs[i].c_str();
    const char*    port = prms.ports[i].c_str();
    EbLfLink*      link;
    const unsigned tmo(120000);         // Milliseconds
    if ( (rc = _l3Transport.connect(addr, port, tmo, &link)) )
    {
      fprintf(stderr, "%s:\n  Error connecting to Ctrb at %s:%s\n",
              __PRETTY_FUNCTION__, addr, port);
      return rc;
    }
    if ( (rc = link->preparePoster(_id, region, regSize)) )
    {
      fprintf(stderr, "%s:\n  Failed to prepare link with Ctrb at %s:%s\n",
              __PRETTY_FUNCTION__, addr, port);
      return rc;
    }
    _l3Links[link->id()] = link;

    printf("Outbound link with Ctrb ID %d connected\n", link->id());
  }

  if ( (rc = _mrqTransport.initialize(prms.ifAddr, prms.mrqPort, prms.numMrqs)) )
  {
    fprintf(stderr, "%s:\n  Failed to initialize MonReq EbLfServer\n",
            __PRETTY_FUNCTION__);
    return rc;
  }

  _mrqLinks.resize(prms.numMrqs);

  for (unsigned i = 0; i < prms.numMrqs; ++i)
  {
    EbLfLink*      link;
    const unsigned tmo(120000);         // Milliseconds
    if ( (rc = _mrqTransport.connect(&link, tmo)) )
    {
      fprintf(stderr, "%s:\n  Error connecting to MonReq %d\n",
              __PRETTY_FUNCTION__, i);
      return rc;
    }

    if ( (rc = link->preparePender(prms.id)) )
    {
      fprintf(stderr, "%s:\n  Failed to prepare MonReq %d\n",
              __PRETTY_FUNCTION__, i);
      return rc;
    }
    _mrqLinks[link->id()] = link;
    if ( (rc = link->postCompRecv()) )
    {
      fprintf(stderr, "%s:\n  Failed to post CQ buffers: %d\n",
              __PRETTY_FUNCTION__, rc);
    }

    printf("Inbound link with MonReq ID %d connected\n", link->id());
  }

  return 0;
}

int Teb::configure(const EbParams& prms,
                   Trigger*        object,
                   unsigned        prescale)
{
  EbAppBase::configure(prms);

  _trigger    = object;
  _prescale   = prescale - 1;
  _wrtCounter = _prescale;        // Reset prescale counter

  return 0;
}

void Teb::run()
{
  pinThread(pthread_self(), _prms.core[0]);

  //_trimmed       = 0;
  _eventCount    = 0;
  _batchCount    = 0;
  _writeCount    = 0;
  _monitorCount  = 0;
  _prescaleCount = 0;

  while (true)
  {
    int rc;
    if (!lRunning)
    {
      if (checkEQ() == -FI_ENOTCONN)  break;
    }

    if ( (rc = EbAppBase::process()) < 0)
    {
      if (checkEQ() == -FI_ENOTCONN)  break;
    }
  }

  for (auto it = _mrqLinks.begin(); it != _mrqLinks.end(); ++it)
  {
    _mrqTransport.shutdown(*it);
  }
  _mrqLinks.clear();
  _mrqTransport.shutdown();

  for (auto it = _l3Links.begin(); it != _l3Links.end(); ++it)
  {
    _l3Transport.shutdown(*it);
  }
  _l3Links.clear();

  EbAppBase::shutdown();

  _batMan.dump();
  _batMan.shutdown();

  _id = -1;
  _rcvrs.fill(0);
}

void Teb::process(EbEvent* event)
{
  // Accumulate output datagrams (result) from the event builder into a batch
  // datagram.  When the batch completes, post it to the contributors.

  if (_verbose > 3)
  {
    static unsigned cnt = 0;
    printf("Teb::process event dump:\n");
    event->dump(++cnt);
  }
  ++_eventCount;

  const Dgram& dg = *event->creator();

  if (ImmData::rsp(ImmData::flg(event->parameter())) == ImmData::Response)
  {
    Batch*       batch = _batMan.fetch(dg);
    ResultDgram& rdg   = *new(batch->allocate()) ResultDgram(dg, dg.xtc.src.value());

    rdg.xtc.damage.increase(event->damage().value());

    // Accumulate the list of ctrbs to this batch
    batch->accumRcvrs(_receivers(dg));
    batch->accumRogs(dg);

    if (dg.seq.isEvent())
    {
      // Present event contributions to "user" code for building a result datagram
      _trigger->event(event->begin(), event->end(), rdg); // Consume

      unsigned line = 0;                // Revisit: For future expansion

      // Handle prescale
      if (!rdg.persist(line) && !_wrtCounter--)
      {
        _prescaleCount++;

        rdg.prescale(line, true);
        _wrtCounter = _prescale;
      }

      if (rdg.persist())  _writeCount++;
      if (rdg.monitor())
      {
        _monitorCount++;

        uint64_t data;
        int      rc = _mrqTransport.poll(&data);
        rdg.monBufNo((rc < 0) ? 0 : data);
        if ((rc > 0) && (rc = _mrqLinks[ImmData::src(data)]->postCompRecv()) )
        {
          fprintf(stderr, "%s:\n  Failed to post CQ buffers: %d\n",
                  __PRETTY_FUNCTION__, rc);
        }
      }
    }

    if (_verbose > 2) // || rdg.monitor())
    {
      uint64_t  pid = rdg.seq.pulseId().value();
      unsigned  idx = Batch::batchNum(pid);
      unsigned  ctl = rdg.seq.pulseId().control();
      size_t    sz  = sizeof(rdg) + rdg.xtc.sizeofPayload();
      unsigned  src = rdg.xtc.src.value();
      unsigned  env = rdg.env;
      printf("TEB processed                result  [%5d] @ "
             "%16p, ctl %02x, pid %014lx, sz %6zd, src %2d, env %08x, res [%08x, %08x]\n",
             idx, &rdg, ctl, pid, sz, src, env, rdg.persist(), rdg.monitor());
    }
  }

  _tryPost(dg);
}

void Teb::_tryPost(const Dgram& dg)
{
  const auto pid   = dg.seq.pulseId().value();
  const auto idx   = Batch::batchNum(pid);
  auto       cur   = _batMan.batch(idx);
  bool       flush = !(dg.seq.isEvent() || (dg.seq.service() == TransitionId::SlowUpdate));

  for (auto it = _batchList.cbegin(); it != _batchList.cend(); )
  {
    auto batch = *it;
    auto rogs  = batch->rogsRem(dg);    // Take down RoG bits

    if ((batch->expired(pid) && !rogs) || flush)
    {
      batch->terminate();
      _post(*batch);

      it = _batchList.erase(it);
    }
    else
    {
      ++it;
    }
    if (batch == cur)  return;          // Insert only once
  }

  if (flush)
  {
    cur->terminate();
   _post(*cur);
  }
  else
    _batchList.insert(cur);
}

void Teb::_post(const Batch& batch)
{
  uint32_t    idx    = batch.index();
  uint64_t    data   = ImmData::value(ImmData::Buffer, _id, idx);
  size_t      extent = batch.extent();
  unsigned    offset = idx * _batMan.maxBatchSize();
  const void* buffer = batch.buffer();
  uint64_t    destns = batch.receivers(); // & ~_trimmed;

  ++_batchCount;

  while (destns)
  {
    unsigned  dst  = __builtin_ffsl(destns) - 1;
    EbLfLink* link = _l3Links[dst];

    destns &= ~(1ul << dst);

    if (_verbose)
    {
      uint64_t pid    = batch.id();
      void*    rmtAdx = (void*)link->rmtAdx(offset);
      printf("TEB posts          %9ld result  [%5d] @ "
             "%16p,         pid %014lx, sz %6zd, dst %2d @ %16p\n",
             _batchCount, idx, buffer, pid, extent, dst, rmtAdx);
    }

    int rc;
    if ( (rc = link->post(buffer, extent, offset, data)) < 0)
    {
      if (rc != -FI_ETIMEDOUT)  break;  // Revisit: Right thing to do?

      // If we were to trim, here's how to do it.  For now, we don't.
      //static unsigned retries = 0;
      //trim(dst);
      //if (retries++ == 5)  { _trimmed |= 1ul << dst; retries = 0; }
      //printf("%s:  link->post() to %d returned %d, trimmed = %016lx\n",
      //       __PRETTY_FUNCTION__, dst, rc, _trimmed);
    }
  }

  // Revisit: The following deallocation constitutes a race with the posts to
  // the transport above as the batch's memory cannot be allowed to be reused
  // for a subsequent batch before the transmit completes.  Waiting for
  // completion here would impact performance.  Since there are many batches,
  // and only one is active at a time, a previous batch will have completed
  // transmitting before the next one starts (or the subsequent transmit won't
  // start), thus making it safe to "pre-delete" it here.
  _batMan.release(&batch);
}

uint64_t Teb::_receivers(const Dgram& ctrb) const
{
  // This method is called when the event is processed, which happens when the
  // event builder has built the event.  The supplied contribution contains
  // information from the L1 trigger that identifies which readout groups were
  // involved.  This routine can thus look up the list of receivers expecting
  // results from the event for each of the readout groups and logically OR
  // them together to provide the overall receiver list.  The list of receivers
  // in each readout group desiring event results is provided at configuration
  // time.

  uint64_t receivers = 0;
  unsigned groups    = ctrb.readoutGroups();

  while (groups)
  {
    unsigned group = __builtin_ffs(groups) - 1;
    groups &= ~(1 << group);

    receivers |= _rcvrs[group];
  }
  return receivers;
}


class TebApp : public CollectionApp
{
public:
  TebApp(const std::string& collSrv, EbParams&, std::shared_ptr<MetricExporter>&);
  virtual ~TebApp();
public:                                 // For CollectionApp
  json connectionInfo() override;
  void handleConnect(const json& msg) override;
  void handleDisconnect(const json& msg) override;
  void handlePhase1(const json& msg) override;
  void handleReset(const json& msg) override;
private:
  int  _connect(const json& msg);
  int  _configure(const json& msg);
  int  _parseConnectionParams(const json& msg);
  void _buildContract(const Document& top);
private:
  EbParams&                  _prms;
  Teb                        _teb;
  std::thread                _appThread;
  json                       _connectMsg;
  Trg::Factory<Trg::Trigger> _factory;
};

TebApp::TebApp(const std::string&               collSrv,
               EbParams&                        prms,
               std::shared_ptr<MetricExporter>& exporter) :
  CollectionApp(collSrv, prms.partition, "teb", prms.alias),
  _prms        (prms),
  _teb         (prms, exporter)
{
  Py_Initialize();
}

TebApp::~TebApp()
{
  Py_Finalize();
}

json TebApp::connectionInfo()
{
  // Allow the default NIC choice to be overridden
  std::string ip = _prms.ifAddr.empty() ? getNicIp() : _prms.ifAddr;
  json body = {{"connect_info", {{"nic_ip", ip}}}};
  return body;
}

int TebApp::_connect(const json& msg)
{
  int rc = _parseConnectionParams(msg["body"]);
  if (rc)  return rc;

  rc = _teb.connect(_prms);
  if (rc)  return rc;

  lRunning = 1;

  _appThread = std::thread(&Teb::run, std::ref(_teb));

  return 0;
}

void TebApp::handleConnect(const json& msg)
{
  json body = json({});
  int  rc   = _connect(msg);
  if (rc)
  {
    std::string errorMsg = "Failed to connect";
    body["err_info"] = errorMsg;
    fprintf(stderr, "%s:\n  %s\n", __PRETTY_FUNCTION__, errorMsg.c_str());
  }

  // Save a copy of the json so we can use it to connect to
  // the config database on configure
  _connectMsg = msg;

  // Reply to collection with transition status
  reply(createMsg("connect", msg["header"]["msg_id"], getId(), body));
}

void TebApp::_buildContract(const Document& top)
{
  const json& body = _connectMsg["body"];

  for (auto it : body["drp"].items())
  {
    unsigned    drpId   = it.value()["drp_id"];
    std::string alias   = it.value()["proc_info"]["alias"];
    size_t      found   = alias.rfind('_');
    std::string detName = alias.substr(0, found);

    auto group = unsigned(it.value()["det_info"]["readout"]);

    if (top.HasMember(detName.c_str()))
      _prms.contractors[group] |= 1ul << drpId;
  }
}

int TebApp::_configure(const json& msg)
{
  int rc = 0;
  const std::string configAlias(msg["body"]["config_alias"]);
  const std::string detName("tmoTrigger");
  Document          top;
  if (Pds::Trg::fetchDocument(_connectMsg.dump(), configAlias, detName, top))
  {
    fprintf(stderr, "%s:\n  Document '%s' not found in ConfigDb\n",
            __PRETTY_FUNCTION__, detName.c_str());
    return -1;
  }

  _buildContract(top);

  const std::string symbol("create_consumer");
  Trigger* trigger = _factory.create(top, detName, symbol);
  if (!trigger)
  {
    fprintf(stderr, "%s:\n  Failed to create Trigger\n",
            __PRETTY_FUNCTION__);
    return -1;
  }

  if (trigger->configure(_connectMsg, top))
  {
    fprintf(stderr, "%s:\n  Failed to configure Trigger\n",
            __PRETTY_FUNCTION__);
    return -1;
  }

# define _FETCH(key, item)                                               \
  if (top.HasMember(key))  item = top[key].GetUint();                    \
  else { fprintf(stderr, "%s:\n  Key '%s' not found in Document %s\n",   \
                 __PRETTY_FUNCTION__, key, detName.c_str());  rc = -1; }

  unsigned prescale;  _FETCH("prescale", prescale);

# undef _FETCH

  _teb.configure(_prms, trigger, prescale);

  return rc;
}

void TebApp::handlePhase1(const json& msg)
{
  json        body = json({});
  std::string key  = msg["header"]["key"];

  if (key == "configure")
  {
    int rc = _configure(msg);
    if (rc)
    {
      std::string errorMsg = "Phase 1 error: ";
      errorMsg += "Failed to set up Trigger";
      body["err_info"] = errorMsg;
      fprintf(stderr, "%s:\n  %s\n", __PRETTY_FUNCTION__, errorMsg.c_str());
    }
  }

  // Reply to collection with transition status
  reply(createMsg(key, msg["header"]["msg_id"], getId(), body));
}

void TebApp::handleDisconnect(const json& msg)
{
  lRunning = 0;

  if (_appThread.joinable())  _appThread.join();

  // Reply to collection with transition status
  json body = json({});
  reply(createMsg("disconnect", msg["header"]["msg_id"], getId(), body));
}

void TebApp::handleReset(const json& msg)
{
  if (_appThread.joinable())  _appThread.join();
}

static void _printGroups(unsigned groups, const u64arr_t& array)
{
  while (groups)
  {
    unsigned group = __builtin_ffs(groups) - 1;
    groups &= ~(1 << group);

    printf("%d: 0x%016lx  ", group, array[group]);
  }
  printf("\n");
}

int TebApp::_parseConnectionParams(const json& body)
{
  const unsigned numPorts    = MAX_DRPS + MAX_TEBS + MAX_TEBS + MAX_MEBS;
  const unsigned tebPortBase = TEB_PORT_BASE + numPorts * _prms.partition;
  const unsigned drpPortBase = DRP_PORT_BASE + numPorts * _prms.partition;
  const unsigned mrqPortBase = MRQ_PORT_BASE + numPorts * _prms.partition;

  std::string id = std::to_string(getId());
  printf("%s: id %zu 0x%08zx '%s'\n", __PRETTY_FUNCTION__, getId(), getId(), id.c_str());
  _prms.id = body["teb"][id]["teb_id"];
  if (_prms.id >= MAX_TEBS)
  {
    fprintf(stderr, "TEB ID %d is out of range 0 - %d\n", _prms.id, MAX_TEBS - 1);
    return 1;
  }

  _prms.ifAddr  = body["teb"][id]["connect_info"]["nic_ip"];
  _prms.ebPort  = std::to_string(tebPortBase + _prms.id);
  _prms.mrqPort = std::to_string(mrqPortBase + _prms.id);

  _prms.contributors = 0;
  _prms.addrs.clear();
  _prms.ports.clear();

  _prms.contractors.fill(0);            // Filled in during Configure
  _prms.receivers.fill(0);

  uint16_t groups = 0;
  if (body.find("drp") == body.end())
  {
    fprintf(stderr, "Missing required DRP specs\n");
    return 1;
  }

  for (auto it : body["drp"].items())
  {
    unsigned    drpId   = it.value()["drp_id"];
    std::string address = it.value()["connect_info"]["nic_ip"];
    if (drpId > MAX_DRPS - 1)
    {
      fprintf(stderr, "DRP ID %d is out of range 0 - %d\n", drpId, MAX_DRPS - 1);
      return 1;
    }
    _prms.contributors |= 1ul << drpId;
    _prms.addrs.push_back(address);
    _prms.ports.push_back(std::string(std::to_string(drpPortBase + drpId)));

    auto group = unsigned(it.value()["det_info"]["readout"]);
    if (group > NUM_READOUT_GROUPS - 1)
    {
      fprintf(stderr, "Readout group %d is out of range 0 - %d\n", group, NUM_READOUT_GROUPS - 1);
      return 1;
    }
    _prms.receivers[group]  |= 1ul << drpId; // All contributors receive results
    groups |= 1 << group;
  }
  auto& vec =_prms.maxTrSize;
  vec.resize(body["drp"].size());
  std::fill(vec.begin(), vec.end(), max_contrib_size); // Same for all contributors

  _prms.numMrqs = 0;
  if (body.find("meb") != body.end())
  {
    for (auto it : body["meb"].items())
    {
      _prms.numMrqs++;
    }
  }

  printf("\nParameters of TEB ID %d:\n",                      _prms.id);
  printf("  Thread core numbers:         %d, %d\n",           _prms.core[0], _prms.core[1]);
  printf("  Partition:                   %d\n",               _prms.partition);
  printf("  Bit list of contributors:  0x%016lx, cnt: %zd\n", _prms.contributors,
                                                              std::bitset<64>(_prms.contributors).count());
  printf("  Readout group contractors:   ");                  _printGroups(groups, _prms.contractors);
  printf("  Readout group receivers:     ");                  _printGroups(groups, _prms.receivers);
  printf("  Number of MEB requestors:    %d\n",               _prms.numMrqs);
  printf("  Batch duration:            0x%014lx = %ld uS\n",  BATCH_DURATION, BATCH_DURATION);
  printf("  Batch pool depth:            %d\n",               MAX_BATCHES);
  printf("  Max # of entries / batch:    %d\n",               MAX_ENTRIES);
  printf("  # of contrib. buffers:       %d\n",               MAX_LATENCY);
  printf("  Max result     Dgram size:   %zd\n",              _prms.maxResultSize);
  printf("  Max transition Dgram size:   %zd\n",              _prms.maxTrSize[0]);
  printf("\n");
  printf("  TEB port range: %d - %d\n", tebPortBase, tebPortBase + MAX_TEBS - 1);
  printf("  DRP port range: %d - %d\n", drpPortBase, drpPortBase + MAX_DRPS - 1);
  printf("  MRQ port range: %d - %d\n", mrqPortBase, mrqPortBase + MAX_MEBS - 1);
  printf("\n");

  return 0;
}


static void usage(char *name, char *desc, const EbParams& prms)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s [OPTIONS]\n", name);

  if (desc)
    fprintf(stderr, "\n%s\n", desc);

  fprintf(stderr, "\nOptions:\n");

  fprintf(stderr, " %-22s %s (default: %s)\n",        "-A <interface_addr>",
          "IP address of the interface to use",       "libfabric's 'best' choice");

  fprintf(stderr, " %-22s %s (required)\n",           "-C <address>",
          "Collection server");
  fprintf(stderr, " %-22s %s (required)\n",           "-p <partition number>",
          "Partition number");
  fprintf(stderr, " %-22s %s\n",                      "-P <instrument>",
          "Instrument name");
  fprintf(stderr, " %-22s %s (required)\n",           "-u <alias>",
          "Alias for teb process");
  fprintf(stderr, " %-22s %s (default: %d)\n",        "-1 <core>",
          "Core number for pinning App thread to",    core_0);
  fprintf(stderr, " %-22s %s (default: %d)\n",        "-2 <core>",
          "Core number for pinning other threads to", core_1);

  fprintf(stderr, " %-22s %s\n", "-v", "enable debugging output (repeat for increased detail)");
  fprintf(stderr, " %-22s %s\n", "-h", "display this help output");
}


int main(int argc, char **argv)
{
  const unsigned NO_PARTITION = unsigned(-1u);
  int            op           = 0;
  char *         instrument   = NULL;
  std::string    collSrv;
  EbParams       prms { /* .ifAddr        = */ { }, // Network interface to use
                        /* .ebPort        = */ { },
                        /* .mrqPort       = */ { },
                        /* .partition     = */ NO_PARTITION,
                        /* .alias         = */ { }, // Unique name passed on cmd line
                        /* .id            = */ -1u,
                        /* .contributors  = */ 0,   // DRPs
                        /* .addrs         = */ { }, // Result dst addr served by Ctrbs
                        /* .ports         = */ { }, // Result dst port served by Ctrbs
                        /* .maxTrSize     = */ { }, // Filled in at connect
                        /* .maxResultSize = */ max_result_size,
                        /* .numMrqs       = */ 0,   // Number of Mon requestors
                        /* .core          = */ { core_0, core_1 },
                        /* .verbose       = */ 0,
                        /* .contractors   = */ 0,
                        /* .receivers     = */ 0 };

  while ((op = getopt(argc, argv, "C:p:A:1:2:u:P:h?v")) != -1)
  {
    switch (op)
    {
      case 'C':  collSrv         = optarg;             break;
      case 'p':  prms.partition  = std::stoi(optarg);  break;
      case 'P':  instrument      = optarg;             break;
      case 'A':  prms.ifAddr     = optarg;             break;
      case '1':  prms.core[0]    = atoi(optarg);       break;
      case '2':  prms.core[1]    = atoi(optarg);       break;
      case 'u':  prms.alias      = optarg;             break;
      case 'v':  ++prms.verbose;                       break;
      case '?':
      case 'h':
      default:
        usage(argv[0], (char*)"Trigger Event Builder application", prms);
        return 1;
    }
  }
  switch (prms.verbose)
  {
    case 0:  logging::init(instrument, LOG_WARNING);  break;
    case 1:  logging::init(instrument, LOG_INFO);     break;
    default: logging::init(instrument, LOG_DEBUG);    break;
  }
  logging::info("logging configured");
  if (!instrument)
  {
    logging::warning("Missing '-P <instrument>' parameter");
  }
  if (prms.partition == NO_PARTITION)
  {
    fprintf(stderr, "Missing '%s' parameter\n", "-p <Partition number>");
    return 1;
  }
  if (collSrv.empty())
  {
    fprintf(stderr, "Missing '%s' parameter\n", "-C <Collection server>");
    return 1;
  }
  if (prms.alias.empty()) {
    fprintf(stderr, "Missing '%s' parameter\n", "-u <Alias>");
    return 1;
  }

  struct sigaction sigAction;

  sigAction.sa_handler = sigHandler;
  sigAction.sa_flags   = SA_RESTART;
  sigemptyset(&sigAction.sa_mask);
  if (sigaction(SIGINT, &sigAction, &lIntAction) > 0)
    fprintf(stderr, "Failed to set up ^C handler\n");

  // Event builder sorts contributions into a time ordered list
  // Then calls the user's process() with complete events to build the result datagram
  // Post completed result batches to the outlet

  // Iterate over contributions in the batch
  // Event build them according to their trigger group

  std::unique_ptr<prometheus::Exposer> exposer;
  try {
      exposer = std::make_unique<prometheus::Exposer>("0.0.0.0:9200", "/metrics", 1);
  } catch(const std::runtime_error& e) {
      std::cout<<__PRETTY_FUNCTION__<<": error opening monitoring port.  Monitoring disabled.\n";
      std::cout<<e.what()<<std::endl;
  }

  auto exporter = std::make_shared<MetricExporter>();

  TebApp app(collSrv, prms, exporter);

  if (exposer) {
      exposer->RegisterCollectable(exporter);
  }

  try
  {
    app.run();
  }
  catch (std::exception& e)
  {
    fprintf(stderr, "%s\n", e.what());
  }

  app.handleReset(json({}));

  return 0;
}
