package com.slark.sdk

import android.media.AudioProfile
import android.media.MediaCodecInfo
import java.nio.ByteBuffer

fun extractBytes(buffer: ByteBuffer): ByteArray {
    val slice = buffer.slice()
    val result = ByteArray(slice.remaining())
    slice.get(result)
    return result
}