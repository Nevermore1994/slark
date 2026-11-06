//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.navigation

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.navigation
import com.slark.demo.ui.screen.LocalPlayer.*
import com.slark.demo.ui.screen.NetworkPlayer.*
import com.slark.demo.ui.screen.VideoPicker.*
import com.slark.demo.ui.screen.AudioPlayer.AudioPlayerScreenRoute

@Composable
fun NavBackStackEntry.rememberParentEntry(navController: NavController, parentRoute: String): NavBackStackEntry {
    return remember(this) {
        navController.getBackStackEntry(parentRoute)
    }
}

@Composable
fun AppNavHost(navController: NavHostController, innerPadding: PaddingValues) {
    // TEMP DEBUG: land on the local flow when launched with --es video_path. Remove after
    // the measurement run.
    val debugPath = (androidx.compose.ui.platform.LocalContext.current as? android.app.Activity)
        ?.intent?.getStringExtra("video_path")

    NavHost(
        navController = navController,
        startDestination = if (debugPath.isNullOrEmpty()) "network_flow" else "local_flow"
    ) {
        navigation(
            startDestination = Screen.SelectUrlScreen.route,
            route = "network_flow"
        ) {
            composable(Screen.SelectUrlScreen.route) { entry ->
                val flowViewModel: SharedViewModel = viewModel(entry.rememberParentEntry(navController, "network_flow"))
                SelectUrlScreen(
                    innerPadding = innerPadding,
                    onItemClick = { uri ->
                        flowViewModel.setUris(listOf(uri))
                        navController.navigate(Screen.NetworkPlayerScreen.route)
                    }
                )
            }
            composable(Screen.NetworkPlayerScreen.route) { entry ->
                val flowViewModel: SharedViewModel = viewModel(entry.rememberParentEntry(navController, "network_flow"))
                NetworkPlayerScreenRoute(
                    navController = navController,
                    sharedViewModel = flowViewModel
                )
            }
        }

        navigation(
            startDestination = Screen.VideoPickerScreen.route,
            route = "local_flow"
        ) {
            composable(Screen.VideoPickerScreen.route) { entry ->
                val flowViewModel: SharedViewModel = viewModel(entry.rememberParentEntry(navController, "local_flow"))
                // TEMP DEBUG: skip the picker when launched with --es video_path. Remove after
                // the measurement run.
                androidx.compose.runtime.LaunchedEffect(debugPath) {
                    if (!debugPath.isNullOrEmpty() && flowViewModel.firstUri == null) {
                        flowViewModel.setUris(listOf(android.net.Uri.parse(debugPath)))
                        navController.navigate(Screen.LocalPlayerScreen.route)
                    }
                }
                VideoPickerWithPermission(
                    innerPadding = innerPadding,
                    onResult = { selectedUris ->
                        flowViewModel.setUris(selectedUris)
                        navController.navigate(Screen.LocalPlayerScreen.route)
                    }
                )
            }
            composable(Screen.LocalPlayerScreen.route) { entry ->
                val flowViewModel: SharedViewModel = viewModel(entry.rememberParentEntry(navController, "local_flow"))
                LocalPlayerScreenRoute(
                    navController = navController,
                    sharedViewModel = flowViewModel
                )
            }
        }

        composable(Screen.AudioPlayerScreen.route) {
            AudioPlayerScreenRoute(navController = navController, innerPadding = innerPadding)
        }
    }
}
