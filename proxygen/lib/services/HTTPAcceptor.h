/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/async/AsyncServerSocket.h>
#include <memory>
#include <proxygen/lib/services/AcceptorConfiguration.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>
#include <proxygen/lib/utils/WheelTimerInstance.h>
#include <wangle/acceptor/Acceptor.h>

namespace proxygen {

class HTTPAcceptor : public wangle::Acceptor {
 public:
  explicit HTTPAcceptor(const AcceptorConfiguration& accConfig)
      : Acceptor(accConfig), accConfig_(accConfig) {
  }

  /**
   * Returns true if this server is internal to facebook
   */
  bool isInternal() const {
    return accConfig_.internal;
  }

  /**
   * Access the general-purpose timeout manager for transactions.
   */
  virtual const WheelTimerInstance& getTransactionTimeoutSet() {
    return *timer_;
  }

  void init(folly::AsyncServerSocket* serverSocket,
            folly::EventBase* eventBase,
            wangle::SSLStats* /*stat*/ = nullptr) override {
    timer_ = std::make_unique<WheelTimerInstance>(
        accConfig_.transactionIdleTimeout, eventBase);
    Acceptor::init(serverSocket, eventBase);
  }

  const AcceptorConfiguration& getConfig() const {
    return accConfig_;
  }
  const wangle::ServerSocketConfig& getServerSocketConfig() {
    return Acceptor::getConfig();
  }

 protected:
  AcceptorConfiguration accConfig_;

 private:
  AsyncTimeoutSet::UniquePtr tcpEventsTimeouts_;
  std::unique_ptr<WheelTimerInstance> timer_;
};

} // namespace proxygen
