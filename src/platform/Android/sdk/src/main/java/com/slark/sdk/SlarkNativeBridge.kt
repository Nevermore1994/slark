package com.slark.sdk

import android.content.Context

class SlarkNativeBridge {
    companion object {
        init {
            try {
                System.loadLibrary("sdk")
                android.util.Log.d("SlarkSdk", "Library loaded successfully")
            } catch (e: UnsatisfiedLinkError) {
                android.util.Log.e("SlarkSdk", "Failed to load library: ${e.message}")
                throw e
            }
        }

        @JvmStatic
        fun logout(logString: String) {
            println(logString)
        }

        external fun setContext(context: Context?)
    }
}