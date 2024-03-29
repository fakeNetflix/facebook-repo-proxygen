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

#include <folly/IntrusiveList.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>
#include <proxygen/lib/utils/Time.h>

namespace proxygen {

class HTTPTransaction;
class ByteEvent {
 public:
  enum EventType {
    FIRST_BYTE,
    LAST_BYTE,
    PING_REPLY_SENT,
    FIRST_HEADER_BYTE,
    TRACKED_BYTE,
    SECOND_TO_LAST_PACKET,
  };

  ByteEvent(uint64_t byteOffset, EventType eventType)
      : eventType_(eventType),
        timestampTx_(false),
        timestampAck_(false),
        byteOffset_(byteOffset) {
  }
  virtual ~ByteEvent() {
  }
  EventType getType() const {
    return eventType_;
  }
  uint64_t getByteOffset() const {
    return byteOffset_;
  }
  virtual HTTPTransaction* getTransaction() const {
    return nullptr;
  }
  virtual int64_t getLatency() {
    return -1;
  }

  folly::SafeIntrusiveListHook listHook;
  EventType eventType_ : 3; // packed w/ byteOffset_
  // (tx|ack)Tracked_ is used by TX and ACK-tracking ByteEventTrackers to
  // mark if TX and/or ACK timestamping has been requested for this ByteEvent.
  // The ByteEventTracker's configuration determines which events TX and ACK
  // timestamping is requested for.
  //
  // For ByteEvents with timestamps requested, TX and ACK timestamps can be
  // captured by the ByteEventTracker::Callback handler by requesting them
  // via calls to addTxByteEvent and addAckByteEvent respectively when the
  // handler is processing the callback for onByteEvent. If the handler does
  // not add these events, the timestamps will still be generated but will not
  // be delivered to the handler.
  bool timestampTx_ : 1; // packed w/ byteOffset_
  bool timestampAck_ : 1; // packed w/ byteOffset_
  uint64_t byteOffset_ : (8 * sizeof(uint64_t) - 5);
};

std::ostream& operator<<(std::ostream& os, const ByteEvent& txn);

class PingByteEvent : public ByteEvent {
 public:
  PingByteEvent(uint64_t byteOffset, TimePoint pingRequestReceivedTime)
      : ByteEvent(byteOffset, PING_REPLY_SENT),
        pingRequestReceivedTime_(pingRequestReceivedTime) {
  }

  int64_t getLatency() override;

  TimePoint pingRequestReceivedTime_;
};

} // namespace proxygen
