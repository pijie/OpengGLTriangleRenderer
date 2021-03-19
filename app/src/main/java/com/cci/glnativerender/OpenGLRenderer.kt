package com.cci.glnativerender

import android.os.Process
import android.view.Surface
import androidx.annotation.WorkerThread
import java.util.*
import java.util.concurrent.atomic.AtomicInteger

class OpenGLRenderer {
    private var nativeContext: Long = 0L

    private val executor by lazy {
        SingleThreadHandlerExecutor(
            String.format(Locale.US, "GLRenderer-%03d", RENDERED_COUNT.incrementAndGet()),
            Process.THREAD_PRIORITY_DEFAULT
        )
    }

    init {
        executor.execute { nativeContext = initContext() }
    }


    fun attachOutputSurface(surface: Surface) {
        executor.execute {
            setWindowSurface(nativeContext, surface)
        }
    }

    fun invalidateSurface() {
        executor.execute {
            renderTexture(nativeContext)
        }
    }

    fun shutdown() {
        executor.execute {
            closeContext(nativeContext)
            executor.shutdown()
        }
    }

    companion object {
        private val RENDERED_COUNT = AtomicInteger(0)

        init {
            System.loadLibrary("opengl-renderer")
        }

        @WorkerThread
        @JvmStatic
        external fun initContext(): Long

        @WorkerThread
        @JvmStatic
        external fun setWindowSurface(nativeContext: Long, surface: Surface): Boolean

        @WorkerThread
        @JvmStatic
        external fun getTextName(nativeContext: Long): Int

        @WorkerThread
        @JvmStatic
        external fun renderTexture(nativeContext: Long): Boolean

        @WorkerThread
        @JvmStatic
        external fun closeContext(nativeContext: Long)
    }
}