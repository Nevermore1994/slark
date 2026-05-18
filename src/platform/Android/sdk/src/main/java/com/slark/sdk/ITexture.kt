
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
