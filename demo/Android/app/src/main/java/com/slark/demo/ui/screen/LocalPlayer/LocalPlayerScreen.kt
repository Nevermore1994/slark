// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
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
