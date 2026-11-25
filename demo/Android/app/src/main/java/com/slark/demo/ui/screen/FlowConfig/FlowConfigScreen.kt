//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.screen.FlowConfig

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.slark.api.SlarkFlowConfig
import com.slark.demo.ui.model.DemoFlowConfig

private data class Field(val key: String, val label: String, val group: String)

private val kFields = listOf(
    Field("sourceLow", "lowWatermarkSec", "Source"),
    Field("sourceHigh", "highWatermarkSec", "Source"),
    Field("bufferingStart", "startBufferSec", "Buffering"),
    Field("bufferingEnter", "rebufferEnterSec", "Buffering"),
    Field("bufferingExit", "rebufferExitSec", "Buffering"),
    Field("dnsTtl", "cacheTtlSec", "DNS"),
    Field("notifyProgress", "progressStepSec", "Notification"),
    Field("notifyCache", "cacheIntervalSec", "Notification")
)

private fun SlarkFlowConfig.toValues(): Map<String, String> = mapOf(
    "sourceLow" to source.lowWatermarkSec.toString(),
    "sourceHigh" to source.highWatermarkSec.toString(),
    "bufferingStart" to buffering.startBufferSec.toString(),
    "bufferingEnter" to buffering.rebufferEnterSec.toString(),
    "bufferingExit" to buffering.rebufferExitSec.toString(),
    "dnsTtl" to dns.cacheTtlSec.toString(),
    "notifyProgress" to notify.progressStepSec.toString(),
    "notifyCache" to notify.cacheIntervalSec.toString(),
    "dnsUrls" to dns.preResolveUrls.joinToString("\n")
)

private fun buildConfig(values: Map<String, String>): SlarkFlowConfig? {
    val numbers = mutableMapOf<String, Double>()
    for (field in kFields) {
        val value = values[field.key]?.trim()?.toDoubleOrNull() ?: return null
        if (!value.isFinite()) return null
        numbers[field.key] = value
    }
    val urls = values["dnsUrls"].orEmpty()
        .split(',', '\n')
        .map { it.trim() }
        .filter { it.isNotEmpty() }
    val config = SlarkFlowConfig(
        source = SlarkFlowConfig.Source(
            lowWatermarkSec = numbers.getValue("sourceLow"),
            highWatermarkSec = numbers.getValue("sourceHigh")
        ),
        buffering = SlarkFlowConfig.Buffering(
            startBufferSec = numbers.getValue("bufferingStart"),
            rebufferEnterSec = numbers.getValue("bufferingEnter"),
            rebufferExitSec = numbers.getValue("bufferingExit")
        ),
        dns = SlarkFlowConfig.Dns(
            preResolveUrls = urls,
            cacheTtlSec = numbers.getValue("dnsTtl")
        ),
        notify = SlarkFlowConfig.Notify(
            progressStepSec = numbers.getValue("notifyProgress"),
            cacheIntervalSec = numbers.getValue("notifyCache")
        )
    )
    return config.takeIf { it.isValid() }
}

private fun SlarkFlowConfig.isValid(): Boolean =
    source.lowWatermarkSec >= 0.0 && source.lowWatermarkSec < source.highWatermarkSec &&
        buffering.startBufferSec >= 0.0 && buffering.rebufferEnterSec >= 0.0 &&
        buffering.rebufferExitSec >= buffering.rebufferEnterSec &&
        buffering.rebufferExitSec < source.highWatermarkSec &&
        dns.cacheTtlSec > 0.0 && notify.progressStepSec > 0.0 &&
        notify.cacheIntervalSec > 0.0

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FlowConfigScreen(innerPadding: PaddingValues, onBack: () -> Unit) {
    val values = remember {
        mutableStateMapOf<String, String>().apply { putAll(DemoFlowConfig.current.toValues()) }
    }
    var error by remember { mutableStateOf<String?>(null) }

    Scaffold(
        modifier = Modifier.padding(innerPadding),
        contentWindowInsets = WindowInsets(0, 0, 0, 0),
        topBar = {
            TopAppBar(
                title = { Text("Playback Config") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    TextButton(
                        onClick = {
                            values.clear()
                            values.putAll(SlarkFlowConfig().toValues())
                            error = null
                        }
                    ) { Text("Reset") }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            for ((group, fields) in kFields.groupBy { it.group }) {
                Text(
                    text = group,
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.padding(top = 8.dp)
                )
                for (field in fields) {
                    OutlinedTextField(
                        value = values.getValue(field.key),
                        onValueChange = { values[field.key] = it; error = null },
                        label = { Text(field.label) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                        modifier = Modifier.fillMaxWidth()
                    )
                }
                if (group == "DNS") {
                    OutlinedTextField(
                        value = values.getValue("dnsUrls"),
                        onValueChange = { values["dnsUrls"] = it; error = null },
                        label = { Text("preResolveUrls (one per line)") },
                        minLines = 2,
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            }
            error?.let {
                Text(it, color = MaterialTheme.colorScheme.error)
            }
            Button(
                onClick = {
                    val config = buildConfig(values)
                    if (config == null) {
                        error = "Invalid values: check all fields and the watermark / buffering ordering."
                    } else {
                        DemoFlowConfig.apply(config)
                        onBack()
                    }
                },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Apply") }
            Text(
                "Applying rebuilds the current playback; values are read when the player is created.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}
