//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SlarkFlowConfigTest {
    @Test
    fun missingConfigLeavesSourceDefaultsToPlayer() {
        assertNull(SlarkPlayerConfig("sample.mp4").flowConfig)
    }

    @Test
    fun defaultConfigMatchesCoreDefaults() {
        val config = SlarkFlowConfig()
        assertEquals(3.0, config.source.lowWatermarkSec, 0.0)
        assertEquals(10.0, config.source.highWatermarkSec, 0.0)
        assertEquals(2.0, config.buffering.startBufferSec, 0.0)
        assertEquals(3.0, config.buffering.rebufferEnterSec, 0.0)
        assertEquals(4.5, config.buffering.rebufferExitSec, 0.0)
        assertEquals(emptyList<String>(), config.dns.preResolveUrls)
        assertEquals(60.0, config.dns.cacheTtlSec, 0.0)
    }

    @Test
    fun nativeFieldLayoutMatchesFlowConfigGroups() {
        val groups = mapOf(
            "source" to SlarkFlowConfig.Source::class.java,
            "buffering" to SlarkFlowConfig.Buffering::class.java,
            "dns" to SlarkFlowConfig.Dns::class.java
        )
        groups.forEach { (field, type) ->
            assertEquals(type, SlarkFlowConfig::class.java.getDeclaredField(field).type)
        }
        fun checkFields(type: Class<*>, vararg expected: Pair<String, Class<*>>) {
            expected.forEach { (name, fieldType) ->
                assertEquals(fieldType, type.getDeclaredField(name).type)
            }
        }
        val doubleType = Double::class.javaPrimitiveType!!
        checkFields(SlarkFlowConfig.Source::class.java,
            "lowWatermarkSec" to doubleType, "highWatermarkSec" to doubleType)
        checkFields(SlarkFlowConfig.Buffering::class.java,
            "startBufferSec" to doubleType, "rebufferEnterSec" to doubleType,
            "rebufferExitSec" to doubleType)
        checkFields(SlarkFlowConfig.Dns::class.java,
            "preResolveUrls" to java.util.List::class.java, "cacheTtlSec" to doubleType)
    }

    @Test
    fun explicitConfigIsPreservedAsWholeValue() {
        val override = SlarkFlowConfig(
            source = SlarkFlowConfig.Source(lowWatermarkSec = 1.0, highWatermarkSec = 6.0),
            buffering = SlarkFlowConfig.Buffering(startBufferSec = 1.0),
            dns = SlarkFlowConfig.Dns(preResolveUrls = listOf("https://cdn.example.com/a.m3u8"))
        )
        assertEquals(override, SlarkPlayerConfig("sample.mp4", flowConfig = override).flowConfig)
    }
}
