package com.slark.demo.ui.screen.VideoPicker

import android.content.ContentUris
import android.content.Context
import android.net.Uri
import android.provider.MediaStore
import java.util.Locale

data class VideoItem(
    val uri: Uri,
    val durationMs: Long
)

fun queryAllVideos(context: Context): List<VideoItem> {
    val projection = arrayOf(
        MediaStore.Video.Media._ID,
        MediaStore.Video.Media.DURATION
    )

    val videos = mutableListOf<VideoItem>()
    val uriExternal = MediaStore.Video.Media.EXTERNAL_CONTENT_URI

    context.contentResolver.query(
        uriExternal,
        projection,
        null,
        null,
        "${MediaStore.Video.Media.DATE_ADDED} DESC"
    )?.use { cursor ->
        val idColumn = cursor.getColumnIndexOrThrow(MediaStore.Video.Media._ID)
        val durationColumn = cursor.getColumnIndexOrThrow(MediaStore.Video.Media.DURATION)

        while (cursor.moveToNext()) {
            val id = cursor.getLong(idColumn)
            val duration = cursor.getLong(durationColumn)
            val contentUri = ContentUris.withAppendedId(uriExternal, id)
            videos.add(VideoItem(contentUri, duration))
        }
    }

    return videos
}

fun formatDuration(ms: Long): String {
    val totalSec = ms / 1000
    val min = totalSec / 60
    val sec = totalSec % 60
    return String.format(Locale.ENGLISH, "Video length:%02d:%02d", min, sec)
}