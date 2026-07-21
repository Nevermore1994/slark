
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.screen.LocalPlayer

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.ViewModel
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.navigation.NavHostController
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerFactory
import com.slark.demo.ui.component.PlayerScreen
import com.slark.demo.ui.model.PlayerViewModel

class SharedViewModel : ViewModel() {
    private val _selectedVideoUris = mutableStateListOf<Uri>()
    var uriVersion by mutableIntStateOf(0)
        private set
    var playerViewModel: PlayerViewModel? by mutableStateOf(null)
        private set
    private var pfd: ParcelFileDescriptor? = null

    val firstUri: Uri? get() = _selectedVideoUris.firstOrNull()

    fun setUris(uris: List<Uri>) {
        _selectedVideoUris.clear()
        _selectedVideoUris.addAll(uris)
        uriVersion++
    }

    fun createPlayer(context: Context) {
        releasePlayer()
        if (_selectedVideoUris.isEmpty()) return
        val uri = _selectedVideoUris[0]
        val path = resolveUri(context, uri)
        val config = SlarkPlayerConfig(path)
        playerViewModel = PlayerViewModel(
            SlarkPlayerFactory.createPlayer(config),
            context.applicationContext
        )
    }

    fun releasePlayer() {
        val old = playerViewModel
        playerViewModel = null
        pfd?.close()
        pfd = null
        old?.release()
    }

    private fun resolveUri(context: Context, uri: Uri): String {
        return if (uri.scheme == "content") {
            val fd = context.contentResolver.openFileDescriptor(uri, "r") ?: return uri.toString()
            pfd = fd
            "/proc/self/fd/${fd.fd}"
        } else {
            uri.toString()
        }
    }

    override fun onCleared() {
        super.onCleared()
        releasePlayer()
        pfd?.close()
        pfd = null
    }
}

@Composable
fun LocalPlayerScreenRoute(navController: NavHostController, sharedViewModel: SharedViewModel) {
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
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }

    val context = LocalContext.current

    DisposableEffect(Unit) {
        onDispose {
            sharedViewModel.releasePlayer()
        }
    }

    LaunchedEffect(sharedViewModel.uriVersion) {
        if (sharedViewModel.uriVersion > 0) {
            sharedViewModel.createPlayer(context)
        }
    }

    sharedViewModel.playerViewModel?.let { playerViewModel ->
        PlayerScreen(
            playerViewModel,
            onBackClick = { navController.popBackStack() }
        )
    }

}