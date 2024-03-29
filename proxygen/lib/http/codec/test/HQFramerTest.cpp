/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/test/HQFramerTest.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/HTTP3ErrorCode.h>
#include <proxygen/lib/http/codec/HQFramer.h>
#include <proxygen/lib/http/codec/HQUtils.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <quic/codec/QuicInteger.h>

using namespace folly;
using namespace folly::io;
using namespace proxygen::hq;
using namespace proxygen;
using namespace std;
using namespace testing;

template <class T>
class HQFramerTestFixture : public T {
 public:
  HQFramerTestFixture() {
  }

  template <typename Func, typename... Args>
  void parse(ParseResult parseError,
             Func&& parseFn,
             FrameHeader& outHeader,
             Args&&... outArgs) {
    parse(parseError,
          queue_.front(),
          parseFn,
          outHeader,
          std::forward<Args>(outArgs)...);
  }

  template <typename Func, typename... Args>
  void parse(ParseResult parseError,
             const IOBuf* data,
             Func&& parseFn,
             FrameHeader& outHeader,
             Args&&... outArgs) {
    Cursor cursor(data);
    size_t prevLen = cursor.totalLength();

    auto type = quic::decodeQuicInteger(cursor);
    ASSERT_TRUE(type.hasValue());
    outHeader.type = FrameType(type->first);
    auto length = quic::decodeQuicInteger(cursor);
    ASSERT_TRUE(length.hasValue());
    outHeader.length = length->first;

    auto readBytes = prevLen - cursor.totalLength();
    ASSERT_LE(type->second + length->second, kMaxFrameHeaderSize);
    ASSERT_EQ(type->second + length->second, readBytes);
    auto ret2 = (*parseFn)(cursor, outHeader, std::forward<Args>(outArgs)...);
    ASSERT_EQ(ret2, parseError);
  }

  IOBufQueue queue_{IOBufQueue::cacheChainLength()};
};

class HQFramerTest : public HQFramerTestFixture<testing::Test> {};

TEST_F(HQFramerTest, TestWriteFrameHeaderManual) {
  auto res = writeFrameHeaderManual(
      queue_, 0, static_cast<uint8_t>(proxygen::hq::FrameType::DATA));
  EXPECT_EQ(res, 2);
}

TEST_F(HQFramerTest, TestWriteUnframedBytes) {
  auto data = IOBuf::copyBuffer("I just met you and this is crazy.");
  auto dataLen = data->length();
  auto res = writeUnframedBytes(queue_, std::move(data));
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, dataLen);
  EXPECT_EQ("I just met you and this is crazy.",
            queue_.front()->clone()->moveToFbString().toStdString());
}

TEST_F(HQFramerTest, DataFrameZeroLength) {
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(proxygen::hq::FrameType::DATA), 0);
  FrameHeader outHeader;
  std::unique_ptr<IOBuf> outBuf;
  Cursor cursor(queue_.front());
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_DATA,
        parseData,
        outHeader,
        outBuf);
}

struct FrameHeaderLengthParams {
  uint8_t headerLength;
  ParseResult error;
};

struct DataOnlyFrameParams {
  proxygen::hq::FrameType type;
  WriteResult (*writeFn)(folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>);
  ParseResult (*parseFn)(Cursor& cursor,
                         const FrameHeader&,
                         std::unique_ptr<folly::IOBuf>&);
  HTTP3::ErrorCode error;
};

class HQFramerTestDataOnlyFrames
    : public HQFramerTestFixture<TestWithParam<DataOnlyFrameParams>> {};

TEST_P(HQFramerTestDataOnlyFrames, TestDataOnlyFrame) {
  // Test writing and parsing a valid frame
  auto data = makeBuf(500);
  auto res = GetParam().writeFn(queue_, data->clone());
  EXPECT_FALSE(res.hasError());
  FrameHeader header;
  std::unique_ptr<IOBuf> outBuf;
  parse(folly::none, GetParam().parseFn, header, outBuf);
  EXPECT_EQ(outBuf->moveToFbString(), data->moveToFbString());
}

INSTANTIATE_TEST_CASE_P(
    DataOnlyFrameWriteParseTests,
    HQFramerTestDataOnlyFrames,
    Values((DataOnlyFrameParams){proxygen::hq::FrameType::DATA,
                                 writeData,
                                 parseData,
                                 HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_DATA},
           (DataOnlyFrameParams){
               proxygen::hq::FrameType::HEADERS,
               writeHeaders,
               parseHeaders,
               HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_HEADERS}));

TEST_F(HQFramerTest, ParsePriorityFrameOk) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::REQUEST_STREAM,
                                  // dependencyType
                                  PriorityElementType::REQUEST_STREAM,
                                  true, // exclusive
                                  123,  // prioritizedElementId
                                  234,  // elementDependencyId
                                  30,   // weight
                              });
  EXPECT_FALSE(result.hasError());

  FrameHeader header;
  PriorityUpdate priority;
  parse(folly::none, &parsePriority, header, priority);

  EXPECT_EQ(FrameType::PRIORITY, header.type);
  EXPECT_EQ(priority.prioritizedType, PriorityElementType::REQUEST_STREAM);
  EXPECT_EQ(priority.dependencyType, PriorityElementType::REQUEST_STREAM);
  EXPECT_TRUE(priority.exclusive);
  EXPECT_EQ(123, priority.prioritizedElementId);
  EXPECT_EQ(234, priority.elementDependencyId);
  EXPECT_EQ(30, priority.weight);
}

TEST_F(HQFramerTest, ParsePriorityFramePrioritizeRoot) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::TREE_ROOT,
                                  // dependencyType
                                  PriorityElementType::REQUEST_STREAM,
                                  true, // exclusive
                                  123,  // prioritizedElementId
                                  234,  // elementDependencyId
                                  30,   // weight
                              });
  EXPECT_FALSE(result.hasError());

  FrameHeader header;
  PriorityUpdate priority;
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY,
        &parsePriority,
        header,
        priority);
}

TEST_F(HQFramerTest, ParsePriorityFramePrioritizedIdOptional) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::REQUEST_STREAM,
                                  // dependencyType
                                  PriorityElementType::TREE_ROOT,
                                  true, // exclusive
                                  123,  // prioritizedElementId
                                  234,  // elementDependencyId (ignored!)
                                  30,   // weight
                              });
  EXPECT_FALSE(result.hasError());

  FrameHeader header;
  PriorityUpdate priority;

  parse(folly::none, &parsePriority, header, priority);
  EXPECT_EQ(FrameType::PRIORITY, header.type);
  EXPECT_EQ(priority.prioritizedType, PriorityElementType::REQUEST_STREAM);
  EXPECT_EQ(priority.dependencyType, PriorityElementType::TREE_ROOT);
  EXPECT_TRUE(priority.exclusive);
  EXPECT_EQ(123, priority.prioritizedElementId);
  EXPECT_EQ(0, priority.elementDependencyId);
  EXPECT_EQ(30, priority.weight);
}

TEST_F(HQFramerTest, ParsePriorityWrongWeight) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::REQUEST_STREAM,
                                  // dependencyType
                                  PriorityElementType::REQUEST_STREAM,
                                  true, // exclusive
                                  1,    // prioritizedElementId
                                  2,    // elementDependencyId
                                  30,   // weight
                              });
  EXPECT_FALSE(result.hasError());
  // Flip a bit in the buffer so to force the varlength integer parsing logic
  // to read extra bytes for prioritizedElementId.
  auto buf = queue_.move();
  buf->coalesce();
  RWPrivateCursor cursor(buf.get());
  // 2 bytes frame header (payload length is 4) + 1 byte for flags
  cursor.skip(3);
  // This will cause the parser to not have enough data to read for the
  // weight field
  cursor.writeBE<uint8_t>(0x42);
  queue_.append(std::move(buf));

  FrameHeader header;
  PriorityUpdate priority;
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY,
        &parsePriority,
        header,
        priority);
}

TEST_F(HQFramerTest, ParsePriorityWrongElementDependency) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::REQUEST_STREAM,
                                  // dependencyType
                                  PriorityElementType::REQUEST_STREAM,
                                  true, // exclusive
                                  1,    // prioritizedElementId
                                  2,    // elementDependencyId
                                  255,  // weight
                              });
  EXPECT_FALSE(result.hasError());
  // Flip a bit in the buffer so to force the varlength integer parsing logic
  // to read extra bytes for prioritizedElementId.
  auto buf = queue_.move();
  buf->coalesce();
  RWPrivateCursor cursor(buf.get());
  // 2 bytes frame header (payload length is 4) + 1 byte for flags
  cursor.skip(3);
  // This will cause the parser to read two bytes (instead of 1) for the
  // prioritizedElementId, then one byte for the elementDependencyId from what
  // was written as the weight field. weight being all 1s the parser will try to
  // read an 8-byte quic integer and there are not enough bytes available
  cursor.writeBE<uint8_t>(0x42);
  queue_.append(std::move(buf));

  FrameHeader header;
  PriorityUpdate priority;
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY,
        &parsePriority,
        header,
        priority);
}

TEST_F(HQFramerTest, ParsePriorityTrailingJunk) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::REQUEST_STREAM,
                                  // dependencyType
                                  PriorityElementType::REQUEST_STREAM,
                                  true, // exclusive
                                  1,    // prioritizedElementId
                                  2,    // elementDependencyId
                                  255,  // weight
                              });
  EXPECT_FALSE(result.hasError());
  // Trim the frame header off
  queue_.trimStart(2);
  auto buf = queue_.move();
  // Put in a new frame header (too long)
  auto badLength = buf->computeChainDataLength() + 4;
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::PRIORITY), badLength);
  queue_.append(std::move(buf));
  queue_.append(IOBuf::copyBuffer("junk"));

  FrameHeader header;
  PriorityUpdate priority;
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY,
        &parsePriority,
        header,
        priority);
}

TEST_F(HQFramerTest, ParsePriorityFrameMalformedEmpty) {
  auto result = writePriority(queue_,
                              {
                                  // prioritizedType
                                  PriorityElementType::REQUEST_STREAM,
                                  // dependencyType
                                  PriorityElementType::REQUEST_STREAM,
                                  true, // exclusive
                                  1,    // prioritizedElementId
                                  2,    // elementDependencyId
                                  30,   // weight
                              });
  EXPECT_FALSE(result.hasError());
  // modify the flags field so that the 'empty' field is not zero
  auto buf = queue_.move();
  buf->coalesce();
  RWPrivateCursor cursor(buf.get());
  // 2 bytes frame header (payload length is 4)
  cursor.skip(2);
  cursor.writeBE<uint8_t>(0x0E);
  queue_.append(std::move(buf));

  FrameHeader header;
  PriorityUpdate priority;
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY,
        &parsePriority,
        header,
        priority);
}

TEST_F(HQFramerTest, ParsePushPromiseFrameOK) {
  auto data = makeBuf(1000);
  PushId inPushId = 4563 | kPushIdMask;
  auto result = writePushPromise(queue_, inPushId, data->clone());
  EXPECT_FALSE(result.hasError());

  FrameHeader outHeader;
  PushId outPushId;
  std::unique_ptr<IOBuf> outBuf;
  parse(folly::none, parsePushPromise, outHeader, outPushId, outBuf);
  EXPECT_EQ(outPushId, inPushId | kPushIdMask);
  EXPECT_EQ(outBuf->moveToFbString(), data->moveToFbString());
}

struct IdOnlyFrameParams {
  proxygen::hq::FrameType type;
  WriteResult (*writeFn)(folly::IOBufQueue&, uint64_t);
  ParseResult (*parseFn)(Cursor& cursor, const FrameHeader&, uint64_t&);
  HTTP3::ErrorCode error;
};

class HQFramerTestIdOnlyFrames
    : public HQFramerTestFixture<TestWithParam<IdOnlyFrameParams>> {};

TEST_P(HQFramerTestIdOnlyFrames, TestIdOnlyFrame) {
  // test writing a valid ID
  {
    queue_.move();
    uint64_t validVarLenInt = 123456;
    if (GetParam().type == proxygen::hq::FrameType::MAX_PUSH_ID ||
        GetParam().type == proxygen::hq::FrameType::CANCEL_PUSH) {
      validVarLenInt |= kPushIdMask;
    }
    auto result = GetParam().writeFn(queue_, validVarLenInt);
    EXPECT_FALSE(result.hasError());

    FrameHeader header;
    uint64_t outId;
    parse(folly::none, GetParam().parseFn, header, outId);
    EXPECT_EQ(GetParam().type, header.type);
    EXPECT_EQ(validVarLenInt, outId);
  }

  // test writing an invalid ID
  {
    queue_.move();
    uint64_t invalidVarLenInt = std::numeric_limits<uint64_t>::max();
    auto result = GetParam().writeFn(queue_, invalidVarLenInt);
    EXPECT_TRUE(result.hasError());
  }
  // test writing a valid ID, then modifying to make the parser try to read
  // too much data
  {
    queue_.move();
    uint64_t validVarLenInt = 63; // requires just 1 byte
    if (GetParam().type == proxygen::hq::FrameType::MAX_PUSH_ID ||
        GetParam().type == proxygen::hq::FrameType::CANCEL_PUSH) {
      validVarLenInt |= kPushIdMask;
    }
    auto result = GetParam().writeFn(queue_, validVarLenInt);
    EXPECT_FALSE(result.hasError());

    // modify one byte in the buf
    auto buf = queue_.move();
    buf->coalesce();
    RWPrivateCursor wcursor(buf.get());
    // 2 bytes frame header (payload length is just 1)
    wcursor.skip(2);
    wcursor.writeBE<uint8_t>(0x42); // this varint requires two bytes
    queue_.append(std::move(buf));

    FrameHeader header;
    uint64_t outId;
    parse(GetParam().error, GetParam().parseFn, header, outId);
  }

  {
    queue_.move();
    uint64_t id = 3; // requires just 1 byte
    if (GetParam().type == proxygen::hq::FrameType::MAX_PUSH_ID ||
        GetParam().type == proxygen::hq::FrameType::CANCEL_PUSH) {
      id |= kPushIdMask;
    }
    auto result = GetParam().writeFn(queue_, id);
    EXPECT_FALSE(result.hasError());

    // Trim the frame header off
    queue_.trimStart(2);
    auto buf = queue_.move();
    // Put in a new frame header (too long)
    auto badLength = buf->computeChainDataLength() - 1 + 4;
    writeFrameHeaderManual(
        queue_, static_cast<uint64_t>(GetParam().type), badLength);
    // clip the bad frame length
    queue_.trimEnd(1);
    queue_.append(std::move(buf));
    queue_.append(IOBuf::copyBuffer("junk"));

    FrameHeader header;
    uint64_t outId;
    parse(GetParam().error, GetParam().parseFn, header, outId);
  }
}

INSTANTIATE_TEST_CASE_P(
    IdOnlyFrameWriteParseTests,
    HQFramerTestIdOnlyFrames,
    Values(
        (IdOnlyFrameParams){proxygen::hq::FrameType::CANCEL_PUSH,
                            writeCancelPush,
                            parseCancelPush,
                            HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_CANCEL_PUSH},
        (IdOnlyFrameParams){proxygen::hq::FrameType::GOAWAY,
                            writeGoaway,
                            parseGoaway,
                            HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_GOAWAY},
        (IdOnlyFrameParams){
            proxygen::hq::FrameType::MAX_PUSH_ID,
            writeMaxPushId,
            parseMaxPushId,
            HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_MAX_PUSH_ID}));

TEST_F(HQFramerTest, SettingsFrameOK) {
  deque<hq::SettingPair> settings = {
      {hq::SettingId::NUM_PLACEHOLDERS, (SettingValue)3},
      {hq::SettingId::MAX_HEADER_LIST_SIZE, (SettingValue)4},
      // Unknown IDs get ignored, and identifiers of the format
      // "0x1f * N + 0x21" are reserved exactly for this
      {(hq::SettingId)*getGreaseId(kMaxGreaseIdIndex), (SettingValue)5}};
  writeSettings(queue_, settings);

  FrameHeader header;
  std::deque<hq::SettingPair> outSettings;
  parse(folly::none, &parseSettings, header, outSettings);

  ASSERT_EQ(proxygen::hq::FrameType::SETTINGS, header.type);
  // Remove the last setting before comparison,
  // it must be ignored since it's a GREASE setting
  settings.pop_back();
  ASSERT_EQ(settings, outSettings);
}

struct SettingsValuesParams {
  hq::SettingId id;
  hq::SettingValue value;
  bool allowed;
};

class HQFramerTestSettingsValues
    : public HQFramerTestFixture<TestWithParam<SettingsValuesParams>> {};

TEST_P(HQFramerTestSettingsValues, ValueAllowed) {
  deque<hq::SettingPair> settings = {{GetParam().id, GetParam().value}};

  writeSettings(queue_, settings);

  FrameHeader header;
  std::deque<hq::SettingPair> outSettings;
  ParseResult expectedParseResult = folly::none;
  if (!GetParam().allowed) {
    expectedParseResult = HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_SETTINGS;
  }
  parse(expectedParseResult, &parseSettings, header, outSettings);

  ASSERT_EQ(proxygen::hq::FrameType::SETTINGS, header.type);
  if (GetParam().allowed) {
    ASSERT_EQ(settings, outSettings);
  } else {
    ASSERT_EQ(outSettings.size(), 0);
  }
}

INSTANTIATE_TEST_CASE_P(
    SettingsValuesAllowedTests,
    HQFramerTestSettingsValues,
    Values((SettingsValuesParams){hq::SettingId::NUM_PLACEHOLDERS, 0, true},
           (SettingsValuesParams){hq::SettingId::NUM_PLACEHOLDERS,
                                  std::numeric_limits<uint32_t>::max(),
                                  true},
           (SettingsValuesParams){hq::SettingId::MAX_HEADER_LIST_SIZE, 0, true},
           (SettingsValuesParams){hq::SettingId::MAX_HEADER_LIST_SIZE,
                                  std::numeric_limits<uint32_t>::max(),
                                  true},
           (SettingsValuesParams){
               hq::SettingId::QPACK_BLOCKED_STREAMS, 0, true},
           (SettingsValuesParams){hq::SettingId::QPACK_BLOCKED_STREAMS,
                                  std::numeric_limits<uint32_t>::max(),
                                  true},
           (SettingsValuesParams){hq::SettingId::HEADER_TABLE_SIZE, 0, true},
           (SettingsValuesParams){hq::SettingId::HEADER_TABLE_SIZE,
                                  std::numeric_limits<uint32_t>::max(),
                                  true}));

TEST_F(HQFramerTest, SettingsFrameEmpty) {
  const deque<hq::SettingPair> settings = {};
  writeSettings(queue_, settings);

  FrameHeader header;
  std::deque<hq::SettingPair> outSettings;
  parse(folly::none, &parseSettings, header, outSettings);

  ASSERT_EQ(proxygen::hq::FrameType::SETTINGS, header.type);
  ASSERT_EQ(settings, outSettings);
}

TEST_F(HQFramerTest, SettingsFrameTrailingJunk) {
  deque<hq::SettingPair> settings = {
      {hq::SettingId::NUM_PLACEHOLDERS, (SettingValue)3},
      {hq::SettingId::MAX_HEADER_LIST_SIZE, (SettingValue)4},
      // Unknown IDs get ignored, and identifiers of the format
      // "0x1f * N + 0x21" are reserved exactly for this
      {(hq::SettingId)*getGreaseId(1234), (SettingValue)5}};
  writeSettings(queue_, settings);
  // Trim the frame header off
  queue_.trimStart(2);
  auto buf = queue_.move();
  // Put in a new frame header (too long)
  auto badLength = buf->computeChainDataLength() + 1;
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::SETTINGS), badLength);
  queue_.append(std::move(buf));
  queue_.append(IOBuf::copyBuffer("j"));

  FrameHeader header;
  std::deque<hq::SettingPair> outSettings;
  parse(HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_SETTINGS,
        &parseSettings,
        header,
        outSettings);
}

TEST_F(HQFramerTest, SettingsFrameWriteError) {
  deque<hq::SettingPair> settings = {
      {(hq::SettingId)*getGreaseId(54321),
       SettingValue(std::numeric_limits<uint64_t>::max())}};
  auto res = writeSettings(queue_, settings);
  ASSERT_TRUE(res.hasError());
}

TEST_F(HQFramerTest, SettingsFrameUnknownId) {
  deque<hq::SettingPair> settings = {
      {(hq::SettingId)0x1234, SettingValue(100000)}};
  writeSettings(queue_, settings);

  FrameHeader header;
  std::deque<hq::SettingPair> outSettings;
  parse(folly::none, &parseSettings, header, outSettings);

  ASSERT_EQ(proxygen::hq::FrameType::SETTINGS, header.type);
  ASSERT_TRUE(outSettings.empty());
}

TEST_F(HQFramerTest, DecoratedPushIds) {
  PushId testId = 10000;
  PushId internalTestId = testId | kPushIdMask;

  ASSERT_TRUE(proxygen::hq::isExternalPushId(testId));
  ASSERT_FALSE(proxygen::hq::isInternalPushId(testId));

  ASSERT_TRUE(proxygen::hq::isInternalPushId(internalTestId));
  ASSERT_FALSE(proxygen::hq::isExternalPushId(internalTestId));
}
