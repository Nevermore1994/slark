//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.debug

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Surface
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale

private val kCpuColor = Color(0xFF4FC3F7)
private val kMemoryColor = Color(0xFF81C784)
private val kGridColor = Color(0x33FFFFFF)

private val kTabs = listOf(
    "Overview" to listOf("Playback", "Sync"),
    "Media" to listOf("Source", "Video", "Audio"),
    "Pipeline" to listOf("Buffer", "Queue", "Render"),
    "Network" to listOf("Network"),
    "System" to listOf("System")
)

@Composable
fun MetricChart(
    values: List<Float>,
    lineColor: Color,
    modifier: Modifier = Modifier,
    fillAlpha: Float = 0.16f,
    zeroBased: Boolean = true
) {
    Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height
        if (w <= 0f || h <= 0f) return@Canvas

        for (i in 0..4) {
            val y = h * i / 4f
            drawLine(kGridColor, Offset(0f, y), Offset(w, y), strokeWidth = 1f)
        }
        for (i in 0..4) {
            val x = w * i / 4f
            drawLine(kGridColor, Offset(x, 0f), Offset(x, h), strokeWidth = 1f)
        }
        if (values.isEmpty()) return@Canvas

        val low = if (zeroBased) 0f else values.min()
        val high = values.max()
        val padding = ((high - low) * 0.15f).coerceAtLeast(
            if (zeroBased) 1f else (high * 0.002f).coerceAtLeast(0.5f)
        )
        val min = low - if (zeroBased) 0f else padding
        val max = high + padding
        val span = (max - min).coerceAtLeast(1f)
        val step = if (values.size > 1) w / (values.size - 1) else w
        val points = values.mapIndexed { index, value ->
            Offset(index * step, h - ((value - min) / span) * (h - 4f) - 2f)
        }

        val fill = Path().apply {
            moveTo(points.first().x, h)
            points.forEach { lineTo(it.x, it.y) }
            lineTo(points.last().x, h)
            close()
        }
        drawPath(fill, lineColor.copy(alpha = fillAlpha))

        val line = Path().apply {
            moveTo(points.first().x, points.first().y)
            points.drop(1).forEach { lineTo(it.x, it.y) }
        }
        drawPath(line, lineColor, style = Stroke(width = 2.5f))
    }
}

@Composable
private fun ChartCard(
    title: String,
    value: String,
    values: List<Float>,
    lineColor: Color,
    stats: String,
    zeroBased: Boolean = true,
    modifier: Modifier = Modifier
) {
    Surface(
        color = Color(0xFF161A20),
        shape = RoundedCornerShape(10.dp),
        modifier = modifier
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.Bottom) {
                Text(title, color = Color(0xFFB0BEC5), fontSize = 11.sp)
                Spacer(Modifier.weight(1f))
                Text(value, color = lineColor, fontSize = 18.sp, fontFamily = FontFamily.Monospace)
            }
            Spacer(Modifier.height(4.dp))
            MetricChart(
                values = values,
                lineColor = lineColor,
                zeroBased = zeroBased,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(96.dp)
            )
            Spacer(Modifier.height(4.dp))
            Text(stats, color = Color(0xFF90A4AE), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
        }
    }
}

private fun statsOf(values: List<Float>, unit: String): String {
    if (values.isEmpty()) return "no samples"
    val current = values.last()
    val peak = values.max()
    val min = values.min()
    return String.format(
        Locale.US,
        "now %.1f%s  peak %.1f%s  min %.1f%s  %d pts",
        current, unit, peak, unit, min, unit, values.size
    )
}

@Composable
fun DebugCharts(points: List<MetricPoint>, compact: Boolean) {
    val cpu = points.map { it.cpuPercent }
    val rss = points.map { it.rssKB.toFloat() / 1024f }
    val cpuNow = cpu.lastOrNull() ?: 0f
    val rssNow = rss.lastOrNull() ?: 0f

    ChartCard(
        title = "CPU",
        value = String.format(Locale.US, "%.1f%%", cpuNow),
        values = cpu,
        lineColor = kCpuColor,
        stats = if (compact) "" else statsOf(cpu, "%"),
        modifier = Modifier.fillMaxWidth()
    )
    Spacer(Modifier.height(8.dp))
    ChartCard(
        title = "Memory RSS",
        value = String.format(Locale.US, "%.1f MB", rssNow),
        values = rss,
        lineColor = kMemoryColor,
        stats = if (compact) "" else statsOf(rss, " MB"),
        zeroBased = false,
        modifier = Modifier.fillMaxWidth()
    )
    if (!compact) {
        Spacer(Modifier.height(8.dp))
        Text(
            "1s sampling · last ${points.size} points · up to 2 min · CPU: per-core usage, >100% on multicore",
            color = Color(0xFF78909C),
            fontSize = 10.sp
        )
    }
}

@Composable
private fun GroupCard(group: DebugGroup) {
    Surface(
        color = Color(0xFF161A20),
        shape = RoundedCornerShape(10.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                group.title.uppercase(),
                color = kCpuColor,
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace
            )
            Spacer(Modifier.height(6.dp))
            if (group.rows.isEmpty()) {
                Text("n/a", color = Color(0xFF78909C), fontSize = 11.sp)
            } else {
                group.rows.forEach { (label, value) ->
                    Row(modifier = Modifier.padding(vertical = 2.dp)) {
                        Text(
                            label,
                            color = Color(0xFFB0BEC5),
                            fontSize = 11.sp,
                            modifier = Modifier.weight(0.55f)
                        )
                        Text(
                            value,
                            color = Color.White,
                            fontSize = 11.sp,
                            fontFamily = FontFamily.Monospace,
                            textAlign = TextAlign.End,
                            modifier = Modifier.weight(0.45f)
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun DebugTabContent(tabIndex: Int, groups: List<DebugGroup>, points: List<MetricPoint>) {
    val (_, groupNames) = kTabs[tabIndex]
    if (tabIndex == 4) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp)
        ) {
            item { DebugCharts(points, compact = false) }
            items(groupNames) { name ->
                groups.firstOrNull { it.title == name }?.let { GroupCard(it) }
            }
        }
        return
    }
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp)
    ) {
        items(groupNames) { name ->
            groups.firstOrNull { it.title == name }?.let { GroupCard(it) }
        }
    }
}

@Composable
fun DebugTabRow(selected: Int, onSelect: (Int) -> Unit) {
    ScrollableTabRow(
        selectedTabIndex = selected,
        containerColor = Color(0xFF0E1116),
        edgePadding = 0.dp,
        divider = {}
    ) {
        kTabs.forEachIndexed { index, (title, _) ->
            Tab(
                selected = selected == index,
                onClick = { onSelect(index) },
                modifier = Modifier.height(36.dp),
                text = {
                    Text(
                        title,
                        fontSize = 10.sp,
                        maxLines = 1,
                        softWrap = false,
                        color = if (selected == index) Color.White else Color(0xFF90A4AE)
                    )
                }
            )
        }
    }
}

@Composable
fun DebugEmptyHint() {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text(
            "No player attached. CPU / memory curves keep sampling.",
            color = Color(0xFF78909C),
            fontSize = 12.sp
        )
    }
}

@Composable
fun DebugWindowTheme(content: @Composable () -> Unit) {
    Surface(color = Color(0xFF0E1116), modifier = Modifier.fillMaxSize()) {
        Column(modifier = Modifier.fillMaxSize().background(Color(0xFF0E1116))) {
            content()
        }
    }
}
