/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "ZstdStreamDecompressor.h"

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>

namespace proxygen {

void ZstdStreamDecompressor::freeDCtx(ZSTD_DCtx* dctx) {
  ZSTD_freeDCtx(dctx);
}

ZstdStreamDecompressor::ZstdStreamDecompressor()
    : status_(ZstdStatusType::NONE), dctx_(ZSTD_createDCtx()) {
}

std::unique_ptr<folly::IOBuf> ZstdStreamDecompressor::decompress(
    const folly::IOBuf* in) {
  if (!dctx_) {
    status_ = ZstdStatusType::ERROR;
  }
  if (hasError()) {
    return nullptr;
  }

  const size_t outBufAllocSize = ZSTD_DStreamOutSize();

  auto out = folly::IOBuf::create(outBufAllocSize);
  auto appender = folly::io::Appender(out.get(), outBufAllocSize);

  for (const folly::ByteRange range : *in) {
    if (range.data() == nullptr) {
      continue;
    }

    ZSTD_inBuffer ibuf = {range.data(), range.size(), 0};
    while (ibuf.pos < ibuf.size) {
      status_ = ZstdStatusType::CONTINUE;
      appender.ensure(outBufAllocSize);
      DCHECK_GT(appender.length(), 0);

      ZSTD_outBuffer obuf = {appender.writableData(), appender.length(), 0};
      auto ret = ZSTD_decompressStream(dctx_.get(), &obuf, &ibuf);
      if (ZSTD_isError(ret)) {
        status_ = ZstdStatusType::ERROR;
        return nullptr;
      } else if (ret == 0) {
        status_ = ZstdStatusType::FINISHED;
      }

      appender.append(obuf.pos);
    }
  }

  return out;
}
} // namespace proxygen
