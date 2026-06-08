// Copyright (c) 2026 Nevermore-. All rights reserved.

package com.slark.demo.ui.component

import android.content.Context
import android.view.Surface
import android.view.TextureView
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Toast
import androidx.compose.animation.core.*
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.slark.demo.R
import com.slark.demo.ui.model.PlayerViewModel
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun PlayerScreen(
    viewModel: PlayerViewModel,
    onBackClick: () -> Unit,
    onPickClick: (() -> Unit)? = null
) {
    var controlsVisible by remember { mutableStateOf(true) }
    val scope = rememberCoroutineScope()
    var hideJob by remember { mutableStateOf<Job?>(null) }
    val context = LocalContext.current
    val errorMessage = viewModel.errorMessage

    // 屏幕方向
    val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    val displayRotation = windowManager.defaultDisplay.rotation
    val screenRotationDeg = when (displayRotation) {
        Surface.ROTATION_90  -> 90
        Surface.ROTATION_180 -> 180
        Surface.ROTATION_270 -> 270
        else -> 0
    }

    LaunchedEffect(errorMessage) {
        errorMessage?.let {
            Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
            viewModel.errorMessage = null
        }
    }

    fun showControls() {
        controlsVisible = true
        hideJob?.cancel()
        hideJob = scope.launch {
            delay(4000)
            controlsVisible = false
        }
    }

    LaunchedEffect(Unit) {
        viewModel.prepare()
        showControls()
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                detectTapGestures { showControls() }
            }
    ) {
        val totalRotation = (viewModel.rotationDegrees + screenRotationDeg) % 360

        // 视频渲染
        AndroidView(
            factory = { ctx ->
                TextureView(ctx).apply {
                    layoutParams = ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT
                    )
                    viewModel.setRenderTarget(this)
                }
            },
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer { rotationZ = totalRotation.toFloat() }
        )

        // loading
        if (viewModel.isLoading) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                RotatingImage()
            }
        }

        // 中央播放/暂停
        if (controlsVisible) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .size(72.dp)
                        .clip(CircleShape)
                        .background(Color.Black.copy(alpha = 0.5f))
                        .clickable { viewModel.play(!viewModel.isPlaying) },
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        painter = painterResource(
                            id = if (viewModel.isPlaying) R.drawable.pause_icon else R.drawable.play_icon
                        ),
                        contentDescription = if (viewModel.isPlaying) "Pause" else "Play",
                        tint = Color.White,
                        modifier = Modifier.size(52.dp)
                    )
                }
            }
        }

        // 顶部返回
        if (controlsVisible) {
            IconButton(
                onClick = onBackClick,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(top = 48.dp, start = 8.dp)
                    .size(40.dp)
            ) {
                Icon(
                    imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = "Back",
                    tint = Color.White
                )
            }
        }

        // 底部控制面板
        if (controlsVisible) {
            PlayerControls(
                viewModel,
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
            )
        }
    }
}

@Composable
fun RotatingImage() {
    val infiniteTransition = rememberInfiniteTransition(label = "")
    val angle by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(
            animation = tween(1000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = ""
    )
    Image(
        painter = painterResource(id = R.drawable.loading),
        contentDescription = null,
        colorFilter = ColorFilter.tint(Color.LightGray),
        modifier = Modifier
            .size(75.dp)
            .graphicsLayer { rotationZ = angle }
    )
}
