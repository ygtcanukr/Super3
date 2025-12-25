package com.izzy2lost.super3

import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import android.widget.ImageView
import androidx.core.content.ContextCompat
import com.google.android.material.color.MaterialColors
import java.io.File
import java.util.concurrent.Executors

object FlyerBitmapLoader {
    private val executor = Executors.newFixedThreadPool(2) { r ->
        Thread(r, "Super3FlyerDecode").apply { isDaemon = true }
    }

    private val cache =
        object : LruCache<String, Bitmap>(16 * 1024) {
            override fun sizeOf(key: String, value: Bitmap): Int = value.byteCount / 1024
        }

    fun load(imageView: ImageView, file: File?, targetMaxPx: Int = 1200) {
        if (file == null || !file.exists()) {
            setPlaceholder(imageView)
            imageView.tag = null
            return
        }

        val key = "${file.absolutePath}:${file.lastModified()}:${targetMaxPx}"
        imageView.tag = key
        cache.get(key)?.let { cached ->
            clearTint(imageView)
            imageView.setImageBitmap(cached)
            return
        }

        setPlaceholder(imageView)
        executor.execute {
            val bmp = decodeScaled(file, targetMaxPx) ?: return@execute
            cache.put(key, bmp)
            imageView.post {
                if (imageView.tag == key) {
                    clearTint(imageView)
                    imageView.setImageBitmap(bmp)
                }
            }
        }
    }

    private fun decodeScaled(file: File, targetMaxPx: Int): Bitmap? {
        val bounds =
            BitmapFactory.Options().apply {
                inJustDecodeBounds = true
            }
        BitmapFactory.decodeFile(file.absolutePath, bounds)
        val srcW = bounds.outWidth
        val srcH = bounds.outHeight
        if (srcW <= 0 || srcH <= 0) return null

        val srcMax = maxOf(srcW, srcH)
        var sample = 1
        while (srcMax / sample > targetMaxPx) sample *= 2

        val opts =
            BitmapFactory.Options().apply {
                inSampleSize = sample
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }

        return BitmapFactory.decodeFile(file.absolutePath, opts)
    }

    private fun setPlaceholder(imageView: ImageView) {
        val color = MaterialColors.getColor(imageView, com.google.android.material.R.attr.colorOnSurfaceVariant)
        imageView.imageTintList = ColorStateList.valueOf(color)
        imageView.setImageDrawable(ContextCompat.getDrawable(imageView.context, R.drawable.image_24px))
    }

    private fun clearTint(imageView: ImageView) {
        imageView.imageTintList = null
    }
}

