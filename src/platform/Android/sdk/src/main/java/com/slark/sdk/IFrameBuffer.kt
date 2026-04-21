
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

interface IFrameBuffer {
    val id: Int
    val width: Int
    val height: Int

    fun attachColorTexture(texture: ITexture)
    fun bind()
    fun unbind()
    fun release()
}
