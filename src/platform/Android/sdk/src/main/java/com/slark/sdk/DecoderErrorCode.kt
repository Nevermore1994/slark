
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
    DecoderNotFound(200),
    DecoderError(201),
    DecoderInputError(202),
    DecoderInputTooLarge(203),
    DecoderNotStarted(204),
    DecoderNotProcessed(205),
    DecoderAgain(206),
    DecoderUnsupportedFormat(207),
    DecoderResourceExhausted(208),
    DecoderConfigInvalid(209),
    NetworkError(300),
    ConnectionFailed(301),
    ConnectionTimeout(302),
    ConnectionReset(303),
    ConnectionRefused(304);

    companion object {
        fun fromValue(value: Int): DecoderErrorCode {
            return entries.firstOrNull { it.value == value } ?: DecoderError
        }
    }
}
