#include <jni.h>
#include <string>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <vector>
#include <sstream>
#include <iomanip>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#define JAVA_CLASS "com/cci/glnativerender/OpenGLRenderer"

const char *LOG_TAG = "OpenGLRenderer";

jlong initContext(JNIEnv *env, jclass clazz);

jboolean setWindowSurface(JNIEnv *env, jclass clazz, jlong nativeContext, jobject surface);

jint getTextName(JNIEnv *env, jclass clazz, jlong nativeContext);

jboolean renderTexture(JNIEnv *env, jclass clazz, jlong nativeContext);

void closeContext(JNIEnv *env, jclass clazz, jlong nativeContext);

const static JNINativeMethod nativeMethods[] = {
        {"initContext",      "()J",                        reinterpret_cast<void *>(initContext)},
        {"setWindowSurface", "(JLandroid/view/Surface;)Z", reinterpret_cast<void *>(setWindowSurface)},
        {"getTextName",      "(J)I",                       reinterpret_cast<void *>(getTextName)},
        {"renderTexture",    "(J)Z",                       reinterpret_cast<void *>(renderTexture)},
        {"closeContext",     "(J)V",                       reinterpret_cast<void *>(closeContext)}
};


namespace {
    std::string GLErrorString(GLenum error) {
        switch (error) {
            case GL_NO_ERROR:
                return "GL_NO_ERROR";
            case GL_INVALID_ENUM:
                return "GL_INVALID_ENUM";
            case GL_INVALID_VALUE:
                return "GL_INVALID_VALUE";
            case GL_INVALID_OPERATION:
                return "GL_INVALID_OPERATION";
            case GL_STACK_OVERFLOW_KHR:
                return "GL_STACK_OVERFLOW";
            case GL_STACK_UNDERFLOW_KHR:
                return "GL_STACK_UNDERFLOW";
            case GL_OUT_OF_MEMORY:
                return "GL_OUT_OF_MEMORY";
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                return "GL_INVALID_FRAMEBUFFER_OPERATION";
            default: {
                std::ostringstream oss;
                oss << "<Unknown GL Error 0x" << std::setfill('0') <<
                    std::setw(4) << std::right << std::hex << error << ">";
                return oss.str();
            }
        }
    }

    std::string EGLErrorString(EGLenum error) {

        switch (error) {
            case EGL_SUCCESS:
                return "EGL_SUCCESS";
            case EGL_NOT_INITIALIZED:
                return "EGL_NOT_INITIALIZED";
            case EGL_BAD_ACCESS:
                return "EGL_BAD_ACCESS";
            case EGL_BAD_ALLOC:
                return "EGL_BAD_ALLOC";
            case EGL_BAD_ATTRIBUTE:
                return "EGL_BAD_ATTRIBUTE";
            case EGL_BAD_CONTEXT:
                return "EGL_BAD_CONTEXT";
            case EGL_BAD_CONFIG:
                return "EGL_BAD_CONFIG";
            case EGL_BAD_CURRENT_SURFACE:
                return "EGL_BAD_CURRENT_SURFACE";
            case EGL_BAD_DISPLAY:
                return "EGL_BAD_DISPLAY";
            case EGL_BAD_SURFACE:
                return "EGL_BAD_SURFACE";
            case EGL_BAD_MATCH:
                return "EGL_BAD_MATCH";
            case EGL_BAD_PARAMETER:
                return "EGL_BAD_PARAMETER";
            case EGL_BAD_NATIVE_PIXMAP:
                return "EGL_BAD_NATIVE_PIXMAP";
            case EGL_BAD_NATIVE_WINDOW:
                return "EGL_BAD_NATIVE_WINDOW";
            case EGL_CONTEXT_LOST:
                return "EGL_CONTEXT_LOST";
            default: {
                std::ostringstream oss;
                oss << "<Unknown EGL Error 0x" << std::setfill('0') <<
                    std::setw(4) << std::right << std::hex << error << ">";
                return oss.str();
            }
        }
    }

    constexpr char VERTEX_SHADER_SRC[] = R"SRC(
        attribute vec4 vPosition;
        void main(){
            gl_Position = vPosition;
        }
    )SRC";

    constexpr char FRAGMENT_SHARED_SRC[] = R"SRC(
        precision mediump float;
        uniform vec4 vColor;
        void main(){
            gl_FragColor = vColor;
        }
    )SRC";

    void ThrowException(JNIEnv *env, const char *exceptionName, const char *msg) {
        jclass exClass = env->FindClass(exceptionName);
        assert(exClass != nullptr);
        jint throwSuccess = env->ThrowNew(exClass, msg);
        assert(throwSuccess == JNI_OK);
    }

    struct NativeContext {
        EGLContext display;
        EGLConfig config;
        EGLContext context;
        std::pair<ANativeWindow *, EGLSurface> windowSurface;
        EGLSurface pbufferSurface;
        GLuint program;
        GLint vPostionHandle;
        GLint vColorHandle;
        GLuint textureId;

        NativeContext(EGLDisplay display, EGLConfig config, EGLContext context,
                      ANativeWindow *window, EGLSurface surface, EGLSurface pbufferSurface) :
                display(display),
                config(config),
                context(context),
                windowSurface(std::make_pair(window, surface)),
                pbufferSurface(pbufferSurface),
                program(0),
                vPostionHandle(-1),
                vColorHandle(-1),
                textureId(0) {}
    };

    const char *ShaderTypeString(GLenum shaderType) {
        switch (shaderType) {
            case GL_VERTEX_SHADER:
                return "GL_VERTEX_SHADER";
            case GL_FRAGMENT_SHADER:
                return "GL_FRAGMENT_SHADER";
            default:
                return "<Unknown shader type>";
        }
    }


    GLuint CompileShader(GLenum shaderType, const char *shaderSrc) {
        GLuint shader = glCreateShader(shaderType);
        assert(shader);
        glShaderSource(shader, 1, &shaderSrc, nullptr);
        glCompileShader(shader);
        GLint compileStatus = 0;
        // 函数可以用来检测着色器编译是否成功
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
        // 如果编译不成功
        if (!compileStatus) {
            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> logBuffer(logLength);
            if (logLength > 0) {
                glGetShaderInfoLog(shader, logLength, nullptr, &logBuffer[0]);
            }
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unable to compile %s shader:\n $s.",
                                ShaderTypeString(shaderType),
                                logLength > 0 ? &logBuffer[0] : "(unknown error)");
            shader = 0;
        }
        assert(shader);
        return shader;
    }


    GLuint CreateGlProgram() {
        GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
        assert(vertexShader);
        GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHARED_SRC);
        assert(fragmentShader);
        GLuint program = glCreateProgram();
        alloca(program);
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        GLint linkStatus = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (!linkStatus) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> logBuffer(logLength);
            if (logLength > 0) {
                glGetProgramInfoLog(program, logLength, nullptr, &logBuffer[0]);
            }

            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unable to link program:\n %s.",
                                logLength > 0 ? &logBuffer[0] : "(unknown error)");
            glDeleteProgram(program);
        }

        return program;
    }

    void DestroySurface(NativeContext *nativeContext) {
        if (nativeContext->windowSurface.first) {
            eglMakeCurrent(nativeContext->display, nativeContext->pbufferSurface,
                           nativeContext->pbufferSurface, nativeContext->context);
            eglDestroySurface(nativeContext->display, nativeContext->windowSurface.second);
            nativeContext->windowSurface.second = nullptr;
            ANativeWindow_release(nativeContext->windowSurface.first);
            nativeContext->windowSurface.first = nullptr;
        }
    }
}


extern "C" {
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
    JNIEnv *env;
    if (jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    env->RegisterNatives(env->FindClass(JAVA_CLASS), nativeMethods,
                         sizeof(nativeMethods) / sizeof(nativeMethods[0]));
    return JNI_VERSION_1_6;
}

}

jlong initContext(JNIEnv *env, jclass clazz) {
    // 获取系统默认显示设备句柄
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(eglDisplay != EGL_NO_DISPLAY);
    EGLint majorVer;
    EGLint minorVer;
    // 初始化
    EGLBoolean initSuccess = eglInitialize(eglDisplay, &majorVer, &minorVer);
    if (initSuccess != EGL_TRUE) {
        ThrowException(env, "java/lang/RuntimeException", "EGL Error: eglInitialize fail.");
    }
    const char *eglVendorString = eglQueryString(eglDisplay, EGL_VENDOR);
    const char *eglVersionString = eglQueryString(eglDisplay, EGL_VERSION);
    // 打印 EGL 信息
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "EGL Initialized [Vendor: %s,Version: %s]",
                        eglVendorString == nullptr ? "Unknown" : eglVendorString,
                        eglVersionString == nullptr ? "Unknown" : eglVersionString);
    // 指定 FrameBuffer 参数
    // 其中attr_list是以EGL_NONE结束的参数数组，通常以id,value依次存放，对于个别标识性的属性可以只有 id，没有value
    int configAttrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
            EGL_RECORDABLE_ANDROID, EGL_TRUE,
            EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    EGLint configSize = 1;
    EGLBoolean chooseConfigSuccess = eglChooseConfig(eglDisplay, static_cast<EGLint *>(configAttrs),
                                                     &config, configSize, &numConfigs);
    if (chooseConfigSuccess != EGL_TRUE) {
        ThrowException(env, "java/lang/IllegalArgumentException",
                       "EGL Error: eglChooseConfig failed. ");
        return 0;
    }
    assert(numConfigs > 0);
    int contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT,
                                             static_cast<EGLint *>(contextAttrs));
    assert(eglContext != EGL_NO_CONTEXT);
    int pbufferAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface eglPbuffer = eglCreatePbufferSurface(eglDisplay, config, pbufferAttrs);
    assert(eglPbuffer != EGL_NO_DISPLAY);
//     EGL变量之间的绑定
    eglMakeCurrent(eglDisplay, eglPbuffer, eglPbuffer, eglContext);

    // 打印opengl信息
    const GLubyte *glVendorString = glGetString(GL_VENDOR);
    const GLubyte *glVersionString = glGetString(GL_VERSION);
    const GLubyte *glslVersionString = glGetString(GL_SHADING_LANGUAGE_VERSION);
    const GLubyte *glRendererString = glGetString(GL_RENDERER);
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG,
                        "OpenGL Initialized [Vendor: %s, Version: %s, GLSL Version: %s, Renderer: %s]",
                        glVendorString == nullptr ? "Unknown" : (const char *) glVendorString,
                        glVersionString == nullptr ? "Unknown" : (const char *) glVersionString,
                        glslVersionString == nullptr ? "Unknown" : (const char *) glslVersionString,
                        glRendererString == nullptr ? "Unknown" : (const char *) glRendererString
    );


    auto *nativeContext = new NativeContext(eglDisplay, config, eglContext, nullptr, nullptr,
                                            eglPbuffer);
    nativeContext->program = CreateGlProgram();
    nativeContext->vPostionHandle = glGetAttribLocation(nativeContext->program, "vPosition");
    nativeContext->vColorHandle = glGetUniformLocation(nativeContext->program, "vColor");

    glGenTextures(1, &(nativeContext->textureId));
    return reinterpret_cast<jlong>(nativeContext);
}

jboolean setWindowSurface(JNIEnv *env, jclass clazz, jlong context, jobject jsurface) {
    auto *nativeContext = reinterpret_cast<NativeContext *>(context);
    DestroySurface(nativeContext);
    if (!jsurface) {
        return JNI_FALSE;
    }
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, jsurface);
    if (nativeWindow == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Failed to set window surface,: Unable to acquire native window.");
        return  JNI_FALSE;
    }

    EGLSurface surface = eglCreateWindowSurface(nativeContext->display,nativeContext->config,nativeWindow,nullptr);
    EGLenum eglError = eglGetError();
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        " %s",
                        EGLErrorString(eglError).c_str());
    assert(surface != EGL_NO_DISPLAY);
    nativeContext->windowSurface = std::make_pair(nativeWindow,surface);
    eglMakeCurrent(nativeContext->display,surface,surface,nativeContext->context);
    glViewport(0,0,ANativeWindow_getWidth(nativeWindow),ANativeWindow_getHeight(nativeWindow));
    glScissor(0,0,ANativeWindow_getWidth(nativeWindow),ANativeWindow_getHeight(nativeWindow));

    return JNI_TRUE;
}

jint getTextName(JNIEnv *env, jclass clazz, jlong context) {
    auto *nativeContext = reinterpret_cast<NativeContext*>(context);
    return nativeContext ->textureId;
}

jboolean renderTexture(JNIEnv *env, jclass clazz, jlong context) {
    auto *nativeContext = reinterpret_cast<NativeContext *>(context);
    constexpr GLfloat vertices[] = {
            0.5f,0.5f, // top
            -0.5f,-0.5f,// bottom left
            0.5f,0.5f // bottom right
    };

    constexpr GLfloat  colors[] = {
            1.0f,0.0f,0.0f,1.0f
    };

    glEnableVertexAttribArray(nativeContext->vPostionHandle);
    glVertexAttribPointer(nativeContext->vPostionHandle,3,GL_FLOAT,false,3*4,vertices);
    glUniform4fv(nativeContext->vColorHandle,1,colors);
    glUseProgram(nativeContext->program);

    glDrawArrays(GL_TRIANGLES,0,3);
//    eglSwapBuffers(nativeContext->display,nativeContext->windowSurface.second)
//    EGLBoolean swapped = eglSwapBuffers(nativeContext->display,
//                                        nativeContext->windowSurface.second);
//    if (!swapped) {
//        EGLenum eglError = eglGetError();
//        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
//                            "Failed to swap buffers with EGL error: %s",
//                            EGLErrorString(eglError).c_str());
//        return JNI_FALSE;
//    }

    return true;
}

void closeContext(JNIEnv *env, jclass clazz, jlong context) {
    auto *nativeContext = reinterpret_cast<NativeContext *>(context);
    if (nativeContext->program){
        glDeleteProgram(nativeContext->program);
        nativeContext->program = 0;
    }
    DestroySurface(nativeContext);
    eglDestroySurface(nativeContext->display,nativeContext->pbufferSurface);
    eglMakeCurrent(nativeContext->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(nativeContext->display,nativeContext->context);
    eglTerminate(nativeContext->display);
    delete nativeContext;
}

