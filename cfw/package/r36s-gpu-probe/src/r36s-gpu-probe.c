// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static int contains_ci(const char *haystack, const char *needle)
{
    size_t hlen, nlen;

    if (!haystack || !needle)
        return 0;

    hlen = strlen(haystack);
    nlen = strlen(needle);

    for (size_t i = 0; i + nlen <= hlen; ++i) {
        size_t j;
        for (j = 0; j < nlen; ++j) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (j == nlen)
            return 1;
    }

    return 0;
}

static int open_drm_node(const char **chosen)
{
    static const char *nodes[] = {
        "/dev/dri/renderD128",
        "/dev/dri/card0",
        NULL
    };

    for (size_t i = 0; nodes[i]; ++i) {
        int fd = open(nodes[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            *chosen = nodes[i];
            return fd;
        }
    }

    return -1;
}

static void egl_fail(const char *where)
{
    fprintf(stderr, "%s failed: EGL error 0x%04x\n", where, eglGetError());
}

int main(void)
{
    const char *node = NULL;
    int fd = open_drm_node(&node);
    if (fd < 0) {
        fprintf(stderr, "FAIL: cannot open DRM node: %s\n", strerror(errno));
        return 10;
    }

    drmVersionPtr ver = drmGetVersion(fd);
    if (ver) {
        printf("drm.node=%s\n", node);
        printf("drm.driver=%.*s\n", ver->name_len, ver->name);
        printf("drm.version=%d.%d.%d\n",
               ver->version_major, ver->version_minor, ver->version_patchlevel);
        drmFreeVersion(ver);
    }

    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) {
        fprintf(stderr, "FAIL: gbm_create_device\n");
        close(fd);
        return 11;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");

    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (get_platform_display)
        dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
#if defined(EGL_VERSION_1_5)
    if (dpy == EGL_NO_DISPLAY)
        dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
#endif
    if (dpy == EGL_NO_DISPLAY) {
        egl_fail("eglGetPlatformDisplay");
        gbm_device_destroy(gbm);
        close(fd);
        return 12;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        egl_fail("eglInitialize");
        gbm_device_destroy(gbm);
        close(fd);
        return 13;
    }

    printf("egl.version=%d.%d\n", major, minor);
    printf("egl.vendor=%s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("egl.client_apis=%s\n", eglQueryString(dpy, EGL_CLIENT_APIS));

    const char *egl_ext = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("egl.surfaceless=%s\n",
           egl_ext && strstr(egl_ext, "EGL_KHR_surfaceless_context") ? "yes" : "no");

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        egl_fail("eglBindAPI");
        eglTerminate(dpy);
        gbm_device_destroy(gbm);
        close(fd);
        return 14;
    }

    const EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_NONE
    };
    EGLConfig cfg = NULL;
    EGLint count = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &count) || count < 1) {
        egl_fail("eglChooseConfig");
        eglTerminate(dpy);
        gbm_device_destroy(gbm);
        close(fd);
        return 15;
    }

    const EGLint ctx_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        egl_fail("eglCreateContext");
        eglTerminate(dpy);
        gbm_device_destroy(gbm);
        close(fd);
        return 16;
    }

    EGLSurface surface = EGL_NO_SURFACE;
    int has_surfaceless =
        egl_ext && strstr(egl_ext, "EGL_KHR_surfaceless_context");

    if (!has_surfaceless) {
        const EGLint pbuffer_attr[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        surface = eglCreatePbufferSurface(dpy, cfg, pbuffer_attr);
        if (surface == EGL_NO_SURFACE) {
            egl_fail("eglCreatePbufferSurface");
            eglDestroyContext(dpy, ctx);
            eglTerminate(dpy);
            gbm_device_destroy(gbm);
            close(fd);
            return 17;
        }
    }

    if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
        egl_fail("eglMakeCurrent");
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(dpy, surface);
        eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        gbm_device_destroy(gbm);
        close(fd);
        return 18;
    }

    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *sl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("gl.vendor=%s\n", vendor ? vendor : "(null)");
    printf("gl.renderer=%s\n", renderer ? renderer : "(null)");
    printf("gl.version=%s\n", version ? version : "(null)");
    printf("glsl.version=%s\n", sl ? sl : "(null)");

    int software =
        contains_ci(renderer, "llvmpipe") ||
        contains_ci(renderer, "softpipe") ||
        contains_ci(renderer, "swrast") ||
        contains_ci(renderer, "software");

    int panfrost =
        contains_ci(renderer, "mali") ||
        contains_ci(renderer, "panfrost");

    if (software) {
        fprintf(stderr, "FAIL: software renderer detected\n");
    } else if (!panfrost) {
        fprintf(stderr, "WARN: renderer is hardware-looking but not identified as Mali/Panfrost\n");
    } else {
        printf("result=PASS_PANFROST_HARDWARE\n");
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(dpy, surface);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);

    return software ? 20 : 0;
}
