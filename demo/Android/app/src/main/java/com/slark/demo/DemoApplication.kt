package com.slark.demo

import android.app.Application
import com.slark.api.SlarkSdk

class DemoApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Thread { SlarkSdk.init(this) }.start()
    }
}
