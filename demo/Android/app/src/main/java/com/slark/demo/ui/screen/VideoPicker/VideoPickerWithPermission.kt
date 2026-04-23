
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

package com.slark.demo.ui.screen.VideoPicker

import android.net.Uri
import android.os.Build
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.*
import androidx.compose.ui.platform.LocalContext
import androidx.compose.material3.Text
import com.google.accompanist.permissions.*
import com.google.accompanist.permissions.rememberPermissionState

@OptIn(ExperimentalPermissionsApi::class)
@Composable
fun VideoPickerWithPermission(
    innerPadding: PaddingValues = PaddingValues(),
    onResult: (List<Uri>) -> Unit
) {
    val context = LocalContext.current
    val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        android.Manifest.permission.READ_MEDIA_VIDEO
    } else {
        android.Manifest.permission.READ_EXTERNAL_STORAGE
    }

    val permissionState = rememberPermissionState(permission)

    LaunchedEffect(Unit) {
        if (!permissionState.status.isGranted) {
            permissionState.launchPermissionRequest()
        }
    }

    when {
        permissionState.status.isGranted -> {
            VideoPickerScreen(innerPadding = innerPadding, onResult = onResult)
        }

        permissionState.status.shouldShowRationale -> {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("Please grant video access to continue")
            }
        }

        else -> {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("Permission denied, cannot continue")
            }
        }
    }
}
