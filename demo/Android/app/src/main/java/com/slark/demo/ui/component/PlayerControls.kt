// Copyright (c) 2026 Nevermore-. All rights reserved.

package com.slark.demo.ui.component

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slark.demo.R
import com.slark.demo.ui.model.PlayerViewModel
import com.slark.sdk.GraphicsBackend
import kotlin.math.cos
import kotlin.math.round
import kotlin.math.sin

private val kSpeedOptions = listOf(0.5f, 1.0f, 1.5f, 2.0f)

private fun formatTime(ms: Long): String {
    val totalSeconds = ((ms + 500) / 1000).toInt()
    val hours = totalSeconds / 3600
    val minutes = (totalSeconds % 3600) / 60
    val seconds = totalSeconds % 60
    return if (hours > 0) "%02d:%02d:%02d".format(hours, minutes, seconds)
    else "%02d:%02d".format(minutes, seconds)
}

private val kControlTint = Color(0xFFF0F0F0)

@Composable
fun PlayerControls(
    viewModel: PlayerViewModel,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(
                Brush.verticalGradient(
                    colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.6f))
                )
            )
            .padding(top = 16.dp, bottom = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (viewModel.hasVideo) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(end = 12.dp, bottom = 4.dp),
                horizontalArrangement = Arrangement.End
            ) {
                RenderBackendButton(
                    backend = viewModel.graphicsBackend,
                    onClick = { viewModel.switchRenderBackend() }
                )
            }
        }
        PlayerProgressBar(viewModel)
        Spacer(modifier = Modifier.height(8.dp))
        PlayerBottomRow(viewModel)
    }
}

@Composable
fun PlayerBottomRow(viewModel: PlayerViewModel) {
    var speedExpanded by remember { mutableStateOf(false) }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // 音量
        IconButton(
            onClick = { viewModel.mute(!viewModel.isMute) },
            modifier = Modifier.size(32.dp)
        ) {
            Icon(
                painterResource(id = if (viewModel.isMute) R.drawable.mute else R.drawable.volume),
                contentDescription = null,
                tint = kControlTint,
                modifier = Modifier.size(28.dp)
            )
        }
        Slider(
            value = viewModel.volume / 100f,
            onValueChange = { viewModel.volume = it * 100f },
            colors = SliderDefaults.colors(
                thumbColor = kControlTint,
                activeTrackColor = kControlTint,
                inactiveTrackColor = Color.White.copy(alpha = 0.3f)
            ),
            modifier = Modifier.width(120.dp).height(28.dp)
        )

        Spacer(modifier = Modifier.width(16.dp))

        // 循环
        IconButton(
            onClick = { viewModel.loop(!viewModel.isLoop) },
            modifier = Modifier.size(32.dp)
        ) {
            Icon(
                painter = painterResource(id = if (viewModel.isLoop) R.drawable.loop else R.drawable.stop_loop ),
                contentDescription = null,
                tint = kControlTint,
                modifier = Modifier.size(24.dp)
            )
        }

        Spacer(modifier = Modifier.width(12.dp))

        if (viewModel.hasVideo) {
            IconButton(
                onClick = { viewModel.rotate() },
                modifier = Modifier.size(32.dp)
            ) {
                Icon(
                    painter = painterResource(id = R.drawable.ic_rotate),
                    contentDescription = "Rotate",
                    tint = kControlTint,
                    modifier = Modifier.size(24.dp)
                )
            }
            Spacer(modifier = Modifier.width(12.dp))
        }

        Spacer(modifier = Modifier.weight(1f))

        // 倍速选择（最右，下拉时不传播点击到父层）
        Box(
            modifier = Modifier.clickable(
                indication = null,
                interactionSource = remember { MutableInteractionSource() }
            ) { /* 消耗点击事件，不触发上层 showControls */ }
        ) {
            val speedLabel = when (viewModel.speed) {
                0.5f  -> "0.5x"
                1.0f  -> "1.0x"
                1.5f  -> "1.5x"
                2.0f  -> "2.0x"
                else  -> "%.1fx".format(viewModel.speed)
            }
            Text(
                text = speedLabel,
                color = kControlTint,
                fontSize = 13.sp,
                modifier = Modifier
                    .border(1.5.dp, kControlTint, RoundedCornerShape(6.dp))
                    .clickable {
                        speedExpanded = !speedExpanded
                    }
                    .padding(horizontal = 10.dp, vertical = 5.dp)
            )
            DropdownMenu(
                expanded = speedExpanded,
                onDismissRequest = { speedExpanded = false },
                modifier = Modifier.background(Color(0xFF2A2A2A))
            ) {
                kSpeedOptions.forEach { opt ->
                    DropdownMenuItem(
                        text = {
                            val label = if (opt == opt.toLong().toFloat()) "%.0fx".format(opt) else "%.1fx".format(opt)
                            Text(text = label, color = kControlTint, fontSize = 14.sp)
                        },
                        onClick = {
                            viewModel.speed = opt
                            speedExpanded = false
                        }
                    )
                }
            }
        }
    }
}

@Composable
fun PlayerTopBar(viewModel: PlayerViewModel) {}

@Composable
fun RenderBackendButton(backend: GraphicsBackend, onClick: () -> Unit) {
    val label = when (backend) {
        GraphicsBackend.OpenGL -> "GL"
        GraphicsBackend.Vulkan -> "VK"
    }
    Box(
        modifier = Modifier
            .border(1.5.dp, kControlTint, RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 5.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text = label, color = kControlTint, fontSize = 13.sp)
    }
}

@Composable
fun SpeedButton(speed: Float, onSpeedChange: (Float) -> Unit, modifier: Modifier = Modifier) {
    val label = if (speed == speed.toLong().toFloat()) "%.0fx".format(speed) else "%.1fx".format(speed)
    Box(
        modifier = modifier
            .border(1.5.dp, kControlTint, RoundedCornerShape(6.dp))
            .clickable {
                val idx = kSpeedOptions.indexOfFirst { kotlin.math.abs(it - speed) < 0.01f }
                val next = kSpeedOptions[(if (idx < 0) 1 else (idx + 1) % kSpeedOptions.size)]
                onSpeedChange(next)
            }
            .padding(horizontal = 10.dp, vertical = 5.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(text = label, color = kControlTint, fontSize = 13.sp)
    }
}

@Composable
fun PlayerProgressBar(viewModel: PlayerViewModel) {
    val currentTime = (viewModel.currentTime * 1000).toLong()
    val totalTime = (viewModel.totalTime * 1000).toLong()
    // 注意：viewModel.cacheTime / totalTime 都是秒；不要用毫秒的 totalTime 与秒的 cacheTime 相除
    val bufferedFraction = if (viewModel.totalTime > 0.0)
        (viewModel.cacheTime.toFloat() / viewModel.totalTime.toFloat()).coerceIn(0f, 1f)
    else 0f

    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp)) {
        CustomSlider(
            value = if (totalTime > 0) (currentTime.toFloat() / totalTime.toFloat()) else 0f,
            onValueChange = { t, b ->
                viewModel.currentTime = (t * totalTime) / 1000.0
                viewModel.seekTo(viewModel.currentTime, b)
            },
            valueRange = 0f..1f,
            bufferedColor = Color(0xFFFFFFFF),
            bufferedValue = bufferedFraction,
            trackHeight = 4.dp,
            modifier = Modifier.fillMaxWidth().height(28.dp)
        )
        Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 2.dp)) {
            val timeStyle = MaterialTheme.typography.labelSmall.copy(
                shadow = Shadow(color = Color.Black, offset = Offset(1f, 1f), blurRadius = 3f)
            )
            Text(
                text = formatTime(currentTime),
                color = kControlTint,
                style = timeStyle.copy(fontSize = 12.sp)
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = formatTime(totalTime),
                color = kControlTint,
                style = timeStyle.copy(fontSize = 12.sp)
            )
        }
    }
}
