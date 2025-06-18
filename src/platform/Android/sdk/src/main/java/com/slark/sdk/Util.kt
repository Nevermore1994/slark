package com.slark.sdk

import android.util.Size
import java.nio.ByteBuffer
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

fun extractBytes(buffer: ByteBuffer): ByteArray {
    val slice = buffer.slice()
    val result = ByteArray(slice.remaining())
    slice.get(result)
    return result
}

fun makeAspectRatioSize(
    aspectSize: Size,
    boundingSize: Size
): Size {
    val targetRatio = aspectSize.width.toFloat() / aspectSize.height
    val containerRatio = boundingSize.width.toFloat() / boundingSize.height

    return if (targetRatio > containerRatio) {
        return Size(boundingSize.width, (boundingSize.width / targetRatio).toInt())
    } else {
        return Size((boundingSize.height * targetRatio).toInt(), boundingSize.height)
    }
}

fun LocalDateTime.toMicrosecondString(): String {
    val formatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
    val formattedTime = this.format(formatter)
    val nanoOfSecond = this.nano
    val microseconds = nanoOfSecond / 1000
    return "$formattedTime.${microseconds.toString().padStart(6, '0')}"
}
