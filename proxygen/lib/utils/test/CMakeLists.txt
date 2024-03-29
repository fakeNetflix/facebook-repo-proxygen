# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

proxygen_add_test(TARGET AsyncTimeoutSetTest
  SOURCES
    AsyncTimeoutSetTest.cpp
  DEPENDS
    proxygen
    testmain
)

proxygen_add_test(TARGET TraceEventTest
  SOURCES
    TraceEventTest.cpp
  DEPENDS
    proxygen
    testmain
)

proxygen_add_test(TARGET UtilTests
  SOURCES
    Base64Test.cpp
    ConditionalGateTest.cpp
    CryptUtilTest.cpp
    GenericFilterTest.cpp
    HTTPTimeTest.cpp
    LoggingTests.cpp
    ParseURLTest.cpp
    PerfectIndexMapTest.cpp
    RendezvousHashTest.cpp
    TimeTest.cpp
    UtilTest.cpp
    ZlibTests.cpp
    #ZstdTests.cpp
  DEPENDS
    proxygen
    testmain
)
