//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Error.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(Error, ToStringGeneral) {
    EXPECT_EQ(toString(Error::Success), "Success");
    EXPECT_EQ(toString(Error::OutOfMemory), "Out of memory");
    EXPECT_EQ(toString(Error::OutOfRange), "Out of range");
    EXPECT_EQ(toString(Error::NullPointer), "Null pointer");
    EXPECT_EQ(toString(Error::BufferOverflow), "Buffer overflow");
    EXPECT_EQ(toString(Error::InvalidOperation), "Invalid operation");
    EXPECT_EQ(toString(Error::InvalidArgument), "Invalid argument");
    EXPECT_EQ(toString(Error::SizeTooLarge), "Size too large");
    EXPECT_EQ(toString(Error::EmptyData), "Empty data");
    EXPECT_EQ(toString(Error::NotFound), "Not found");
    EXPECT_EQ(toString(Error::Cancelled), "Cancelled");
}

TEST(Error, ToStringIO) {
    EXPECT_EQ(toString(Error::IOError), "I/O error");
    EXPECT_EQ(toString(Error::EndOfFile), "End of file");
    EXPECT_EQ(toString(Error::PermissionDenied), "Permission denied");
    EXPECT_EQ(toString(Error::FileNotFound), "File not found");
    EXPECT_EQ(toString(Error::FileAlreadyExists), "File already exists");
    EXPECT_EQ(toString(Error::StreamNotReady), "Stream not ready");
    EXPECT_EQ(toString(Error::StreamInvalidData), "Stream invalid data");
}

TEST(Error, ToStringCodec) {
    EXPECT_EQ(toString(Error::CodecNotFound), "Codec not found");
    EXPECT_EQ(toString(Error::CodecError), "Codec error");
    EXPECT_EQ(toString(Error::CodecInputError), "Codec input error");
    EXPECT_EQ(toString(Error::CodecAgain), "Codec needs more input");
    EXPECT_EQ(toString(Error::CodecUnsupportedFormat), "Codec unsupported format");
}

TEST(Error, ToStringNetwork) {
    EXPECT_EQ(toString(Error::NetworkError), "Network error");
    EXPECT_EQ(toString(Error::ConnectionFailed), "Connection failed");
    EXPECT_EQ(toString(Error::ConnectionTimeout), "Connection timeout");
    EXPECT_EQ(toString(Error::DnsResolveFailed), "DNS resolve failed");
    EXPECT_EQ(toString(Error::WouldBlock), "Operation would block");
    EXPECT_EQ(toString(Error::BrokenPipe), "Broken pipe");
}

TEST(Error, ToStringHttp) {
    EXPECT_EQ(toString(Error::HttpError), "HTTP error");
    EXPECT_EQ(toString(Error::HttpTimeout), "HTTP timeout");
    EXPECT_EQ(toString(Error::HttpSslError), "HTTP SSL error");
    EXPECT_EQ(toString(Error::HttpSslHandshakeFailed), "HTTP SSL handshake failed");
}

TEST(Error, ToStringMedia) {
    EXPECT_EQ(toString(Error::MediaOpenFailed), "Media open failed");
    EXPECT_EQ(toString(Error::MediaEndOfFile), "Media end of file");
    EXPECT_EQ(toString(Error::MediaInvalidData), "Media invalid data");
}

TEST(Error, ToStringPipeline) {
    EXPECT_EQ(toString(Error::PipelineOpenFailed), "Pipeline open failed");
    EXPECT_EQ(toString(Error::PipelineFormatNotSupported), "Pipeline format not supported");
    EXPECT_EQ(toString(Error::PipelineDemuxError), "Pipeline demux error");
    EXPECT_EQ(toString(Error::PipelineDecodeError), "Pipeline decode error");
    EXPECT_EQ(toString(Error::PipelineRenderError), "Pipeline render error");
}

TEST(Error, ToStringUnknown) {
    auto unknown = static_cast<Error>(9999);
    EXPECT_EQ(toString(unknown), "Unknown error");
}

TEST(Error, ExpectedSuccess) {
    Expected<int32_t> result = 42;
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
    EXPECT_TRUE(isSuccess(result));
}

TEST(Error, ExpectedFailure) {
    Expected<int32_t> result = makeUnexpected<int32_t>(Error::NotFound);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::NotFound);
    EXPECT_FALSE(isSuccess(result));
}

TEST(Error, ValueOr) {
    Expected<int32_t> ok = 10;
    Expected<int32_t> fail = makeUnexpected<int32_t>(Error::IOError);
    EXPECT_EQ(valueOr(ok, 99), 10);
    EXPECT_EQ(valueOr(fail, 99), 99);
}
