//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

data class SlarkFlowConfig(
    val source: Source = Source(),
    val audioDecoder: Decoder = Decoder(lowWatermarkFrames = 4, highWatermarkFrames = 16),
    val videoDecoder: Decoder = Decoder(),
    val buffering: Buffering = Buffering(),
    val catchUp: CatchUp = CatchUp()
) {
    data class Source(
        val lowWatermarkSec: Double = 3.0,
        val highWatermarkSec: Double = 10.0
    )

    data class Decoder(
        val lowWatermarkFrames: Int = 2,
        val highWatermarkFrames: Int = 8
    )

    data class Buffering(
        val startBufferSec: Double = 2.0,
        val rebufferScale: Double = 1.5,
        val starvationThreshold: Int = 5
    )

    data class CatchUp(
        val maxDropPerVsync: Int = 8,
        val seekFastForwardMs: Long = 300L
    )
}
