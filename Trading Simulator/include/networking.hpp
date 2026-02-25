#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace networking {

inline void set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    throw std::runtime_error("fcntl F_GETFL failed");
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    throw std::runtime_error("fcntl F_SETFL nonblock failed");
  }
}

// UDP Multicast Functions

// Create a socket that broadcasts to a multicast group
inline int create_udp_multicast_sender(const std::string &multicast_ip,
                                       int port, sockaddr_in &addr_out) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    throw std::runtime_error("Failed to create UDP socket");

  // Set TTL for multicast packets (1 = local subnet)
  unsigned char ttl = 1;
  if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
    close(sock);
    throw std::runtime_error("Failed to set IP_MULTICAST_TTL");
  }

  addr_out.sin_family = AF_INET;
  addr_out.sin_addr.s_addr = inet_addr(multicast_ip.c_str());
  addr_out.sin_port = htons(port);

  return sock;
}

// Create a socket that listens to a multicast group
inline int create_udp_multicast_receiver(const std::string &multicast_ip,
                                         int port) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    throw std::runtime_error("Failed to create UDP socket");

  // Allow multiple instances to listen on the same port
  int reuse = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
    close(sock);
    throw std::runtime_error("Failed to set SO_REUSEPORT");
  }

  sockaddr_in local_addr{};
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  local_addr.sin_port = htons(port);

  if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
    close(sock);
    throw std::runtime_error("Failed to bind UDP socket");
  }

  // Join the multicast group
  ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = inet_addr(multicast_ip.c_str());
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) <
      0) {
    close(sock);
    throw std::runtime_error("Failed to join multicast group");
  }

  return sock;
}

// TCP Functions

// Create a TCP server socket that listens for incoming connections
inline int create_tcp_listener(int port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    throw std::runtime_error("Failed to create TCP socket");

  int opt = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(sock);
    throw std::runtime_error("Failed to set SO_REUSEADDR");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(sock);
    throw std::runtime_error("Failed to bind TCP socket");
  }

  if (listen(sock, 128) < 0) {
    close(sock);
    throw std::runtime_error("Failed to listen on TCP socket");
  }

  set_non_blocking(sock);

  return sock;
}

// Connect to a TCP server (for the Subscriber)
inline int connect_tcp_client(const std::string &ip, int port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    throw std::runtime_error("Failed to create TCP socket");

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip.c_str());
  addr.sin_port = htons(port);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(sock);
    throw std::runtime_error("Failed to connect to TCP server");
  }

  return sock;
}

} // namespace networking
