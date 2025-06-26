# KTP Socket Implementation

## Overview

KTP (KGP Transport Protocol) is a reliable, message-oriented transport protocol built on top of UDP sockets. It provides end-to-end reliable data transfer with window-based flow control, guaranteeing in-order message delivery over unreliable network channels.

## Features

- **Reliable Data Transfer**: Guarantees message delivery using acknowledgments and retransmissions
- **Flow Control**: Window-based flow control with configurable window sizes
- **Message-Oriented**: Fixed-size messages (512 bytes) with sequence numbering
- **Multi-Socket Support**: Supports multiple concurrent KTP sockets
- **Error Simulation**: Built-in message dropping simulation for testing unreliable networks
- **Thread-Safe**: Uses shared memory with proper synchronization

## Architecture

### Core Components

1. **KTP Socket Library (`libksocket.a`)**: Static library containing KTP socket functions
2. **Initialization Process (`initksocket`)**: Manages R and S threads plus garbage collection
3. **Thread R**: Handles incoming messages and acknowledgments
4. **Thread S**: Manages timeouts and retransmissions
5. **Shared Memory**: Stores socket state information across processes

### Protocol Details

- **Message Size**: Fixed 512 bytes
- **Sequence Numbers**: 8-bit length, starting from 1
- **Buffer Sizes**: 
  - Receiver buffer: 10 messages
  - Initial sender window: 10 messages
- **Timeout**: 5 seconds (configurable)
- **Addressing**: Each socket bound to source and destination IP/port pairs

## File Structure

```
├── ksocket.h           # Header file with KTP definitions
├── ksocket.c           # KTP socket library implementation
├── initksocket.c       # Initialization process
├── user1.c             # Test sender application
├── user2.c             # Test receiver application
├── Makefile            # Build configuration
├── documentation.txt   # Detailed technical documentation
└── README.md          # This file
```

## API Functions

### Core KTP Socket Functions

```c
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int k_sendto(int sockfd, const void *buf, size_t len, int flags,
             const struct sockaddr *dest_addr, socklen_t addrlen);
int k_recvfrom(int sockfd, void *buf, size_t len, int flags,
               struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
```

### Error Codes

- `ENOSPACE`: No space available in buffer or socket table
- `ENOTBOUND`: Socket not properly bound to destination
- `ENOMESSAGE`: No message available in receive buffer

## Building the Project

### Prerequisites

- GCC compiler
- Linux environment
- POSIX threads support

### Compilation Steps

1. **Build the KTP socket library**:
   ```bash
   make libksocket.a
   ```

2. **Build the initialization process**:
   ```bash
   make initksocket
   ```

3. **Build test applications**:
   ```bash
   make user1 user2
   ```

4. **Build everything**:
   ```bash
   make all
   ```

## Usage

### 1. Initialize KTP Socket System

First, start the initialization process:
```bash
./initksocket
```

This creates the shared memory segment and starts the R and S threads.

### 2. Run Test Applications

**Terminal 1 (Sender)**:
```bash
./user1
```

**Terminal 2 (Receiver)**:
```bash
./user2
```

### Example Usage in Code

```c
#include "ksocket.h"

int main() {
    int sockfd;
    struct sockaddr_in local_addr, remote_addr;
    
    // Create KTP socket
    sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    
    // Configure addresses
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    local_addr.sin_port = htons(8080);
    
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    remote_addr.sin_port = htons(8081);
    
    // Bind socket
    k_bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr));
    
    // Send data
    char message[512] = "Hello, KTP!";
    k_sendto(sockfd, message, sizeof(message), 0,
             (struct sockaddr*)&remote_addr, sizeof(remote_addr));
    
    // Close socket
    k_close(sockfd);
    return 0;
}
```

## Testing

### Reliability Testing

The implementation includes a `dropMessage()` function that simulates packet loss:

```c
int dropMessage(float p);  // p = probability of message drop (0.0 to 1.0)
```

### Test Scenarios

1. **Basic Functionality**: Transfer files > 100KB between two sockets
2. **Packet Loss Simulation**: Test with varying drop probabilities (0.05 to 0.5)
3. **Multiple Sockets**: Run multiple sender-receiver pairs simultaneously
4. **Timeout Testing**: Verify retransmission mechanisms

### Performance Metrics

Monitor these metrics during testing:
- Average number of transmissions per message
- File transfer completion time
- Throughput under different loss rates
- Memory usage patterns

## Configuration

### Tunable Parameters (in ksocket.h)

```c
#define T 5              // Timeout in seconds
#define DROP_PROB 0.1    // Packet drop probability for testing
#define MAX_SOCKETS 10   // Maximum concurrent KTP sockets
#define MSG_SIZE 512     // Message size in bytes
#define BUFFER_SIZE 10   // Receiver buffer size (in messages)
```

## How It Works

### Sender Side Flow Control

1. Application writes data to sender buffer via `k_sendto()`
2. Thread S sends messages within the current window size
3. Thread R receives ACKs and updates the sending window
4. On timeout, Thread S retransmits unacknowledged messages

### Receiver Side Flow Control

1. Thread R receives messages and stores them in receiver buffer
2. In-order messages trigger ACK responses with updated window size
3. Out-of-order messages are buffered but don't generate ACKs
4. Application reads messages via `k_recvfrom()`, freeing buffer space

### Window Management

- **Sending Window (swnd)**: Tracks unacknowledged messages and available send capacity
- **Receiving Window (rwnd)**: Indicates expected sequence numbers and buffer availability
- Windows slide as messages are acknowledged and buffer space becomes available

## Troubleshooting

### Common Issues

1. **"No space available" errors**:
   - Check if initksocket is running
   - Verify shared memory creation
   - Ensure socket limit not exceeded

2. **Messages not being received**:
   - Verify both endpoints are properly bound
   - Check IP/port configurations
   - Ensure receiver is calling k_recvfrom() regularly

3. **High retransmission rates**:
   - Adjust timeout value T
   - Check network conditions
   - Verify acknowledgment processing

### Debug Tips

- Use `strace` to monitor system calls
- Check shared memory with `ipcs -m`
- Monitor UDP socket activity with `netstat -u`
- Add debug prints to track message flow

## Performance Considerations

- **Buffer Management**: Receiver must regularly call k_recvfrom() to prevent buffer overflow
- **Window Sizing**: Optimal window size depends on network conditions
- **Timeout Tuning**: Balance between responsiveness and unnecessary retransmissions
- **Thread Synchronization**: Proper locking prevents race conditions in shared memory

## Limitations

- Maximum of N concurrent sockets (configurable)
- Fixed message size of 512 bytes
- 8-bit sequence numbers (wraps after 256 messages)
- Single-threaded receiver processing per socket

## Use Cases

- **File Transfer**: Reliable transfer of large files over unreliable networks
- **Message Queuing**: Guaranteed delivery of discrete messages
- **Network Testing**: Simulation of various network conditions
- **Protocol Research**: Study of transport layer reliability mechanisms



