#include "TebContributor.hh"

#include "Endpoint.hh"
#include "EbLfClient.hh"
#include "Batch.hh"
#include "EbCtrbInBase.hh"

#include "utilities.hh"

#include "psalg/utils/SysLog.hh"
#include "psdaq/service/MetricExporter.hh"
#include "xtcdata/xtc/Dgram.hh"

#ifdef NDEBUG
//#undef NDEBUG
#endif

#include <cassert>
#include <string.h>
#include <cassert>
#include <cstdint>
#include <bitset>
#include <string>
#include <thread>

using namespace XtcData;
using namespace Pds::Eb;
using logging  = psalg::SysLog;


TebContributor::TebContributor(const TebCtrbParams&                   prms,
                               const std::shared_ptr<MetricExporter>& exporter) :
  _prms        (prms),
  _batMan      (prms.maxInputSize),
  _transport   (prms.verbose),
  _links       (),
  _id          (-1),
  _numEbs      (0),
  _pending     (MAX_BATCHES),
  _batch       (nullptr),
  _eventCount  (0),
  _batchCount  (0)
{
  std::map<std::string, std::string> labels{{"instrument", prms.instrument},{"partition", std::to_string(prms.partition)}};
  exporter->add("TCtbO_EvtRt",  labels, MetricType::Rate,    [&](){ return _eventCount;             });
  exporter->add("TCtbO_EvtCt",  labels, MetricType::Counter, [&](){ return _eventCount;             });
  exporter->add("TCtbO_BtAlCt", labels, MetricType::Counter, [&](){ return _batMan.batchAllocCnt(); });
  exporter->add("TCtbO_BtFrCt", labels, MetricType::Counter, [&](){ return _batMan.batchFreeCnt();  });
  exporter->add("TCtbO_BtWtg",  labels, MetricType::Gauge,   [&](){ return _batMan.batchWaiting();  });
  exporter->add("TCtbO_BatCt",  labels, MetricType::Counter, [&](){ return _batchCount;             });
  exporter->add("TCtbO_TxPdg",  labels, MetricType::Gauge,   [&](){ return _transport.pending();    });
  exporter->add("TCtbO_InFlt",  labels, MetricType::Gauge,   [&](){ return _pending.count();        });
}

int TebContributor::configure(const TebCtrbParams& prms)
{
  _id      = prms.id;
  _numEbs  = std::bitset<64>(prms.builders).count();
  _pending.clear();

  void*  region  = _batMan.batchRegion();     // Local space for Trs is in the batch region
  size_t regSize = _batMan.batchRegionSize(); // No need to add Tr space size here

  _links.resize(prms.addrs.size());
  for (unsigned i = 0; i < _links.size(); ++i)
  {
    int            rc;
    const char*    addr = prms.addrs[i].c_str();
    const char*    port = prms.ports[i].c_str();
    EbLfCltLink*   link;
    const unsigned tmo(120000);         // Milliseconds
    if ( (rc = _transport.connect(&link, addr, port, _id, tmo)) )
    {
      logging::error("%s:\n  Error connecting to TEB at %s:%s\n",
                     __PRETTY_FUNCTION__, addr, port);
      return rc;
    }
    unsigned rmtId = link->id();
    _links[rmtId] = link;

    logging::debug("Outbound link with TEB ID %d connected\n", rmtId);

    if ( (rc = link->prepare(region, regSize)) )
    {
      logging::error("%s:\n  Failed to prepare link with TEB ID %d\n",
                     __PRETTY_FUNCTION__, rmtId);
      return rc;
    }

    logging::info("Outbound link with TEB ID %d connected and configured\n", rmtId);
  }

  return 0;
}

void TebContributor::startup(EbCtrbInBase& in)
{
  _batch      = nullptr;
  _eventCount = 0;
  _batchCount = 0;
  _running.store(true, std::memory_order_release);
  _rcvrThread = std::thread([&] { in.receiver(*this, _running); });
}

void TebContributor::shutdown()
{
  _running.store(false, std::memory_order_release);

  if (_rcvrThread.joinable())  _rcvrThread.join();

  _batMan.stop();
  _batMan.dump();
  _batMan.shutdown();

  for (auto it = _links.begin(); it != _links.end(); ++it)
  {
    _transport.disconnect(*it);
  }
  _links.clear();

  _id = -1;
}

void* TebContributor::allocate(const TimingHeader& hdr, const void* appPrm)
{
  auto pid   = hdr.pulseId();
  auto batch = _batMan.fetch(pid);

  if (_prms.verbose >= VL_EVENT)
  {
    const char* svc = TransitionId::name(hdr.service());
    unsigned    idx = batch ? batch->index() : -1;
    unsigned    ctl = hdr.control();
    unsigned    env = hdr.env;
    printf("Batching  %15s  dg  [%8d]     @ "
           "%16p, ctl %02x, pid %014lx, env %08x,                    prm %p\n",
           svc, idx, &hdr, ctl, pid, env, appPrm);
  }

  if (batch)                            // Null when terminating
  {
    ++_eventCount;                      // Only count events handled

    batch->store(pid, appPrm);          // Save the appPrm for _every_ event

    return batch->allocate();
  }
  return batch;
}

void TebContributor::process(const EbDgram* dgram)
{
  const auto pid        = dgram->pulseId();
  const auto idx        = Batch::batchNum(pid);
  auto       cur        = _batMan.batch(idx);
  bool       flush      = !(dgram->isEvent() ||
                            (dgram->service() == TransitionId::SlowUpdate));
  bool       contractor = dgram->readoutGroups() & _prms.contractor;

  if ((_batch && _batch->expired(pid)) || flush)
  {
    if (_batch)
    {
      _batch->terminate();   // Avoid race: terminate before adding it to the list
      _pending.push(_batch); // Add to the list only when complete, even if empty
      if (contractor)  _post(_batch);
    }

    if (flush && (_batch != cur))
    {
      cur->terminate();      // Avoid race: terminate before adding it to the list
      _pending.push(cur);    // Add to the list only when complete, even if empty
      if (contractor)  _post(cur);
      cur = nullptr;
    }

    _batch = cur;
  }
  else if (!_batch)  _batch = cur;

  // Revisit: Sending the transitions to the non-selected EBs may not be useful
  // since they are not sent with a payload.  The TEB can't really do anything
  // with them since they don't contain any information for the TEB to react to.
  // The only exceptions to this might be that the value of the env may some day
  // be used for something and the presence of a transition is used to flush
  // whatever Result batch is in progress, if that can happen.  For now we leave
  // this code in until there's a compelling reason to remove it.  If it is to
  // be removed, consider removing the additional RDMA memory region space where
  // the transitions land as well, if this doesn't conflict with the MEB's needs.
  if (!dgram->isEvent())
  {
    if (contractor)  _post(dgram);
  }
}

void TebContributor::_post(const Batch* batch) const
{
  if (!batch->empty())
  {
    uint32_t     idx    = batch->index();
    unsigned     dst    = idx % _numEbs;
    EbLfCltLink* link   = _links[dst];
    uint32_t     data   = ImmData::value(ImmData::Buffer | ImmData::Response, _id, idx);
    size_t       extent = batch->extent();
    unsigned     offset = idx * _batMan.maxBatchSize();
    const void*  buffer = batch->buffer();

    if (_prms.verbose >= VL_BATCH)
    {
      uint64_t pid    = batch->id();
      void*    rmtAdx = (void*)link->rmtAdx(offset);
      printf("CtrbOut posts %9ld    batch[%8d]    @ "
             "%16p,         pid %014lx,               sz %6zd, TEB %2d @ %16p, data %08x\n",
             _batchCount, idx, buffer, pid, extent, dst, rmtAdx, data);
    }

    if (link->post(buffer, extent, offset, data) < 0)  return;
  }

  ++_batchCount;                        // Count all batches handled
}

void TebContributor::_post(const EbDgram* dgram) const
{
  // Non-events datagrams are sent to all TEBs, except the one that got the
  // batch containing it.  These EBs won't generate responses.

  uint64_t pid    = dgram->pulseId();
  uint32_t idx    = Batch::batchNum(pid);
  unsigned dst    = idx % _numEbs;
  unsigned tr     = dgram->service();
  uint32_t data   = ImmData::value(ImmData::Transition | ImmData::NoResponse, _id, tr);
  size_t   extent = sizeof(*dgram);  assert(dgram->xtc.sizeofPayload() == 0);
  unsigned offset = _batMan.batchRegionSize() + tr * sizeof(*dgram);

  for (auto it = _links.begin(); it != _links.end(); ++it)
  {
    EbLfCltLink* link = *it;
    if (link->id() != dst)        // Batch posted above included this non-event
    {
      if (_prms.verbose >= VL_BATCH)
      {
        unsigned    env    = dgram->env;
        unsigned    ctl    = dgram->control();
        const char* svc    = TransitionId::name(dgram->service());
        void*       rmtAdx = (void*)link->rmtAdx(offset);
        printf("CtrbOut posts    %15s              @ "
               "%16p, ctl %02x, pid %014lx, env %08x, sz %6zd, TEB %2d @ %16p, data %08x\n",
               svc, dgram, ctl, pid, env, extent, link->id(), rmtAdx, data);
      }

      link->post(dgram, extent, offset, data); // Not a batch
    }
  }
}
