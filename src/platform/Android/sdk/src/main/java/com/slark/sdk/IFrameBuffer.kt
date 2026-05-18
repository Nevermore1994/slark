
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
