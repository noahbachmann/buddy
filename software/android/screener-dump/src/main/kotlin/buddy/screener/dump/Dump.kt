package buddy.screener.dump

import android.accessibilityservice.AccessibilityServiceInfo
import android.app.Activity
import android.app.Instrumentation
import android.app.UiAutomation
import android.graphics.Point
import android.graphics.Rect
import android.net.LocalServerSocket
import android.net.LocalSocket
import android.os.Bundle
import android.os.Process
import android.os.SystemClock
import android.util.Base64
import android.util.Xml
import android.view.WindowManager
import android.view.accessibility.AccessibilityNodeInfo
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.util.concurrent.TimeoutException
import java.util.zip.GZIPOutputStream

/**
 * The screener's screen reader: the active window's accessibility tree as `uiautomator dump` writes it, but without
 * waiting for the screen to go idle. An autoplaying video never lets it, so `uiautomator dump` fails ("could not get
 * idle state") whenever one is on screen.
 *
 * Run: `adb shell am instrument -w buddy.screener.dump/.Dump` → `INSTRUMENTATION_RESULT: xml=<base64 gzip UTF-8>`
 * (+ `idle=false` when the screen never settled within [IDLE_TIMEOUT_MS]), or `error=<reason>`.
 *
 * Serve mode: `am instrument -w -e serve 1 buddy.screener.dump/.Dump` keeps one UiAutomation connection and answers
 * reads on the abstract local socket [SOCKET] (`adb forward tcp:0 localabstract:buddy.screener.dump`), one client at a
 * time: request line `dump <idle_ms>` → `ok <len> <idle 0|1>` + `len` bytes of gzip XML, or `err <reason>`; `quit`
 * ends it. Saves the instrumentation's start (~0.8 s of a one-shot read's ~1.5 s). Ends by itself after [SERVE_IDLE_MS]
 * without a request: a crashed caller leaves nothing running.
 *
 * Same format as `uiautomator dump` (Android 9's AccessibilityNodeInfoDumper): same attributes, only children
 * visible to the user, bounds clipped to the display, not-important views included.
 */
class Dump : Instrumentation() {

    private var serve = false
    @Volatile private var lastRequest = 0L

    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        serve = arguments?.getString("serve") == "1"
        start()
    }

    override fun onStart() {
        if (serve) {
            serve()
            return
        }
        val out = Bundle()
        val code = try {
            val ua = connect()
            val (gz, idle) = read(ua, IDLE_MS)
            if (!idle) out.putString("idle", "false")
            out.putString("xml", Base64.encodeToString(gz, Base64.NO_WRAP))
            Activity.RESULT_OK
        } catch (e: Exception) {
            out.putString("error", "${e.javaClass.simpleName}: ${e.message}")
            Activity.RESULT_CANCELED
        }
        finish(code, out)
    }

    private fun connect(): UiAutomation {
        val ua = uiAutomation
        ua.serviceInfo = ua.serviceInfo.apply { flags = flags or AccessibilityServiceInfo.FLAG_INCLUDE_NOT_IMPORTANT_VIEWS }
        return ua
    }

    /** gzip XML of the active window + whether the screen settled. A playing video never settles: read anyway. */
    private fun read(ua: UiAutomation, idleMs: Long): Pair<ByteArray, Boolean> {
        var idle = true
        try {
            ua.waitForIdle(idleMs, IDLE_TIMEOUT_MS)
        } catch (e: TimeoutException) {
            idle = false
        }
        val root = root(ua) ?: throw IllegalStateException("no_window")
        return gzip(dump(root)) to idle
    }

    private fun serve() {
        val out = Bundle()
        lastRequest = SystemClock.uptimeMillis()
        val server = LocalServerSocket(SOCKET)
        // accept() can't time out: a watchdog ends the process after SERVE_IDLE_MS without a request
        Thread {
            while (true) {
                SystemClock.sleep(5_000)
                if (SystemClock.uptimeMillis() - lastRequest > SERVE_IDLE_MS) Process.killProcess(Process.myPid())
            }
        }.apply { isDaemon = true }.start()
        val code = try {
            val ua = connect()
            var quit = false
            while (!quit) {
                val client = server.accept()
                quit = try {
                    client.soTimeout = SERVE_IDLE_MS.toInt()
                    talk(ua, client)
                } catch (e: IOException) {
                    false   // caller gone: wait for the next
                } finally {
                    client.close()
                }
            }
            Activity.RESULT_OK
        } catch (e: Exception) {
            out.putString("error", "${e.javaClass.simpleName}: ${e.message}")
            Activity.RESULT_CANCELED
        } finally {
            server.close()
        }
        finish(code, out)
    }

    /** One client's requests until it hangs up (false) or says `quit` (true). */
    private fun talk(ua: UiAutomation, client: LocalSocket): Boolean {
        val input = client.inputStream
        val output = client.outputStream
        while (true) {
            val line = readLine(input) ?: return false
            lastRequest = SystemClock.uptimeMillis()
            val parts = line.trim().split(" ")
            when (parts[0]) {
                "quit" -> return true
                "dump" -> {
                    val reply = try {
                        val (gz, idle) = read(ua, parts.getOrNull(1)?.toLongOrNull() ?: IDLE_MS)
                        "ok ${gz.size} ${if (idle) 1 else 0}\n".toByteArray() + gz
                    } catch (e: Exception) {
                        "err ${"${e.javaClass.simpleName}: ${e.message}".replace('\n', ' ')}\n".toByteArray()
                    }
                    output.write(reply)
                    output.flush()
                }
                else -> {
                    output.write("err unknown_request\n".toByteArray())
                    output.flush()
                }
            }
        }
    }

    /** ASCII line without the newline; null at end of stream. */
    private fun readLine(input: InputStream): String? {
        val b = StringBuilder()
        while (true) {
            val c = input.read()
            if (c < 0) return if (b.isEmpty()) null else b.toString()
            if (c == '\n'.code) return b.toString()
            if (b.length > 200) throw IOException("request too long")
            b.append(c.toChar())
        }
    }

    /** Null right after connecting now and then: retried briefly. */
    private fun root(ua: UiAutomation): AccessibilityNodeInfo? {
        val end = SystemClock.uptimeMillis() + ROOT_TIMEOUT_MS
        while (true) {
            ua.rootInActiveWindow?.let { return it }
            if (SystemClock.uptimeMillis() >= end) return null
            SystemClock.sleep(50)
        }
    }

    private fun dump(root: AccessibilityNodeInfo): String {
        @Suppress("DEPRECATION") // getSize, as uiautomator: bounds clip to the app area, not the real display
        val display = (context.getSystemService(WindowManager::class.java)).defaultDisplay
        val size = Point().also { @Suppress("DEPRECATION") display.getSize(it) }
        val buf = ByteArrayOutputStream()
        val xml = Xml.newSerializer()
        xml.setOutput(buf, "UTF-8")
        xml.startDocument("UTF-8", true)
        xml.startTag("", "hierarchy")
        xml.attribute("", "rotation", display.rotation.toString())
        node(xml, root, 0, size.x, size.y)
        xml.endTag("", "hierarchy")
        xml.endDocument()
        return buf.toString("UTF-8")
    }

    private fun node(xml: org.xmlpull.v1.XmlSerializer, n: AccessibilityNodeInfo, index: Int, w: Int, h: Int) {
        xml.startTag("", "node")
        xml.attribute("", "index", index.toString())
        xml.attribute("", "text", safe(n.text))
        xml.attribute("", "resource-id", safe(n.viewIdResourceName))
        xml.attribute("", "class", safe(n.className))
        xml.attribute("", "package", safe(n.packageName))
        xml.attribute("", "content-desc", safe(n.contentDescription))
        xml.attribute("", "checkable", n.isCheckable.toString())
        xml.attribute("", "checked", n.isChecked.toString())
        xml.attribute("", "clickable", n.isClickable.toString())
        xml.attribute("", "enabled", n.isEnabled.toString())
        xml.attribute("", "focusable", n.isFocusable.toString())
        xml.attribute("", "focused", n.isFocused.toString())
        xml.attribute("", "scrollable", n.isScrollable.toString())
        xml.attribute("", "long-clickable", n.isLongClickable.toString())
        xml.attribute("", "password", n.isPassword.toString())
        xml.attribute("", "selected", n.isSelected.toString())
        xml.attribute("", "bounds", visibleBounds(n, w, h).toShortString())
        for (i in 0 until n.childCount) {
            val c = n.getChild(i) ?: continue
            if (c.isVisibleToUser) node(xml, c, i, w, h)
            c.recycle()
        }
        xml.endTag("", "node")
    }

    private fun visibleBounds(n: AccessibilityNodeInfo, w: Int, h: Int): Rect {
        val r = Rect()
        n.getBoundsInScreen(r)
        return if (r.intersect(0, 0, w, h)) r else Rect()
    }

    // as uiautomator: characters XML can't hold become "."
    private fun safe(s: CharSequence?): String {
        if (s == null) return ""
        val b = StringBuilder(s.length)
        for (ch in s) {
            val ok = ch == '\t' || ch == '\n' || ch == '\r' || ch in ' '..'퟿' || ch in '\uD800'..'�'
            b.append(if (ok) ch else '.')
        }
        return b.toString()
    }

    private fun gzip(s: String): ByteArray {
        val buf = ByteArrayOutputStream()
        GZIPOutputStream(buf).use { it.write(s.toByteArray(Charsets.UTF_8)) }
        return buf.toByteArray()
    }

    companion object {
        const val IDLE_MS = 500L           // quiet this long = settled (uiautomator: 1000)
        const val IDLE_TIMEOUT_MS = 2500L  // uiautomator: 10 s, then gives up; we read anyway
        const val ROOT_TIMEOUT_MS = 2000L
        const val SOCKET = "buddy.screener.dump"
        const val SERVE_IDLE_MS = 120_000L  // serve mode ends after this long without a request
    }
}
