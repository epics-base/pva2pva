
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <map>

#if !defined(_WIN32)
#include <signal.h>
#define USE_SIGNAL
#endif

#include <epicsStdlib.h>
#include <epicsGetopt.h>
#include <iocsh.h>

#include <pv/json.h>

#include <pv/pvAccess.h>
#include <pv/clientFactory.h>
#include <pv/configuration.h>
#include <pv/serverContext.h>

#define epicsExportSharedSymbols
#include "server.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

namespace {

pvd::StructureConstPtr schema(pvd::getFieldCreate()->createFieldBuilder()
                              ->add("version", pvd::pvUInt)
                              ->addNestedStructureArray("clients")
                                 ->add("name", pvd::pvString)
                                 ->add("provider", pvd::pvString)
                                 ->add("addrlist", pvd::pvString)
                                 ->add("autoaddrlist", pvd::pvBoolean)
                                 ->add("serverport", pvd::pvUShort)
                                 ->add("bcastport", pvd::pvUShort)
                              ->endNested()
                              ->addNestedStructureArray("servers")
                                 ->add("name", pvd::pvString)
                                 ->add("client", pvd::pvString)
                                 ->add("interface", pvd::pvString)
                                 ->add("serverport", pvd::pvUShort)
                                 ->add("bcastport", pvd::pvUShort)
                                 ->add("control_prefix", pvd::pvString)
                              ->endNested()
                              ->createStructure());


void usage(const char *me)
{
    std::cerr<<"Usage: "<<me<<" [-vhiIC] <config file>\n";
}

void getargs(ServerConfig& arg, int argc, char *argv[])
{
    int opt;
    bool checkonly = false;

    while( (opt=getopt(argc, argv, "vhiIC"))!=-1)
    {
        switch(opt) {
        case 'v':
            arg.debug++;
            break;
        case 'I':
            arg.interactive = true;
            break;
        case 'i':
            arg.interactive = false;
            break;
        case 'C':
            checkonly = true;
            break;
        default:
            std::cerr<<"Unknown argument -"<<char(opt)<<"\n";
        case 'h':
            usage(argv[0]);
            exit(1);
        }
    }

    if(optind!=argc-1) {
        std::cerr<<"Exactly one positional argument expected\n";
        usage(argv[0]);
        exit(1);
    }

    arg.conf = pvd::getPVDataCreate()->createPVStructure(schema);
    std::ifstream strm(argv[optind]);
    pvd::parseJSON(strm, arg.conf);

    unsigned version = arg.conf->getSubFieldT<pvd::PVUInt>("version")->get();
    if(version==0) {
        std::cerr<<"Warning: config file missing \"version\" key.  Assuming 1\n";
    } else if(version!=1) {
        std::cerr<<"config file version mis-match. expect 1 found "<<version<<"\n";
        exit(1);
    }
    if(arg.conf->getSubFieldT<pvd::PVStructureArray>("clients")->view().empty()) {
        std::cerr<<"No clients configured\n";
        exit(1);
    }
    if(arg.conf->getSubFieldT<pvd::PVStructureArray>("servers")->view().empty()) {
        std::cerr<<"No servers configured\n";
        exit(1);
    }

    if(checkonly) {
        std::cerr<<"Config file OK\n";
        exit(0);
    }
}

GWServerChannelProvider::shared_pointer configure_client(const pvd::PVStructurePtr& conf)
{
    std::string provider(conf->getSubFieldT<pvd::PVString>("provider")->get());

    pva::Configuration::shared_pointer C(pva::ConfigurationBuilder()
                                         .add("EPICS_PVA_ADDR_LIST", conf->getSubFieldT<pvd::PVString>("addrlist")->get())
                                         .add("EPICS_PVA_AUTO_ADDR_LIST", conf->getSubFieldT<pvd::PVBoolean>("autoaddrlist")->get())
                                         .add("EPICS_PVA_SERVER_PORT", conf->getSubFieldT<pvd::PVScalar>("serverport")->getAs<pvd::uint16>())
                                         .add("EPICS_PVA_BROADCAST_PORT", conf->getSubFieldT<pvd::PVScalar>("bcastport")->getAs<pvd::uint16>())
                                         .push_map()
                                         .build());

    pva::ChannelProvider::shared_pointer base(pva::ChannelProviderRegistry::clients()->createProvider(provider, C));
    if(!base)
        throw std::runtime_error("Can't create ChannelProvider");

    GWServerChannelProvider::shared_pointer ret(new GWServerChannelProvider(base));
    return ret;
}

pva::ServerContext::shared_pointer configure_server(ServerConfig& arg, const pvd::PVStructurePtr& conf)
{
    pva::Configuration::shared_pointer C(pva::ConfigurationBuilder()
                                         .add("EPICS_PVAS_INTF_ADDR_LIST", conf->getSubFieldT<pvd::PVString>("interface")->get())
                                         .add("EPICS_PVAS_SERVER_PORT", conf->getSubFieldT<pvd::PVScalar>("serverport")->getAs<pvd::uint16>())
                                         .add("EPICS_PVAS_BROADCAST_PORT", conf->getSubFieldT<pvd::PVScalar>("bcastport")->getAs<pvd::uint16>())
                                         .push_map()
                                         .build());

    ServerConfig::clients_t::const_iterator it(arg.clients.find(conf->getSubFieldT<pvd::PVString>("client")->get()));
    if(it==arg.clients.end())
        throw std::runtime_error("Server references non-existant client");

    pva::ServerContext::shared_pointer ret(pva::ServerContext::create(pva::ServerContext::Config()
                                                                      .config(C)
                                                                      .provider(it->second)));
    return ret;
}

volatile int quit;
epicsEvent done;

#ifdef USE_SIGNAL
void sigdone(int num)
{
    (void)num;
    quit = 1;
    done.signal();
}
#endif

}// namespace

int main(int argc, char *argv[])
{
    try {
        ServerConfig arg;
        getargs(arg, argc, argv);

        pva::ClientFactory::start();

        pvd::PVStructureArray::const_svector arr;

        arr = arg.conf->getSubFieldT<pvd::PVStructureArray>("clients")->view();

        for(size_t i=0; i<arr.size(); i++) {
            if(!arr[i]) continue;
            const pvd::PVStructurePtr& client = arr[i];

            std::string name(client->getSubFieldT<pvd::PVString>("name")->get());
            if(name.empty())
                throw std::runtime_error("Client with empty name not allowed");

            ServerConfig::clients_t::const_iterator it(arg.clients.find(name));
            if(it!=arg.clients.end())
                throw std::runtime_error(std::string("Duplicate client name not allowed : ")+name);

            arg.clients[name] = configure_client(client);
        }

        arr = arg.conf->getSubFieldT<pvd::PVStructureArray>("servers")->view();

        for(size_t i=0; i<arr.size(); i++) {
            if(!arr[i]) continue;
            const pvd::PVStructurePtr& server = arr[i];

            std::string name(server->getSubFieldT<pvd::PVString>("name")->get());
            if(name.empty())
                throw std::runtime_error("Server with empty name not allowed");

            ServerConfig::servers_t::const_iterator it(arg.servers.find(name));
            if(it!=arg.servers.end())
                throw std::runtime_error(std::string("Duplicate server name not allowed : ")+name);

            arg.servers[name] = configure_server(arg, server);
        }

        int ret = 0;
        if(arg.interactive) {
            ret = iocsh(NULL);
        } else {
#ifdef USE_SIGNAL
            signal(SIGINT, sigdone);
            signal(SIGTERM, sigdone);
            signal(SIGQUIT, sigdone);
#endif

            while(!quit) {
                done.wait();
            }
        }

        return ret;
    }catch(std::exception& e){
        std::cerr<<"Fatal Error : "<<e.what()<<"\n";
        return 1;
    }
}
