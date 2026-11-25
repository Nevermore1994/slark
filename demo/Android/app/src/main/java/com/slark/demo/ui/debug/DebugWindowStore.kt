//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.debug

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.slark.api.SlarkSdk
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class MetricPoint(
    val timestampUs: Long,
    val cpuPercent: Float,
    val rssKB: Long,
    val vmSizeKB: Long
)

data class DebugGroup(val title: String, val rows: List<Pair<String, String>>)

private val kGroupTitles = listOf(
    "Source", "Video", "Audio", "Playback", "Sync", "Buffer", "Queue", "Render", "Network", "System"
)

object DebugWindowStore {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    var points by mutableStateOf<List<MetricPoint>>(emptyList())
        private set
    var groups by mutableStateOf<List<DebugGroup>>(emptyList())
        private set
    var floatingVisible by mutableStateOf(true)

    @Volatile
    private var snapshotProvider: (() -> String)? = null

    private var started = false

    fun start() {
        if (started) return
        started = true
        scope.launch {
            while (isActive) {
                val csv = runCatching { SlarkSdk.debugMetricsTick() }.getOrDefault("")
                points = parseCsv(csv)
                groups = parseSnapshot(snapshotProvider?.invoke().orEmpty())
                delay(1000)
            }
        }
    }

    fun registerSnapshot(provider: () -> String) {
        snapshotProvider = provider
    }

    fun unregisterSnapshot() {
        snapshotProvider = null
    }

    private fun parseCsv(csv: String): List<MetricPoint> = buildList {
        for (line in csv.lineSequence()) {
            if (line.isEmpty()) continue
            val parts = line.split(',')
            if (parts.size != 4) continue
            val point = MetricPoint(
                timestampUs = parts[0].toLongOrNull() ?: continue,
                cpuPercent = parts[1].toFloatOrNull() ?: continue,
                rssKB = parts[2].toLongOrNull() ?: continue,
                vmSizeKB = parts[3].toLongOrNull() ?: continue
            )
            add(point)
        }
    }

    private fun parseSnapshot(text: String): List<DebugGroup> {
        val result = mutableMapOf<String, MutableList<Pair<String, String>>>()
        kGroupTitles.forEach { result[it] = mutableListOf() }
        var current: String? = null
        for (line in text.lineSequence()) {
            if (line.isEmpty()) continue
            if (line.startsWith('[') && line.endsWith(']')) {
                current = line.removePrefix("[").removeSuffix("]")
                result.getOrPut(current) { mutableListOf() }
                continue
            }
            val target = current ?: continue
            val separator = line.indexOf(": ")
            if (separator < 0) {
                if (line.trim() == "n/a") result[target]?.add("Status" to "n/a")
                continue
            }
            result[target]?.add(line.substring(0, separator) to line.substring(separator + 2))
        }
        return kGroupTitles.map { DebugGroup(it, result[it].orEmpty()) }
    }
}
