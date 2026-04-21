
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

interface ITexture {
    val id: Int
    val width: Int
    val height: Int

    fun bind(unit: Int = 0)
    fun unbind()
    fun release()
}
