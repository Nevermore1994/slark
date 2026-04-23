// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
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

@Composable
fun NavBackStackEntry.rememberParentEntry(navController: NavController, parentRoute: String): NavBackStackEntry {
    return remember(this) {
        navController.getBackStackEntry(parentRoute)
    }
}

@Composable
fun AppNavHost(navController: NavHostController, innerPadding: PaddingValues) {
    NavHost(
        navController = navController,
        startDestination = Screen.SelectUrlScreen.route
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
                        flowViewModel.selectedVideoUris.clear()
                        flowViewModel.selectedVideoUris.add(uri)
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
                VideoPickerWithPermission(
                    innerPadding = innerPadding,
                    onResult = { selectedUris ->
                        flowViewModel.selectedVideoUris.clear()
                        flowViewModel.selectedVideoUris.addAll(selectedUris)
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
    }
}
