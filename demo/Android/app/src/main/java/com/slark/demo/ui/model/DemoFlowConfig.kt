//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.model

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import com.slark.api.SlarkFlowConfig

object DemoFlowConfig {
    var config: SlarkFlowConfig? = null

    var version by mutableIntStateOf(0)
        private set

    val current: SlarkFlowConfig
        get() = config ?: SlarkFlowConfig()

    fun apply(next: SlarkFlowConfig) {
        config = next
        version++
    }
}
