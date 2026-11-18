package com.james.epaperwatch.ota

import android.os.Handler
import android.os.SystemClock
import android.util.Log
import com.james.epaperwatch.link.ReliableChannel
import com.james.epaperwatch.ota.OtaState.Phase
import com.james.epaperwatch.protocol.InMessage
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import com.james.epaperwatch.protocol.Protocol

/**
 * Phone side of PROTOCOL.md "Firmware update over Bluetooth (OTA)". Owned by the link service
 * and run on its handler thread; publishes to [OtaState].
 *
 * `OTA_BEGIN` -> wait for `OTA_STATE|ACCEPT|n` (the wearer confirms on the watch) -> chunks to
 * the OTA data characteristic, one write-with-response at a time, each `offset` (4 bytes LE)
 * + at most min(n, MTU - 7) bytes -> `OTA_END` -> `OTA_STATE|READY` -> the watch reboots and
 * reports `INSTALLED` / `FAILED` in its next session.
 *
 * `OTA_STATE|PROGRESS|offset` handling. The watch sends it "about every 16 KiB and after any
 * rejected chunk", through its ordinary notification queue, so a periodic report can arrive
 * after later chunks were already written and acknowledged. Rewinding on every report that is
 * behind our position would then resend accepted data and cause a rejection storm. So:
 *  - a report *ahead* of our position is always adopted (the watch also reports once at the
 *    end of the data, which ends the short wait before `OTA_END`);
 *  - a report *behind* it is adopted when it is repeated (periodic reports never repeat, a
 *    watch that is rejecting chunks repeats its expected offset for each one), or when all
 *    bytes have been written and we are about to send `OTA_END`.
 */
class OtaManager(
    private val handler: Handler,
    private val host: Host,
) {
    /** What the transfer needs from the link service. */
    interface Host {
        val sessionActive: Boolean
        /** HELLO flags include OTA and the OTA data characteristic exists. */
        val otaSupported: Boolean
        val mtu: Int
        /** Send an OTA control message even while ordinary traffic is suppressed. */
        fun sendControl(message: OutMessage, result: ReliableChannel.Result? = null)
        fun writeChunk(packet: ByteArray, done: (Boolean) -> Unit)
        fun setHighPriority(high: Boolean)
        fun holdWakeLock(hold: Boolean)
        /** The transfer ended while the link is still up: ordinary traffic may flow again. */
        fun onTrafficResumed()
    }

    private var image: FirmwareImage? = null
    /** Bumped for every transfer and at its end, so stale callbacks are ignored. */
    private var transferId = 0
    private var chunk = 0
    /** Offset of the next chunk to write. */
    private var offset = 0
    private var writing = false
    private var inflightStart = 0
    private var inflightEnd = 0
    /** Set while a write is in flight and the watch asked to continue elsewhere. */
    private var rewindTo = -1
    private var lastProgress = -1
    private var draining = false
    private var highPriority = false
    private var sawDisconnect = false

    private var speedMark = 0L
    private var speedBytes = 0
    private var speed = 0.0
    private var lastPublish = 0L

    private val confirmTimeout = Runnable {
        failTransfer("The update was not confirmed on the watch in time.", abort = true)
    }
    private val verifyTimeout = Runnable {
        failTransfer("The watch did not confirm the transfer.", abort = true)
    }
    private val drainDone = Runnable { sendEnd() }
    private val installTimeout = Runnable {
        finish(Phase.FAILED, "The watch did not come back with an install result. Switch Bluetooth on at the " +
            "watch and check its firmware version; USB DFU remains the recovery path.")
    }
    private val reportTimeout = Runnable {
        finish(Phase.DONE, "The watch restarted but sent no install report. " +
            "Check the firmware version below.")
    }

    /** While true the service sends nothing but OTA control messages (spec: only OTA_ABORT). */
    val blocksTraffic: Boolean
        get() = when (OtaState.phase) {
            Phase.REQUESTING, Phase.WAITING_CONFIRM, Phase.SENDING, Phase.VERIFYING -> true
            Phase.INSTALLING -> !sawDisconnect // the watch is about to reboot
            else -> false
        }

    /** Returns null if the transfer was started, else why it could not be. */
    fun start(firmware: FirmwareImage): String? {
        if (OtaState.busy) return "An update is already running"
        if (!host.sessionActive) return "Watch is not connected"
        if (!host.otaSupported) return "This watch does not support updates over Bluetooth"
        val id = ++transferId
        image = firmware
        offset = 0
        writing = false
        rewindTo = -1
        lastProgress = -1
        draining = false
        sawDisconnect = false
        speed = 0.0
        host.holdWakeLock(true)
        OtaState.setProgress(0, firmware.size, 0, 0)
        OtaState.setPhase(Phase.REQUESTING, "Asking the watch")
        host.sendControl(Out.otaBegin(firmware.size, firmware.crc32, firmware.versionLabel)) { outcome, reason ->
            if (id != transferId || OtaState.phase != Phase.REQUESTING) return@sendControl
            when (outcome) {
                ReliableChannel.Outcome.ACKED -> {
                    OtaState.setPhase(
                        Phase.WAITING_CONFIRM,
                        "Confirm the update on the watch: Select accepts, Back declines.",
                    )
                    handler.postDelayed(confirmTimeout, CONFIRM_TIMEOUT_MS)
                }
                ReliableChannel.Outcome.NAKED -> failTransfer(
                    if (reason == "UNKNOWN") {
                        "This watch firmware does not know Bluetooth updates."
                    } else {
                        "The watch rejected the update request ($reason)."
                    },
                    abort = false,
                )
                ReliableChannel.Outcome.TIMED_OUT ->
                    failTransfer("The watch did not answer the update request.", abort = false)
                ReliableChannel.Outcome.CANCELLED ->
                    failTransfer("The connection dropped before the update started.", abort = false)
            }
        }
        return null
    }

    /** User pressed Cancel (or dismissed a finished result). */
    fun cancel() {
        when (OtaState.phase) {
            Phase.REQUESTING, Phase.WAITING_CONFIRM, Phase.SENDING, Phase.VERIFYING ->
                failTransfer("Update cancelled.", abort = true)
            Phase.DONE, Phase.FAILED -> OtaState.setPhase(Phase.IDLE, "")
            Phase.INSTALLING, Phase.IDLE -> Unit // the install cannot be stopped from here
        }
    }

    /** `OTA_STATE|state|detail` from the watch. */
    fun onOtaState(m: InMessage) {
        val state = m.str(0)
        val detail = m.str(1)
        when (state) {
            "ACCEPT" -> if (OtaState.phase == Phase.WAITING_CONFIRM || OtaState.phase == Phase.REQUESTING) {
                // A third field of 1: the watch takes full-size packets (see OTA_SAFE_CHUNK).
                startSending(m.int(1), bigPackets = m.has(2) && m.int(2) == 1)
            }
            "DECLINED" -> ifBusy { failTransfer("The update was declined on the watch, or not confirmed there within a minute.", abort = false) }
            "BUSY" -> ifBusy {
                failTransfer("The watch is busy and cannot take an update now. Try again shortly.", abort = false)
            }
            "UNSUPPORTED" -> ifBusy {
                failTransfer("This watch cannot be updated over Bluetooth (use USB DFU).", abort = false)
            }
            "PROGRESS" -> onProgress(m.int(1), m.has(1))
            "ERROR" -> ifBusy { failTransfer(describeError(detail), abort = false) }
            "READY" -> if (OtaState.phase == Phase.VERIFYING || OtaState.phase == Phase.SENDING) {
                handler.removeCallbacks(verifyTimeout)
                handler.removeCallbacks(drainDone)
                transferId++
                dropHighPriority()
                image?.let { OtaState.setProgress(it.size, it.size, 0, chunk) }
                OtaState.setPhase(
                    Phase.INSTALLING,
                    "Transfer verified. The watch is installing the update and restarts; this takes " +
                        "about half a minute. If it does not reconnect, switch Bluetooth on at the watch.",
                )
                handler.postDelayed(installTimeout, INSTALL_TIMEOUT_MS)
            }
            // Reported once in the first session after the reboot (also if this app was
            // restarted meanwhile and no longer remembers the transfer).
            "INSTALLED" -> if (OtaState.phase == Phase.INSTALLING || !OtaState.busy) {
                finish(Phase.DONE, "The update was installed.")
            }
            "FAILED" -> if (OtaState.phase == Phase.INSTALLING || !OtaState.busy) {
                finish(Phase.FAILED, describeInstallFailure(detail))
            }
            else -> Log.w(TAG, "Unknown OTA_STATE: $m")
        }
    }

    /** The BLE link (or the session on it) ended. */
    fun onLinkLost() {
        when (OtaState.phase) {
            Phase.REQUESTING, Phase.WAITING_CONFIRM, Phase.SENDING, Phase.VERIFYING -> failTransfer(
                "The connection dropped during the transfer. The watch discarded the partial " +
                    "update; start again.",
                abort = false,
            )
            Phase.INSTALLING -> sawDisconnect = true // expected: the watch reboots
            else -> Unit
        }
    }

    /** A new session started (HELLO was acknowledged). */
    fun onSessionStarted() {
        if (OtaState.phase == Phase.INSTALLING && sawDisconnect) {
            handler.removeCallbacks(installTimeout)
            handler.removeCallbacks(reportTimeout)
            OtaState.setPhase(Phase.INSTALLING, "The watch is back; waiting for its install report.")
            handler.postDelayed(reportTimeout, REPORT_TIMEOUT_MS)
        }
    }

    /** The service is stopping. */
    fun shutdown() {
        if (OtaState.busy) {
            val wasInstalling = OtaState.phase == Phase.INSTALLING
            finish(
                Phase.FAILED,
                if (wasInstalling) {
                    "The link service stopped while the watch was installing; check its firmware version."
                } else {
                    "The link service stopped during the transfer."
                },
            )
        }
    }

    // ------------------------------------------------------------------ data phase

    private fun startSending(maxChunk: Int, bigPackets: Boolean) {
        handler.removeCallbacks(confirmTimeout)
        val firmware = image ?: return
        chunk = minOf(maxChunk.coerceAtMost(Protocol.OTA_MAX_CHUNK), host.mtu - Protocol.OTA_CHUNK_OVERHEAD)
        if (!bigPackets) chunk = chunk.coerceAtMost(Protocol.OTA_SAFE_CHUNK)
        if (chunk < 1) {
            failTransfer("The watch offered an unusable chunk size ($maxChunk).", abort = true)
            return
        }
        offset = 0
        host.setHighPriority(true)
        highPriority = true
        speedMark = SystemClock.elapsedRealtime()
        speedBytes = 0
        OtaState.setProgress(0, firmware.size, 0, chunk)
        OtaState.setPhase(Phase.SENDING, "Sending")
        sendNext()
    }

    private fun sendNext() {
        if (OtaState.phase != Phase.SENDING || writing) return
        val firmware = image ?: return
        if (offset >= firmware.size) {
            // Everything is written. Give a late PROGRESS (a rejected tail) a moment to arrive
            // before OTA_END makes the watch check the size.
            if (!draining) {
                draining = true
                handler.postDelayed(drainDone, DRAIN_MS)
            }
            return
        }
        val at = offset
        val length = minOf(chunk, firmware.size - at)
        val packet = ByteArray(4 + length)
        packet[0] = at.toByte()
        packet[1] = (at ushr 8).toByte()
        packet[2] = (at ushr 16).toByte()
        packet[3] = (at ushr 24).toByte()
        System.arraycopy(firmware.bytes, at, packet, 4, length)
        val id = transferId
        writing = true
        inflightStart = at
        inflightEnd = at + length
        host.writeChunk(packet) { ok ->
            if (id != transferId) return@writeChunk
            writing = false
            if (OtaState.phase != Phase.SENDING) return@writeChunk
            if (!ok) {
                failTransfer("A Bluetooth write failed during the transfer.", abort = true)
                return@writeChunk
            }
            if (rewindTo >= 0) {
                offset = rewindTo
                rewindTo = -1
            } else {
                offset = at + length
                speedBytes += length
            }
            publishProgress(force = false)
            sendNext()
        }
    }

    private fun onProgress(expected: Int, present: Boolean) {
        val firmware = image ?: return
        if (OtaState.phase != Phase.SENDING || !present) return
        if (expected < 0 || expected > firmware.size) return
        val position = if (writing) inflightEnd else offset
        when {
            expected == position -> {
                lastProgress = -1
                // The watch's end-of-data report: it has every byte, no need to wait longer.
                if (draining && expected == firmware.size) {
                    handler.removeCallbacks(drainDone)
                    sendEnd()
                }
            }
            expected > position -> resumeFrom(expected)
            // The start of the chunk in flight: a prompt report for the chunk before it.
            writing && expected == inflightStart -> lastProgress = -1
            draining || expected == lastProgress -> resumeFrom(expected)
            else -> lastProgress = expected
        }
    }

    private fun resumeFrom(expected: Int) {
        Log.i(TAG, "Watch expects offset $expected; resuming there")
        lastProgress = -1
        if (draining) {
            draining = false
            handler.removeCallbacks(drainDone)
        }
        if (writing) {
            rewindTo = expected
        } else {
            offset = expected
            sendNext()
        }
    }

    private fun sendEnd() {
        draining = false
        if (OtaState.phase != Phase.SENDING || writing) return
        val firmware = image ?: return
        if (offset < firmware.size) {
            sendNext()
            return
        }
        val id = transferId
        publishProgress(force = true)
        OtaState.setPhase(Phase.VERIFYING, "Verifying on the watch")
        handler.postDelayed(verifyTimeout, VERIFY_TIMEOUT_MS)
        host.sendControl(Out.otaEnd()) { outcome, reason ->
            if (id != transferId || OtaState.phase != Phase.VERIFYING) return@sendControl
            when (outcome) {
                ReliableChannel.Outcome.ACKED -> Unit // READY or ERROR follows
                ReliableChannel.Outcome.NAKED ->
                    failTransfer("The watch rejected the end of the transfer ($reason).", abort = true)
                ReliableChannel.Outcome.TIMED_OUT ->
                    failTransfer("The watch did not acknowledge the end of the transfer.", abort = true)
                ReliableChannel.Outcome.CANCELLED -> Unit // link lost: onLinkLost reports it
            }
        }
    }

    private fun publishProgress(force: Boolean) {
        val firmware = image ?: return
        val now = SystemClock.elapsedRealtime()
        if (!force && now - lastPublish < PUBLISH_INTERVAL_MS) return
        val elapsed = now - speedMark
        if (elapsed >= SPEED_WINDOW_MS) {
            val instant = speedBytes * 1000.0 / elapsed
            speed = if (speed == 0.0) instant else speed * 0.6 + instant * 0.4
            speedMark = now
            speedBytes = 0
        }
        lastPublish = now
        OtaState.setProgress(offset.coerceAtMost(firmware.size), firmware.size, speed.toInt(), chunk)
    }

    // ------------------------------------------------------------------ endings

    private inline fun ifBusy(action: () -> Unit) {
        when (OtaState.phase) {
            Phase.REQUESTING, Phase.WAITING_CONFIRM, Phase.SENDING, Phase.VERIFYING -> action()
            else -> Unit
        }
    }

    /** End a transfer that has not reached the install stage. */
    private fun failTransfer(message: String, abort: Boolean) {
        val linkUp = host.sessionActive
        // Queued before anything the resync below sends, so the watch discards its copy first.
        if (abort && linkUp) host.sendControl(Out.otaAbort())
        finish(Phase.FAILED, message)
        if (linkUp) host.onTrafficResumed()
    }

    /** Every exit path: timers, connection priority, wake lock, final state. */
    private fun finish(phase: Phase, message: String) {
        transferId++
        handler.removeCallbacks(confirmTimeout)
        handler.removeCallbacks(verifyTimeout)
        handler.removeCallbacks(drainDone)
        handler.removeCallbacks(installTimeout)
        handler.removeCallbacks(reportTimeout)
        writing = false
        draining = false
        rewindTo = -1
        dropHighPriority()
        image = null
        host.holdWakeLock(false)
        OtaState.setPhase(phase, message)
    }

    private fun dropHighPriority() {
        if (!highPriority) return
        highPriority = false
        host.setHighPriority(false)
    }

    companion object {
        private const val TAG = "OtaManager"
        /** The watch waits 60 s for the wearer. */
        private const val CONFIRM_TIMEOUT_MS = 75_000L
        private const val VERIFY_TIMEOUT_MS = 60_000L
        private const val INSTALL_TIMEOUT_MS = 5L * 60 * 1000
        private const val REPORT_TIMEOUT_MS = 30_000L
        private const val DRAIN_MS = 800L
        private const val PUBLISH_INTERVAL_MS = 250L
        private const val SPEED_WINDOW_MS = 1_000L

        /** Longest a transfer plus install may hold the CPU awake. */
        const val WAKE_LOCK_LIMIT_MS = 25L * 60 * 1000

        /** `OTA_STATE|ERROR|detail` (PROTOCOL.md). */
        fun describeError(detail: String): String = when (detail) {
            "SIZE" -> "The watch received a different number of bytes than announced."
            "CRC" -> "The copy on the watch failed its checksum. Try again."
            "IMAGE" -> "The watch rejected the file: it is not a valid firmware image."
            "FLASH" -> "The watch could not write its external flash."
            "TIMEOUT" -> "The watch gave up: no data arrived for 30 seconds."
            else -> "The watch reported an error ($detail)."
        }

        /** `OTA_STATE|FAILED|code`: bootloader result codes from code/EXTERNAL_FLASH.md. */
        fun describeInstallFailure(code: String): String = when (code) {
            "0" -> "Not installed: the watch's bootloader predates Bluetooth updates. " +
                "Update the bootloader over USB first."
            "2" -> "Not installed: the staged image failed its checksum. The old firmware is untouched."
            "3" -> "Not installed: the staged image is not a valid firmware image. " +
                "The old firmware is untouched."
            "4" -> "Install failed while programming the internal flash. Reinstall over USB DFU."
            else -> "The watch reported that the install failed (code $code)."
        }
    }
}
