#ifndef Eb_EbEvent_hh
#define Eb_EbEvent_hh

#include <stdint.h>

#include "psdaq/service/LinkedList.hh"
#include "psdaq/service/Pool.hh"
#include "xtcdata/xtc/Dgram.hh"


namespace Pds {

  class GenericPool;

  namespace Eb {

    class EventBuilder;

    class EbEvent : public LinkedList<EbEvent>
    {
    public:
      PoolDeclare;
    public:
      EbEvent(uint64_t              contract,
              EbEvent*              after,
              const XtcData::Dgram* ctrb,
              unsigned              prm);
      ~EbEvent();
    public:
      unsigned        parameter() const;
      uint64_t        sequence()  const;
      size_t          size()      const;
      uint64_t        remaining() const;
      uint64_t        contract()  const;
      XtcData::Damage damage()    const;
      void            damage(XtcData::Damage::Value);
    public:
      const XtcData::Dgram*  const  creator() const;
      const XtcData::Dgram*  const* begin()   const;
      const XtcData::Dgram** const  end()     const;
    public:
      void     dump(int number);
    private:
      friend class EventBuilder;
    private:
      EbEvent* _add(const XtcData::Dgram*);
      void     _insert(const XtcData::Dgram*);
      bool     _alive();
    private:
      size_t                 _size;            // Total contribution size (in bytes)
      uint64_t               _remaining;       // List of clients which have contributed
      const uint64_t         _contract;        // -> potential list of contributors
      int                    _living;          // Aging counter
      unsigned               _prm;             // An application level free parameter
      XtcData::Damage        _damage;          // Accumulate damage about this event
      const XtcData::Dgram** _last;            // Pointer into the contributions array
      const XtcData::Dgram*  _contributions[]; // Array of contributions
    };
  };
};

/*
** ++
**
**    Give EventBuilder user interface access to the free parameter.
**
** --
*/

inline unsigned Pds::Eb::EbEvent::parameter() const
{
  return _prm;
}

/*
** ++
**
**    Give EventBuilder user interface access to the sequence number.
**
** --
*/

inline uint64_t Pds::Eb::EbEvent::sequence() const
{
  return creator()->seq.pulseId().value();
}

/*
** ++
**
**   Return the size (in bytes) of the event's payload
**
** --
*/

inline size_t Pds::Eb::EbEvent::size() const
{
  return _size;
}

/*
** ++
**
**   Returns a bit-list which specifies the slots expected to contribute
**   to this event. If a bit is SET at a particular offset, the slot
**   corresponding to that offset is an expected contributor.
**
** --
*/

inline uint64_t Pds::Eb::EbEvent::contract() const
{
  return _contract;
}

/*
** ++
**
**   Returns a bit-list which specifies the slots remaining to contribute
**   to this event. If a bit is SET at a particular offset, the slot
**   corresponding to that offset is remaining as a contributor. Consequently,
**   a "complete" event will return a value of zero (0).
**
** --
*/

inline uint64_t Pds::Eb::EbEvent::remaining() const
{
  return _remaining;
}

/*
** ++
**
**   Method to retrieve the event's damage value.
**
** --
*/

inline XtcData::Damage Pds::Eb::EbEvent::damage() const
{
  return _damage;
}

/*
** ++
**
**   Method to increase the event's damage value.
**
** --
*/

inline void Pds::Eb::EbEvent::damage(XtcData::Damage::Value value)
{
  _damage.increase(value);
}

/*
** ++
**
**    An event comes into existence with the arrival of its first
**    expected contributor. This function will a pointer to the packet
**    corresponding to its first contributor.
**
** --
*/

inline const XtcData::Dgram* const Pds::Eb::EbEvent::creator() const
{
  return _contributions[0];
}

inline const XtcData::Dgram* const* Pds::Eb::EbEvent::begin() const
{
  return _contributions;
}

inline const XtcData::Dgram** const Pds::Eb::EbEvent::end() const
{
  return _last;
}

/*
** ++
**
**   In principle an event could sit on the pending queue forever,
**   waiting for its contract to complete. To handle this scenario,
**   the EB times-out the oldest event on the pending queue.
**   This is accomplished by periodically calling this function,
**   which counts down a counter whose initial value is specified
**   at construction time. When the counter drops to less than
**   or equal to zero the event is expired. Note: the arrival of
**   any contributor to this event will RESET the counter.
**
** --
*/

inline bool Pds::Eb::EbEvent::_alive()
{
  return --_living > 0;
}

#endif
