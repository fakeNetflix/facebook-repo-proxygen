# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

proxygen_add_test(TARGET LibHTTPTests
  SOURCES
    HTTPCommonHeadersTests.cpp
    HTTPConnectorWithFizzTest.cpp
    HTTPMessageTest.cpp
    RFC2616Test.cpp
    WindowTest.cpp
  DEPENDS
    proxygen
    testmain
)
