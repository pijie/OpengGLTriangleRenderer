#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cassert>
#include <GLES2/gl2.h>
#include <vector>
#include <android/log.h>

#define JAVA_CLASS "com/cci/glnativerender/GlTriangleRenderer"

const char *LOG_TAG = "OpenGLTriangleRenderer";
jlong drawTriangle(JNIEnv *env,jclass clazz,jobject jsurface);
const static JNINativeMethod Methods[] = {
        {"nativeDrawTriangle", "(Landroid/view/Surface;)V", reinterpret_cast<void *>(drawTriangle)}
};

extern "C"{
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
    JNIEnv *env;
    if (jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    env->RegisterNatives(env->FindClass(JAVA_CLASS), Methods,
                         sizeof(Methods) / sizeof(Methods[0]));
    return JNI_VERSION_1_6;
}
}

namespace {
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
}


jlong drawTriangle(JNIEnv *env,jclass clazz,jobject jsurface) {
    // 获取当前平台的窗口
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, jsurface);
    // 创建显示设备
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    // 创建eglsurface
    EGLConfig config;
    EGLint configNum;
    EGLint configAttrs[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE
    };
    if (EGL_TRUE != eglChooseConfig(eglDisplay, configAttrs, &config, 1, &configNum)) {
        ThrowException(env, "java/lang/IllegalArgumentException",
                       "EGL Error: eglChooseConfig failed. ");
        return 0;
    }
    assert(configNum > 0);
    // 创建surface
    EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, config, nativeWindow, nullptr);
    // 创建上下文
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 2,EGL_NONE};
    EGLContext eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, ctxAttr);
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    glClearColor(1.0f,1.0f,1.0f,1.0f);
    // 颜色缓存区，深度缓存区，模板缓存区
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glViewport(0,0,ANativeWindow_getWidth(nativeWindow)/2,ANativeWindow_getHeight(nativeWindow)/2);
    // 使能裁剪测试
    glEnable(GL_SCISSOR_TEST);
    // 裁剪框
    glScissor(0,0,ANativeWindow_getWidth(nativeWindow)/4,ANativeWindow_getHeight(nativeWindow)/4);
    constexpr GLfloat vertices[] = {
            0.5f, 0.5f, 0.0f,    // top
            -0.5f, -0.5f, 0.0f,  // bottom left
            0.5f, -0.5f, 0.0f    // bottom right
    };
    constexpr GLfloat colors[] = {1.0f, 0.0f, 0.0f, 1.0f};
    GLint program = CreateGlProgram();
    glUseProgram(program);
    GLint vPositionHandle = glGetAttribLocation(program, "vPosition");
    GLint vColorHandle = glGetUniformLocation(program, "vColor");
    glEnableVertexAttribArray(vPositionHandle);
    glVertexAttribPointer(vPositionHandle,3,GL_FLOAT,GL_FALSE,12,vertices);
    glUniform4fv(vColorHandle,1,colors);
//    glDrawArrays(GL_TRIANGLES,0,3);
    glDrawArrays(GL_LINE_LOOP, 0,3);
    eglSwapBuffers(eglDisplay,eglSurface);
    // glCullFace(GL_FRONT_FACE); // 正面剔除
    // glCullFace(GL_BACK);  // 背面剔除
    // glCullFace(GL_FRONT_AND_BACK);
    // 使能表面剔除
    // glEnable(GL_CULL_FACE);
    // 失能表面剔除
    // glDisable(GL_CULL_FACE)

    return 0;
}
