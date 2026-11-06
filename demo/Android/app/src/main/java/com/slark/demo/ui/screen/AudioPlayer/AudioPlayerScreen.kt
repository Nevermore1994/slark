//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.screen.AudioPlayer

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import com.slark.demo.R
import com.slark.demo.ui.component.kControlTint
import com.slark.demo.ui.model.PlayerViewModel
import com.slark.demo.ui.screen.LocalPlayer.SharedViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

private data class AudioTrack(val title: String, val uri: Uri)

private const val kBundledAssetName = "sample-3s.wav"

@Composable
fun AudioPlayerScreenRoute(navController: NavHostController, innerPadding: PaddingValues) {
    val context = LocalContext.current
    val sharedViewModel: SharedViewModel = viewModel()
    val scope = rememberCoroutineScope()

    var tracks by rememberSaveable { mutableStateOf<List<AudioTrack>>(emptyList()) }
    var currentIndex by rememberSaveable { mutableIntStateOf(-1) }
    var loopEnabled by rememberSaveable { mutableStateOf(false) }

    fun playIndex(index: Int) {
        if (index !in tracks.indices) return
        currentIndex = index
        sharedViewModel.setUris(listOf(tracks[index].uri))
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        // The system grant is per-session; keep the URI readable across page changes.
        runCatching {
            context.contentResolver.takePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val added = tracks + AudioTrack(displayName(context, uri), uri)
        tracks = added
        currentIndex = added.lastIndex
        sharedViewModel.setUris(listOf(uri))
    }

    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> sharedViewModel.playerViewModel?.onBackground(false)
                Lifecycle.Event.ON_PAUSE -> sharedViewModel.playerViewModel?.onBackground(true)
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    LaunchedEffect(Unit) {
        if (tracks.isEmpty()) {
            val path = copyAssetToCache(context, kBundledAssetName)
            if (path != null) {
                tracks = listOf(AudioTrack(kBundledAssetName, Uri.parse(path)))
                playIndex(0)
            }
        } else if (currentIndex >= 0 && sharedViewModel.firstUri == null) {
            sharedViewModel.setUris(listOf(tracks[currentIndex].uri))
        }
    }

    LaunchedEffect(sharedViewModel.uriVersion) {
        if (sharedViewModel.uriVersion > 0) {
            sharedViewModel.createPlayer(context)
            sharedViewModel.playerViewModel?.loop(loopEnabled)
        }
    }

    DisposableEffect(Unit) {
        onDispose { sharedViewModel.releasePlayer() }
    }

    val player = sharedViewModel.playerViewModel
    val title = tracks.getOrNull(currentIndex)?.title.orEmpty()

    if (player != null) {
        AudioPlayerScreen(
            viewModel = player,
            title = title,
            tracks = tracks,
            currentIndex = currentIndex,
            innerPadding = innerPadding,
            onSelect = ::playIndex,
            onPrev = { playIndex(if (currentIndex > 0) currentIndex - 1 else tracks.lastIndex) },
            onNext = { playIndex(if (currentIndex < tracks.lastIndex) currentIndex + 1 else 0) },
            onAdd = { picker.launch(arrayOf("audio/*")) },
            loop = loopEnabled,
            onToggleLoop = {
                loopEnabled = !loopEnabled
                sharedViewModel.playerViewModel?.loop(loopEnabled)
            }
        )
    }
}

@Composable
private fun AudioPlayerScreen(
    viewModel: PlayerViewModel,
    title: String,
    tracks: List<AudioTrack>,
    currentIndex: Int,
    innerPadding: PaddingValues,
    onSelect: (Int) -> Unit,
    onPrev: () -> Unit,
    onNext: () -> Unit,
    onAdd: () -> Unit,
    loop: Boolean,
    onToggleLoop: () -> Unit
) {
    LaunchedEffect(viewModel) {
        viewModel.prepare()
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .padding(innerPadding)
            .padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = title,
            color = kControlTint,
            fontSize = 16.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
        Spacer(modifier = Modifier.height(16.dp))
        Image(
            painter = painterResource(id = R.drawable.music_normal),
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(vertical = 16.dp)
        )
        AudioProgress(viewModel)
        AudioTransport(viewModel, loop, onPrev, onNext, onToggleLoop)
        Spacer(modifier = Modifier.height(8.dp))
        TrackList(
            tracks = tracks,
            currentIndex = currentIndex,
            onSelect = onSelect,
            onAdd = onAdd,
            modifier = Modifier
                .fillMaxWidth()
                .height(200.dp)
        )
    }
}

@Composable
private fun AudioProgress(viewModel: PlayerViewModel) {
    val total = viewModel.totalTime
    val fraction = if (total > 0.0) (viewModel.currentTime / total).toFloat().coerceIn(0f, 1f) else 0f
    Column(modifier = Modifier.fillMaxWidth()) {
        Slider(
            value = fraction,
            onValueChange = { f ->
                val target = (f * total).toDouble()
                viewModel.currentTime = target
                viewModel.seekTo(target, false)
            },
            enabled = viewModel.isSeekable && total > 0.0,
            colors = SliderDefaults.colors(
                thumbColor = kControlTint,
                activeTrackColor = kControlTint,
                inactiveTrackColor = Color.White.copy(alpha = 0.3f)
            )
        )
        Row(modifier = Modifier.fillMaxWidth()) {
            Text(text = formatSeconds(viewModel.currentTime), color = kControlTint, fontSize = 12.sp)
            Spacer(modifier = Modifier.weight(1f))
            Text(text = formatSeconds(total), color = kControlTint, fontSize = 12.sp)
        }
    }
}

@Composable
private fun AudioTransport(
    viewModel: PlayerViewModel,
    loop: Boolean,
    onPrev: () -> Unit,
    onNext: () -> Unit,
    onToggleLoop: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconButton(onClick = onPrev) {
            Icon(
                painter = painterResource(id = R.drawable.prev),
                contentDescription = "Previous",
                tint = kControlTint,
                modifier = Modifier.size(36.dp)
            )
        }
        IconButton(onClick = { viewModel.play(!viewModel.isPlaying) }) {
            Icon(
                painter = painterResource(
                    id = if (viewModel.isPlaying) R.drawable.pause_icon else R.drawable.play_icon
                ),
                contentDescription = if (viewModel.isPlaying) "Pause" else "Play",
                tint = kControlTint,
                modifier = Modifier.size(56.dp)
            )
        }
        IconButton(onClick = onNext) {
            Icon(
                painter = painterResource(id = R.drawable.next),
                contentDescription = "Next",
                tint = kControlTint,
                modifier = Modifier.size(36.dp)
            )
        }
        IconButton(onClick = onToggleLoop) {
            Icon(
                painter = painterResource(id = if (loop) R.drawable.loop else R.drawable.stop_loop),
                contentDescription = "Loop",
                tint = kControlTint,
                modifier = Modifier.size(28.dp)
            )
        }
    }
}

@Composable
private fun TrackList(
    tracks: List<AudioTrack>,
    currentIndex: Int,
    onSelect: (Int) -> Unit,
    onAdd: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onAdd)
                .padding(vertical = 10.dp),
            horizontalArrangement = Arrangement.Center
        ) {
            Text(text = "+ Add audio file", color = kControlTint, fontSize = 14.sp)
        }
        HorizontalDivider(color = Color.White.copy(alpha = 0.3f))
        LazyColumn(modifier = Modifier.fillMaxWidth()) {
            itemsIndexed(tracks) { index, track ->
                val selected = index == currentIndex
                Text(
                    text = track.title,
                    color = if (selected) Color(0xFF4A9EFF) else kControlTint,
                    fontSize = 14.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onSelect(index) }
                        .padding(vertical = 12.dp, horizontal = 4.dp)
                )
                HorizontalDivider(color = Color.White.copy(alpha = 0.15f))
            }
        }
    }
}

private fun formatSeconds(seconds: Double): String {
    val total = seconds.toLong().coerceAtLeast(0L)
    return "%02d:%02d".format(total / 60, total % 60)
}

private fun displayName(context: Context, uri: Uri): String =
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
        ?.use { if (it.moveToFirst()) it.getString(0) else null }
        ?: uri.lastPathSegment.orEmpty()

private suspend fun copyAssetToCache(context: Context, assetName: String): String? =
    withContext(Dispatchers.IO) {
        try {
            val outFile = File(context.cacheDir, assetName)
            if (!outFile.exists()) {
                context.assets.open(assetName).use { input ->
                    FileOutputStream(outFile).use { output -> input.copyTo(output) }
                }
            }
            outFile.absolutePath
        } catch (e: Exception) {
            null
        }
    }
