package com.cci.glnativerender

import android.os.Process
import android.view.Surface
import androidx.annotation.WorkerThread
import java.util.*
import java.util.concurrent.atomic.AtomicInteger

class GlTriangleRenderer {

    private val executor by lazy {
        SingleThreadHandlerExecutor(
            String.format(Locale.US, "GLRenderer-%03d", RENDERED_COUNT.incrementAndGet()),
            Process.THREAD_PRIORITY_DEFAULT
        )
    }

    fun drawTriangle(surface: Surface){
        executor.execute {
            nativeDrawTriangle(surface)

        }
    }


    companion object{
        private val RENDERED_COUNT = AtomicInteger(0)

        init {
            System.loadLibrary("opengl-renderer")
        }

        @WorkerThread
        @JvmStatic
        external fun nativeDrawTriangle( surface: Surface)
    }
}