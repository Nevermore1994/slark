package com.slark.api

data class KtTimeRange(val start: KtTime, val duration: KtTime) {
    val end: KtTime
        get() = start + duration

    fun get(): Pair<Double, Double> {
        return Pair(start.toSeconds(), end.toSeconds())
    }

    fun contains(time: KtTime): Boolean {
        return time >= start && time < end
    }

    fun isOverlap(other: KtTimeRange): Boolean {
        return this.start < other.end && other.start < this.end
    }

    fun overlap(other: KtTimeRange): KtTimeRange? {
        if (!isOverlap(other)) return null
        val newStart = maxOf(this.start, other.start)
        val newEnd = minOf(this.end, other.end)
        return KtTimeRange(newStart, newEnd - newStart)
    }

    fun union(other: KtTimeRange): KtTimeRange? {
        if (!isOverlap(other) && this.end != other.start && other.end != this.start) return null
        val newStart = minOf(this.start, other.start)
        val newEnd = maxOf(this.end, other.end)
        return KtTimeRange(newStart, newEnd - newStart)
    }

    companion object {
        val zero = KtTimeRange(KtTime.zero, KtTime.zero)
    }
}