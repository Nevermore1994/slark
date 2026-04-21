
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


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
