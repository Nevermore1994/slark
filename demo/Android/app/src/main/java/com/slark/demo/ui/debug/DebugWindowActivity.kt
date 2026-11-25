//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.debug

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

class DebugWindowActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        DebugWindowStore.start()
        setContent {
            DebugWindowTheme {
                var selectedTab by remember { mutableIntStateOf(0) }
                val points = DebugWindowStore.points
                val groups = DebugWindowStore.groups

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp)
                ) {
                    Text(
                        "Slark Debug",
                        color = Color.White,
                        fontSize = 15.sp,
                        fontFamily = FontFamily.Monospace
                    )
                    Spacer(Modifier.weight(1f))
                    Text(
                        "${points.size} samples",
                        color = Color(0xFF90A4AE),
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace
                    )
                    IconButton(onClick = { DebugWindowStore.floatingVisible = !DebugWindowStore.floatingVisible }) {
                        Text("◱", color = Color.White, fontSize = 15.sp)
                    }
                    IconButton(onClick = { finish() }) {
                        Text("✕", color = Color.White, fontSize = 15.sp)
                    }
                }
                DebugTabRow(selected = selectedTab, onSelect = { selectedTab = it })
                androidx.compose.foundation.layout.Box(modifier = Modifier.fillMaxSize()) {
                    DebugTabContent(selectedTab, groups, points)
                }
            }
        }
    }
}
