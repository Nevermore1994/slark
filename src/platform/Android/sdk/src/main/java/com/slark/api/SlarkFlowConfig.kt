//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

data class SlarkFlowConfig(
    val source: Source = Source(),
    val buffering: Buffering = Buffering(),
    val dns: Dns = Dns(),
    val notify: Notify = Notify()
) {
    data class Source(
        val lowWatermarkSec: Double = SlarkConfigDefaults.sourceLowWatermarkSec,
        val highWatermarkSec: Double = SlarkConfigDefaults.sourceHighWatermarkSec
    )

    data class Buffering(
        val startBufferSec: Double = SlarkConfigDefaults.bufferingStartBufferSec,
        val rebufferEnterSec: Double = SlarkConfigDefaults.bufferingRebufferEnterSec,
        val rebufferExitSec: Double = SlarkConfigDefaults.bufferingRebufferExitSec
    )

    data class Dns(
        val preResolveUrls: List<String> = emptyList(),
        val cacheTtlSec: Double = SlarkConfigDefaults.dnsCacheTtlSec
    )

    data class Notify(
        val progressStepSec: Double = SlarkConfigDefaults.notifyProgressStepSec,
        val cacheIntervalSec: Double = SlarkConfigDefaults.notifyCacheIntervalSec
    )
}
