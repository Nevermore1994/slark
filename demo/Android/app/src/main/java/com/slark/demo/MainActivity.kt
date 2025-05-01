package com.slark.demo

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.slark.demo.ui.theme.DemoTheme
import kotlin.contracts.contract

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            Scaffold(modifier = Modifier.fillMaxSize()
                .background(Color.Green)
            ) { innerPadding ->
                InitMainView(innerPadding)
            }
        }
    }
}

@Composable
fun InitMainView(innerPadding: PaddingValues) {
    val configuration = LocalConfiguration.current
    Box(modifier = Modifier.fillMaxSize()
        .padding(innerPadding) ){
        Column {
            Text("这是第一行")
            Button(onClick = {}) {
                Text("按钮1")
            }
        }

        Row (modifier = Modifier.offset(y = 100.0.dp)) {
            Text(
                text = "这是第二行",
                color = Color.Blue,
                modifier = Modifier.background(Color.Red)
                    .width(100.dp)
            )
            Button(onClick = {}) {
                Text("按钮2")
            }
        }
    }

}
