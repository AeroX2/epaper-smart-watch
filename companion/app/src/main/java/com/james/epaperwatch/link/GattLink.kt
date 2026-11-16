package com.james.epaperwatch.link

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.util.Log
import com.james.epaperwatch.protocol.Protocol
import java.util.ArrayDeque

/**
 * One BLE link to the associated watch.
 *
 * - Connects with `connectGatt(autoConnect = true)` to the stored (public) address; after a
 *   link loss the GATT object is closed and a new auto-connect is issued (with backoff on
 *   immediate errors).
 * - Every GATT operation (MTU, discovery, CCC write, message writes, OTA data writes) goes
 *   through one serialized queue; the next operation starts only when the previous one's
 *   callback arrived. A write whose callback does not arrive in time means the link is dead:
 *   it is torn down and reconnected, never followed by another operation.
 * - Watch -> phone notifications are reassembled (PROTOCOL.md "Fragmentation") before they
 *   reach the listener.
 * - If the watch demands an encrypted link (an operation fails with insufficient
 *   authentication / encryption), the phone bonds and then redoes discovery and the CCC write.
 *
 * All methods and listener callbacks run on [handler]'s (main) thread.
 */
@SuppressLint("MissingPermission")
class GattLink(
    private val context: Context,
    private val handler: Handler,
    private val listener: Listener,
) {
    interface Listener {
        fun onLinkStatus(status: Status, detail: String)
        fun onReady()
        fun onDisconnected()
        fun onMessage(bytes: ByteArray)
    }

    enum class Status { IDLE, BLUETOOTH_OFF, NO_PERMISSION, WAITING, SETTING_UP, READY, PAIRING_FAILED }

    private enum class Kind { MTU, DISCOVER, DESCRIPTOR, WRITE, OTA }

    private inner class Op(
        val kind: Kind,
        val timeoutMs: Long,
        val start: (BluetoothGatt) -> Boolean,
        val done: (Boolean) -> Unit,
    )

    var status: Status = Status.IDLE
        private set

    /** Negotiated ATT MTU of the current connection (23 until the exchange completes). */
    var mtu: Int = DEFAULT_MTU
        private set

    /** True if the connected watch exposes the OTA data characteristic. */
    val hasOtaCharacteristic: Boolean get() = ota != null

    private var address: String? = null
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var ota: BluetoothGattCharacteristic? = null
    private var generation = 0
    private var reconnectDelayMs = INITIAL_RECONNECT_MS
    private val reassembler = FragmentReassembler()

    /** Waiting for the bond to complete; no GATT operation is issued meanwhile. */
    private var awaitingBond = false
    /** createBond() was issued (or pairing found running) for a secure watch on this connection. */
    private var secureBondRequested = false
    /** Auth failures seen on this connection while already bonded. */
    private var authRetries = 0
    /** Shown instead of "Waiting for watch" until a session works again. */
    private var stickyFailure: String? = null

    private val ops = ArrayDeque<Op>()
    private var current: Op? = null
    private val opTimeout = Runnable { onOpTimeout() }
    private val reconnect = Runnable { openGatt() }
    private val bondTimeout = Runnable { onBondTimeout() }
    private val resumeSetup = Runnable {
        awaitingBond = false
        gatt?.let { discover(it) }
    }

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BluetoothAdapter.ACTION_STATE_CHANGED -> onAdapterState(
                    intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR),
                )
                BluetoothDevice.ACTION_BOND_STATE_CHANGED -> {
                    val device: BluetoothDevice? = if (Build.VERSION.SDK_INT >= 33) {
                        intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                    }
                    if (device?.address.equals(address, ignoreCase = true)) {
                        onBondState(
                            intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR),
                            intent.getIntExtra(
                                BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.ERROR,
                            ),
                        )
                    }
                }
            }
        }
    }
    private var receiverRegistered = false

    fun connect(address: String) {
        if (!receiverRegistered) {
            val filter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED).apply {
                addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            }
            if (Build.VERSION.SDK_INT >= 33) {
                context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
            } else {
                @Suppress("UnspecifiedRegisterReceiverFlag")
                context.registerReceiver(receiver, filter)
            }
            receiverRegistered = true
        }
        if (this.address == address && (gatt != null || handler.hasCallbacksCompat(reconnect))) return
        teardown()
        this.address = address
        reconnectDelayMs = INITIAL_RECONNECT_MS
        stickyFailure = null
        openGatt()
    }

    /** Drop the link and any pending auto-connect. */
    fun shutdown() {
        address = null
        teardown()
        if (receiverRegistered) {
            context.unregisterReceiver(receiver)
            receiverRegistered = false
        }
        setStatus(Status.IDLE, "Stopped")
    }

    /**
     * Force a fresh connection attempt with backoff (the watch failed the handshake, or it
     * stopped acknowledging messages).
     */
    fun restart() {
        val target = address ?: return
        teardown()
        address = target
        handler.postDelayed(reconnect, reconnectDelayMs)
        reconnectDelayMs = (reconnectDelayMs * 2).coerceAtMost(MAX_RECONNECT_MS)
    }

    /**
     * The session on this link works (HELLO was ACKed): reset the reconnect backoff. Not done
     * at READY, so a watch that connects but fails the handshake is retried with backoff.
     */
    fun markHealthy() {
        reconnectDelayMs = INITIAL_RECONNECT_MS
        stickyFailure = null
    }

    /**
     * The watch announced (HELLO flag 1) that it requires an encrypted link. A secure watch
     * does not answer with an ATT error: it silently ignores every write until the link is
     * encrypted and only repeats its HELLO. So the phone starts the bond itself; the
     * connection, discovery and CCC stay as they are and the service keeps retrying its own
     * HELLO until the watch ACKs. A no-op when already bonded or already bonding.
     */
    fun ensureBonded() {
        val g = gatt ?: return
        if (secureBondRequested || awaitingBond) return
        when (bondStateOf(g)) {
            BluetoothDevice.BOND_BONDED -> Unit // the stack encrypts the link by itself
            BluetoothDevice.BOND_BONDING -> {
                secureBondRequested = true
                setStatus(status, "Pairing with watch")
            }
            else -> {
                secureBondRequested = true
                val started = try {
                    g.device.createBond()
                } catch (e: SecurityException) {
                    false
                }
                Log.i(TAG, "Secure watch: createBond() started=$started")
                setStatus(status, "Pairing with watch")
            }
        }
    }

    /** True while a bond is being created for a secure watch (its HELLO is not ACKed yet). */
    val isBonding: Boolean
        get() = gatt?.let { bondStateOf(it) == BluetoothDevice.BOND_BONDING } ?: false

    /**
     * The secure watch never accepted our messages (pairing did not complete, or one side has
     * lost the other's key): show the pairing-failed status and retry with a long backoff.
     */
    fun failPairing() = bondFailed("the secure watch never acknowledged")

    /** Queue a write-with-response to the phone -> watch characteristic. */
    fun write(value: ByteArray, done: (Boolean) -> Unit) = queueWrite(Kind.WRITE, value, done)

    /** Queue a write-with-response to the OTA data characteristic (same serialized queue). */
    fun writeOta(value: ByteArray, done: (Boolean) -> Unit) = queueWrite(Kind.OTA, value, done)

    /** Ask for a short connection interval during a firmware transfer, or go back to balanced. */
    fun setHighPriority(high: Boolean) {
        try {
            gatt?.requestConnectionPriority(
                if (high) {
                    BluetoothGatt.CONNECTION_PRIORITY_HIGH
                } else {
                    BluetoothGatt.CONNECTION_PRIORITY_BALANCED
                },
            )
        } catch (e: SecurityException) {
            Log.w(TAG, "requestConnectionPriority refused", e)
        }
    }

    private fun queueWrite(kind: Kind, value: ByteArray, done: (Boolean) -> Unit) {
        if (status != Status.READY) {
            handler.post { done(false) }
            return
        }
        enqueue(Op(kind, WRITE_TIMEOUT_MS, { g ->
            val characteristic = (if (kind == Kind.OTA) ota else rx) ?: return@Op false
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(
                    characteristic,
                    value,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                ) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION")
                characteristic.value = value
                @Suppress("DEPRECATION")
                g.writeCharacteristic(characteristic)
            }
        }, done))
    }

    // ---------------------------------------------------------------- connection lifecycle

    private fun onAdapterState(state: Int) {
        when (state) {
            BluetoothAdapter.STATE_ON -> if (address != null && gatt == null) {
                reconnectDelayMs = INITIAL_RECONNECT_MS
                handler.removeCallbacks(reconnect)
                openGatt()
            }
            BluetoothAdapter.STATE_TURNING_OFF, BluetoothAdapter.STATE_OFF -> {
                teardown()
                setStatus(Status.BLUETOOTH_OFF, "Bluetooth is off")
            }
        }
    }

    private fun openGatt() {
        val target = address ?: return
        if (gatt != null) return
        if (!hasConnectPermission(context)) {
            setStatus(Status.NO_PERMISSION, "Nearby devices permission needed")
            return
        }
        val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter == null || !adapter.isEnabled) {
            setStatus(Status.BLUETOOTH_OFF, "Bluetooth is off")
            return
        }
        val device = try {
            if (Build.VERSION.SDK_INT >= 33) {
                // The watch advertises a fixed public address (PROTOCOL.md "Clarifications").
                adapter.getRemoteLeDevice(target, BluetoothDevice.ADDRESS_TYPE_PUBLIC)
            } else {
                adapter.getRemoteDevice(target)
            }
        } catch (e: IllegalArgumentException) {
            setStatus(Status.IDLE, "Invalid watch address")
            return
        }
        generation++
        val failure = stickyFailure
        if (failure != null) {
            setStatus(Status.PAIRING_FAILED, failure)
        } else {
            setStatus(Status.WAITING, "Waiting for watch")
        }
        gatt = try {
            device.connectGatt(context, true, Callback(generation), BluetoothDevice.TRANSPORT_LE)
        } catch (e: SecurityException) {
            null
        }
        if (gatt == null) scheduleReconnect()
    }

    private fun scheduleReconnect() {
        if (address == null) return
        handler.removeCallbacks(reconnect)
        handler.postDelayed(reconnect, reconnectDelayMs)
        reconnectDelayMs = (reconnectDelayMs * 2).coerceAtMost(MAX_RECONNECT_MS)
    }

    /** Close everything and invalidate in-flight callbacks; does not schedule a reconnect. */
    private fun teardown() {
        generation++
        handler.removeCallbacks(reconnect)
        handler.removeCallbacks(bondTimeout)
        handler.removeCallbacks(resumeSetup)
        awaitingBond = false
        secureBondRequested = false
        failAllOps()
        val wasReady = status == Status.READY
        gatt?.let {
            try {
                it.disconnect()
                it.close()
            } catch (_: SecurityException) {
            }
        }
        gatt = null
        rx = null
        tx = null
        ota = null
        mtu = DEFAULT_MTU
        reassembler.reset()
        if (wasReady) {
            status = Status.WAITING
            listener.onDisconnected()
        }
    }

    private fun onConnected(g: BluetoothGatt) {
        authRetries = 0
        secureBondRequested = false
        mtu = DEFAULT_MTU
        reassembler.reset()
        setStatus(Status.SETTING_UP, "Negotiating MTU")
        enqueue(Op(Kind.MTU, SETUP_TIMEOUT_MS, { it.requestMtu(Protocol.REQUESTED_MTU) }) {
            // Continue even if the MTU exchange failed: long writes still work.
            discover(g)
        })
    }

    private fun discover(g: BluetoothGatt) {
        setStatus(Status.SETTING_UP, "Discovering services")
        enqueue(Op(Kind.DISCOVER, DISCOVERY_TIMEOUT_MS, { it.discoverServices() }) { ok ->
            val service = if (ok) g.getService(Protocol.SERVICE_UUID) else null
            rx = service?.getCharacteristic(Protocol.PHONE_TO_WATCH_UUID)
            tx = service?.getCharacteristic(Protocol.WATCH_TO_PHONE_UUID)
            ota = service?.getCharacteristic(Protocol.OTA_DATA_UUID)
            val notifyChar = tx
            if (rx == null || notifyChar == null) {
                Log.w(TAG, "Watch service not found (discovery ok=$ok)")
                setStatus(Status.SETTING_UP, "Watch service missing")
                dropConnection()
                return@Op
            }
            enableNotifications(g, notifyChar)
        })
    }

    private fun enableNotifications(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        setStatus(Status.SETTING_UP, "Enabling notifications")
        enqueue(Op(Kind.DESCRIPTOR, SETUP_TIMEOUT_MS, { gatt ->
            val descriptor = characteristic.getDescriptor(Protocol.CCC_UUID) ?: return@Op false
            if (!gatt.setCharacteristicNotification(characteristic, true)) return@Op false
            if (Build.VERSION.SDK_INT >= 33) {
                gatt.writeDescriptor(
                    descriptor,
                    BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE,
                ) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                gatt.writeDescriptor(descriptor)
            }
        }) { ok ->
            if (!ok) {
                setStatus(Status.SETTING_UP, "Could not enable notifications")
                dropConnection()
                return@Op
            }
            reassembler.reset()
            setStatus(Status.READY, "Connected")
            listener.onReady()
        })
    }

    /**
     * Setup failed on a connected link: close it and retry with backoff. Does not wait for a
     * disconnect callback (which may never come, and reports a "clean" status that would
     * reset the backoff and retry every second).
     */
    private fun dropConnection() {
        teardown()
        scheduleReconnect()
    }

    private fun onDisconnected(status: Int) {
        Log.i(TAG, "Disconnected (status $status)")
        val wasBonded = gatt?.let { bondStateOf(it) == BluetoothDevice.BOND_BONDED } ?: false
        teardown()
        if (wasBonded && isEncryptionFailure(status)) {
            // Bonded here, but the link could not be encrypted: the watch lost our key.
            Log.w(TAG, "Encryption failed with a bonded watch (status $status)")
            stickyFailure = PAIRING_FAILED_TEXT
            reconnectDelayMs = reconnectDelayMs.coerceAtLeast(BOND_FAILURE_BACKOFF_MS)
            setStatus(Status.PAIRING_FAILED, PAIRING_FAILED_TEXT)
            scheduleReconnect()
            return
        }
        setStatus(Status.WAITING, stickyFailure ?: "Waiting for watch")
        // A clean link loss reconnects promptly; immediate errors (e.g. 133) back off.
        if (status == BluetoothGatt.GATT_SUCCESS || status == GATT_CONN_TIMEOUT ||
            status == GATT_CONN_TERMINATE_PEER
        ) {
            reconnectDelayMs = INITIAL_RECONNECT_MS
        }
        scheduleReconnect()
    }

    // ---------------------------------------------------------------- bonding

    private fun bondStateOf(g: BluetoothGatt): Int = try {
        g.device.bondState
    } catch (e: SecurityException) {
        BluetoothDevice.BOND_NONE
    }

    /**
     * The watch refused an operation for lack of authentication / encryption (or announced
     * that it needs a secure link). End the session but keep the connection, bond, and redo
     * discovery + CCC (the service then sends HELLO again) once bonded.
     */
    private fun requireBond(reason: String) {
        val g = gatt ?: return
        if (awaitingBond) return
        Log.i(TAG, "Bond needed: $reason")
        awaitingBond = true
        val wasReady = status == Status.READY
        status = Status.SETTING_UP
        failAllOps()
        reassembler.reset()
        if (wasReady) listener.onDisconnected()
        setStatus(Status.SETTING_UP, "Pairing with watch")
        when (bondStateOf(g)) {
            BluetoothDevice.BOND_BONDED -> {
                // Bonded but refused: encryption may still be starting. Retry the setup once;
                // a second refusal means the watch no longer has our keys.
                if (authRetries >= 1) {
                    bondFailed("the watch refused the stored bond")
                    return
                }
                authRetries++
                handler.postDelayed(resumeSetup, BONDED_RETRY_MS)
            }
            BluetoothDevice.BOND_BONDING -> handler.postDelayed(bondTimeout, BOND_TIMEOUT_MS)
            else -> {
                val started = try {
                    g.device.createBond()
                } catch (e: SecurityException) {
                    false
                }
                // If createBond() was refused Android may already be pairing by itself (it
                // starts on the insufficient-encryption error); the broadcast or the timeout
                // settles it either way.
                handler.postDelayed(bondTimeout, if (started) BOND_TIMEOUT_MS else BOND_START_GRACE_MS)
            }
        }
    }

    private fun onBondState(state: Int, previous: Int) {
        if (!awaitingBond) {
            // Bond started by ensureBonded(): nothing to redo, the service is retrying HELLO.
            if (!secureBondRequested) return
            when (state) {
                BluetoothDevice.BOND_BONDED -> {
                    secureBondRequested = false
                    setStatus(status, "Paired, starting session")
                }
                BluetoothDevice.BOND_NONE ->
                    if (previous == BluetoothDevice.BOND_BONDING) bondFailed("pairing was rejected or failed")
            }
            return
        }
        when (state) {
            BluetoothDevice.BOND_BONDED -> {
                handler.removeCallbacks(bondTimeout)
                handler.removeCallbacks(resumeSetup)
                // Give the stack a moment to finish starting encryption.
                handler.postDelayed(resumeSetup, BOND_SETTLE_MS)
            }
            BluetoothDevice.BOND_BONDING -> {
                handler.removeCallbacks(bondTimeout)
                handler.postDelayed(bondTimeout, BOND_TIMEOUT_MS)
            }
            BluetoothDevice.BOND_NONE ->
                if (previous == BluetoothDevice.BOND_BONDING) bondFailed("pairing was rejected or failed")
        }
    }

    private fun onBondTimeout() {
        if (!awaitingBond) return
        val g = gatt ?: return
        if (bondStateOf(g) == BluetoothDevice.BOND_BONDED) {
            resumeSetup.run()
        } else {
            bondFailed("pairing did not complete")
        }
    }

    private fun bondFailed(why: String) {
        Log.w(TAG, "Bonding failed: $why")
        stickyFailure = PAIRING_FAILED_TEXT
        teardown()
        // Do not hammer the watch (and the user) with pairing attempts.
        reconnectDelayMs = reconnectDelayMs.coerceAtLeast(BOND_FAILURE_BACKOFF_MS)
        setStatus(Status.PAIRING_FAILED, PAIRING_FAILED_TEXT)
        scheduleReconnect()
    }

    // ---------------------------------------------------------------- operation queue

    private fun enqueue(op: Op) {
        ops.addLast(op)
        runNext()
    }

    private fun runNext() {
        while (current == null && !awaitingBond) {
            val op = ops.pollFirst() ?: return
            val g = gatt
            if (g == null) {
                op.done(false)
                continue
            }
            current = op
            handler.postDelayed(opTimeout, op.timeoutMs)
            val started = try {
                op.start(g)
            } catch (e: SecurityException) {
                false
            }
            if (!started) {
                Log.w(TAG, "GATT ${op.kind} could not start")
                complete(op, false)
            }
        }
    }

    private fun complete(op: Op, ok: Boolean) {
        if (current !== op) return
        handler.removeCallbacks(opTimeout)
        current = null
        op.done(ok)
        runNext()
    }

    private fun completeCurrent(kind: Kind, ok: Boolean) {
        val op = current ?: return
        if (op.kind == kind) complete(op, ok)
    }

    private fun onOpTimeout() {
        val op = current ?: return
        val g = gatt
        if (g != null && bondStateOf(g) == BluetoothDevice.BOND_BONDING) {
            // Android started pairing by itself and is holding the operation back.
            requireBond("pairing in progress")
            return
        }
        when (op.kind) {
            Kind.WRITE, Kind.OTA -> {
                // The stack may still be busy with this write, and a late callback could be
                // taken for the next operation's: the link is dead, start over.
                Log.w(TAG, "GATT ${op.kind} timed out; restarting the link")
                dropConnection()
                setStatus(Status.WAITING, "Watch stopped responding, reconnecting")
            }
            else -> complete(op, false)
        }
    }

    private fun failAllOps() {
        handler.removeCallbacks(opTimeout)
        val pending = ArrayList<Op>()
        current?.let { pending += it }
        current = null
        pending += ops
        ops.clear()
        // Only write callbacks care about failure here; setup continuations must not run.
        pending.filter { it.kind == Kind.WRITE || it.kind == Kind.OTA }.forEach { it.done(false) }
    }

    private fun setStatus(status: Status, detail: String) {
        this.status = status
        listener.onLinkStatus(status, detail)
    }

    // ---------------------------------------------------------------- GATT callback

    private inner class Callback(private val gen: Int) : BluetoothGattCallback() {
        private fun post(block: () -> Unit) {
            handler.post { if (gen == generation) block() }
        }

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            post {
                when {
                    newState == BluetoothProfile.STATE_CONNECTED &&
                        status == BluetoothGatt.GATT_SUCCESS -> onConnected(g)
                    newState == BluetoothProfile.STATE_DISCONNECTED ||
                        status != BluetoothGatt.GATT_SUCCESS -> onDisconnected(status)
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            post {
                Log.i(TAG, "MTU $mtu (status $status)")
                if (status == BluetoothGatt.GATT_SUCCESS) this@GattLink.mtu = mtu
                completeCurrent(Kind.MTU, status == BluetoothGatt.GATT_SUCCESS)
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            post { completeCurrent(Kind.DISCOVER, status == BluetoothGatt.GATT_SUCCESS) }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            post {
                if (isAuthFailure(status)) {
                    requireBond("CCC write refused (status $status)")
                } else {
                    completeCurrent(Kind.DESCRIPTOR, status == BluetoothGatt.GATT_SUCCESS)
                }
            }
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            c: BluetoothGattCharacteristic,
            status: Int,
        ) {
            val kind = if (c.uuid == Protocol.OTA_DATA_UUID) Kind.OTA else Kind.WRITE
            post {
                if (isAuthFailure(status)) {
                    requireBond("write refused (status $status)")
                } else {
                    completeCurrent(kind, status == BluetoothGatt.GATT_SUCCESS)
                }
            }
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            c: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (c.uuid != Protocol.WATCH_TO_PHONE_UUID) return
            val copy = value.copyOf()
            post { deliver(copy) }
        }

        @Deprecated("Android 12 and earlier")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            // Android 13+ calls both overloads; only the legacy path is handled here.
            if (Build.VERSION.SDK_INT >= 33 || c.uuid != Protocol.WATCH_TO_PHONE_UUID) return
            @Suppress("DEPRECATION")
            val copy = c.value?.copyOf() ?: return
            post { deliver(copy) }
        }

        private fun deliver(notification: ByteArray) {
            reassembler.accept(notification)?.let(listener::onMessage)
        }
    }

    companion object {
        private const val TAG = "GattLink"
        private const val DEFAULT_MTU = 23
        private const val INITIAL_RECONNECT_MS = 1_000L
        private const val MAX_RECONNECT_MS = 60_000L
        private const val SETUP_TIMEOUT_MS = 5_000L
        private const val DISCOVERY_TIMEOUT_MS = 15_000L
        private const val WRITE_TIMEOUT_MS = 5_000L
        private const val BOND_TIMEOUT_MS = 40_000L
        private const val BOND_START_GRACE_MS = 5_000L
        private const val BOND_SETTLE_MS = 600L
        private const val BONDED_RETRY_MS = 2_000L
        private const val BOND_FAILURE_BACKOFF_MS = 30_000L
        private const val GATT_CONN_TIMEOUT = 8
        private const val GATT_CONN_TERMINATE_PEER = 19

        const val PAIRING_FAILED_TEXT =
            "Pairing failed — forget the watch in Android Bluetooth settings (and use " +
                "Forget phone on the watch), then pair again"

        /**
         * Disconnect reasons (HCI) that mean the link could not be encrypted: authentication
         * failure (0x05), PIN or key missing (0x06), MIC failure (0x3D), plus the stack's
         * GATT_AUTH_FAIL (137).
         */
        private fun isEncryptionFailure(status: Int): Boolean =
            status == 0x05 || status == 0x06 || status == 0x3D || status == 137

        /**
         * ATT / stack status codes that mean "this needs an encrypted, bonded link":
         * insufficient authentication (5), insufficient authorization (8), insufficient
         * encryption (15), and the stack's authentication failure (137).
         */
        private fun isAuthFailure(status: Int): Boolean =
            status == 5 || status == 8 || status == 15 || status == 137

        fun hasConnectPermission(context: Context): Boolean =
            Build.VERSION.SDK_INT < 31 ||
                context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) ==
                PackageManager.PERMISSION_GRANTED

        private fun Handler.hasCallbacksCompat(r: Runnable): Boolean =
            Build.VERSION.SDK_INT >= 29 && hasCallbacks(r)
    }
}
