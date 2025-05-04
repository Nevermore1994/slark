package com.slark.demo

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.*
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val navController = rememberNavController()
            AppNavHost(navController = navController)
        }
    }
}

@Composable
fun HomeScreen(innerPadding: PaddingValues,
               onItemClick: (Int) -> Unit) {
    val items = listOf("local player", "network player")
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
                        .clickable { onItemClick(index) }
                        .fillMaxWidth()
                        .padding(top = 16.dp))
                HorizontalDivider(thickness = 1.dp, color = Color.Gray)
            }

        }
    }
}

@Composable
fun HomeScreenRoute(navController: NavHostController) {
    Scaffold(modifier = Modifier.fillMaxSize()
    ) { innerPadding ->
        HomeScreen(
            innerPadding = innerPadding,
            onItemClick = { index ->
                when (index) {
                    0 -> navController.navigate(Screen.LocalPlayerScreen.route)
                }
            }
        )
    }
}

@Composable
fun AppNavHost(navController: NavHostController) {
    NavHost(
        navController = navController,
        startDestination = Screen.HomeScreen.route
    ) {
        composable(Screen.HomeScreen.route) {
            HomeScreenRoute(navController)
        }

        composable(Screen.LocalPlayerScreen.route) {
            LocalPlayerScreen()
        }
    }
}