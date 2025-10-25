# HTSim Project Guide for AI Agents

## Project Overview
HTSim is a high-performance network simulation framework written in C++. The project implements various network components and protocols for testing and analyzing network behavior.

## Core Architecture

### Key Components
- **Packet System**: Base class `Packet` in `network.h` defines the fundamental packet structure with flags, priorities, and routing capabilities
- **Network Elements**: 
  - `PacketFlow`: Manages packet lifecycle and traffic logging
  - `PacketSink`: Interface for components that can receive packets
  - `PacketDB`: Template class for efficient packet allocation/reuse

### Important Constants (`htsim.h`)
- MSS_BYTES: 1500 (Max segment size)
- BDP_BYTES: 12500 (Bandwidth-delay product)
- Time units: Uses picoseconds internally (simtime_picosec)
- Speed units: Uses bits per second (linkspeed_bps)

## Development Workflow

### Build System
```bash
# Build the simulator
make

# Clean build artifacts
make clean
```

The build system uses Clang++ with C++11 and generates dependencies automatically in `.d/` directory.

### Project Structure
- `*.h` files: Core interfaces and declarations
- `*.cpp` files: Implementations
- `data/`: Output directory for simulation results
- `.obj/`: Build artifacts
- `.d/`: Auto-generated dependencies

## Conventions and Patterns

### Memory Management
- Use `PacketDB` template for packet allocation/reuse instead of raw new/delete
- Virtual `free()` method in `Packet` class returns packets to the pool

### Time and Speed Conversions
Use provided conversion functions:
- Time: `timeFromSec()`, `timeFromMs()`, `timeAsUs()`, etc.
- Speed: `speedFromGbps()`, `speedFromMbps()`, `speedAsPktps()`, etc.

### Random Number Generation
Use provided distribution functions:
- `pareto(alpha, mean)`: Pareto distribution
- `exponential(lambda)`: Exponential distribution

## Extension Points
- Create new packet types by inheriting from `Packet`
- Implement new network components by inheriting from `PacketSink`
- Add new traffic patterns via `PacketFlow` subclasses

## How to run experiments (CLI)

HTSim is a single executable `htsim` that takes experiments and key/value args using `--arg=value` style. The entry point (`main.cpp`) expects at minimum `--expt=<n>` where `n` is one of:

- `1` single_link_simulation
- `2` conga_testbed
- `3` fat_tree_testbed

Example invocations (run from project root or `data/`):

```bash
# build first
make

# run a short single-link sim for 10s at 75% utilization (default)
./htsim --expt=1 --duration=10 --utilization=0.75

# run single-link with a 40Gbps link and custom buffer
./htsim --expt=1 --linkspeed=40000000000 --linkbuffer=1024000 --duration=20

# set RNG seed for reproducibility
./htsim --expt=1 --rngseed=42 --duration=5
```

Where to look for supported flags and defaults:
- `main.cpp` — arg parsing and `--expt` driver
- `test.h` — helper parsers (`parseInt`, `parseDouble`, etc.) and `run_experiment`
- `test_single_link.cpp` — concrete flags used by `single_link_simulation` (e.g. `duration`, `linkspeed`, `linkbuffer`, `utilization`, `queue`, `endhost`, `trace`, `afq*`)

Output and logs
- The default logfile base path is `data/htsim-log`. Use `--logfile=path` to change the base log filename. The simulator writes event logs and named object registrations via `Logfile`/`Loggers` (see `logfile.h` / `loggers.h`).

Files to inspect for common extension tasks
- `network.h` / `datapacket.h` — packet definitions and lifecycle
- `queue.h` / `fairqueue.h` / `aprx-fairqueue.h` / `stoc-fairqueue.h` — queue implementations and interesting AQM logic
- `tcp.h` / `tcp.cpp` — TCP source/sink implementations used by end-hosts
- `flow-generator.h` / `flow-generator.cpp` & `workloads.h` — traffic generation and distributions (pareto/uniform/enterprise)

Project-specific conventions
- Time is represented in picoseconds (`simtime_picosec`) — use the helper converters in `htsim.h` when creating or printing durations.
- Speeds are in bits-per-second (`linkspeed_bps`) and flows are often calculated by multiplying link speed by utilization.
- Packet reuse: many packet types use `PacketDB<T>` pools and rely on `Packet::free()` to return objects to the pool — avoid raw `delete` for packet types unless you inspect the packet's `free()` implementation.

Debugging and build notes
- The `Makefile` is written for `clang++` (C++11). If you prefer `g++`, you'll need to update `CXX` in the `Makefile`.
- Common gotcha: ensure headers that use fixed-width integer types include `<cstdint>` (we needed to add this to `loggertypes.h`).
- To reproduce the environment used during development, run builds inside WSL/Ubuntu where Clang is available.

Quick follow-ups you can ask me to do next
- Add CLI documentation with all supported flags (I can parse and list flags from all `test_*` files).
- Create a `README.md` with quick-start examples and expected outputs in `data/`.
- Add a GitHub Actions CI workflow that runs `make` on Ubuntu to detect compile regressions.

If any section needs more detail or you want me to auto-generate CLI docs from source, tell me which experiments to prioritize.