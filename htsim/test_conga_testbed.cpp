#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "aprx-fairqueue.h"
#include "fairqueue.h"
#include "priorityqueue.h"
#include "stoc-fairqueue.h"
#include "flow-generator.h"
#include "pipe.h"
#include "test.h"
#include "workloads.h"
#include "network.h"
#include "tcp.h"
#include <vector>
#include <sstream>
#include <iostream>
#include <cstdlib>

namespace conga {

    // tesdbed configuration
    const int N_CORE = 12;
    const int N_LEAF = 24;
    const int N_SERVER = 32;   // Per leaf

    const uint64_t LEAF_BUFFER = 512000;
    const uint64_t CORE_BUFFER = 1024000;
    const uint64_t ENDH_BUFFER = 8192000;

    const uint64_t LEAF_SPEED = 10000000000; // 10gbps
    const uint64_t CORE_SPEED = 40000000000; // 40gbps

    
}

using namespace std;
using namespace conga;

void
conga_testbed(const ArgList &args, Logfile &logfile)
{
    EventList& eventlist = EventList::Get();
    double duration = 10.0;
    double utilization = 0.75;
    uint32_t flow_size = BDP_BYTES;
    
    // Parse arguments with defaults
    parseDouble(args, "duration", duration);
    parseDouble(args, "utilization", utilization);
    parseInt(args, "flow_size", flow_size);

    #include <sstream>

    // Create network components
    vector<PacketSink*> core_switches(N_CORE);
    vector<PacketSink*> leaf_switches(N_LEAF);
    vector<vector<PacketSink*>> servers(N_LEAF, vector<PacketSink*>(N_SERVER));
    
    // Initialize core switches with queues
    for (int i = 0; i < N_CORE; i++) {
        std::stringstream ss;
        ss << "core_" << i;
        core_switches[i] = new FairQueue(CORE_SPEED, CORE_BUFFER, new QueueLoggerSampling(timeFromMs(10)));
    }

    // Initialize leaf switches with queues
    for (int i = 0; i < N_LEAF; i++) {
        std::stringstream ss;
        ss << "leaf_" << i;
        leaf_switches[i] = new FairQueue(LEAF_SPEED, LEAF_BUFFER, new QueueLoggerSampling(timeFromMs(10)));
    }

    // Initialize servers and connect to leaf switches
    for (int i = 0; i < N_LEAF; i++) {
        for (int j = 0; j < N_SERVER; j++) {
            std::stringstream ss;
            ss << "server_" << i << "_" << j;
            servers[i][j] = new FairQueue(LEAF_SPEED, ENDH_BUFFER, new QueueLoggerSampling(timeFromMs(10)));
            
            // Connect server to leaf switch (bidirectional)
            Pipe* up_pipe = new Pipe(timeFromUs(10));
            Pipe* down_pipe = new Pipe(timeFromUs(10));
            
            // Set up routes between server and leaf switch
            route_t* up_route = new route_t();
            up_route->push_back(up_pipe);
            up_route->push_back(leaf_switches[i]);
            
            route_t* down_route = new route_t();
            down_route->push_back(down_pipe);
            down_route->push_back(servers[i][j]);
        }
    }

    // Connect leaf switches to core switches (full mesh)
    for (int i = 0; i < N_LEAF; i++) {
        for (int j = 0; j < N_CORE; j++) {
            std::stringstream ss;
            ss << "leaf_" << i << "_core_" << j;
            
            // Bidirectional links between leaf and core
            Pipe* up_pipe = new Pipe(timeFromUs(10));
            Pipe* down_pipe = new Pipe(timeFromUs(10));
            
            // Set up routes between leaf and core switches
            route_t* up_route = new route_t();
            up_route->push_back(up_pipe);
            up_route->push_back(core_switches[j]);
            
            route_t* down_route = new route_t();
            down_route->push_back(down_pipe);
            down_route->push_back(leaf_switches[i]);
        }
    }

    // Create route generator function
    auto route_gen = [&](route_t*& fwd, route_t*& rev, uint32_t& src_id, uint32_t& dst_id) {
        src_id = rand() % (N_LEAF * N_SERVER);
        do {
            dst_id = rand() % (N_LEAF * N_SERVER);
        } while (dst_id == src_id);
        
        uint32_t src_leaf = src_id / N_SERVER;
        uint32_t dst_leaf = dst_id / N_SERVER;
        uint32_t core_switch = rand() % N_CORE;
        
        fwd = new route_t();
        rev = new route_t();
        
        // Forward path: server -> leaf -> core -> leaf -> server
        fwd->push_back(servers[src_leaf][src_id % N_SERVER]);
        fwd->push_back(leaf_switches[src_leaf]);
        fwd->push_back(core_switches[core_switch]);
        fwd->push_back(leaf_switches[dst_leaf]);
        fwd->push_back(servers[dst_leaf][dst_id % N_SERVER]);
        
        // Reverse path
        rev->push_back(servers[dst_leaf][dst_id % N_SERVER]);
        rev->push_back(leaf_switches[dst_leaf]);
        rev->push_back(core_switches[core_switch]);
        rev->push_back(leaf_switches[src_leaf]);
        rev->push_back(servers[src_leaf][src_id % N_SERVER]);
    };

    // Create flow generator with TCP endpoints
    FlowGenerator* fg = new FlowGenerator(
        DataSource::TCP,  // Use TCP endpoints
        route_gen,               // Route generator function
        LEAF_SPEED,             // Flow rate (limited by leaf switch speed)
        flow_size,              // Average flow size
        Workloads::PARETO       // Flow size distribution
    );
    
    // Set time limits for flow generation
    fg->setTimeLimits(0, timeFromSec(duration));
    
    // Configure endhost queues
    fg->setEndhostQueue(LEAF_SPEED, ENDH_BUFFER);
    
    // Start the simulation
    eventlist.sourceIsPendingRel(*fg, 0);
    
    // Set simulation end time
    eventlist.setEndtime(timeFromSec(duration));
    
    // Run simulation
    while (eventlist.doNextEvent()) {
        if (eventlist.now() % timeFromSec(1) == 0) {
            std::cout << "Progress: " << timeAsMs(eventlist.now()) << "ms" << std::endl;
        }
    }
}
