
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

enum class DecoderErrorCode(val value: Int) {
    Success(0),
    OutOfMemory(1),
    OutOfRange(2),
    NullPointer(3),
    BufferOverflow(4),
    InvalidOperation(5),
    InvalidArgument(6),
    SizeTooLarge(7),
    EmptyData(8),
    NotFound(9),
    Cancelled(10),
    IOError(100),
    EndOfFile(101),
    PermissionDenied(102),
    FileNotFound(103),
    FileAlreadyExists(104),
    StreamNotReady(110),
    StreamInvalidData(111),
    CodecNotFound(200),
    CodecError(201),
    CodecInputError(202),
    CodecInputTooLarge(203),
    CodecNotStarted(204),
    CodecNotProcessed(205),
    CodecAgain(206),
    CodecUnsupportedFormat(207),
    CodecResourceExhausted(208),
    CodecConfigInvalid(209),
    NetworkError(300),
    ConnectionFailed(301),
    ConnectionTimeout(302),
    ConnectionReset(303),
    ConnectionRefused(304);

    companion object {
        fun fromValue(value: Int): DecoderErrorCode {
            return entries.firstOrNull { it.value == value } ?: CodecError
        }
    }
}
