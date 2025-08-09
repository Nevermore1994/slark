package com.slark.api

import android.app.Application
import com.slark.sdk.MediaCodecDecoder
import com.slark.sdk.MediaCodecDecoderPool
import com.slark.sdk.SlarkNativeBridge

class SlarkSdk {
    companion object {
        @JvmStatic
        fun init(app: Application) {
            SlarkNativeBridge.setContext(app.applicationContext)
            MediaCodecDecoderPool.warmUpAsync()
        }
    }
}