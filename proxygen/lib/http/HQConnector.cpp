/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HQConnector.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <proxygen/lib/http/session/HQSession.h>
#include <quic/api/QuicSocket.h>
#include <quic/congestion_control/CongestionControllerFactory.h>

using namespace folly;
using namespace std;
using namespace fizz::client;

namespace proxygen {

std::chrono::milliseconds HQConnector::timeElapsed() {
  if (timePointInitialized(connectStart_)) {
    return millisecondsSince(connectStart_);
  }
  return std::chrono::milliseconds(0);
}

void HQConnector::reset() {
  if (session_) {
    // This destroys the session
    session_->dropConnection();
    session_ = nullptr;
  }
}

void HQConnector::setTransportSettings(
    quic::TransportSettings transportSettings) {
  transportSettings_ = transportSettings;
}

void HQConnector::setQuicPskCache(
    std::shared_ptr<quic::QuicPskCache> quicPskCache) {
  quicPskCache_ = std::move(quicPskCache);
}

void HQConnector::connect(
    EventBase* eventBase,
    const folly::SocketAddress& connectAddr,
    std::shared_ptr<const FizzClientContext> fizzContext,
    std::shared_ptr<const fizz::CertificateVerifier> verifier,
    std::chrono::milliseconds connectTimeout,
    const AsyncSocket::OptionMap& /* socketOptions */,
    folly::Optional<std::string> sni,
    std::shared_ptr<quic::Logger> logger,
    std::shared_ptr<quic::QLogger> qLogger,
    std::shared_ptr<quic::LoopDetectorCallback> quicLoopDetectorCallback) {

  DCHECK(!isBusy());
  auto sock = std::make_unique<folly::AsyncUDPSocket>(eventBase);
  auto quicClient =
      quic::QuicClientTransport::newClient(eventBase, std::move(sock));
  quicClient->setFizzClientContext(fizzContext);
  quicClient->setCertificateVerifier(std::move(verifier));
  quicClient->setHostname(sni.value_or(connectAddr.getAddressStr()));
  quicClient->addNewPeerAddress(connectAddr);
  quicClient->setCongestionControllerFactory(
      std::make_shared<quic::DefaultCongestionControllerFactory>());
  quicClient->setTransportSettings(transportSettings_);
  quicClient->setPskCache(quicPskCache_);
  quicClient->setLogger(std::move(logger));
  quicClient->setQLogger(std::move(qLogger));
  quicClient->setLoopDetectorCallback(std::move(quicLoopDetectorCallback));
  session_ = new proxygen::HQUpstreamSession(transactionTimeout_,
                                             connectTimeout,
                                             nullptr, // controller
                                             wangle::TransportInfo(),
                                             nullptr,  // InfoCallback
                                             nullptr); // codecfiltercallback

  session_->setSocket(quicClient);
  session_->setConnectCallback(this);
  session_->startNow();

  VLOG(4) << "connecting to " << connectAddr.describe();
  connectStart_ = getCurrentTime();
  quicClient->start(session_);
}

void HQConnector::onReplaySafe() noexcept {
  CHECK(session_);
  if (cb_) {
    auto session = session_;
    session_ = nullptr;
    cb_->connectSuccess(session);
  }
}

void HQConnector::connectError(
    std::pair<quic::QuicErrorCode, std::string> error) noexcept {
  CHECK(session_);
  reset();
  if (cb_) {
    cb_->connectError(error.first);
  }
}

} // namespace proxygen
