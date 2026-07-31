package com.james.epaperwatch

import android.Manifest
import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelUuid
import java.nio.charset.StandardCharsets
import java.util.ArrayDeque

class WatchLinkService : Service() {
    private val handler = Handler(Looper.getMainLooper())
    private val outgoing = ArrayDeque<String>()
    private lateinit var sleepBridge: SleepApiBridge
    private lateinit var mediaBridge: MediaBridge
    private val bluetoothManager by lazy { getSystemService(BluetoothManager::class.java) }

    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var scanning = false
    private var writing = false
    private var desiredConnection = false
    private var connected = false
    private var statusText = "Idle"

    private val mediaPoll = object : Runnable {
        override fun run() {
            if (connected) mediaBridge.publish()
            handler.postDelayed(this, 5_000L)
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Ready to connect"))
        sleepBridge = SleepApiBridge(this, { connected }, ::enqueue)
        sleepBridge.register()
        mediaBridge = MediaBridge(this, ::enqueue)
        desiredConnection =
            getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(PREF_CONNECT, false)
        handler.post(mediaPoll)
        if (desiredConnection) handler.post { beginScan() }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CONNECT -> {
                desiredConnection = true
                getSharedPreferences(PREFS, MODE_PRIVATE)
                    .edit().putBoolean(PREF_CONNECT, true).apply()
                beginScan()
            }
            ACTION_DISCONNECT -> disconnect()
            ACTION_SEND -> intent.getStringExtra(EXTRA_PAYLOAD)?.let(::enqueue)
        }
        return START_STICKY
    }

    override fun onDestroy() {
        desiredConnection = false
        handler.removeCallbacksAndMessages(null)
        sleepBridge.unregister()
        closeGatt()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    @SuppressLint("MissingPermission")
    private fun beginScan() {
        if (connected || scanning || !hasBluetoothPermission()) return
        val adapter = bluetoothManager.adapter
        if (adapter == null || !adapter.isEnabled) {
            updateStatus("Bluetooth is off")
            return
        }
        updateStatus("Scanning for E-Paper Watch")
        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(WatchProtocol.SERVICE_UUID))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanning = true
        adapter.bluetoothLeScanner?.startScan(listOf(filter), settings, scanCallback)
        handler.postDelayed({
            if (scanning) {
                stopScan()
                updateStatus("Watch not found — is BLE advertising?")
                if (desiredConnection) handler.postDelayed(::beginScan, 5_000L)
            }
        }, 15_000L)
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        if (!scanning || !hasBluetoothPermission()) return
        bluetoothManager.adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        stopScan()
        closeGatt()
        updateStatus("Connecting to ${device.name ?: "watch"}")
        gatt = device.connectGatt(
            this,
            false,
            gattCallback,
            BluetoothDevice.TRANSPORT_LE,
            BluetoothDevice.PHY_LE_1M_MASK,
        )
    }

    @SuppressLint("MissingPermission")
    private fun disconnect() {
        desiredConnection = false
        getSharedPreferences(PREFS, MODE_PRIVATE)
            .edit().putBoolean(PREF_CONNECT, false).apply()
        stopScan()
        gatt?.disconnect()
        closeGatt()
        updateStatus("Disconnected")
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        gatt?.close()
        gatt = null
        rx = null
        tx = null
        connected = false
        writing = false
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            connect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            updateStatus("BLE scan failed ($errorCode)")
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    updateStatus("Discovering watch services")
                    gatt.requestMtu(185)
                    gatt.discoverServices()
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    closeGatt()
                    updateStatus("Connection lost")
                    if (desiredConnection) handler.postDelayed(::beginScan, 2_000L)
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            handler.post {
                val service: BluetoothGattService? =
                    gatt.getService(WatchProtocol.SERVICE_UUID)
                rx = service?.getCharacteristic(WatchProtocol.PHONE_TO_WATCH_UUID)
                tx = service?.getCharacteristic(WatchProtocol.WATCH_TO_PHONE_UUID)
                if (status != BluetoothGatt.GATT_SUCCESS || rx == null || tx == null) {
                    updateStatus("Watch service is missing")
                    gatt.disconnect()
                    return@post
                }
                gatt.setCharacteristicNotification(tx, true)
                val descriptor = tx?.getDescriptor(WatchProtocol.CCC_UUID)
                if (descriptor != null) {
                    if (Build.VERSION.SDK_INT >= 33) {
                        gatt.writeDescriptor(
                            descriptor,
                            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE,
                        )
                    } else {
                        @Suppress("DEPRECATION")
                        descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        @Suppress("DEPRECATION")
                        gatt.writeDescriptor(descriptor)
                    }
                }
                connected = true
                updateStatus("Connected")
                enqueue(WatchProtocol.time())
                mediaBridge.publish(force = true)
                drainQueue()
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            onWatchMessage(value.toString(StandardCharsets.UTF_8))
        }

        @Deprecated("Used on Android 12 and earlier")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            @Suppress("DEPRECATION")
            onWatchMessage(characteristic.value.toString(StandardCharsets.UTF_8))
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            handler.post {
                writing = false
                drainQueue()
            }
        }
    }

    private fun onWatchMessage(message: String) {
        handler.post {
            when {
                message.startsWith("MEDIA,") -> mediaBridge.handleWatchCommand(message)
                message.startsWith("SLEEP,DATA,") -> {
                    message.substringAfterLast(',').toIntOrNull()?.let(sleepBridge::onMovement)
                }
                message.startsWith("SLEEP,") -> sleepBridge.onWatchCommand(message)
                message == "NOTIFICATION,DISMISSED" -> Unit
            }
        }
    }

    private fun enqueue(payload: String) {
        val bounded = payload.take(160)
        if (outgoing.size >= 20) outgoing.removeFirst()
        outgoing.addLast(bounded)
        drainQueue()
    }

    @SuppressLint("MissingPermission")
    private fun drainQueue() {
        val characteristic = rx ?: return
        val activeGatt = gatt ?: return
        if (!connected || writing || outgoing.isEmpty() || !hasBluetoothPermission()) return
        val value = outgoing.removeFirst().toByteArray(StandardCharsets.UTF_8)
        writing = true
        val started = if (Build.VERSION.SDK_INT >= 33) {
            activeGatt.writeCharacteristic(
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
            activeGatt.writeCharacteristic(characteristic)
        }
        if (!started) {
            writing = false
            handler.postDelayed(::drainQueue, 250L)
        }
    }

    private fun hasBluetoothPermission(): Boolean {
        return Build.VERSION.SDK_INT < 31 ||
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) ==
            PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) ==
            PackageManager.PERMISSION_GRANTED
    }

    private fun updateStatus(value: String) {
        statusText = value
        val manager = getSystemService(NotificationManager::class.java)
        manager.notify(NOTIFICATION_ID, buildNotification(value))
        sendBroadcast(
            Intent(ACTION_STATUS)
                .setPackage(packageName)
                .putExtra(EXTRA_STATUS, value)
                .putExtra(EXTRA_CONNECTED, connected),
        )
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Watch connection",
            NotificationManager.IMPORTANCE_LOW,
        )
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun buildNotification(status: String): android.app.Notification {
        val launch = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return android.app.Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("E-Paper Watch")
            .setContentText(status)
            .setContentIntent(launch)
            .setOngoing(true)
            .build()
    }

    companion object {
        const val ACTION_STATUS = "com.james.epaperwatch.STATUS"
        const val EXTRA_STATUS = "status"
        const val EXTRA_CONNECTED = "connected"
        private const val ACTION_CONNECT = "com.james.epaperwatch.CONNECT"
        private const val ACTION_DISCONNECT = "com.james.epaperwatch.DISCONNECT"
        private const val ACTION_SEND = "com.james.epaperwatch.SEND"
        private const val EXTRA_PAYLOAD = "payload"
        private const val CHANNEL_ID = "watch-link"
        private const val NOTIFICATION_ID = 71
        private const val PREFS = "watch-link"
        private const val PREF_CONNECT = "connect"

        fun start(context: Context) {
            val intent = Intent(context, WatchLinkService::class.java)
            context.startForegroundService(intent)
        }

        fun connect(context: Context) = command(context, ACTION_CONNECT)
        fun disconnect(context: Context) = command(context, ACTION_DISCONNECT)

        fun send(context: Context, payload: String) {
            command(context, ACTION_SEND, payload)
        }

        private fun command(context: Context, action: String, payload: String? = null) {
            val intent = Intent(context, WatchLinkService::class.java)
                .setAction(action)
                .putExtra(EXTRA_PAYLOAD, payload)
            context.startForegroundService(intent)
        }
    }
}
