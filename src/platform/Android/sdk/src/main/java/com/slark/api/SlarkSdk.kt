
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.api

import android.app.Application
import com.slark.sdk.MediaCodecDecoder
import com.slark.sdk.MediaCodecDecoderPool
import com.slark.sdk.SlarkNativeBridge

class SlarkSdk {
    companion object {
        @JvmStatic
        @JvmOverloads
        fun init(app: Application, logConfig: SlarkLogConfig? = null) {
            SlarkNativeBridge.setContext(app.applicationContext)
            logConfig?.let {
                SlarkNativeBridge.setLogConfig(
                    it.progressLogBytes,
                    it.maxLogFileSize,
                    it.maxLogFileCount,
                    it.minLogLevel.value,
                    it.logDir,
                    it.logFileName
                )
            }
            MediaCodecDecoderPool.warmUpAsync()
        }
    }
}
