package com.slark.api

/**
 * The default time unit is milliseconds.
 */
data class KtTime(val value: Long, val timescale: Int = 1000) : Comparable<KtTime> {
    init {
        require(timescale != 0) { "Timescale cannot be zero." }
    }

    fun toSeconds(): Double = value.toDouble() / timescale

    operator fun plus(other: KtTime): KtTime {
        val lcm = lcm(timescale, other.timescale)
        val scaledValue1 = value * (lcm / timescale)
        val scaledValue2 = other.value * (lcm / other.timescale)
        return KtTime(scaledValue1 + scaledValue2, lcm)
    }

    operator fun minus(other: KtTime): KtTime {
        val lcm = lcm(timescale, other.timescale)
        val scaledValue1 = value * (lcm / timescale)
        val scaledValue2 = other.value * (lcm / other.timescale)
        return KtTime(scaledValue1 - scaledValue2, lcm)
    }

    override fun compareTo(other: KtTime): Int {
        val diff = this - other
        return diff.value.compareTo(0)
    }

    companion object {
        fun fromSeconds(seconds: Double, timescale: Int = 600): KtTime {
            return KtTime((seconds * timescale).toLong(), timescale)
        }

        private fun gcd(a: Int, b: Int): Int {
            var x = a
            var y = b
            while (y != 0) {
                val temp = y
                y = x % y
                x = temp
            }
            return x
        }

        private fun lcm(a: Int, b: Int): Int {
            return a / gcd(a, b) * b
        }

        val zero = KtTime(0, 1)
    }
}