package com.slark.demo.ui.screen.VideoPicker

import android.content.Context
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.memory.MemoryCache
import coil.ImageLoader
import coil.request.ImageRequest
import coil.request.videoFrameMicros
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@Composable
fun VideoPickerScreen(
    onResult: (List<Uri>) -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var videoItems by remember { mutableStateOf<List<VideoItem>>(emptyList()) }
    val selectedItems = remember { mutableStateListOf<Uri>() }

    LaunchedEffect(Unit) {
        val videos = withContext(Dispatchers.IO) {
            queryAllVideos(context)
        }
        videoItems = videos
    }

    Scaffold(
        bottomBar = {
            if (selectedItems.isNotEmpty()) {
                Button(
                    onClick = { onResult(selectedItems.toList()) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp)
                ) {
                    Text("确定（${selectedItems.size}）")
                }
            }
        }
    ) { paddingValues ->
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minSize = 100.dp),
            contentPadding = paddingValues,
            modifier = Modifier.fillMaxSize()
        ) {
            items(videoItems) { video ->
                val isSelected = selectedItems.contains(video.uri)
                Box(
                    modifier = Modifier
                        .padding(4.dp)
                        .aspectRatio(1f)
                        .clickable {
                            if (isSelected) {
                                selectedItems.remove(video.uri)
                            } else {
                                selectedItems.add(video.uri)
                            }
                        }
                        .border(
                            2.dp,
                            if (isSelected) Color.Green else Color.Transparent,
                            RoundedCornerShape(8.dp)
                        )
                ) {
                    VideoThumbnail(video.uri, Modifier.fillMaxSize())
                    Text(
                        text = formatDuration(video.durationMs),
                        color = Color.White,
                        fontSize = 12.sp,
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .background(Color.Black.copy(alpha = 0.5f))
                            .padding(4.dp)
                    )
                }
            }
        }
    }
}

object ImageLoaderFactory {
    private var imageLoader: ImageLoader? = null
    private const val MEMORY_CACHE_SIZE = 20 * 1024 * 1024 // 20MB

    fun create(context: Context): ImageLoader {
        if (imageLoader == null) {
            imageLoader = ImageLoader.Builder(context)
                .components {
                    add(VideoFrameDecoder.Factory())
                }
                .memoryCache {
                    MemoryCache.Builder(context)
                        .maxSizeBytes(MEMORY_CACHE_SIZE)
                        .build()
                }
                .build()
        }
        return imageLoader!!
    }

    fun clear() {
        imageLoader?.memoryCache?.clear()
    }
}

@Composable
fun VideoThumbnail(uri: Uri, modifier: Modifier = Modifier) {
    val context = LocalContext.current

    val imageLoader = remember {
        ImageLoaderFactory.create(context)
    }

    AsyncImage(
        model = ImageRequest.Builder(context)
            .data(uri)
            .videoFrameMicros(0)
            .build(),
        imageLoader = imageLoader,
        contentDescription = null,
        contentScale = ContentScale.Crop,
        modifier = modifier
    )
}
