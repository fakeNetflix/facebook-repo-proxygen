/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/ByteEvents.h>

#include <proxygen/lib/utils/Time.h>

namespace proxygen {

const char* const kTypeStrings[] =  {
  "FIRST_BYTE",
  "LAST_BYTE",
  "PING_REPLY_SENT",
  "FIRST_HEADER_BYTE",
};

std::ostream& operator<<(std::ostream& os, const ByteEvent& be) {
  os << folly::to<std::string>(
    "(", kTypeStrings[be.eventType_], ", ", be.byteOffset_, ")");
  return os;
}

int64_t PingByteEvent::getLatency() {
  return millisecondsSince(pingRequestReceivedTime_).count();
}

} // proxygen
