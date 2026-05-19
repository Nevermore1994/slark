//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.painterResource
import androidx.navigation.NavHostController
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.slark.api.SlarkSdk
import com.slark.demo.ui.navigation.AppNavHost
import com.slark.demo.ui.navigation.Screen
import com.slark.demo.ui.theme.SlarkDemoTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        SlarkSdk.init(application)
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            SlarkDemoTheme {
                val navController = rememberNavController()
                MainScreen(navController)
            }
        }
    }
}

@Composable
fun MainScreen(navController: NavHostController) {
    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route

    val showBottomBar = currentRoute in listOf(
        Screen.SelectUrlScreen.route,
        Screen.VideoPickerScreen.route,
        Screen.AudioPlayerScreen.route
    )

    Scaffold(
        bottomBar = {
            if (showBottomBar) {
                NavigationBar {
                    NavigationBarItem(
                        selected = currentRoute == Screen.SelectUrlScreen.route,
                        onClick = {
                            navController.navigate(Screen.SelectUrlScreen.route) {
                                popUpTo(Screen.SelectUrlScreen.route) { inclusive = true }
                            }
                        },
                        icon = {
                            Icon(painterResource(R.drawable.video), contentDescription = null)
                        },
                        label = { Text("Network") }
                    )
                    NavigationBarItem(
                        selected = currentRoute == Screen.VideoPickerScreen.route,
                        onClick = {
                            navController.navigate(Screen.VideoPickerScreen.route) {
                                popUpTo(Screen.SelectUrlScreen.route) { inclusive = false }
                            }
                        },
                        icon = {
                            Icon(painterResource(R.drawable.music_file), contentDescription = null)
                        },
                        label = { Text("Local") }
                    )
                    NavigationBarItem(
                        selected = currentRoute == Screen.AudioPlayerScreen.route,
                        onClick = {
                            navController.navigate(Screen.AudioPlayerScreen.route) {
                                popUpTo(Screen.SelectUrlScreen.route) { inclusive = false }
                            }
                        },
                        icon = {
                            Icon(painterResource(R.drawable.music), contentDescription = null)
                        },
                        label = { Text("Audio") }
                    )
                }
            }
        }
    ) { innerPadding ->
        AppNavHost(navController = navController, innerPadding = innerPadding)
    }
}
