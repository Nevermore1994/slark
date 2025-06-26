package com.slark.demo.ui.screen.NetworkPlayer

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import android.net.Uri
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Scaffold
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri

@Composable
fun SelectUrlScreen(onItemClick: (Uri) -> Unit) {
    val items = listOf("", "")
    Scaffold(
        modifier = Modifier.fillMaxSize()
    ) { innerPadding ->
        LazyColumn (modifier = Modifier
            .fillMaxWidth()
            .padding(innerPadding)
            .padding(top = 16.dp, start = 16.dp, end = 16.dp)) {
            itemsIndexed(items) { index, item ->
                Column {
                    Text(
                        text = item,
                        textAlign = TextAlign.Center,
                        modifier = Modifier
                            .clickable { onItemClick(item.toUri()) }
                            .fillMaxWidth()
                            .padding(top = 16.dp))
                    HorizontalDivider(thickness = 1.dp, color = Color.Gray)
                }
            }
        }
    }
}