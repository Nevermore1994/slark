//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.component

import androidx.compose.animation.Crossfade
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.constraintlayout.compose.ConstraintLayout
import androidx.constraintlayout.compose.Dimension
import com.slark.demo.R
import com.slark.demo.ui.model.PlayerViewModel
import com.slark.sdk.GraphicsBackend
import kotlin.math.round

private val kSpeedOptions = listOf(0.5f, 1.0f, 1.5f, 2.0f)

private fun formatTime(ms: Long): String {
    val totalSeconds = ((ms + 500) / 1000).toInt()
    val hours = totalSeconds / 3600
    val minutes = (totalSeconds % 3600) / 60
    val seconds = totalSeconds % 60
    return if (hours > 0) "%02d:%02d:%02d".format(hours, minutes, seconds)
    else "%02d:%02d".format(minutes, seconds)
}

@Composable
fun PlayerControls(
    viewModel: PlayerViewModel,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Bottom
    ) {
        PlayerTopBar(viewModel)
        Spacer(modifier = Modifier.height(8.dp))
        PlayerProgressBar(viewModel)
        Spacer(modifier = Modifier.height(4.dp))
        PlayerControlBar(viewModel)
    }
}

@Composable
fun PlayerTopBar(viewModel: PlayerViewModel) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Spacer(modifier = Modifier.weight(1f))

        IconButton(
            onClick = { viewModel.rotate() },
            modifier = Modifier.size(32.dp)
        ) {
            Icon(
                painterResource(id = R.drawable.ic_rotate),
                contentDescription = "Rotate",
                tint = Color(0xFFE6E6E6),
                modifier = Modifier.size(22.dp)
            )
        }

        Spacer(modifier = Modifier.width(12.dp))

        RenderBackendButton(
            backend = viewModel.graphicsBackend,
            onClick = { viewModel.switchRenderBackend() }
        )
    }
}

@Composable
fun RenderBackendButton(backend: GraphicsBackend, onClick: () -> Unit) {
    val label = if (backend == GraphicsBackend.OpenGL) "OpenGL" else "Vulkan"
    Box(
        modifier = Modifier
            .border(1.dp, Color(0xFFE6E6E6), RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 4.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text = label, color = Color(0xFFE6E6E6), fontSize = 12.sp)
    }
}

@Composable
fun PlayerControlBar(viewModel: PlayerViewModel) {
    ConstraintLayout(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp)
    ) {
        val (muteIcon, volumeSlider, prevBtn, playBtn, nextBtn, loopBtn, speedBtn) = createRefs()

        IconButton(
            onClick = { viewModel.mute(!viewModel.isMute) },
            modifier = Modifier
                .size(28.dp)
                .constrainAs(muteIcon) {
                    start.linkTo(parent.start)
                    centerVerticallyTo(parent)
                }
        ) {
            Crossfade(targetState = viewModel.isMute, label = "") {
                Icon(
                    painterResource(id = if (it) R.drawable.mute else R.drawable.volume),
                    contentDescription = null,
                    tint = Color(0xFFE6E6E6)
                )
            }
        }

        CustomSlider(
            value = viewModel.volume,
            onValueChange = { t, _ -> viewModel.volume = t },
            valueRange = 0f..100f,
            trackColor = Color.LightGray,
            activeColor = Color(0xFFE6E6E6),
            modifier = Modifier.constrainAs(volumeSlider) {
                start.linkTo(muteIcon.end, margin = 4.dp)
                centerVerticallyTo(parent)
                width = Dimension.percent(0.18f)
            }
        )

        IconButton(
            onClick = { viewModel.skipPrev() },
            enabled = false,
            modifier = Modifier
                .size(40.dp)
                .constrainAs(prevBtn) {
                    end.linkTo(playBtn.start, margin = 20.dp)
                    centerVerticallyTo(parent)
                }
        ) {
            Icon(
                painterResource(id = R.drawable.prev),
                contentDescription = null,
                tint = Color(0xFFE6E6E6).copy(alpha = 0.3f)
            )
        }

        IconButton(
            onClick = { viewModel.play(!viewModel.isPlaying) },
            modifier = Modifier
                .size(40.dp)
                .constrainAs(playBtn) { centerTo(parent) }
        ) {
            Crossfade(targetState = viewModel.isPlaying, label = "") {
                Icon(
                    painter = painterResource(id = if (it) R.drawable.pause_icon else R.drawable.play_icon),
                    contentDescription = null,
                    tint = Color(0xFFE6E6E6),
                    modifier = Modifier.size(52.dp)
                )
            }
        }

        IconButton(
            onClick = { viewModel.skipNext() },
            enabled = false,
            modifier = Modifier
                .size(40.dp)
                .constrainAs(nextBtn) {
                    start.linkTo(playBtn.end, margin = 20.dp)
                    centerVerticallyTo(parent)
                }
        ) {
            Icon(
                painterResource(id = R.drawable.next),
                contentDescription = null,
                tint = Color(0xFFE6E6E6).copy(alpha = 0.3f)
            )
        }

        IconButton(
            onClick = { viewModel.loop(!viewModel.isLoop) },
            modifier = Modifier
                .size(28.dp)
                .constrainAs(loopBtn) {
                    end.linkTo(speedBtn.start, margin = 12.dp)
                    centerVerticallyTo(parent)
                }
        ) {
            Crossfade(targetState = viewModel.isLoop, label = "") {
                Icon(
                    painter = painterResource(id = if (it) R.drawable.loop else R.drawable.stop_loop),
                    contentDescription = null,
                    tint = Color(0xFFE6E6E6)
                )
            }
        }

        SpeedButton(
            speed = viewModel.speed,
            onSpeedChange = { viewModel.setSpeed(it) },
            modifier = Modifier.constrainAs(speedBtn) {
                end.linkTo(parent.end)
                centerVerticallyTo(parent)
            }
        )
    }
}

@Composable
fun SpeedButton(speed: Float, onSpeedChange: (Float) -> Unit, modifier: Modifier = Modifier) {
    val label = if (speed == speed.toLong().toFloat()) "%.0fx".format(speed) else "%.1fx".format(speed)
    Box(
        modifier = modifier
            .border(1.dp, Color(0xFFE6E6E6), RoundedCornerShape(6.dp))
            .clickable {
                val idx = kSpeedOptions.indexOfFirst { kotlin.math.abs(it - speed) < 0.01f }
                val next = kSpeedOptions[(if (idx < 0) 1 else (idx + 1) % kSpeedOptions.size)]
                onSpeedChange(next)
            }
            .padding(horizontal = 8.dp, vertical = 4.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text = label, color = Color(0xFFE6E6E6), fontSize = 13.sp)
    }
}

@Composable
fun PlayerProgressBar(viewModel: PlayerViewModel) {
    val currentTime = (viewModel.currentTime * 1000).toLong()
    val totalTime = (viewModel.totalTime * 1000).toLong()
    val cacheTime = (viewModel.cacheTime * 1000).toLong()

    ConstraintLayout(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp)
    ) {
        val (startTime, endTime, slider) = createRefs()

        CustomSlider(
            value = if (totalTime > 0) (currentTime.toFloat() / totalTime.toFloat()) else 0f,
            onValueChange = { t, b ->
                if (!b) viewModel.currentTime = (t * totalTime) / 1000.0
                viewModel.seekTo(viewModel.currentTime, !b)
            },
            valueRange = 0f..1f,
            bufferedColor = Color.Gray,
            bufferedValue = if (totalTime > 0) (cacheTime.toFloat() / totalTime.toFloat()) else 0f,
            modifier = Modifier
                .fillMaxWidth()
                .height(24.dp)
                .constrainAs(slider) {
                    top.linkTo(parent.top)
                    start.linkTo(parent.start)
                    end.linkTo(parent.end)
                }
        )

        val timeStyle = MaterialTheme.typography.labelMedium.copy(
            shadow = Shadow(color = Color.Black, offset = Offset(1f, 1f), blurRadius = 2f)
        )
        Text(
            text = formatTime(currentTime),
            color = Color.LightGray,
            style = timeStyle,
            modifier = Modifier.constrainAs(startTime) {
                start.linkTo(parent.start)
                top.linkTo(slider.bottom, margin = 2.dp)
            }
        )
        Text(
            text = formatTime(totalTime),
            color = Color.LightGray,
            style = timeStyle,
            modifier = Modifier.constrainAs(endTime) {
                end.linkTo(parent.end)
                top.linkTo(slider.bottom, margin = 2.dp)
            }
        )
    }
}
