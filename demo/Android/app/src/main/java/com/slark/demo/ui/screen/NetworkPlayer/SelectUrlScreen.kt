
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

package com.slark.demo.ui.screen.NetworkPlayer

import android.net.Uri
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import com.slark.demo.ui.PlayUrl

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SelectUrlScreen(innerPadding: PaddingValues, onItemClick: (Uri) -> Unit) {
    val items = PlayUrl.urls
    var customUrl by remember { mutableStateOf("") }
    val focusManager = LocalFocusManager.current

    fun playUrl(url: String) {
        val trimmed = url.trim()
        if (trimmed.isEmpty()) return
        focusManager.clearFocus()
        onItemClick(trimmed.toUri())
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(innerPadding)
    ) {
        TopAppBar(title = { Text("Network Player") })

        OutlinedTextField(
            value = customUrl,
            onValueChange = { customUrl = it },
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp),
            placeholder = { Text("Enter URL (http/https)", style = MaterialTheme.typography.bodyMedium) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(
                keyboardType = KeyboardType.Uri,
                imeAction = ImeAction.Go
            ),
            keyboardActions = KeyboardActions(onGo = { playUrl(customUrl) }),
            trailingIcon = {
                IconButton(onClick = { playUrl(customUrl) }) {
                    Icon(Icons.Default.PlayArrow, contentDescription = "Play")
                }
            }
        )

        Text(
            text = "Presets",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(start = 20.dp, top = 4.dp, bottom = 4.dp)
        )

        HorizontalDivider()

        LazyColumn(modifier = Modifier.fillMaxWidth()) {
            itemsIndexed(items) { _, item ->
                Column(
                    modifier = Modifier
                        .clickable { onItemClick(item.url.toUri()) }
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp)
                ) {
                    Text(text = item.name, style = MaterialTheme.typography.bodyMedium)
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = item.url,
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            }
        }
    }
}
