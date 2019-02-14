#pragma once

#include <thread>
//#include "Collector.hh"
#include "PGPReader.hh"
#include "psdaq/service/Collection.hh"
#include "psdaq/eb/TebContributor.hh"
#include "psdaq/eb/MebContributor.hh"
#include "psdaq/eb/EbCtrbInBase.hh"

#pragma pack(push,4)
class MyDgram : public XtcData::Dgram {
public:
    MyDgram(XtcData::Sequence& sequence, uint64_t val, unsigned contributor_id);
private:
    uint64_t _data;
};
#pragma pack(pop)

class EbReceiver : public Pds::Eb::EbCtrbInBase
{
public:
    EbReceiver(const Parameters& para, MemPool& pool, Pds::Eb::MebContributor* mon);
    virtual ~EbReceiver() {};
    void process(const XtcData::Dgram* result, const void* input) override;
private:
    MemPool& _pool;
    FILE* _xtcFile;
    Pds::Eb::MebContributor* _mon;
    unsigned nreceive;
};

struct Parameters;
struct MemPool;

class DrpApp : public CollectionApp
{
public:
    DrpApp(Parameters* para);
    void handleConnect(const json& msg) override;
    void handleConfigure(const json& msg);
    void handleReset(const json& msg) override;
private:
    void parseConnectionParams(const json& msg);
    void collector();

    Parameters* m_para;
    std::thread m_pgpThread;
    std::thread m_collectorThread;
    std::thread m_monitorThread;
    ZmqContext m_context;
    ZmqSocket m_inprocRecv;
    MemPool m_pool;
    std::unique_ptr<PGPReader> m_pgpReader;
    std::unique_ptr<Pds::Eb::TebContributor> m_ebContributor;
    std::unique_ptr<EbReceiver> m_ebRecv;
};