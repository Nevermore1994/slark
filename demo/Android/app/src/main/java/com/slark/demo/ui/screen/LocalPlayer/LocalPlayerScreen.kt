//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.screen.LocalPlayer

import androidx.compose.runtime.Composable
import com.slark.demo.ui.component.PlayerScreen
import com.slark.demo.ui.model.PlayerViewModel

@Composable
fun LocalPlayerScreen(
    viewModel: PlayerViewModel,
    onBackClick: () -> Unit
) {
    PlayerScreen(viewModel = viewModel, onBackClick = onBackClick)
}
