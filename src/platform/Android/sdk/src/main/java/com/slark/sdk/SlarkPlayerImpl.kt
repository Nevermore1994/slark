//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import android.graphics.SurfaceTexture
import android.util.Size
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.TextureView
import android.view.View
import com.slark.api.KtTimeRange
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayer.Rotation
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerEvent
import com.slark.api.SlarkPlayerObserver
import com.slark.api.SlarkPlayerState
import com.slark.api.SlarkRenderTarget
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.ceil
import kotlin.math.min

class SlarkPlayerImpl(val config: SlarkPlayerConfig, private val playerId: String): SlarkPlayer {
    private var playerObserver: SlarkPlayerObserver? = null
    private var renderThread: IRenderThread? = null
    private var renderView: View? = null
    private var isPlayingBeforeBackground = false
    private var graphicsBackend: GraphicsBackend = GraphicsBackend.OpenGL
    private var currentSurface: Surface? = null
    private var currentSurfaceWidth: Int = 0
    private var currentSurfaceHeight: Int = 0
    private var isReleased = false
    private val releaseScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var lastVideoRotation: Int = 0
    private var pendingSurfaceRebuild = false

    override var isLoop: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setLoop(playerId, value)
        }

    override var isMute: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setMute(playerId, value)
        }

    override var volume: Float = 100.0f
        set(value) {
            field = min(100.0f, maxOf(0.0f, value))
            SlarkPlayerManager.setVolume(playerId, field)
        }

    override fun playerId(): String {
        return playerId
    }

    override fun prepare() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PREPARE.ordinal)
        // The core drops setLoop before prepare, so replay the cached value.
        if (isLoop) {
            SlarkPlayerManager.setLoop(playerId, true)
        }
    }

    override fun play() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PLAY.ordinal)
    }

    override fun pause() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PAUSE.ordinal)
    }

    override fun release() {
        if (isReleased) return
        isReleased = true
        renderThread?.stop()
        releaseScope.launch {
            renderThread?.shutdown()
            renderThread?.join()
            renderThread = null
            SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.RELEASE.ordinal)
            SlarkPlayerManager.removePlayer(playerId)
        }
    }

    override fun seekTo(time: Double, isAccurate: Boolean) {
        val t = ceil(time * 1000) / 1000.0
        SlarkPlayerManager.seek(playerId, t, isAccurate)
    }

    override fun setObserver(observer: SlarkPlayerObserver?) {
        playerObserver = observer
    }

    override fun setRenderTarget(renderTarget: SlarkRenderTarget) {
        when (renderTarget) {
            is SlarkRenderTarget.FromSurface -> {
                onSurfaceAvailable(renderTarget.surface, renderTarget.width, renderTarget.height)
            }
            is SlarkRenderTarget.FromSurfaceView -> {
                setupSurfaceView(renderTarget.view)
            }
            is SlarkRenderTarget.FromTextureView -> {
                setupTextureView(renderTarget.view)
            }
        }
    }

    override fun setRenderSize(width: Int, height: Int) {
        renderThread?.setRenderSize(width, height)
    }

    override val hasVideo: Boolean
        get() = SlarkPlayerManager.hasVideo(playerId)

    override fun setRotation(rotation: Rotation) {
        val degrees = rotation.ordinal * 90
        SlarkLog.i(LOG_TAG, "setRotation(api): rotation=$rotation, degrees={$degrees}")
        renderThread?.setRotation(degrees)
        renderThread?.presentBackup()
    }

    fun setVideoRotation(rotationDegrees: Int) {
        SlarkLog.i(LOG_TAG, "setVideoRotation(metadata): degrees=$rotationDegrees")
        lastVideoRotation = rotationDegrees
        renderThread?.setRotation(rotationDegrees)
        renderThread?.presentBackup()
    }

    override fun onBackground(isBackground: Boolean) {
        if (isBackground) {
            if (state() == SlarkPlayerState.Playing) {
                isPlayingBeforeBackground = true
                pause()
            } else {
                isPlayingBeforeBackground = false
            }
        } else {
            if (isPlayingBeforeBackground) {
                play()
            }
        }
    }

    override fun totalDuration(): Double {
        return SlarkPlayerManager.totalDuration(playerId)
    }

    override fun currentTime(): Double {
        return SlarkPlayerManager.currentPlayedTime(playerId)
    }

    override fun debugSnapshot(): String {
        return SlarkPlayerManager.debugSnapshot(playerId)
    }

    override val isSeekable: Boolean
        get() = SlarkPlayerManager.isSeekable(playerId)

    override fun state(): SlarkPlayerState {
        return SlarkPlayerManager.state(playerId)
    }

    fun observer(): SlarkPlayerObserver? {
        return playerObserver
    }

    fun requestRender(textureRenderId: Int, width: Int, height: Int) {
        renderThread?.render(textureRenderId, width, height)
    }

    fun reconcileRendering() {
        if (SlarkPlayerManager.vsyncDesired(playerId)) {
            renderThread?.start()
        } else {
            renderThread?.stop()
        }
    }

    fun setRenderSuspended(suspended: Boolean) {
        SlarkPlayerManager.setRenderSuspended(playerId, suspended)
    }

    @Volatile
    private var isSwitchingBackend = false

    fun switchGraphicsBackend(backend: GraphicsBackend) {
        SlarkLog.i(LOG_TAG, "switchGraphicsBackend: {} -> {}", graphicsBackend, backend)
        if (graphicsBackend == backend) return
        isSwitchingBackend = true
        pendingSurfaceRebuild = false
        setRenderSuspended(true)
        graphicsBackend = backend
        if (currentSurface != null) {
            rebuildRenderThread()
        }
        setRenderSuspended(false)
        isSwitchingBackend = false
        if (pendingSurfaceRebuild) {
            pendingSurfaceRebuild = false
            if (currentSurface != null) {
                rebuildRenderThread()
            } else {
                renderThread?.shutdown()
                renderThread = null
            }
        }
        reconcileRendering()
        SlarkLog.i(LOG_TAG, "switchGraphicsBackend: done")
    }

    private fun rebuildRenderThread() {
        val surface = currentSurface ?: return
        val old = renderThread
        old?.shutdown()
        old?.join()
        renderThread = RenderThreadFactory.create(
            graphicsBackend, surface, currentSurfaceWidth, currentSurfaceHeight
        )
        renderThread?.playerId = playerId
        renderThread?.initContext()
        renderThread?.setRotation(lastVideoRotation)
        renderThread?.presentBackup()
    }

    private fun onSurfaceAvailable(surface: Surface, width: Int, height: Int) {
        currentSurface = surface
        currentSurfaceWidth = width
        currentSurfaceHeight = height
        if (isSwitchingBackend) {
            pendingSurfaceRebuild = true
            return
        }
        rebuildRenderThread()
        reconcileRendering()
    }

    private fun onSurfaceDestroyed() {
        currentSurface = null
        if (isSwitchingBackend) {
            pendingSurfaceRebuild = true
            return
        }
        renderThread?.shutdown()
        renderThread = null
    }

    private fun setupTextureView(textureView: TextureView) {
        renderView = textureView
        textureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
                onSurfaceAvailable(Surface(surface), width, height)
            }

            override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
                renderThread?.setRenderSize(width, height)
                renderThread?.presentBackup()
            }

            override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
                onSurfaceDestroyed()
                return true
            }

            override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {}
        }
        if (textureView.isAvailable) {
            onSurfaceAvailable(Surface(textureView.surfaceTexture), textureView.width, textureView.height)
        }
    }

    private fun setupSurfaceView(surfaceView: SurfaceView) {
        renderView = surfaceView
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                onSurfaceAvailable(holder.surface, holder.surfaceFrame.width(), holder.surfaceFrame.height())
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                renderThread?.setRenderSize(width, height)
                renderThread?.presentBackup()
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                onSurfaceDestroyed()
            }
        })
        val holderSurface = surfaceView.holder.surface
        if (holderSurface != null && holderSurface.isValid) {
            onSurfaceAvailable(holderSurface, surfaceView.holder.surfaceFrame.width(), surfaceView.holder.surfaceFrame.height())
        }
    }

    companion object {
        const val LOG_TAG = "SlarkPlayerImpl"

        fun create(config: SlarkPlayerConfig): SlarkPlayerImpl {
            if (config.dataSource.isEmpty()) {
                throw IllegalArgumentException("dataSource is empty")
            }
            val (start, duration) = config.timeRange.get()
            val playerId = config.flowConfig?.let {
                SlarkPlayerManager.createPlayerWithFlowConfig(
                    config.dataSource, start, duration, it, config.perfEnabled
                )
            } ?: SlarkPlayerManager.createPlayer(
                config.dataSource, start, duration, config.perfEnabled
            )
            val player = SlarkPlayerImpl(config, playerId)
            SlarkPlayerManager.addPlayer(playerId, player)
            return player
        }
    }
}
