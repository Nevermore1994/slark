package com.slark.api

import android.app.Application
import com.slark.sdk.SlarkNativeBridge

class SlarkSdk {
    companion object {
        @JvmStatic
        fun init(app: Application) {
            SlarkNativeBridge.setContext(app.applicationContext)
        }
    }
}