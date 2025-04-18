package com.slark.sdk

import android.content.Context

class SlarkNativeBridge {

    external fun setContext(context: Context?)

    companion object {
        init {
            System.loadLibrary("sdk")
        }

        @JvmStatic
        fun logout(logString: String) {
            println(logString)
        }
    }

}