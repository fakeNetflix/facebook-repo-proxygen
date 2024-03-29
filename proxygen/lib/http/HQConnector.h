/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <fizz/client/AsyncFizzClient.h>
#include <proxygen/lib/http/session/HQUpstreamSession.h>
#include <quic/api/LoopDetectorCallback.h>
#include <quic/api/QuicSocket.h>
#include <quic/client/QuicClientTransport.h>
#include <quic/client/handshake/QuicPskCache.h>
#include <quic/logging/QLogger.h>

namespace proxygen {

class HQSession;

/**
 * This class establishes new connections to HTTP servers over a QUIC transport.
 * It can be reused, even to connect to different addresses, but it can only
 * service setting up one connection at a time.
 */
class HQConnector : public HQSession::ConnectCallback {
 public:
  class Callback {
   public:
    virtual ~Callback() {
    }
    virtual void connectSuccess(HQUpstreamSession* session) = 0;
    virtual void connectError(const quic::QuicErrorCode& code) = 0;
  };

  explicit HQConnector(Callback* callback,
                       std::chrono::milliseconds transactionTimeout)
      : cb_(CHECK_NOTNULL(callback)), transactionTimeout_(transactionTimeout) {
  }

  ~HQConnector() override {
    reset();
  }

  void setTransportSettings(quic::TransportSettings transportSettings);

  void setQuicPskCache(std::shared_ptr<quic::QuicPskCache> quicPskCache);

  void reset();

  void connect(
      folly::EventBase* eventBase,
      const folly::SocketAddress& connectAddr,
      std::shared_ptr<const fizz::client::FizzClientContext> fizzContext,
      std::shared_ptr<const fizz::CertificateVerifier> verifier,
      std::chrono::milliseconds connectTimeout = std::chrono::milliseconds(0),
      const folly::AsyncSocket::OptionMap& /* socketOptions */ =
          folly::AsyncSocket::emptyOptionMap,
      folly::Optional<std::string> sni = folly::none,
      std::shared_ptr<quic::Logger> logger = nullptr,
      std::shared_ptr<quic::QLogger> qLogger = nullptr,
      std::shared_ptr<quic::LoopDetectorCallback> quicLoopDetectorCallback =
          nullptr);

  std::chrono::milliseconds timeElapsed();

  bool isBusy() const {
    return (session_ != nullptr);
  }

  // HQSession::ConnectCallback
  void onReplaySafe() noexcept override;
  void connectError(
      std::pair<quic::QuicErrorCode, std::string> error) noexcept override;

 private:
  Callback* cb_;
  std::chrono::milliseconds transactionTimeout_;
  TimePoint connectStart_;
  HQUpstreamSession* session_{nullptr};
  quic::TransportSettings transportSettings_;
  std::shared_ptr<quic::QuicPskCache> quicPskCache_;
};

} // namespace proxygen
