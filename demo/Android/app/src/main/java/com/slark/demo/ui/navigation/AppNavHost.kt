package com.slark.demo.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.NavController
import androidx.navigation.navigation
import com.slark.demo.HomeScreenRoute
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
fun AppNavHost(navController: NavHostController) {

    NavHost(
        navController = navController,
        startDestination = Screen.HomeScreen.route
    ) {
        composable(Screen.HomeScreen.route) {
            HomeScreenRoute(navController)
        }

        navigation(
            startDestination = Screen.VideoPickerScreen.route,
            route = "local_flow"
        ) {
            composable(Screen.VideoPickerScreen.route) { entry ->
                val flowViewModel: SharedViewModel = viewModel(entry.rememberParentEntry(navController, "local_flow"))

                VideoPickerWithPermission(
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

        navigation(
            startDestination = Screen.SelectUrlScreen.route,
            route = "network_flow"
        ) {
            composable(Screen.SelectUrlScreen.route) { entry ->
                val flowViewModel: SharedViewModel = viewModel(entry.rememberParentEntry(navController, "network_flow"))

                SelectUrlScreen(
                    onItemClick = { selectedUris ->
                        flowViewModel.selectedVideoUris.clear()
                        flowViewModel.selectedVideoUris.add(selectedUris)
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
    }
}