package com.slark.sdk

import android.media.AudioFormat
import android.media.MediaFormat
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

fun Int.hasFlag(flag: Int) = (this and flag) == flag

fun MediaFormat.isAudioFormat(): Boolean {
    return this.getString(MediaFormat.KEY_MIME)?.let { mime ->
        isAudioMime(mime)
    } == true
}

fun MediaFormat.isVideoFormat(): Boolean {
    return this.getString(MediaFormat.KEY_MIME)?.let { mime ->
        isVideoMime(mime)
    } == true
}

fun isVideoMime(mime: String): Boolean {
    return mime.startsWith("video/") == true
}

fun isAudioMime(mime: String): Boolean {
    return mime.startsWith("audio/") == true
}

fun getChannelConfig(channelCount: Int): Int {
    return when (channelCount) {
        1 -> AudioFormat.CHANNEL_OUT_MONO
        2 -> AudioFormat.CHANNEL_OUT_STEREO
        4 -> AudioFormat.CHANNEL_OUT_QUAD
        6 -> AudioFormat.CHANNEL_OUT_5POINT1
        else -> AudioFormat.CHANNEL_OUT_DEFAULT
    }
}