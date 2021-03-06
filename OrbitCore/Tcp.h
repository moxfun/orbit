// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef _WIN32
//#define _WIN32_WINNT 0x0501
#include <sdkddkver.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <asio.hpp>
#include <ctime>
#include <iostream>
#include <string>
#include <unordered_set>

#include "Message.h"
#include "TcpEntity.h"

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  static std::shared_ptr<TcpConnection> create(asio::io_service& io_service) {
    return std::shared_ptr<TcpConnection>(new TcpConnection(io_service));
  }

  asio::ip::tcp::socket& GetSocket() { return socket_; }

  void start() { ReadMessage(); }

  void ReadMessage();
  void ReadPayload();
  void ReadFooter();
  void DecodeMessage(MessageOwner&& message);

  uint64_t GetNumBytesReceived() { return num_bytes_received_; }

  void ResetStats();
  std::vector<std::string> GetStats();

 private:
  TcpConnection(asio::io_service& io_service)
      : socket_(io_service), num_bytes_received_(0) {}
  // handle_write() is responsible for any further actions
  // for this client connection.
  void handle_write(const asio::error_code& /*error*/,
                    size_t /*bytes_transferred*/) {}

  asio::ip::tcp::socket socket_;
  Message message_;
  std::vector<char> payload_;
  uint64_t num_bytes_received_;
};

//-----------------------------------------------------------------------------
class tcp_server : public std::enable_shared_from_this<tcp_server> {
 public:
  tcp_server(asio::io_service& io_service, unsigned short port);
  ~tcp_server();

  void Disconnect();
  bool HasConnection() { return connection_ != nullptr; }
  asio::ip::tcp::socket* GetSocket() {
    return connection_ != nullptr ? &connection_->GetSocket() : nullptr;
  }
  void RegisterConnection(std::shared_ptr<TcpConnection> connection);
  ULONG64 GetNumBytesReceived() {
    return connection_ != nullptr ? connection_->GetNumBytesReceived() : 0;
  }
  void ResetStats() {
    if (connection_ != nullptr) {
      connection_->ResetStats();
    }
  }

 private:
  void start_accept();
  void handle_accept(std::shared_ptr<TcpConnection> new_connection,
                     const asio::error_code& error);

  asio::ip::tcp::acceptor acceptor_;
  std::shared_ptr<TcpConnection> connection_;
  // Is this here to keep them alive until server is destroyed?
  // This is not really used for anything else.
  std::unordered_set<std::shared_ptr<TcpConnection>> connections_set_;
};

//-----------------------------------------------------------------------------
class SharedConstBuffer {
 public:
  SharedConstBuffer() {}
  explicit SharedConstBuffer(const Message& message, const void* payload)
      : data_(new std::vector<char>()) {
    const auto footer = OrbitCore::GetMagicFooter();
    data_->resize(sizeof(Message) + message.m_Size + footer.size());
    std::memcpy(data_->data(), &message, sizeof(Message));

    if (payload != nullptr) {
      std::memcpy(data_->data() + sizeof(Message), payload, message.m_Size);
    }

    // Footer
    std::memcpy(data_->data() + sizeof(Message) + message.m_Size, footer.data(),
                footer.size());

    buffer_ = asio::buffer(*data_);
  }

  explicit SharedConstBuffer(TcpPacket& packet) {
    data_ = packet.Data();
    buffer_ = asio::buffer(*data_);
  }

  // Implement the ConstBufferSequence requirements.
  typedef asio::const_buffer value_type;
  typedef const asio::const_buffer* const_iterator;
  const asio::const_buffer* begin() const { return &buffer_; }
  const asio::const_buffer* end() const { return &buffer_ + 1; }

  std::shared_ptr<std::vector<char>> Data() { return data_; };

 private:
  std::shared_ptr<std::vector<char>> data_;
  asio::const_buffer buffer_;
};
