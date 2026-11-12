//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

data class SlarkFlowConfig(
    val source: Source = Source(),
    val buffering: Buffering = Buffering(),
    val dns: Dns = Dns()
) {
    data class Source(
        val lowWatermarkSec: Double = 3.0,
        val highWatermarkSec: Double = 10.0
    )

    data class Buffering(
        val startBufferSec: Double = 2.0,
        val rebufferEnterSec: Double = 3.0,
        val rebufferExitSec: Double = 4.5
    )

    data class Dns(
        val preResolveUrls: List<String> = emptyList(),
        val cacheTtlSec: Double = 60.0
    )
}
