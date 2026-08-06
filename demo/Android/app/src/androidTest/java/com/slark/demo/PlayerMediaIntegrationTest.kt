//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Real-media playback integration tests (Android instrumentation).
// Validates the playback pipeline in the full JVM + MediaCodec environment
// on emulators/devices.

package com.slark.demo

import android.app.Application
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerEvent
import com.slark.api.SlarkPlayerFactory
import com.slark.api.SlarkPlayerObserver
import com.slark.api.SlarkPlayerState
import com.slark.api.SlarkSdk
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit

/**
 * Real-media playback integration tests.
 * Media assets reuse the app's assets: sample-3s.wav (3s PCM).
 */
@RunWith(AndroidJUnit4::class)
class PlayerMediaIntegrationTest {

    @Before
    fun initSdk() {
        // Initialize the SDK synchronously (triggers System.loadLibrary + MediaCodec warm-up)
        // to avoid UnsatisfiedLinkError from the app's unfinished async init
        val app = InstrumentationRegistry.getInstrumentation().targetContext.applicationContext
            as Application
        SlarkSdk.init(app)
    }

    private fun copyAssetToCache(name: String): String {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val out = File(context.cacheDir, name)
        context.assets.open(name).use { input ->
            out.outputStream().use { output -> input.copyTo(output) }
        }
        return out.absolutePath
    }

    private fun waitForPrepared(player: SlarkPlayer, timeoutMs: Long = 10000): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val s = player.state()
            // Leaving the init states counts as prepared (Android may go straight to Buffering)
            if (s != SlarkPlayerState.NotInited && s != SlarkPlayerState.Initializing) return true
            Thread.sleep(50)
        }
        return player.state() != SlarkPlayerState.NotInited
    }

    private fun waitForState(
        player: SlarkPlayer,
        target: SlarkPlayerState,
        timeoutMs: Long
    ): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (player.state() == target) return true
            Thread.sleep(20)
        }
        return player.state() == target
    }

    private fun createPlayer(
        config: SlarkPlayerConfig,
        onCompleted: () -> Unit = {},
        onEvent: (SlarkPlayerEvent) -> Unit = {},
        states: MutableList<SlarkPlayerState>? = null,
        events: MutableList<SlarkPlayerEvent>? = null
    ): SlarkPlayer {
        val player = SlarkPlayerFactory.createPlayer(config)!!
        player.setObserver(object : SlarkPlayerObserver {
            override fun notifyTime(playerId: String, time: Double) {}

            override fun notifyState(playerId: String, state: SlarkPlayerState) {
                states?.add(state)
                if (state == SlarkPlayerState.Completed) onCompleted()
            }

            override fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
                events?.add(event)
                onEvent(event)
            }
        })
        return player
    }

    @Test
    fun wavPlaysToCompletion() {
        val path = copyAssetToCache("sample-3s.wav")
        val events = CopyOnWriteArrayList<SlarkPlayerEvent>()
        val states = CopyOnWriteArrayList<SlarkPlayerState>()
        val latch = CountDownLatch(1)
        val player = createPlayer(
            SlarkPlayerConfig(path),
            onCompleted = { latch.countDown() },
            onEvent = { if (it == SlarkPlayerEvent.PlayEnd) latch.countDown() },
            states = states,
            events = events
        )
        player.prepare()
        assertTrue("wav should reach Prepared", waitForPrepared(player))
        player.play()
        // PlayEnd arrives before the Completed state; the Completed callback may lag across
        // threads, so query JNI state() as a fallback to avoid a callback-timing race
        assertTrue("wav should complete in 15s", latch.await(15, TimeUnit.SECONDS))
        assertTrue("PlayEnd event received", events.contains(SlarkPlayerEvent.PlayEnd))
        val completedViaQuery = waitForState(player, SlarkPlayerState.Completed, 3000)
        assertTrue(
            "Completed state reached (callback=$states)",
            states.contains(SlarkPlayerState.Completed) || completedViaQuery
        )
        player.release()
    }

    @Test
    fun wavSeekDuringPlayback() {
        val path = copyAssetToCache("sample-3s.wav")
        val events = CopyOnWriteArrayList<SlarkPlayerEvent>()
        val latch = CountDownLatch(1)
        val player = createPlayer(
            SlarkPlayerConfig(path),
            onCompleted = { latch.countDown() },
            onEvent = { if (it == SlarkPlayerEvent.PlayEnd) latch.countDown() },
            events = events
        )
        player.prepare()
        assertTrue("wav should reach Prepared", waitForPrepared(player))
        player.play()
        // Wait for playback to start
        Thread.sleep(500)
        player.seekTo(1.5, false)
        assertTrue("wav should finish after seek in 15s", latch.await(15, TimeUnit.SECONDS))
        assertTrue("PlayEnd after seek", events.contains(SlarkPlayerEvent.PlayEnd))
        player.release()
    }

    @Test
    fun missingFileReportsError() {
        val states = CopyOnWriteArrayList<SlarkPlayerState>()
        val latch = CountDownLatch(1)
        val player = createPlayer(
            SlarkPlayerConfig("/data/local/tmp/not_exist.wav"),
            onCompleted = {},
            onEvent = { if (it == SlarkPlayerEvent.OnError) latch.countDown() },
            states = states
        )
        player.prepare()
        assertTrue("missing file should report error in 10s", latch.await(10, TimeUnit.SECONDS))
        assertTrue("Error state reached", states.contains(SlarkPlayerState.Error))
        player.release()
    }
}
