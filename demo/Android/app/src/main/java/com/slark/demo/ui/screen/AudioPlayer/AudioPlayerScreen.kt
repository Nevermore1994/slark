//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.screen.AudioPlayer

import android.content.Context
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.navigation.NavHostController
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerFactory
import com.slark.demo.R
import com.slark.demo.ui.component.PlayerControls
import com.slark.demo.ui.model.PlayerViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

@Composable
fun AudioPlayerScreen(viewModel: PlayerViewModel) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Image(
            painter = painterResource(id = R.drawable.music_normal),
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(horizontal = 60.dp, vertical = 40.dp)
        )
        PlayerControls(
            viewModel = viewModel,
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 40.dp)
        )
    }
}

@Composable
fun AudioPlayerScreenRoute(navController: NavHostController) {
    var playerViewModel by remember { mutableStateOf<PlayerViewModel?>(null) }
    val context = LocalContext.current

    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> playerViewModel?.onBackground(false)
                Lifecycle.Event.ON_PAUSE -> playerViewModel?.onBackground(true)
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    LaunchedEffect(Unit) {
        val path = copyAssetToCache(context, "sample-3s.wav")
        if (path != null) {
            val config = SlarkPlayerConfig(path)
            val player: SlarkPlayer? = SlarkPlayerFactory.createPlayer(config)
            playerViewModel = PlayerViewModel(player, context.applicationContext)
        }
    }

    DisposableEffect(Unit) {
        onDispose {
            playerViewModel?.release()
            playerViewModel = null
        }
    }

    playerViewModel?.let { AudioPlayerScreen(it) }
}

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
