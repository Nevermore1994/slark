package com.slark.sdk

import android.content.Context

class SlarkNativeBridge {

    companion object {
        init {
            System.loadLibrary("sdk")
        }

        @JvmStatic
        fun logout(logString: String) {
            println(logString)
        }

        external fun setContext(context: Context?)
    }
}