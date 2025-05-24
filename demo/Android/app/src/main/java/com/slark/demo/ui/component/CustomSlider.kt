package com.slark.demo.ui.component

import android.annotation.SuppressLint
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.foundation.Canvas
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.*
import androidx.compose.ui.util.lerp


@SuppressLint("UnusedBoxWithConstraintsScope")
@Composable
fun CustomSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    bufferedValue: Float = value,
    trackHeight: Dp = 4.dp,
    thumbRadius: Dp = 5.dp,
    trackColor: Color = Color.LightGray,
    bufferedColor: Color = Color.Unspecified,
    activeColor: Color = Color(0xFFA9A9A9),
) {
    val thumbRadiusPx = with(LocalDensity.current) { thumbRadius.toPx() }
    val trackHeightPx = with(LocalDensity.current) { trackHeight.toPx() }

    BoxWithConstraints(
        modifier = modifier
            .height(thumbRadius * 2)
            .pointerInput(Unit) {
                detectTapGestures { offset ->
                    val width = size.width - thumbRadiusPx * 2
                    val fraction = ((offset.x - thumbRadiusPx) / width).coerceIn(0f, 1f)
                    val newValue = lerp(valueRange.start, valueRange.endInclusive, fraction)
                    onValueChange(newValue)
                }
            }
            .pointerInput(Unit) {
                detectDragGestures { change, _ ->
                    val width = size.width - thumbRadiusPx * 2
                    val fraction = ((change.position.x - thumbRadiusPx) / width).coerceIn(0f, 1f)
                    val newValue = lerp(valueRange.start, valueRange.endInclusive, fraction)
                    onValueChange(newValue)
                }
            }
    ) {
        val height = maxHeight
        val trackY = constraints.maxHeight / 2f
        val progress = ((value - valueRange.start) / (valueRange.endInclusive - valueRange.start)).coerceIn(0f, 1f)
        val buffered = ((bufferedValue - valueRange.start) / (valueRange.endInclusive - valueRange.start)).coerceIn(0f, 1f)

        Canvas(modifier = Modifier.fillMaxSize()) {
            val xStart = thumbRadiusPx
            val xEnd = size.width - thumbRadiusPx
            val totalWidth = xEnd - xStart

            val bufferedWidth = totalWidth * buffered
            val playedWidth = totalWidth * progress

            drawRoundRect(
                color = trackColor,
                topLeft = Offset(xStart, trackY - trackHeightPx / 2),
                size = Size(totalWidth, trackHeightPx),
                cornerRadius = CornerRadius(trackHeightPx / 2)
            )

            drawRoundRect(
                color = bufferedColor,
                topLeft = Offset(xStart, trackY - trackHeightPx / 2),
                size = Size(bufferedWidth, trackHeightPx),
                cornerRadius = CornerRadius(trackHeightPx / 2)
            )

            drawRoundRect(
                color = activeColor,
                topLeft = Offset(xStart, trackY - trackHeightPx / 2),
                size = Size(playedWidth, trackHeightPx),
                cornerRadius = CornerRadius(trackHeightPx / 2)
            )

            val thumbX = xStart + playedWidth
            drawCircle(
                color = activeColor,
                radius = thumbRadiusPx,
                center = Offset(thumbX, trackY)
            )
        }
    }
}