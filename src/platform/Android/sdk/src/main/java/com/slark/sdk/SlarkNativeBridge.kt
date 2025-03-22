package com.slark.sdk

import android.content.Context

class SlarkNativeBridge {

    external fun setContext(context: Context?)

    companion object {
        // Used to load the 'sdk' library on application startup.
        init {
            System.loadLibrary("sdk")
        }

        @JvmStatic
        fun logout(logString: String) {
            println(logString)
        }
    }

}