#include "simple_test.h"
#include "nexus/transport/UdpTransport.h"
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace Nexus::rpc;

TEST(UdpTransportCoverageFinal, Initialize_BindFail) {
    UdpTransport transport;
    
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // Let OS choose
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr*)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);
    
    // Now try to bind same port with UdpTransport
    ASSERT_FALSE(transport.initialize(port));
    
    close(fd);
}

TEST(UdpTransportCoverageFinal, Send_InvalidAddress) {
    UdpTransport transport;
    ASSERT_TRUE(transport.initialize(0));
    
    std::vector<uint8_t> data(10, 0);
    ASSERT_FALSE(transport.send(data.data(), data.size(), "invalid_ip", 12345));
}

TEST(UdpTransportCoverageFinal, Send_Uninitialized) {
    UdpTransport transport;
    std::vector<uint8_t> data(10, 0);
    ASSERT_FALSE(transport.send(data.data(), data.size(), "127.0.0.1", 12345));
}

TEST(UdpTransportCoverageFinal, Shutdown_Uninitialized) {
    UdpTransport transport;
    transport.shutdown(); // Should be safe
}

TEST(UdpTransportCoverageFinal, Broadcast) {
    UdpTransport transport;
    ASSERT_TRUE(transport.initialize(0));
    std::vector<uint8_t> data(10, 0);
    transport.broadcast(data.data(), data.size());
}
