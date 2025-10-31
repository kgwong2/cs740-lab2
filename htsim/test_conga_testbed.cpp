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

    // Namespace-scope containers and route generator declaration so the
    // FlowGenerator can reference a function pointer (no captures).
    std::vector<Queue*> core_switches;
    std::vector<Queue*> leaf_switches;
    std::vector<std::vector<Queue*>> servers;

    void generateRoute(route_t*& fwd, route_t*& rev, uint32_t& src_id, uint32_t& dst_id);
}

using namespace std;
using namespace conga;

void
conga_testbed(const ArgList &args, Logfile &logfile)
{
    double duration = 10.0;
    double utilization = 0.75;
    uint32_t AvgFlowSize = 100000;    // Average flow size.
    
    // Parse arguments with defaults
    parseDouble(args, "duration", duration);
    parseDouble(args, "utilization", utilization);
    parseInt(args, "flowsize", AvgFlowSize);

    // Create TCP logger
    TcpLoggerSimple* logTcp = new TcpLoggerSimple();
    logfile.addLogger(*logTcp);

    // Namespace-level network components (defined below) - resize for this test
    core_switches.assign(N_CORE, nullptr);
    leaf_switches.assign(N_LEAF, nullptr);
    servers.assign(N_LEAF, vector<Queue*>(N_SERVER));
    
    // Initialize core switches with queues
    for (int i = 0; i < N_CORE; i++) {
        std::stringstream ss;
        ss << "core_" << i;
        QueueLoggerSampling* qs = new QueueLoggerSampling(timeFromMs(10));
        logfile.addLogger(*qs);
        core_switches[i] = new FairQueue(CORE_SPEED, CORE_BUFFER, qs);
        core_switches[i]->setName(ss.str());
        logfile.writeName(*core_switches[i]);
    }

    // Initialize leaf switches with queues
    for (int i = 0; i < N_LEAF; i++) {
        std::stringstream ss;
        ss << "leaf_" << i;
        QueueLoggerSampling* qs = new QueueLoggerSampling(timeFromMs(10));
        logfile.addLogger(*qs);
        leaf_switches[i] = new FairQueue(LEAF_SPEED, LEAF_BUFFER, qs);
        leaf_switches[i]->setName(ss.str());
        logfile.writeName(*leaf_switches[i]);
    }

    // Initialize servers and connect to leaf switches
    for (int i = 0; i < N_LEAF; i++) {
        for (int j = 0; j < N_SERVER; j++) {
            std::stringstream ss;
            ss << "server_" << i << "_" << j;
            QueueLoggerSampling* qs = new QueueLoggerSampling(timeFromMs(10));
            logfile.addLogger(*qs);
            servers[i][j] = new FairQueue(LEAF_SPEED, ENDH_BUFFER, qs);
            servers[i][j]->setName(ss.str());
            logfile.writeName(*servers[i][j]);
            
            // Connect server to leaf switch (bidirectional)
            Pipe* up_pipe = new Pipe(timeFromUs(10));
            std::stringstream pss1;
            pss1 << "pipe_server_" << i << "_" << j << "_to_leaf_" << i;
            up_pipe->setName(pss1.str());
            logfile.writeName(*up_pipe);

            Pipe* down_pipe = new Pipe(timeFromUs(10));
            std::stringstream pss2;
            pss2 << "pipe_leaf_" << i << "_to_server_" << i << "_" << j;
            down_pipe->setName(pss2.str());
            logfile.writeName(*down_pipe);
            
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
            std::stringstream pss1;
            pss1 << "pipe_leaf_" << i << "_to_core_" << j;
            up_pipe->setName(pss1.str());
            logfile.writeName(*up_pipe);

            Pipe* down_pipe = new Pipe(timeFromUs(10));
            std::stringstream pss2;
            pss2 << "pipe_core_" << j << "_to_leaf_" << i;
            down_pipe->setName(pss2.str());
            logfile.writeName(*down_pipe);
            
            // Set up routes between leaf and core switches
            route_t* up_route = new route_t();
            up_route->push_back(up_pipe);
            up_route->push_back(core_switches[j]);
            
            route_t* down_route = new route_t();
            down_route->push_back(down_pipe);
            down_route->push_back(leaf_switches[i]);
        }
    }

    // Create flow generator with TCP endpoints
    FlowGenerator* fg = new FlowGenerator(
        DataSource::TCP,      // Use TCP endpoints
        generateRoute,        // Route generator function (namespace-level)
        LEAF_SPEED * utilization, // Flow rate (limited by leaf switch speed)
        AvgFlowSize,            // Average flow size
        Workloads::PARETO     // Flow size distribution
    );
    
    
    // Configure endhost queues
    fg->setEndhostQueue(LEAF_SPEED, ENDH_BUFFER);
    // Set time limits for flow generation
    fg->setTimeLimits(0, timeFromSec(duration) - 1);
    
    EventList::Get().setEndtime(timeFromSec(duration));
}

// Implementation of the namespace-level route generator.
void
conga::generateRoute(route_t*& fwd, route_t*& rev, uint32_t& src_id, uint32_t& dst_id)
{
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
}
