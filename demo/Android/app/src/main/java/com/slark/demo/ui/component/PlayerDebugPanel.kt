// Copyright (c) 2026 Nevermore-. All rights reserved.

package com.slark.demo.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slark.demo.ui.model.PlayerViewModel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

@Composable
fun PlayerDebugPanel(viewModel: PlayerViewModel, modifier: Modifier = Modifier) {
    var snapshot by remember { mutableStateOf("") }

    LaunchedEffect(viewModel) {
        while (isActive) {
            snapshot = viewModel.debugSnapshot() + "Backend: ${viewModel.graphicsBackend}\n"
            delay(200)
        }
    }

    val scroll = rememberScrollState()
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(Color.Black.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .padding(8.dp)
            .verticalScroll(scroll)
    ) {
        Text(
            text = snapshot,
            color = Color.White,
            fontSize = 10.sp,
            lineHeight = 12.sp
        )
    }
}
