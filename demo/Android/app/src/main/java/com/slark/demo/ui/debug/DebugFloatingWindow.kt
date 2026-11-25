//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.debug

import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale
import kotlin.math.roundToInt

@Composable
fun DebugFloatingWindow(modifier: Modifier = Modifier) {
    if (!DebugWindowStore.floatingVisible) return

    val context = LocalContext.current
    var offsetX by remember { mutableFloatStateOf(0f) }
    var offsetY by remember { mutableFloatStateOf(0f) }
    var selectedTab by remember { mutableIntStateOf(4) }
    var collapsed by remember { mutableStateOf(true) }
    val points = DebugWindowStore.points
    val groups = DebugWindowStore.groups

    Column(
        modifier = modifier
            .offset { IntOffset(offsetX.roundToInt(), offsetY.roundToInt()) }
            .width(if (collapsed) 132.dp else 250.dp)
            .background(Color(0xEE0E1116), RoundedCornerShape(12.dp))
            .padding(10.dp)
            .pointerInput(Unit) {
                detectDragGestures { change, dragAmount ->
                    change.consume()
                    offsetX += dragAmount.x
                    offsetY += dragAmount.y
                }
            }
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                if (collapsed && points.isNotEmpty()) {
                    String.format(
                        Locale.US, "%.0f%% %.0fM",
                        points.last().cpuPercent, points.last().rssKB / 1024f
                    )
                } else {
                    "DEBUG"
                },
                color = Color(0xFF4FC3F7),
                fontSize = 11.sp,
                maxLines = 1,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.pointerInput(Unit) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        offsetX += dragAmount.x
                        offsetY += dragAmount.y
                    }
                }
            )
            Spacer(Modifier.weight(1f))
            IconButton(
                onClick = { collapsed = !collapsed },
                modifier = Modifier.size(28.dp)
            ) {
                Text(if (collapsed) "+" else "−", color = Color.White, fontSize = 16.sp)
            }
            IconButton(
                onClick = { context.startActivity(Intent(context, DebugWindowActivity::class.java)) },
                modifier = Modifier.size(28.dp)
            ) {
                Text("⤢", color = Color.White, fontSize = 14.sp)
            }
        }

        if (!collapsed) {
            DebugTabRow(selected = selectedTab, onSelect = { selectedTab = it })
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(250.dp)
            ) {
                DebugTabContent(selectedTab, groups, points)
            }
        }
    }
}
