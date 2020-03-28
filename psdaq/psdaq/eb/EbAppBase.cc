#include "EbAppBase.hh"

#include "Endpoint.hh"
#include "EbEvent.hh"

#include "EbLfServer.hh"

#include "utilities.hh"

#include "xtcdata/xtc/Dgram.hh"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <assert.h>
#include <climits>
#include <bitset>
#include <atomic>
#include <thread>

using namespace XtcData;
using namespace Pds;
using namespace Pds::Fabrics;
using namespace Pds::Eb;


EbAppBase::EbAppBase(const EbParams& prms,
                     const uint64_t  duration,
                     const unsigned  maxEntries,
                     const unsigned  maxBuffers) :
  EventBuilder (maxBuffers + TransitionId::NumberOf,
                maxEntries,
                8 * sizeof(prms.contributors), //Revisit: std::bitset<64>(prms.contributors).count(),
                duration,
                prms.verbose),
  _transport   (prms.verbose),
  _maxEntries  (maxEntries),
  _maxBuffers  (maxBuffers),
  //_dummy       (Level::Fragment),
  _verbose     (prms.verbose),
  _bufferCnt   (0),
  _fixupCnt    (0),
  _region      (nullptr),
  _contributors(0),
  _id          (-1)
{
}

int EbAppBase::configure(const EbParams& prms)
{
  unsigned nCtrbs = std::bitset<64>(prms.contributors).count();

  _links.resize(nCtrbs);
  _trRegSize.resize(nCtrbs);
  _maxTrSize.resize(nCtrbs);
  _maxBufSize.resize(nCtrbs);
  _id           = prms.id;
  _contributors = prms.contributors;
  _contract     = prms.contractors;
  _bufferCnt    = 0;
  _fixupCnt     = 0;

  std::vector<size_t> regSizes(nCtrbs);
  size_t              sumSize = 0;

  int rc;
  if ( (rc = _transport.initialize(prms.ifAddr, prms.ebPort, nCtrbs)) )
  {
    fprintf(stderr, "%s:\n  Failed to initialize Ctrb EbLfServer\n",
            __PRETTY_FUNCTION__);
    return rc;
  }

  for (unsigned i = 0; i < _links.size(); ++i)
  {
    EbLfSvrLink*   link;
    const unsigned tmo(120000);         // Milliseconds
    if ( (rc = _transport.connect(&link, _id, tmo)) )
    {
      fprintf(stderr, "%s:\n  Error connecting to a Ctrb\n",
              __PRETTY_FUNCTION__);
      return rc;
    }
    unsigned rmtId = link->id();
    _links[rmtId] = link;

    if (_verbose)  printf("Inbound link with Ctrb ID %d connected\n", rmtId);

    size_t regSize;
    if ( (rc = link->prepare(&regSize)) )
    {
      fprintf(stderr, "%s:\n  Failed to prepare link with Ctrb ID %d\n",
              __PRETTY_FUNCTION__, rmtId);
      return rc;
    }
    _maxTrSize[rmtId]  = prms.maxTrSize[rmtId];
    _trRegSize[rmtId]  = roundUpSize(TransitionId::NumberOf * _maxTrSize[rmtId]);
    _maxBufSize[rmtId] = regSize / _maxBuffers;
    regSize           += _trRegSize[rmtId];  // Ctrbs don't have a transition space
    regSizes[rmtId]    = regSize;
    sumSize           += regSize;
  }

  _region = allocRegion(sumSize);
  if (!_region)
  {
    fprintf(stderr, "%s:\n  No memory found for Input MR of size %zd\n",
            __PRETTY_FUNCTION__, sumSize);
    return ENOMEM;
  }

  // Note that this loop can't be combined with the one above due to the exchange protocol
  char* region = reinterpret_cast<char*>(_region);
  for (unsigned rmtId = 0; rmtId < _links.size(); ++rmtId)
  {
    EbLfSvrLink* link = _links[rmtId];
    if ( (rc = link->setupMr(region, regSizes[rmtId])) )
    {
      fprintf(stderr, "%s:\n  Failed to set up Input MR for Ctrb ID %d, "
              "%p:%p, size %zd\n", __PRETTY_FUNCTION__,
              rmtId, region, region + regSizes[rmtId], regSizes[rmtId]);
      if (_region)  free(_region);
      _region = nullptr;
      return rc;
    }

    if (link->postCompRecv())
    {
      fprintf(stderr, "%s:\n  Failed to post CQ buffers for Ctrb ID %d\n",
              __PRETTY_FUNCTION__, rmtId);
    }

    region += regSizes[rmtId];

    printf("Inbound link with Ctrb ID %d connected and configured\n", rmtId);
  }

  return 0;
}

void EbAppBase::shutdown()
{
  EventBuilder::dump(0);
  EventBuilder::clear();

  for (auto it = _links.begin(); it != _links.end(); ++it)
  {
    _transport.disconnect(*it);
  }
  _links.clear();
  _transport.shutdown();

  if (_region)  free(_region);
  _region = nullptr;

  _trRegSize.clear();
  _maxTrSize.clear();
  _maxBufSize.clear();
  _contributors = 0;
  _id           = -1;
  _contract.fill(0);
}

int EbAppBase::process()
{
  int rc;

  // Pend for an input datagram and pass it to the event builder
  uint64_t  data;
  const int tmo = 100;       // milliseconds - Also see EbEvent.cc::MaxTimeouts
  if ( (rc = _transport.pend(&data, tmo)) < 0)
  {
    if (rc == -FI_ETIMEDOUT)  EventBuilder::expired();
    return rc;
  }

  ++_bufferCnt;

  unsigned     flg = ImmData::flg(data);
  unsigned     src = ImmData::src(data);
  unsigned     idx = ImmData::idx(data);
  EbLfSvrLink* lnk = _links[src];
  size_t       ofs = (ImmData::buf(flg) == ImmData::Buffer)
                   ? (_trRegSize[src] + idx * _maxBufSize[src])
                   : (idx * _maxTrSize[src]);
  const EbDgram* idg = static_cast<EbDgram*>(lnk->lclAdx(ofs));
  if ( (rc = lnk->postCompRecv()) )
  {
    fprintf(stderr, "%s:\n  Failed to post CQ buffers: %d\n",
            __PRETTY_FUNCTION__, rc);
  }

  if (_verbose >= VL_BATCH)
  {
    unsigned    env = idg->env;
    uint64_t    pid = idg->pulseId();
    unsigned    ctl = idg->control();
    const char* knd = TransitionId::name(idg->service());
    printf("EbAp rcvd %9ld %15s[%5d]   @ "
           "%16p, ctl %02x, pid %014lx, env %08x,            src %2d, data %08lx, ext %4d\n",
           _bufferCnt, knd, idx, idg, ctl, pid, env, lnk->id(), data, idg->xtc.extent);
  }

  // Tr space bufSize value is irrelevant since maxEntries will be 1 for that case
  unsigned maxEntries = (ImmData::buf(flg) == ImmData::Buffer) ? _maxEntries : 1;
  size_t   bufSize    = _maxBufSize[src] / maxEntries;

  EventBuilder::process(idg, bufSize, maxEntries, data);

  return 0;
}

void EbAppBase::trim(unsigned dst)
{
  for (unsigned group = 0; group < _contract.size(); ++group)
  {
    _contract[group]  &= ~(1 << dst);
    //_receivers[group] &= ~(1 << dst);
  }
}

uint64_t EbAppBase::contract(const EbDgram* ctrb) const
{
  // This method is called when the event is created, which happens when the event
  // builder recognizes the first contribution.  This contribution contains
  // information from the L1 trigger that identifies which readout groups are
  // involved.  This routine can thus look up the expected list of contributors
  // (the contract) to the event for each of the readout groups and logically OR
  // them together to provide the overall contract.  The list of contributors
  // participating in each readout group is provided at configuration time.

  uint64_t contract = 0;
  unsigned groups   = ctrb->readoutGroups();

  while (groups)
  {
    unsigned group = __builtin_ffs(groups) - 1;
    groups &= ~(1 << group);

    contract |= _contract[group];
  }
  return contract;
}

void EbAppBase::fixup(EbEvent* event, unsigned srcId)
{
  ++_fixupCnt;

  if (_verbose >= VL_EVENT)
  {
    fprintf(stderr, "%s:\n  Fixup event %014lx, size %zu, for source %d\n",
            __PRETTY_FUNCTION__, event->sequence(), event->size(), srcId);
  }

  event->damage(Damage::DroppedContribution);
}
