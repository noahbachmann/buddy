package buddy.screener.dump

import android.accessibilityservice.AccessibilityServiceInfo
import android.app.Activity
import android.app.Instrumentation
import android.app.UiAutomation
import android.graphics.Point
import android.graphics.Rect
import android.os.Bundle
import android.os.SystemClock
import android.util.Base64
import android.util.Xml
import android.view.WindowManager
import android.view.accessibility.AccessibilityNodeInfo
import java.io.ByteArrayOutputStream
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
 * Same format as `uiautomator dump` (Android 9's AccessibilityNodeInfoDumper): same attributes, only children
 * visible to the user, bounds clipped to the display, not-important views included.
 */
class Dump : Instrumentation() {

    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }

    override fun onStart() {
        val out = Bundle()
        val code = try {
            val ua = uiAutomation
            ua.serviceInfo = ua.serviceInfo.apply { flags = flags or AccessibilityServiceInfo.FLAG_INCLUDE_NOT_IMPORTANT_VIEWS }
            // settles a list after a swipe; a playing video never settles: read anyway
            try {
                ua.waitForIdle(IDLE_MS, IDLE_TIMEOUT_MS)
            } catch (e: TimeoutException) {
                out.putString("idle", "false")
            }
            val root = root(ua) ?: throw IllegalStateException("no_window")
            out.putString("xml", Base64.encodeToString(gzip(dump(root)), Base64.NO_WRAP))
            Activity.RESULT_OK
        } catch (e: Exception) {
            out.putString("error", "${e.javaClass.simpleName}: ${e.message}")
            Activity.RESULT_CANCELED
        }
        finish(code, out)
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
    }
}
