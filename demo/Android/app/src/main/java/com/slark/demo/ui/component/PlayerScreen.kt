// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

package com.slark.demo.ui.component

import android.view.TextureView
import android.view.ViewGroup
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import android.widget.Toast
import androidx.compose.animation.core.*
import androidx.compose.foundation.Image
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.painterResource
import com.slark.demo.R
import com.slark.demo.ui.model.PlayerViewModel
import kotlinx.coroutines.*

@Composable
fun PlayerScreen(
    viewModel: PlayerViewModel,
    onBackClick: () -> Unit,
    onPickClick: () -> Unit
) {
    var controlsVisible by remember { mutableStateOf(true) }
    var hideJob by remember { mutableStateOf<Job?>(null) }
    val context = LocalContext.current
    val errorMessage = viewModel.errorMessage

    LaunchedEffect(errorMessage) {
        errorMessage?.let {
            Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
            viewModel.errorMessage = null
        }
    }

    fun showControls() {
        controlsVisible = true
        hideJob?.cancel()
        hideJob = CoroutineScope(Dispatchers.Main).launch {
            delay(5000)
            controlsVisible = false
        }
    }

    LaunchedEffect(Unit) { showControls() }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                detectTapGestures { showControls() }
            }
    ) {
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
            modifier = Modifier.fillMaxSize()
        )

        if (viewModel.isLoading) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                RotatingImage()
            }
        }

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

            PlayerControls(
                viewModel,
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(16.dp)
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
        modifier = Modifier.size(75.dp).graphicsLayer { rotationZ = angle }
    )
}
