package com.slark.sdk

import java.text.MessageFormat
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel

class SlarkLog {
    companion object {
        init {
            System.loadLibrary("slark")
        }

        private const val LOG_PRINT = "[print]"
        private const val LOG_DEBUG = "[debug]"
        private const val LOG_INFO = "[info]"
        private const val LOG_WARNING = "[warning]"
        private const val LOG_ERROR = "[error]"
        private const val LOG_RECORD = "[record]"

        private val job = SupervisorJob()
        private val logScope = CoroutineScope(job + Dispatchers.Default)
        private val logChannel = Channel<LogMessage>(Channel.BUFFERED)

        private data class LogMessage(
            val level: String,
            val tag: String,
            val format: String,
            val args: Array<out Any?>
        )

        init {
            startLogging()
        }

        private fun startLogging() {
            logScope.launch {
                for (message in logChannel) {
                    processLog(message)
                }
            }
        }

        private external fun nativeLog(message: String)

        private fun formatMessage(format: String, vararg args: Any?): String {
            return MessageFormat.format(format, *(args ?: emptyArray()))
        }

        private fun processLog(message: LogMessage) {
            val logStr = "{${message.level}} [${message.tag}] ${formatMessage(message.format, *message.args)}"
            if (message.level != LOG_PRINT) {
                nativeLog(logStr)
            }
            if (message.level != LOG_RECORD) {
                println(logStr)
            }
        }

        private fun submitLog(level: String, tag: String, format: String, vararg args: Any?) {
            logScope.launch {
                logChannel.send(LogMessage(level, tag, format, args))
            }
        }

        fun p(tag: String, format: String, vararg args: Any?) {
            submitLog(LOG_PRINT, tag, format, args)
        }

        fun d(tag: String, format: String, vararg args: Any?) {
            submitLog(LOG_DEBUG, tag, format, args)
        }

        fun i(tag: String, format: String, vararg args: Any?) {
            submitLog(LOG_INFO, tag, format, args)
        }

        fun w(tag: String, format: String, vararg args: Any?) {
            submitLog(LOG_WARNING, tag, format, args)
        }

        fun e(tag: String, format: String, vararg args: Any?) {
            submitLog(LOG_ERROR, tag, format, args)
        }

        fun r(tag: String, format: String, vararg args: Any?) {
            submitLog(LOG_RECORD, tag, format, args)
        }

        fun cleanup() {
            runBlocking {
                logChannel.close()

                job.cancelAndJoin()

                logScope.cancel()
            }
        }

        fun shutdown(timeout: Long = 5000L) {
            runBlocking {
                withTimeoutOrNull(timeout) {
                    cleanup()
                } ?: run {
                    logScope.cancel()
                }
            }
        }

    }
}