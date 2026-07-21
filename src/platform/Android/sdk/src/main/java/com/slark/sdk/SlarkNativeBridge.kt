
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

import android.content.Context
import android.util.Log

class SlarkNativeBridge {
    companion object {
        private const val TAG = "Slark"

        init {
            try {
                System.loadLibrary("slark")
                Log.d(TAG, "Library loaded successfully")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Failed to load library: ${e.message}")
                throw e
            }
        }

        enum class LogLevel(val value: Int) {
            Print(0), Debug(1), Info(2), Warning(3), Error(4), Assert(5), Record(6);

            companion object {
                fun from(value: Int): LogLevel = entries.find { it.value == value } ?: Print
            }
        }

        @JvmStatic
        fun logout(level: Int, message: String) {
            when (LogLevel.from(level)) {
                LogLevel.Error   -> Log.e(TAG, message)
                LogLevel.Warning -> Log.w(TAG, message)
                LogLevel.Info    -> Log.i(TAG, message)
                LogLevel.Debug   -> Log.d(TAG, message)
                LogLevel.Assert  -> Log.wtf(TAG, message)
                else             -> Log.i(TAG, message)
            }
        }

        external fun setContext(context: Context?)

        external fun setLogConfig(
            progressLogBytes: Long,
            maxLogFileSize: Long,
            maxLogFileCount: Int,
            minLogLevel: Int,
            logDir: String,
            logFileName: String
        )
    }
}
