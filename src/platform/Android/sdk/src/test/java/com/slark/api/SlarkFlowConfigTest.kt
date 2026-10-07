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
    fun defaultConfigMatchesCoreNetworkValues() {
        val config = SlarkFlowConfig()
        assertEquals(3.0, config.source.lowWatermarkSec, 0.0)
        assertEquals(10.0, config.source.highWatermarkSec, 0.0)
        assertEquals(4, config.audioDecoder.lowWatermarkFrames)
        assertEquals(16, config.audioDecoder.highWatermarkFrames)
        assertEquals(2, config.videoDecoder.lowWatermarkFrames)
        assertEquals(8, config.videoDecoder.highWatermarkFrames)
        assertEquals(2.0, config.buffering.startBufferSec, 0.0)
        assertEquals(1.5, config.buffering.rebufferScale, 0.0)
        assertEquals(5, config.buffering.starvationThreshold)
        assertEquals(8, config.catchUp.maxDropPerVsync)
        assertEquals(300L, config.catchUp.seekFastForwardMs)
    }

    @Test
    fun nativeFieldLayoutMatchesFlowConfigGroups() {
        val groups = mapOf(
            "source" to SlarkFlowConfig.Source::class.java,
            "audioDecoder" to SlarkFlowConfig.Decoder::class.java,
            "videoDecoder" to SlarkFlowConfig.Decoder::class.java,
            "buffering" to SlarkFlowConfig.Buffering::class.java,
            "catchUp" to SlarkFlowConfig.CatchUp::class.java
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
        val intType = Int::class.javaPrimitiveType!!
        checkFields(SlarkFlowConfig.Source::class.java,
            "lowWatermarkSec" to doubleType, "highWatermarkSec" to doubleType)
        checkFields(SlarkFlowConfig.Decoder::class.java,
            "lowWatermarkFrames" to intType, "highWatermarkFrames" to intType)
        checkFields(SlarkFlowConfig.Buffering::class.java,
            "startBufferSec" to doubleType, "rebufferScale" to doubleType,
            "starvationThreshold" to intType)
        checkFields(SlarkFlowConfig.CatchUp::class.java,
            "maxDropPerVsync" to intType,
            "seekFastForwardMs" to Long::class.javaPrimitiveType!!)
    }

    @Test
    fun explicitConfigIsPreservedAsWholeValue() {
        val override = SlarkFlowConfig(
            source = SlarkFlowConfig.Source(lowWatermarkSec = 1.0, highWatermarkSec = 6.0),
            buffering = SlarkFlowConfig.Buffering(startBufferSec = 1.0)
        )
        assertEquals(override, SlarkPlayerConfig("sample.mp4", flowConfig = override).flowConfig)
    }
}
