#include <getopt.h>
#include <sstream>
#include <iostream>
#include <Python.h>
#include "drp.hh"
#include "PGPDetectorApp.hh"
#include "psdaq/service/SysLog.hh"
using logging = Pds::SysLog;

void get_kwargs(Drp::Parameters& para, const std::string& kwargs_str) {
    std::istringstream ss(kwargs_str);
    std::string kwarg;
    std::string::size_type pos = 0;
    while (getline(ss, kwarg, ',')) {
        pos = kwarg.find("=", pos);
        if (!pos) {
            throw "drp.cc error: keyword argument with no equal sign: "+kwargs_str;
        }
        std::string key = kwarg.substr(0,pos);
        std::string value = kwarg.substr(pos+1,kwarg.length());
        //cout << kwarg << " " << key << " " << value << endl;
        para.kwargs[key] = value;
    }
}

int main(int argc, char* argv[])
{
    Drp::Parameters para;
    int c;
    para.detSegment = 0;
    std::string kwargs_str;
    bool lVerbose = false;
    char *instrument = NULL;
    while((c = getopt(argc, argv, "p:o:l:D:C:d:u:k:P:v")) != EOF) {
        switch(c) {
            case 'p':
                para.partition = std::stoi(optarg);
                break;
            case 'o':
                para.outputDir = optarg;
                break;
            case 'l':
                para.laneMask = std::stoul(optarg, nullptr, 16);
                break;
            case 'D':
                para.detectorType = optarg;
                break;
            case 'u':
                para.alias = optarg;
                break;
            case 'C':
                para.collectionHost = optarg;
                break;
            case 'd':
                para.device = optarg;
                break;
            case 'k':
                kwargs_str = std::string(optarg);
                break;
            case 'P':
                instrument = optarg;
                break;
            case 'v':
                lVerbose = true;
                break;
            default:
                exit(1);
        }
    }
    if (instrument) {
        logging::init(instrument, lVerbose ? LOG_INFO : LOG_WARNING);
        logging::info("logging configured");
    } else {
        logging::error("-P: instrument is needed to configure logging");
        // logging default configuration does not print to stderr
        fprintf(stderr, "-P: instrument is needed to configure logging\n");
    }

    // Check required parameters
    if (para.device.empty()) {
        logging::critical("-d: device is mandatory");
        exit(1);
    }
    if (para.alias.empty()) {
        logging::critical("-u: alias is mandatory");
        exit(1);
    }
    // Alias must be of form <detName>_<detSegment>
    size_t found = para.alias.rfind('_');
    if ((found == std::string::npos) || !isdigit(para.alias.back())) {
        logging::critical("-u: alias must have _N suffix");
        exit(1);
    }
    para.detName = para.alias.substr(0, found);
    para.detSegment = std::stoi(para.alias.substr(found+1, para.alias.size()));

    get_kwargs(para, kwargs_str);

    para.nworkers = 10;
    para.batchSize = 32; // Must be a power of 2
    Py_Initialize(); // for use by configuration
    Drp::PGPDetectorApp app(para);
    app.run();
    app.shutdown();
    Py_Finalize(); // for use by configuration
    std::cout<<"end of main drp\n";
}
