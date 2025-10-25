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
    EventList &eventlist = EventList::Get();  // Get singleton instance
    
    // Parse arguments with helper functions
    double duration = 10.0;
    parseDouble(args, "duration", duration);
    eventlist.setEndtime(timeFromSec(duration));

    // Create and initialize loggers through constructor
    QueueLoggerSimple qLogger;
    qLogger = QueueLoggerSimple();
    qLogger._logfile = &logfile;
    
    TcpLoggerSimple tcpLogger;
    tcpLogger = TcpLoggerSimple();
    tcpLogger._logfile = &logfile;
    
    // Set up workload generator
    uint32_t avgFlowSize = 100000;  // 100KB default
    parseInt(args, "flowsize", avgFlowSize);
    Workloads::FlowDist flowDist = Workloads::ENTERPRISE;  // Use CONGA enterprise workload
    Workloads workloads(avgFlowSize, flowDist);

    // Create topology matrices
    vector<vector<Queue*>> leaf_queues(N_LEAF);
    vector<vector<Queue*>> core_queues(N_CORE);
    vector<vector<Pipe*>> leaf_pipes(N_LEAF);
    vector<vector<Pipe*>> core_pipes(N_CORE);

    // Initialize queues and pipes
    for (int i = 0; i < N_LEAF; i++) {
        leaf_queues[i].resize(N_CORE);
        leaf_pipes[i].resize(N_CORE);
        
        for (int j = 0; j < N_CORE; j++) {
            // Leaf to core
            leaf_queues[i][j] = new FairQueue(LEAF_SPEED, LEAF_BUFFER, &qLogger);
            leaf_pipes[i][j] = new Pipe(timeFromUs(10));
            leaf_queues[i][j]->setName("LeafToCore_" + to_string(i) + "_" + to_string(j));
            leaf_pipes[i][j]->setName("LeafPipe_" + to_string(i) + "_" + to_string(j));
            
            if (i == 0) {
                core_queues[j].resize(N_LEAF);
                core_pipes[j].resize(N_LEAF);
            }
            
            // Core to leaf
            core_queues[j][i] = new FairQueue(CORE_SPEED, CORE_BUFFER, &qLogger);
            core_pipes[j][i] = new Pipe(timeFromUs(10)); // 10us delay
            core_queues[j][i]->setName("CoreToLeaf_" + to_string(j) + "_" + to_string(i));
            core_pipes[j][i]->setName("CorePipe_" + to_string(j) + "_" + to_string(i));
        }
    }

    // Connect topology
    for (int i = 0; i < N_LEAF; i++) {
        for (int j = 0; j < N_CORE; j++) {
            // Network paths will be established through routes
            // Each PacketSink knows how to forward to the next hop
        }
    }

    // Create endpoints
    vector<TcpSrc*> srcs;
    vector<TcpSink*> sinks;

    for (int leaf = 0; leaf < N_LEAF; leaf++) {
        for (int srv = 0; srv < N_SERVER; srv++) {
            // Create endpoints with traffic logging
            TcpSrc* src = new TcpSrc(&tcpLogger, nullptr);
            TcpSink* sink = new TcpSink();

            src->setName("Src_" + to_string(leaf) + "_" + to_string(srv));
            sink->setName("Sink_" + to_string(leaf) + "_" + to_string(srv));

            srcs.push_back(src);
            sinks.push_back(sink);

            // Round-robin traffic over core switches
            int core = srv % N_CORE;

            // Create route data structures for this flow
            route_t* routeFwd = new route_t();
            route_t* routeRev = new route_t();

            // Create endpoint queue for source
            Queue* srcQueue = new FairQueue(LEAF_SPEED, ENDH_BUFFER, &qLogger);
            srcQueue->setName("SrcQueue_" + to_string(leaf) + "_" + to_string(srv));

            // Forward path: src -> srcQueue -> leaf -> core -> sink
            routeFwd->push_back(srcQueue);
            routeFwd->push_back(leaf_queues[leaf][core]);
            routeFwd->push_back(leaf_pipes[leaf][core]);
            routeFwd->push_back(core_queues[core][leaf]);
            routeFwd->push_back(core_pipes[core][leaf]);
            routeFwd->push_back(sink);

            // Reverse path for ACKs: sink -> core -> leaf -> src
            routeRev->push_back(core_queues[core][leaf]);
            routeRev->push_back(core_pipes[core][leaf]);
            routeRev->push_back(leaf_queues[leaf][core]);
            routeRev->push_back(leaf_pipes[leaf][core]);
            routeRev->push_back(src);

            // Set up source and route generator
            auto routeGen = [routeFwd, routeRev](route_t*& fwd, route_t*& rev,
                                              uint32_t& /*src_id*/, uint32_t& /*dst_id*/) {
                fwd = routeFwd;
                rev = routeRev;
            };
        }
    }

        }
    }

    // Set up shared flow generator
    double load = 0.7;  // 70% network load by default
    parseDouble(args, "load", load);
    
    // Convert load to flow rate based on link speed
    linkspeed_bps flowRate = (linkspeed_bps)(LEAF_SPEED * load);
    
    // Create route generator for dynamic routing
    route_gen_t globalRouteGen = [&leaf_queues, &core_queues, &leaf_pipes, &core_pipes, &sinks, &srcs, &qLogger]
        (route_t*& fwd, route_t*& rev, uint32_t& src_id, uint32_t& dst_id) {
        int src_leaf = src_id / N_SERVER;
        int dst_leaf = dst_id / N_SERVER;
        int core = src_id % N_CORE;  // Round-robin over core switches
        
        // Create endpoint queue for source
        Queue* srcQueue = new FairQueue(LEAF_SPEED, ENDH_BUFFER, &qLogger);
        srcQueue->setName("SrcQueue_" + to_string(src_id));

        fwd = new route_t();
        rev = new route_t();

        // Build forward path
        fwd->push_back(srcQueue);
        fwd->push_back(leaf_queues[src_leaf][core]);
        fwd->push_back(leaf_pipes[src_leaf][core]);
        fwd->push_back(core_queues[core][dst_leaf]);
        fwd->push_back(core_pipes[core][dst_leaf]);
        fwd->push_back(sinks[dst_id]);

        // Build reverse path
        rev->push_back(core_queues[core][src_leaf]);
        rev->push_back(core_pipes[core][src_leaf]);
        rev->push_back(leaf_queues[src_leaf][core]);
        rev->push_back(leaf_pipes[src_leaf][core]);
        rev->push_back(srcs[src_id]);
    };
    
            }
    }

    // Set up flow generator configuration
    double load = 0.7;  // 70% network load by default
    if (args.find("load") != args.end()) {
        load = stod(args.at("load"));
    }
    
    // Convert load to flow rate based on link speed
    linkspeed_bps flowRate = (linkspeed_bps)(LEAF_SPEED * load);
    
    // Create route generator function that will track topology state
    route_gen_t routeGen = [&leaf_queues, &core_queues, &leaf_pipes, &core_pipes, &sinks, &srcs, &qLogger]
        (route_t*& fwd, route_t*& rev, uint32_t& src_id, uint32_t& dst_id) {
        // Calculate path indices
        int src_leaf = src_id / N_SERVER;
        int dst_leaf = dst_id / N_SERVER;
        int core = src_id % N_CORE;  // Round-robin over core switches
        
        // Create endpoint queue
        Queue* srcQueue = new FairQueue(LEAF_SPEED, ENDH_BUFFER, &qLogger);
        srcQueue->setName("SrcQueue_" + to_string(src_id));

        fwd = new route_t();
        rev = new route_t();

        // Forward path
        fwd->push_back(srcQueue);
        fwd->push_back(leaf_queues[src_leaf][core]);
        fwd->push_back(leaf_pipes[src_leaf][core]);
        fwd->push_back(core_queues[core][dst_leaf]);
        fwd->push_back(core_pipes[core][dst_leaf]);
        fwd->push_back(sinks[dst_id]);

        // Reverse path
        rev->push_back(core_queues[core][src_leaf]);
        rev->push_back(core_pipes[core][src_leaf]);
        rev->push_back(leaf_queues[src_leaf][core]);
        rev->push_back(leaf_pipes[src_leaf][core]);
        rev->push_back(srcs[src_id]);
    };
    
    // Create main flow generator
    FlowGenerator* flowGen = new FlowGenerator(DataSource::TCP, routeGen, 
                                             flowRate, avgFlowSize, 
                                             Workloads::ENTERPRISE);
    flowGen->setName("CONGA_FlowGenerator");

    // Run simulation with pause prompts
    cout << "Starting CONGA simulation..." << endl;
    int eventCount = 0;
    const int PAUSE_INTERVAL = 1000;  // Pause every 1000 events
    
    while (true) {
        // Process a batch of events
        for (int i = 0; i < PAUSE_INTERVAL; i++) {
            if (!EventList::Get().doNextEvent()) {
                cout << "\nSimulation complete after " << eventCount << " events." << endl;
                return;
            }
            eventCount++;
        }
        
        // Show progress and prompt
        cout << "\nProcessed " << eventCount << " events. Continue to iterate? (y/n): ";
        string response;
        cin >> response;
        if (response != "y" && response != "Y") {
            cout << "Simulation stopped by user after " << eventCount << " events." << endl;
            break;
        }
    }

    // Run simulation with user prompts
    int eventCount = 0;
    const int PAUSE_INTERVAL = 1000;  // Pause every 1000 events
    
    while (true) {
        // Process a batch of events
        for (int i = 0; i < PAUSE_INTERVAL; i++) {
            if (!EventList::Get().doNextEvent()) {
                return;  // No more events
            }
            eventCount++;
        }
        
        // Prompt user to continue
        cout << "\nProcessed " << eventCount << " events. Continue? (y/n): ";
        string response;
        cin >> response;
        if (response != "y" && response != "Y") {
            break;
        }
    }
}
