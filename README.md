# LibRpc Communication Framework

A lightweight, peer-to-peer RPC communication framework supporting both in-process and inter-process communication via UDP.

## Features

- **Peer-to-peer architecture**: All endpoints are equal nodes
- **Topic-based pub/sub**: Subscribe to specific topics within message groups
- **Multi-node support**: Multiple nodes can coexist in the same process
- **Dual transport**: 
  - In-process: Direct function calls (zero-copy)
  - Inter-process: UDP broadcast
- **Selective delivery**: Only subscribers receive relevant messages
- **Simple API**: 3 main operations (subscribe, unsubscribe, broadcast)
- **Late-joining support**: New nodes automatically discover existing subscriptions
- **Self-healing**: Subscription registry maintains consistency across nodes

## Architecture

### Design Principles

1. **Node-centric**: Each endpoint is a Node instance
2. **Topic filtering**: Messages delivered only to matching subscribers
3. **Process-aware**: Automatic routing between in-process and inter-process nodes
4. **Non-blocking**: UDP transport runs in background thread

### Communication Flow

```
Node1 (Process A)           Node2 (Process A)           Node3 (Process B)
    |                            |                            |
    | subscribe("sensor", ["temp"])  subscribe("sensor", ["temp", "pressure"])
    |                            |                            |
    |                            |                            |
    |------- broadcast("sensor", "temp", "25C") ------------->|
    |                            |                            |
    | (in-process delivery)      |                            |
    |--------------------------->|                            |
    |                            |                            |
    |                       (UDP broadcast)                   |
    |-------------------------------------------------------->|
```

## API Reference

### Node Interface

```cpp
class Node {
public:
    using Property = std::string;
    using Callback = std::function<void(const Property& msg_group, 
                                       const Property& topic, 
                                       const uint8_t* payload, 
                                       size_t size)>;

    // Broadcast message to all subscribers
    virtual Error broadcast(const Property& msg_group, 
                          const Property& topic, 
                          const Property& payload) = 0;

    // Subscribe to topics
    virtual Error subscribe(const Property& msg_group, 
                          const std::vector<Property>& topics, 
                          const Callback& callback) = 0;

    // Unsubscribe from topics
    virtual Error unsubscribe(const Property& msg_group, 
                            const std::vector<Property>& topics) = 0;
};
```

### Factory Functions

```cpp
// Create a new node
std::shared_ptr<Node> createNode(const std::string& node_id = "",
                                 bool use_udp = true,
                                 uint16_t udp_port = 0);

// Get default singleton node
std::shared_ptr<Node> communicationInterface();
```

## Usage Examples

### Example 1: Basic Subscribe and Broadcast

```cpp
#include "Node.h"

// Create node
auto node = librpc::createNode("sensor_node");

// Subscribe to temperature topic
node->subscribe("sensor", {"temperature"}, 
    [](const auto& group, const auto& topic, const auto* payload, size_t size) {
        std::cout << "Temperature: " 
                  << std::string((const char*)payload, size) << std::endl;
    });

// Broadcast temperature data
node->broadcast("sensor", "temperature", "25.5C");
```

### Example 2: Multiple Topics

```cpp
auto node = librpc::createNode("multi_sensor");

// Subscribe to multiple topics
node->subscribe("sensor", {"temperature", "pressure", "humidity"}, 
    [](const auto& group, const auto& topic, const auto* payload, size_t size) {
        std::cout << topic << ": " 
                  << std::string((const char*)payload, size) << std::endl;
    });
```

### Example 3: Multiple Nodes in Same Process

```cpp
// Node 1: Temperature publisher
auto temp_node = librpc::createNode("temp_node");

// Node 2: Temperature subscriber
auto display_node = librpc::createNode("display_node");
display_node->subscribe("sensor", {"temperature"}, 
    [](const auto& group, const auto& topic, const auto* data, size_t size) {
        // Handle temperature
    });

// Publish temperature (display_node will receive in-process)
temp_node->broadcast("sensor", "temperature", "26.0C");
```

### Example 4: Inter-Process Communication

Process A:
```cpp
// Create node with specific UDP port
auto nodeA = librpc::createNode("nodeA", true, 47121);
nodeA->subscribe("ipc", {"commands"}, 
    [](const auto& group, const auto& topic, const auto* data, size_t size) {
        // Handle commands from other processes
    });
```

Process B:
```cpp
// Create node with different UDP port
auto nodeB = librpc::createNode("nodeB", true, 47122);
// Broadcast will be received by Process A via UDP
nodeB->broadcast("ipc", "commands", "START");
```

### Example 5: Selective Subscription

```cpp
auto node1 = librpc::createNode("node1");
auto node2 = librpc::createNode("node2");

// Node1 subscribes only to temperature
node1->subscribe("sensor", {"temperature"}, callback1);

// Node2 subscribes only to pressure
node2->subscribe("sensor", {"pressure"}, callback2);

// Only node1 receives this
node1->broadcast("sensor", "temperature", "25C");

// Only node2 receives this
node2->broadcast("sensor", "pressure", "1013hPa");
```

## Build Instructions

### Prerequisites

- C++14 or later
- Linux/QNX
- pthread
- socket support

### Compile

```bash
cd librpc
make
```

### Run Tests

#### Single Process Test (In-Process Communication Only)
Tests 3 nodes within the same process:
```bash
./run_test.sh single
# Or directly:
LD_LIBRARY_PATH=./lib ./test_both single
```

**What This Tests:**
- In-process message delivery
- Selective subscription (only matching subscribers receive messages)
- Multiple nodes in the same process

#### Inter-Process Test (In-Process + Inter-Process Communication)
Tests 4 nodes across 2 processes (2 nodes per process):
```bash
# Terminal 1:
./run_test.sh process1

# Terminal 2 (wait 3 seconds after Terminal 1):
./run_test.sh process2
```

**What This Tests:**
- In-process communication (nodes within same process)
- Inter-process communication via UDP (nodes across processes)
- Subscription discovery (new nodes query existing subscriptions)
- Selective delivery based on subscription registry
- Bidirectional message flow

**Architecture:**
- **Process 1**: Node1A (subscribes to temperature) + Node1B (subscribes to pressure, humidity)
- **Process 2**: Node2A (subscribes to temperature) + Node2B (subscribes to pressure, humidity)

**Expected Behavior:**
- Temperature messages → Node1A and Node2A receive
- Pressure messages → Node1B and Node2B receive
- Humidity messages → Node1B and Node2B receive
- In-process: Node1A and Node1B can communicate directly
- Inter-process: All messages delivered via UDP with targeted routing

## Message Protocol

### Packet Structure

```
+--------+--------+----------+----------+----------+----------+
| Magic  | Version| GroupLen | TopicLen | PayloadLen| Checksum|
| 4bytes | 2bytes | 2bytes   | 2bytes   | 4bytes   | 4bytes  |
+--------+--------+----------+----------+----------+----------+
| NodeID (64 bytes)                                           |
+-------------------------------------------------------------+
| Group Data (variable)                                       |
+-------------------------------------------------------------+
| Topic Data (variable)                                       |
+-------------------------------------------------------------+
| Payload Data (variable)                                     |
+-------------------------------------------------------------+
```

### UDP Configuration

- **Port assignment**: Each node binds to a unique UDP port
- **Node discovery**: Port scanning (localhost ports 47200-47230, 48000-48020)
- **Communication**: Point-to-point UDP based on discovered node addresses
- **Max message size**: ~64KB
- **Protocol**: Custom message format with checksums

## Performance Characteristics

### In-Process Communication

- **Latency**: < 1μs (direct function call)
- **Throughput**: Limited only by callback processing
- **Memory**: Zero-copy

### Inter-Process Communication (UDP)

- **Latency**: ~100μs (localhost)
- **Throughput**: ~100K messages/sec
- **Memory**: One copy (serialization)

## Thread Safety

- All public APIs are thread-safe
- Callbacks may be invoked from different threads
- Use proper synchronization in callbacks if needed

## Error Handling

```cpp
enum Error {
    NO_ERROR         = 0,
    INVALID_ARG      = 1,
    NOT_INITIALIZED  = 2,
    ALREADY_EXISTS   = 3,
    NOT_FOUND        = 4,
    NETWORK_ERROR    = 5,
    TIMEOUT          = 6,
    UNEXPECTED_ERROR = 99,
};
```

## Limitations

1. UDP broadcast may not work across subnets
2. Message size limited to ~64KB
3. No delivery guarantee for inter-process messages
4. No built-in encryption/authentication

## Best Practices

1. **Use unique node IDs** for easier debugging
2. **Keep callbacks fast** to avoid blocking receive thread
3. **Subscribe before broadcast** in same-process scenarios
4. **Use appropriate UDP ports** to avoid conflicts
5. **Handle callback exceptions** to prevent crashes

## License

Copyright (c) 2025 Baidu.com, Inc. All Rights Reserved
