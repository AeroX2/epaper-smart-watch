package com.james.epaperwatch.link

import android.os.Handler
import android.util.Log
import com.james.epaperwatch.protocol.OutMessage
import com.james.epaperwatch.protocol.Protocol
import java.util.ArrayDeque

/**
 * The v2 stop-and-wait layer (PROTOCOL.md "Reliability").
 *
 * Each new message gets the next seq (1..65535, wrapping to 1). Only one message is in flight:
 * the next is sent after the ACK/NAK for the previous one. Without an answer within 1500 ms the
 * message is resent with the same seq, up to 3 retries, then it is dropped. `NAK|FULL` waits
 * 500 ms and resends (counted as a retry); `NAK|PARSE` / `NAK|UNKNOWN` drop it.
 * A failed GATT write is also treated as a retry.
 *
 * A message with [OutMessage.refresh] is rebuilt at every transmission attempt (same seq), so
 * a delayed or retried TIME carries the time of that attempt.
 *
 * When [UNRESPONSIVE_DROPS] messages in a row are dropped for timeout (no ACK or NAK in
 * between), [onUnresponsive] is called once: the watch has stopped answering.
 *
 * Runs on [handler]'s thread.
 */
class ReliableChannel(
    private val handler: Handler,
    private val onUnresponsive: () -> Unit,
    private val writer: (ByteArray, (Boolean) -> Unit) -> Unit,
) {
    enum class Outcome { ACKED, NAKED, TIMED_OUT, CANCELLED }

    fun interface Result {
        fun onResult(outcome: Outcome, reason: String?)
    }

    private class Pending(var message: OutMessage, var result: Result?) {
        var seq = 0
        var retries = 0
        var bytes: ByteArray = ByteArray(0)
    }

    private val queue = ArrayDeque<Pending>()
    private var inflight: Pending? = null
    private var nextSeq = 1
    private var active = false
    private var consecutiveTimeouts = 0

    private val ackTimeout = Runnable { inflight?.let { retry(it, 0L, "timeout") } }
    private val resend = Runnable { inflight?.let { transmit(it) } }

    val isActive: Boolean get() = active
    val pendingCount: Int get() = queue.size + if (inflight != null) 1 else 0

    fun start() {
        active = true
        consecutiveTimeouts = 0
        pump()
    }

    /** Stop and drop everything (link lost). Callbacks receive CANCELLED with a null reason. */
    fun stop() {
        active = false
        handler.removeCallbacks(ackTimeout)
        handler.removeCallbacks(resend)
        val dropped = ArrayList<Pending>()
        inflight?.let { dropped += it }
        inflight = null
        dropped += queue
        queue.clear()
        dropped.forEach { it.result?.onResult(Outcome.CANCELLED, null) }
    }

    fun send(message: OutMessage, result: Result? = null) {
        if (!active) {
            result?.onResult(Outcome.CANCELLED, null)
            return
        }
        val key = message.coalesceKey
        if (key != null) {
            val queued = queue.firstOrNull { it.message.coalesceKey == key }
            if (queued != null) {
                // Superseded before it was sent: replace in place, keeping queue order.
                val replaced = queued.message
                val previous = queued.result
                queued.message = message.superseding(replaced)
                queued.result = result
                previous?.onResult(Outcome.CANCELLED, REASON_SUPERSEDED)
                return
            }
        }
        if (queue.size >= MAX_QUEUE) {
            Log.w(TAG, "Queue full, dropping ${queue.peekFirst()?.message}")
            queue.pollFirst()?.result?.onResult(Outcome.CANCELLED, REASON_OVERFLOW)
        }
        queue.addLast(Pending(message, result))
        pump()
    }

    fun onAck(seq: Int) {
        val p = inflight ?: return
        if (p.seq != seq) return
        consecutiveTimeouts = 0
        finish(p, Outcome.ACKED, null)
    }

    fun onNak(seq: Int, reason: String) {
        val p = inflight ?: return
        if (p.seq != seq) return
        consecutiveTimeouts = 0 // the watch is answering
        Log.w(TAG, "NAK $reason for ${p.message}")
        if (reason == "FULL") {
            handler.removeCallbacks(ackTimeout)
            retry(p, Protocol.FULL_BACKOFF_MS, "FULL")
        } else {
            finish(p, Outcome.NAKED, reason)
        }
    }

    private fun pump() {
        if (!active || inflight != null) return
        val p = queue.pollFirst() ?: return
        p.seq = allocateSeq()
        inflight = p
        transmit(p)
    }

    private fun transmit(p: Pending) {
        if (inflight !== p) return
        // Built now, not when it was queued: a retried or delayed TIME must not be stale.
        val current = p.message.refresh?.invoke() ?: p.message
        p.bytes = current.encode(p.seq)
        writer(p.bytes) { ok ->
            if (inflight !== p) return@writer // already ACKed (ACK may beat the write response)
            if (ok) {
                handler.removeCallbacks(ackTimeout)
                handler.postDelayed(ackTimeout, Protocol.ACK_TIMEOUT_MS)
            } else {
                retry(p, WRITE_RETRY_MS, "write failed")
            }
        }
    }

    private fun retry(p: Pending, delayMs: Long, why: String) {
        if (inflight !== p) return
        p.retries++
        if (p.retries > Protocol.MAX_RETRIES) {
            Log.w(TAG, "Dropping ${p.message} after ${Protocol.MAX_RETRIES} retries ($why)")
            finish(p, if (why == "FULL") Outcome.NAKED else Outcome.TIMED_OUT, why)
            return
        }
        handler.removeCallbacks(resend)
        handler.postDelayed(resend, delayMs)
    }

    private fun finish(p: Pending, outcome: Outcome, reason: String?) {
        handler.removeCallbacks(ackTimeout)
        handler.removeCallbacks(resend)
        inflight = null
        p.result?.onResult(outcome, reason)
        if (outcome == Outcome.TIMED_OUT && active) {
            consecutiveTimeouts++
            if (consecutiveTimeouts >= UNRESPONSIVE_DROPS) {
                consecutiveTimeouts = 0
                Log.w(TAG, "$UNRESPONSIVE_DROPS messages in a row timed out")
                onUnresponsive()
                return
            }
        }
        pump()
    }

    private fun allocateSeq(): Int {
        val seq = nextSeq
        nextSeq = if (nextSeq >= 65_535) 1 else nextSeq + 1
        return seq
    }

    companion object {
        private const val TAG = "ReliableChannel"
        private const val MAX_QUEUE = 64
        private const val WRITE_RETRY_MS = 250L
        const val UNRESPONSIVE_DROPS = 3
        const val REASON_SUPERSEDED = "superseded"
        const val REASON_OVERFLOW = "overflow"
    }
}
