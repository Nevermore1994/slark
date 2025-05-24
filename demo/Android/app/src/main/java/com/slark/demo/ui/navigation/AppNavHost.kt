package com.slark.demo.ui.navigation

import androidx.activity.ComponentActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import com.slark.demo.HomeScreenRoute
import com.slark.demo.ui.screen.LocalPlayer.*
import com.slark.demo.ui.screen.VideoPicker.*

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
            LocalPlayerScreenRoute(navController)
        }

        composable(Screen.VideoPickerScreen.route) {
            val context = LocalContext.current
            val activity = context as ComponentActivity
            val sharedViewModel: SharedViewModel = viewModel(activity)
            VideoPickerWithPermission(
                onResult = { selectedUris ->
                    sharedViewModel.selectedVideoUris.clear()
                    sharedViewModel.selectedVideoUris.addAll(selectedUris)
                    if (navController.previousBackStackEntry?.destination?.route == Screen.LocalPlayerScreen.route) {
                        navController.popBackStack()
                    } else {
                        navController.navigate(Screen.LocalPlayerScreen.route)
                    }
                }
            )
        }
    }
}