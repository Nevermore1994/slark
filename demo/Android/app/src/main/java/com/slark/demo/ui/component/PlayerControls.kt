package com.slark.demo.ui.component

import androidx.compose.animation.Crossfade
import androidx.compose.foundation.background
import androidx.compose.runtime.Composable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.IconButton
import androidx.compose.material3.Icon
import androidx.compose.runtime.remember
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.constraintlayout.compose.ConstraintLayout
import androidx.constraintlayout.compose.Dimension
import androidx.compose.material3.MaterialTheme
import com.slark.demo.R
import com.slark.demo.ui.model.PlayerViewModel

@Composable
fun PlayerControls(viewModel: PlayerViewModel) {
    val isPlaying = viewModel.isPlaying
    val progress by remember {
        derivedStateOf {
            val current = viewModel.currentTime
            val total = viewModel.totalTime
            if (total > 0)  current.toFloat() / total.toFloat() else 0f
        }
    }
    val currentTime = viewModel.currentTime
    val totalTime = viewModel.totalTime

    Column(
        modifier = Modifier.fillMaxWidth().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        PlayerProgressBar(viewModel)
        Spacer(modifier = Modifier.height(2.dp))
        PlayerControlBar(viewModel)
    }
}

private fun formatTime(ms: Int): String {
    val totalSeconds = ms / 1000
    val minutes = totalSeconds / 60
    val seconds = totalSeconds % 60
    return "%02d:%02d".format(minutes, seconds)
}

@Composable
fun PlayerControlBar(viewModel: PlayerViewModel) {
    ConstraintLayout(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp)
    ) {
        val (volumeIcon, volumeSlider, prevBtn, playBtn, nextBtn) = createRefs()

        IconButton(
            onClick = {  },
            modifier = Modifier
                .size(24.dp)
                .constrainAs(volumeIcon) {
                    start.linkTo(parent.start)
                    top.linkTo(parent.top)
                    bottom.linkTo(parent.bottom)
                }
        ) {
            Icon(
                painterResource(id = R.drawable.volume),
                contentDescription = "Volume",
                tint = Color(0xFFA9A9A9)
            )
        }

        CustomSlider(
            value = viewModel.volume,
            onValueChange = { viewModel.volume = it },
            valueRange = 0f..1f,
            trackColor = Color.LightGray,
            activeColor = Color(0xFFA9A9A9),
            modifier = Modifier
                .constrainAs(volumeSlider) {
                    start.linkTo(volumeIcon.end)
                    centerVerticallyTo(parent)
                    width = Dimension.percent(0.2f)
                }
        )

        IconButton(
            onClick = { viewModel.skipPrev() },
            modifier = Modifier
                .size(36.dp)
                .constrainAs(prevBtn) {
                    end.linkTo(playBtn.start, margin = 24.dp)
                    centerVerticallyTo(parent)
                }
        ) {
            Icon(
                painterResource(id = R.drawable.prev),
                contentDescription = "Prev",
                tint = Color(0xFFA9A9A9)
            )
        }

        IconButton(
            onClick = { viewModel.play(!viewModel.isPlaying) },
            modifier = Modifier
                .size(36.dp)
                .constrainAs(playBtn) {
                    centerTo(parent)
                }
        ) {
            Crossfade(targetState = viewModel.isPlaying, label = "") {
                Icon(
                    painter = painterResource(
                        id = if (it) R.drawable.pause_icon else R.drawable.play_icon
                    ),
                    contentDescription = if (it) "Pause" else "Play",
                    tint = Color(0xFFA9A9A9),
                    modifier = Modifier.size(48.dp)
                )
            }
        }

        IconButton(
            onClick = { viewModel.skipNext() },
            modifier = Modifier
                .size(36.dp)
                .constrainAs(nextBtn) {
                    start.linkTo(playBtn.end, margin = 24.dp)
                    centerVerticallyTo(parent)
                }
        ) {
            Icon(
                painterResource(id = R.drawable.next),
                contentDescription = "Next",
                tint = Color(0xFFA9A9A9)
            )
        }
    }
}

@Composable
fun PlayerProgressBar(viewModel: PlayerViewModel) {
    ConstraintLayout(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp)
    ) {
        val (startTime, endTime, slider) = createRefs()

        CustomSlider(
            value = if (viewModel.totalTime > 0) {
                (viewModel.currentTime / viewModel.totalTime).toFloat()
            } else 0f,
            onValueChange = { viewModel.seekTo(it * viewModel.totalTime) },
            valueRange = 0f..1f,
            bufferedColor = Color.Gray,
            bufferedValue = 0.5f,
            modifier = Modifier
                .fillMaxWidth()
                .height(24.dp)
                .constrainAs(slider) {
                    top.linkTo(parent.top)
                    start.linkTo(parent.start)
                    end.linkTo(parent.end)
                }
        )

        Text(
            text = formatTime(viewModel.currentTime.toInt()),
            modifier = Modifier.constrainAs(startTime) {
                start.linkTo(parent.start)
                top.linkTo(slider.bottom, margin = 2.dp)
            },
            style = MaterialTheme.typography.labelMedium
        )

        Text(
            text = formatTime(viewModel.totalTime.toInt()),
            modifier = Modifier.constrainAs(endTime) {
                end.linkTo(parent.end)
                top.linkTo(slider.bottom, margin = 2.dp)
            },
            style = MaterialTheme.typography.labelMedium
        )
    }
}

@Preview(showBackground = true)
@Composable
fun PlayerControlsPreview() {
    val viewModel = PlayerViewModel(null)
    viewModel.currentTime = 123456.0
    viewModel.totalTime = 654321.0
    viewModel.isPlaying = true

    PlayerControls(viewModel)
}