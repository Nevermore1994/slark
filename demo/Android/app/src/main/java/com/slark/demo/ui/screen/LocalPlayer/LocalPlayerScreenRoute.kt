package com.slark.demo.ui.screen.LocalPlayer

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.ComponentActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.navigation.NavHostController
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerFactory
import com.slark.demo.ui.component.PlayerScreen
import com.slark.demo.ui.model.PlayerViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

class SharedViewModel : ViewModel() {
    val selectedVideoUris = mutableStateListOf<Uri>()
}

@Composable
fun LocalPlayerScreenRoute(navController: NavHostController, sharedViewModel: SharedViewModel) {
    var playerViewModel by remember { mutableStateOf<PlayerViewModel?>(null) }
    var context = LocalContext.current
    val activity = context as ComponentActivity

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
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }

    LaunchedEffect(sharedViewModel.selectedVideoUris) {
        val copiedPaths = copyMultipleVideosToCache(context, sharedViewModel.selectedVideoUris)
        if (copiedPaths.isEmpty()) {
            return@LaunchedEffect
        }
        val config = SlarkPlayerConfig(copiedPaths[0])
        val slarkPlayer: SlarkPlayer? = SlarkPlayerFactory.createPlayer(config)
        playerViewModel = PlayerViewModel(slarkPlayer)
    }

    DisposableEffect(Unit) {
        onDispose {
            playerViewModel?.let { viewModel ->
                viewModel.release()
                playerViewModel = null
            }
        }
    }

    playerViewModel?.let { playerViewModel ->
        PlayerScreen(
            playerViewModel,
            onBackClick = { navController.popBackStack() },
            onPickClick = { navController.navigate("pick_video") }
        )
    }

}

suspend fun copyMultipleVideosToCache(
    context: Context,
    uris: List<Uri>
): List<String> = withContext(Dispatchers.IO) {
    val results = mutableListOf<String>()

    for (uri in uris) {
        try {
            val fileName = getFileNameFromUri(context, uri) ?: "video_${System.currentTimeMillis()}.mp4"
            val dir = File(context.cacheDir.absolutePath + "/temp")
            if (!dir.exists()) {
                dir.mkdirs()
            }
            val outputFile = File(dir, fileName)

            context.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(outputFile).use { output ->
                    input.copyTo(output)
                }
            }

            results.add(outputFile.absolutePath)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    return@withContext results
}


private fun getFileNameFromUri(context: Context, uri: Uri): String? {
    return context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
        val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (cursor.moveToFirst() && nameIndex != -1) {
            cursor.getString(nameIndex)
        } else null
    }
}