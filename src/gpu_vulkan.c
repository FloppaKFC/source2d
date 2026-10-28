/* Source 2D engine - dibujo con Vulkan (experimental: version "Beta 0.1 (Vulkan)").
 * Hace lo mismo que el dibujo con OpenGL de platform_raylib.c (lienzos, texturas, rellenos, degradados, filtros,
 * desenfoque, shaders de capa y presentacion con bandas) pero con Vulkan. La ventana, el teclado, el mando y el
 * sonido siguen siendo de raylib: aqui solo se crea una superficie de Vulkan sobre la misma ventana.
 * Vulkan se carga en tiempo de ejecucion (libvulkan.so.1 / vulkan-1.dll): si no esta o algo falla al iniciar,
 * vk_init devuelve error y el juego sigue con OpenGL como siempre. Los shaders GLSL del proyecto se compilan con
 * shaderc si esta instalado (libshaderc_shared); si no, esas capas se dibujan sin shader.
 * Todo con alfa premultiplicado. Las imagenes viven en VK_IMAGE_LAYOUT_GENERAL y entre pases hay una barrera
 * completa: mas sencillo que seguir cada estado, y de sobra para un juego 2D. */
#define VK_NO_PROTOTYPES
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vulkan/vulkan_core.h"
#ifdef _WIN32
#include <windows.h>
#include "vulkan/vulkan_win32.h"
#else
#include <dlfcn.h>
/* sin las cabeceras de X11: solo hacen falta estos tipos */
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;
#include "vulkan/vulkan_xlib.h"
#endif
#include "gpu_vulkan.h"
#include "vk_shaders.h"

/* ---------------- funciones de Vulkan (se cargan a mano) ---------------- */
#define VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties)
#define VK_INSTANCE(X) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkDestroySurfaceKHR) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties) SURF_FN(X)
#ifdef _WIN32
#define SURF_FN(X) X(vkCreateWin32SurfaceKHR)
#else
#define SURF_FN(X) X(vkCreateXlibSurfaceKHR)
#endif
#define VK_DEVICE(X) X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkQueueWaitIdle) \
    X(vkDeviceWaitIdle) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateImage) X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateBuffer) X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) \
    X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) \
    X(vkUpdateDescriptorSets) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkDestroySemaphore) \
    X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
    X(vkCmdBindVertexBuffers) X(vkCmdDraw) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdPushConstants) \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdClearColorImage)
#define DECL(f) static PFN_##f f;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
VK_GLOBAL(DECL) VK_INSTANCE(DECL) VK_DEVICE(DECL)

/* ventana de raylib (GLFW por dentro) */
typedef struct GLFWwindow GLFWwindow;
GLFWwindow *glfwGetCurrentContext(void);
#ifdef _WIN32
HWND glfwGetWin32Window(GLFWwindow *w);
#else
Display *glfwGetX11Display(void);
Window glfwGetX11Window(GLFWwindow *w);
#endif

#define VT_MAX 1024
#define VX_MAX 16384
#define VB_BYTES (16u << 20)            /* vertices de un fotograma */
#define ST_BYTES (64u << 20)            /* texturas subidas en un fotograma */
#define BLOCK_BYTES (64u << 20)

typedef struct { float x, y, u, v; uint8_t c[4]; } Vert;
typedef struct { int block; VkDeviceSize off, size; VkDeviceMemory own; } Mem;
typedef struct { VkImage img; VkImageView view; VkDescriptorSet ds; VkFramebuffer fb; Mem mem; int w, h; } VImg;
typedef struct { VImg rt, alt; int used, has_alt; } VTarget;
typedef struct { VkDeviceMemory mem; VkDeviceSize size; uint32_t type; VkDeviceSize *fo, *fs; int nf, cap; } Block;

enum { SH_TEX, SH_GRAD, SH_COLOR, SH_BLUR, SH_COUNT };

static struct {
    void *lib;
    VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qf; VkSurfaceKHR surf;
    VkPhysicalDeviceMemoryProperties memp;
    VkSwapchainKHR sc; VkFormat scfmt; VkExtent2D ext; uint32_t nimg; VkImage scimg[8]; VkImageView scview[8]; VkFramebuffer scfb[8];
    VkRenderPass rp_target, rp_present;
    VkDescriptorSetLayout dsl; VkDescriptorPool dpool; VkPipelineLayout pl; VkSampler samp;
    VkShaderModule vs, fs[SH_COUNT];
    VkPipeline pipe[SH_COUNT][4], pipe_present;
    /* dos fotogramas en vuelo: mientras la GPU dibuja uno, la CPU prepara el siguiente */
    VkCommandPool cpool; VkCommandBuffer cb, cbs[2]; VkFence fences[2]; VkSemaphore sems_img[2], sems_done[2]; int fi, sub[2];
    VkBuffer vb, st, rb; Mem vbm, stm, rbm; void *vbp, *stp, *rbp; VkDeviceSize rb_size;
    uint32_t vb_n, st_off;
    int recording, inpass, cur, uncapped, ok, need_resize;
    /* lote pendiente: mismos pipeline e imagen */
    VkPipeline b_pipe, bound_pipe; VkDescriptorSet b_ds, bound_ds; uint32_t b_first, b_count;
    VImg white;
    Block blocks[64]; int nblocks;
    VImg *dead[2]; int ndead[2], capdead[2];
    /* shaders del proyecto (con shaderc si esta) */
    void *shaderc; int shaderc_tried;
    struct { char *src; VkPipeline pipe; int ok; } user[32]; int nuser;
} V;

static VTarget vt[VT_MAX];
static VImg vtex[VX_MAX];
static int vtex_used[VX_MAX], vtex_next = 0;

static char *s2d_dup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); if (d) memcpy(d, s, n); return d; }
static void *vk_sym(void *lib, const char *n) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)lib, n);
#else
    return dlsym(lib, n);
#endif
}
static void *vk_open(const char *const *names) {
    for (int i = 0; names[i]; i++) {
#ifdef _WIN32
        void *h = (void *)LoadLibraryA(names[i]);
#else
        void *h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
#endif
        if (h) return h;
    }
    return NULL;
}

/* ---------------- memoria: bloques grandes repartidos (hay un limite de reservas en cada GPU) ---------------- */
static int mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < V.memp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (V.memp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}
static int mem_alloc(VkMemoryRequirements rq, VkMemoryPropertyFlags want, Mem *out) {
    int t = mem_type(rq.memoryTypeBits, want);
    if (t < 0) return -1;
    memset(out, 0, sizeof *out);
    out->block = -1;
    if (rq.size > BLOCK_BYTES / 4) {                       /* las grandes van solas */
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, rq.size, (uint32_t)t };
        if (vkAllocateMemory(V.dev, &ai, NULL, &out->own) != VK_SUCCESS) return -1;
        out->size = rq.size;
        return 0;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int b = 0; b < V.nblocks; b++) {
            Block *B = &V.blocks[b];
            if (B->type != (uint32_t)t) continue;
            for (int i = 0; i < B->nf; i++) {
                VkDeviceSize a = (B->fo[i] + rq.alignment - 1) / rq.alignment * rq.alignment, pad = a - B->fo[i];
                if (B->fs[i] < pad + rq.size) continue;
                VkDeviceSize end = B->fo[i] + B->fs[i];
                out->block = b; out->off = a; out->size = rq.size;
                /* el hueco se parte: lo de antes (alineacion) y lo de despues */
                if (pad) { B->fs[i] = pad; if (end > a + rq.size) { if (B->nf == B->cap) { B->cap *= 2; B->fo = realloc(B->fo, B->cap * sizeof *B->fo); B->fs = realloc(B->fs, B->cap * sizeof *B->fs); }
                           memmove(B->fo + i + 2, B->fo + i + 1, (B->nf - i - 1) * sizeof *B->fo); memmove(B->fs + i + 2, B->fs + i + 1, (B->nf - i - 1) * sizeof *B->fs);
                           B->fo[i + 1] = a + rq.size; B->fs[i + 1] = end - a - rq.size; B->nf++; } }
                else if (end > a + rq.size) { B->fo[i] = a + rq.size; B->fs[i] = end - a - rq.size; }
                else { memmove(B->fo + i, B->fo + i + 1, (B->nf - i - 1) * sizeof *B->fo); memmove(B->fs + i, B->fs + i + 1, (B->nf - i - 1) * sizeof *B->fs); B->nf--; }
                return 0;
            }
        }
        if (pass || V.nblocks >= 64) break;
        Block *B = &V.blocks[V.nblocks];
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, BLOCK_BYTES, (uint32_t)t };
        if (vkAllocateMemory(V.dev, &ai, NULL, &B->mem) != VK_SUCCESS) return -1;
        B->size = BLOCK_BYTES; B->type = (uint32_t)t; B->cap = 16; B->fo = malloc(16 * sizeof *B->fo); B->fs = malloc(16 * sizeof *B->fs);
        B->fo[0] = 0; B->fs[0] = BLOCK_BYTES; B->nf = 1;
        V.nblocks++;
    }
    return -1;
}
static VkDeviceMemory mem_of(const Mem *m) { return m->own ? m->own : V.blocks[m->block].mem; }
static void mem_free(Mem *m) {
    if (m->own) { vkFreeMemory(V.dev, m->own, NULL); m->own = VK_NULL_HANDLE; return; }
    if (m->block < 0) return;
    Block *B = &V.blocks[m->block];
    int i = 0;
    while (i < B->nf && B->fo[i] < m->off) i++;
    if (B->nf == B->cap) { B->cap *= 2; B->fo = realloc(B->fo, B->cap * sizeof *B->fo); B->fs = realloc(B->fs, B->cap * sizeof *B->fs); }
    memmove(B->fo + i + 1, B->fo + i, (B->nf - i) * sizeof *B->fo); memmove(B->fs + i + 1, B->fs + i, (B->nf - i) * sizeof *B->fs);
    B->fo[i] = m->off; B->fs[i] = m->size; B->nf++;
    /* se junta con los huecos de al lado */
    if (i + 1 < B->nf && B->fo[i] + B->fs[i] == B->fo[i + 1]) { B->fs[i] += B->fs[i + 1]; memmove(B->fo + i + 1, B->fo + i + 2, (B->nf - i - 2) * sizeof *B->fo); memmove(B->fs + i + 1, B->fs + i + 2, (B->nf - i - 2) * sizeof *B->fs); B->nf--; }
    if (i > 0 && B->fo[i - 1] + B->fs[i - 1] == B->fo[i]) { B->fs[i - 1] += B->fs[i]; memmove(B->fo + i, B->fo + i + 1, (B->nf - i - 1) * sizeof *B->fo); memmove(B->fs + i, B->fs + i + 1, (B->nf - i - 1) * sizeof *B->fs); B->nf--; }
    m->block = -1;
}
static int make_buffer(VkDeviceSize size, VkBufferUsageFlags use, VkBuffer *b, Mem *m, void **map) {
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, use, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
    if (vkCreateBuffer(V.dev, &bi, NULL, b) != VK_SUCCESS) return -1;
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(V.dev, *b, &rq);
    if (mem_alloc(rq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m)) return -1;
    vkBindBufferMemory(V.dev, *b, mem_of(m), m->off);
    /* los buffers van siempre solos (own) o al principio de su bloque: se mapea su trozo */
    return vkMapMemory(V.dev, mem_of(m), m->off, size, 0, map) == VK_SUCCESS ? 0 : -1;
}

/* ---------------- grabacion de ordenes ---------------- */
static void barrier(void) {
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT };
    vkCmdPipelineBarrier(V.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
}
static void destroy_img(VImg *im) {
    if (im->fb) vkDestroyFramebuffer(V.dev, im->fb, NULL);
    if (im->ds) vkFreeDescriptorSets(V.dev, V.dpool, 1, &im->ds);
    if (im->view) vkDestroyImageView(V.dev, im->view, NULL);
    if (im->img) vkDestroyImage(V.dev, im->img, NULL);
    mem_free(&im->mem);
    memset(im, 0, sizeof *im);
}
/* lo que ya se grabo puede usar la imagen: se borra cuando la GPU termine este fotograma */
static void kill_later(VImg *im) {
    if (!im->img) return;
    int f = V.fi;
    if (V.ndead[f] == V.capdead[f]) { V.capdead[f] = V.capdead[f] ? V.capdead[f] * 2 : 64; V.dead[f] = realloc(V.dead[f], V.capdead[f] * sizeof *V.dead[f]); }
    V.dead[f][V.ndead[f]++] = *im;
    memset(im, 0, sizeof *im);
}
/* espera a que la GPU termine lo que se mando desde la ranura f y borra lo que quedo pendiente de ella */
static void wait_slot(int f) {
    if (V.sub[f]) { vkWaitForFences(V.dev, 1, &V.fences[f], VK_TRUE, UINT64_MAX); V.sub[f] = 0; }
    for (int i = 0; i < V.ndead[f]; i++) destroy_img(&V.dead[f][i]);
    V.ndead[f] = 0;
}
static void wait_frame(void) { wait_slot(0); wait_slot(1); }
static void begin(void) {
    if (V.recording) return;
    wait_slot(V.fi);
    V.cb = V.cbs[V.fi];
    vkResetFences(V.dev, 1, &V.fences[V.fi]);
    vkResetCommandBuffer(V.cb, 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
    vkBeginCommandBuffer(V.cb, &bi);
    V.recording = 1; V.vb_n = (uint32_t)(V.fi * (VB_BYTES / 2 / sizeof(Vert))); V.st_off = (uint32_t)(V.fi * (ST_BYTES / 2));
    V.bound_pipe = VK_NULL_HANDLE; V.bound_ds = VK_NULL_HANDLE;
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(V.cb, 0, 1, &V.vb, &zero);
}
static void flush_batch(void) {
    if (!V.b_count) return;
    if (V.bound_pipe != V.b_pipe) { vkCmdBindPipeline(V.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, V.b_pipe); V.bound_pipe = V.b_pipe; }
    if (V.bound_ds != V.b_ds) { vkCmdBindDescriptorSets(V.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pl, 0, 1, &V.b_ds, 0, NULL); V.bound_ds = V.b_ds; }
    vkCmdDraw(V.cb, V.b_count, 1, V.b_first, 0);
    V.b_count = 0;
}
static void end_pass(void) {
    flush_batch();
    if (V.inpass) vkCmdEndRenderPass(V.cb);
    V.inpass = 0; V.cur = -1;
}
/* manda lo grabado y espera (para leer de la GPU o cuando se llena un buffer) */
static void submit_wait(void) {
    if (!V.recording) return;
    end_pass();
    vkEndCommandBuffer(V.cb);
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &V.cb, 0, NULL };
    vkQueueSubmit(V.q, 1, &si, V.fences[V.fi]);
    V.recording = 0; V.sub[V.fi] = 1;
    wait_slot(V.fi);
}
static void push(const void *data, uint32_t size) {
    vkCmdPushConstants(V.cb, V.pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
}
static void begin_pass(VImg *im, VkRenderPass rp, VkFramebuffer fb, int w, int h, int clear) {
    VkClearValue cv = { { { 0, 0, 0, 1 } } };
    VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, rp, fb, { { 0, 0 }, { (uint32_t)w, (uint32_t)h } }, clear ? 1u : 0u, clear ? &cv : NULL };
    vkCmdBeginRenderPass(V.cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0, 0, (float)w, (float)h, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
    vkCmdSetViewport(V.cb, 0, 1, &vp);
    vkCmdSetScissor(V.cb, 0, 1, &sc);
    float inv[4] = { 2.0f / (float)w, 2.0f / (float)h, (float)w, (float)h };
    push(inv, sizeof inv);
    V.inpass = 1; V.bound_pipe = VK_NULL_HANDLE; V.bound_ds = VK_NULL_HANDLE;
    (void)im;
}
static int bind_target(int t) {
    if (t < 0 || t >= VT_MAX || !vt[t].used) return 0;
    begin();
    if (V.inpass && V.cur == t) return 1;
    end_pass();
    barrier();
    begin_pass(&vt[t].rt, V.rp_target, vt[t].rt.fb, vt[t].rt.w, vt[t].rt.h, 0);
    V.cur = t;
    return 1;
}
static Vert *verts(VkPipeline p, VkDescriptorSet ds, uint32_t n) {
    if ((V.vb_n + n) * sizeof(Vert) > (size_t)(V.fi + 1) * (VB_BYTES / 2)) {   /* mitad llena: se manda lo que hay y se sigue */
        int t = V.cur;
        submit_wait();
        begin();
        if (t >= 0) bind_target(t);
    }
    if (V.b_count && (V.b_pipe != p || V.b_ds != ds)) flush_batch();
    if (!V.b_count) { V.b_pipe = p; V.b_ds = ds; V.b_first = V.vb_n; }
    Vert *v = (Vert *)V.vbp + V.vb_n;
    V.vb_n += n; V.b_count += n;
    return v;
}
static void vtx(Vert *v, float x, float y, float u, float w, const uint8_t c[4]) { v->x = x; v->y = y; v->u = u; v->v = w; memcpy(v->c, c, 4); }

/* ---------------- imagenes ---------------- */
static VkDescriptorSet make_ds(VkImageView view) {
    VkDescriptorSet ds;
    VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, V.dpool, 1, &V.dsl };
    if (vkAllocateDescriptorSets(V.dev, &ai, &ds) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo ii = { V.samp, view, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, NULL, NULL };
    vkUpdateDescriptorSets(V.dev, 1, &w, 0, NULL);
    return ds;
}
/* imagen RGBA vacia (transparente) en GENERAL; target: tambien se puede dibujar en ella */
static int make_img(VImg *im, int w, int h, int target) {
    memset(im, 0, sizeof *im);
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { (uint32_t)w, (uint32_t)h, 1 }, 1, 1,
                             VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (target ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
                             VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
    if (vkCreateImage(V.dev, &ci, NULL, &im->img) != VK_SUCCESS) return -1;
    VkMemoryRequirements rq;
    vkGetImageMemoryRequirements(V.dev, im->img, &rq);
    if (mem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &im->mem) && mem_alloc(rq, 0, &im->mem)) { vkDestroyImage(V.dev, im->img, NULL); im->img = 0; return -1; }
    vkBindImageMemory(V.dev, im->img, mem_of(&im->mem), im->mem.off);
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, im->img, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                 { 0, 0, 0, 0 }, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCreateImageView(V.dev, &vi, NULL, &im->view);
    im->ds = make_ds(im->view);
    if (target) {
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, V.rp_target, 1, &im->view, (uint32_t)w, (uint32_t)h, 1 };
        vkCreateFramebuffer(V.dev, &fi, NULL, &im->fb);
    }
    im->w = w; im->h = h;
    /* UNDEFINED -> GENERAL y transparente */
    begin();
    end_pass();
    VkImageMemoryBarrier ib = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im->img, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(V.cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    VkClearColorValue clr = { { 0, 0, 0, 0 } };
    VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(V.cb, im->img, VK_IMAGE_LAYOUT_GENERAL, &clr, 1, &rg);
    barrier();
    return 0;
}
/* sube pixeles ya premultiplicados (los escribe fill en el buffer de subida) */
static int upload(VImg *im, int w, int h, void (*fill)(uint8_t *, const void *, size_t), const void *arg) {
    size_t n = (size_t)w * h * 4;
    if (n > ST_BYTES / 2) return -1;
    begin();
    if (V.st_off + n > (size_t)(V.fi + 1) * (ST_BYTES / 2)) { int t = V.cur; submit_wait(); begin(); if (t >= 0) bind_target(t); }
    int t = V.cur;
    end_pass();
    fill((uint8_t *)V.stp + V.st_off, arg, n);
    barrier();
    VkBufferImageCopy bc = { V.st_off, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)w, (uint32_t)h, 1 } };
    vkCmdCopyBufferToImage(V.cb, V.st, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &bc);
    barrier();
    V.st_off += (uint32_t)((n + 15) & ~(size_t)15);
    (void)t;
    return 0;
}
static void fill_premul(uint8_t *pm, const void *arg, size_t n) {
    const uint8_t *rgba = arg;
    for (size_t i = 0; i < n; i += 4) {
        unsigned a = rgba[i + 3];
        if (a == 255) { memcpy(pm + i, rgba + i, 4); continue; }
        pm[i] = (uint8_t)((rgba[i] * a + 127) / 255); pm[i + 1] = (uint8_t)((rgba[i + 1] * a + 127) / 255);
        pm[i + 2] = (uint8_t)((rgba[i + 2] * a + 127) / 255); pm[i + 3] = (uint8_t)a;
    }
}

/* ---------------- pipelines ---------------- */
static VkShaderModule module(const uint32_t *code, size_t bytes) {
    VkShaderModule m = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, bytes, code };
    vkCreateShaderModule(V.dev, &ci, NULL, &m);
    return m;
}
static VkPipeline make_pipe(VkShaderModule fs, int mode, VkRenderPass rp) {
    VkPipelineShaderStageCreateInfo st[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, V.vs, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL } };
    VkVertexInputBindingDescription vb = { 0, sizeof(Vert), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription va[3] = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 }, { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16 } };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 1, &vb, 3, va };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL,
                                                  VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, NULL, VK_FALSE, VK_FALSE };
    static const VkBlendFactor SRC[4] = { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO };
    static const VkBlendFactor DST[4] = { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA };
    VkPipelineColorBlendAttachmentState ba = { VK_TRUE, SRC[mode], DST[mode], VK_BLEND_OP_ADD, SRC[mode], DST[mode], VK_BLEND_OP_ADD, 0xF };
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &ba, { 0, 0, 0, 0 } };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn };
    VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, st, &vi, &ia, NULL, &vp, &rs, &ms, NULL, &cb, &ds,
                                        V.pl, rp, 0, VK_NULL_HANDLE, -1 };
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(V.dev, VK_NULL_HANDLE, 1, &ci, NULL, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
    return p;
}
static VkPipeline pipe(int sh, int mode) {
    if (mode < 0 || mode > 3) mode = 0;
    if (!V.pipe[sh][mode]) V.pipe[sh][mode] = make_pipe(V.fs[sh], mode, V.rp_target);
    return V.pipe[sh][mode];
}
static VkRenderPass make_rp(VkFormat fmt, int present) {
    VkAttachmentDescription ad = { 0, fmt, VK_SAMPLE_COUNT_1_BIT, present ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
                                   VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                   present ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL, present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference ar = { 0, present ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription sd = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, NULL, 1, &ar, NULL, NULL, 0, NULL };
    VkSubpassDependency dep = { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, 0 };
    VkRenderPassCreateInfo ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 1, &ad, 1, &sd, 1, &dep };
    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(V.dev, &ci, NULL, &rp);
    return rp;
}

/* ---------------- cadena de presentacion ---------------- */
static void destroy_swapchain(void) {
    for (uint32_t i = 0; i < V.nimg; i++) {
        if (V.scfb[i]) vkDestroyFramebuffer(V.dev, V.scfb[i], NULL);
        if (V.scview[i]) vkDestroyImageView(V.dev, V.scview[i], NULL);
        V.scfb[i] = VK_NULL_HANDLE; V.scview[i] = VK_NULL_HANDLE;
    }
    if (V.sc) vkDestroySwapchainKHR(V.dev, V.sc, NULL);
    V.sc = VK_NULL_HANDLE; V.nimg = 0;
}
static int make_swapchain(int w, int h) {
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(V.pd, V.surf, &caps) != VK_SUCCESS) return -1;
    VkExtent2D e = caps.currentExtent;
    if (e.width == 0xFFFFFFFFu) { e.width = (uint32_t)w; e.height = (uint32_t)h; }
    if (!e.width || !e.height) return -1;                   /* ventana minimizada */
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(V.pd, V.surf, &nf, NULL);
    VkSurfaceFormatKHR fm[64];
    if (nf > 64) nf = 64;
    vkGetPhysicalDeviceSurfaceFormatsKHR(V.pd, V.surf, &nf, fm);
    VkSurfaceFormatKHR f = fm[0];
    for (uint32_t i = 0; i < nf; i++) if (fm[i].format == VK_FORMAT_B8G8R8A8_UNORM || fm[i].format == VK_FORMAT_R8G8B8A8_UNORM) { f = fm[i]; break; }
    uint32_t np = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(V.pd, V.surf, &np, NULL);
    VkPresentModeKHR pm[16], mode = VK_PRESENT_MODE_FIFO_KHR;
    if (np > 16) np = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(V.pd, V.surf, &np, pm);
    if (V.uncapped) for (uint32_t i = 0; i < np; i++) { if (pm[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) { mode = pm[i]; break; } if (pm[i] == VK_PRESENT_MODE_MAILBOX_KHR) mode = pm[i]; }
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > 8) count = 8;
    VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, V.surf, count, f.format, f.colorSpace, e, 1,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL,
                                    caps.currentTransform, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, mode, VK_TRUE, VK_NULL_HANDLE };
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) ci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (vkCreateSwapchainKHR(V.dev, &ci, NULL, &V.sc) != VK_SUCCESS) return -1;
    if (f.format != V.scfmt) {
        if (V.rp_present) vkDestroyRenderPass(V.dev, V.rp_present, NULL);
        if (V.pipe_present) vkDestroyPipeline(V.dev, V.pipe_present, NULL);
        V.scfmt = f.format;
        V.rp_present = make_rp(f.format, 1);
        V.pipe_present = make_pipe(V.fs[SH_TEX], 0, V.rp_present);
    }
    V.nimg = 8;
    vkGetSwapchainImagesKHR(V.dev, V.sc, &V.nimg, V.scimg);
    for (uint32_t i = 0; i < V.nimg; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, V.scimg[i], VK_IMAGE_VIEW_TYPE_2D, f.format, { 0, 0, 0, 0 },
                                     { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        vkCreateImageView(V.dev, &vi, NULL, &V.scview[i]);
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, V.rp_present, 1, &V.scview[i], e.width, e.height, 1 };
        vkCreateFramebuffer(V.dev, &fi, NULL, &V.scfb[i]);
    }
    V.ext = e;
    return 0;
}

/* ---------------- inicio ---------------- */
int vk_init(int w, int h, int uncapped, char *info, size_t ninfo) {
    memset(&V, 0, sizeof V);
    V.cur = -1; V.uncapped = uncapped;
    if (info && ninfo) info[0] = 0;
#ifdef _WIN32
    static const char *libs[] = { "vulkan-1.dll", NULL };
#else
    static const char *libs[] = { "libvulkan.so.1", "libvulkan.so", NULL };
#endif
    V.lib = vk_open(libs);
    if (!V.lib) { if (info) snprintf(info, ninfo, "no hay Vulkan en este equipo"); return -1; }
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)vk_sym(V.lib, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) return -1;
#define LOADG(f) f = (PFN_##f)vkGetInstanceProcAddr(NULL, #f); if (!f) return -1;
    VK_GLOBAL(LOADG)
    GLFWwindow *win = glfwGetCurrentContext();
    if (!win) return -1;
#ifdef _WIN32
    const char *exts[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
#else
    Display *dpy = glfwGetX11Display();
    Window xw = glfwGetX11Window(win);
    if (!dpy || !xw) { if (info) snprintf(info, ninfo, "la ventana no es de X11"); return -1; }
    const char *exts[] = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
#endif
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "Source 2D", 1, "Source 2D", 1, VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ic = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 2, exts };
    if (vkCreateInstance(&ic, NULL, &V.inst) != VK_SUCCESS) { if (info) snprintf(info, ninfo, "no se pudo iniciar Vulkan"); return -1; }
#define LOADI(f) f = (PFN_##f)vkGetInstanceProcAddr(V.inst, #f); if (!f) return -1;
    VK_INSTANCE(LOADI)
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, NULL, 0, GetModuleHandleA(NULL), glfwGetWin32Window(win) };
    if (vkCreateWin32SurfaceKHR(V.inst, &sci, NULL, &V.surf) != VK_SUCCESS) return -1;
#else
    VkXlibSurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, NULL, 0, dpy, xw };
    if (vkCreateXlibSurfaceKHR(V.inst, &sci, NULL, &V.surf) != VK_SUCCESS) return -1;
#endif
    /* GPU: la primera dedicada que pueda presentar en la ventana; si no, cualquiera que pueda */
    uint32_t npd = 0;
    vkEnumeratePhysicalDevices(V.inst, &npd, NULL);
    VkPhysicalDevice pds[16];
    if (npd > 16) npd = 16;
    vkEnumeratePhysicalDevices(V.inst, &npd, pds);
    int best = -1, bestScore = -1;
    uint32_t bestQf = 0;
    for (uint32_t i = 0; i < npd; i++) {
        VkPhysicalDeviceProperties pp;
        vkGetPhysicalDeviceProperties(pds[i], &pp);
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, NULL);
        VkQueueFamilyProperties qp[32];
        if (nq > 32) nq = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, qp);
        for (uint32_t q = 0; q < nq; q++) {
            VkBool32 pres = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], q, V.surf, &pres);
            if (!(qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !pres) continue;
            int score = pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) { bestScore = score; best = (int)i; bestQf = q; }
            break;
        }
    }
    if (best < 0) { if (info) snprintf(info, ninfo, "ninguna GPU con Vulkan puede dibujar en la ventana"); return -1; }
    V.pd = pds[best]; V.qf = bestQf;
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(V.pd, &pp);
    if (info) snprintf(info, ninfo, "%s", pp.deviceName);
    vkGetPhysicalDeviceMemoryProperties(V.pd, &V.memp);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, V.qf, 1, &prio };
    const char *dext[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qi, 0, NULL, 1, dext, NULL };
    if (vkCreateDevice(V.pd, &dci, NULL, &V.dev) != VK_SUCCESS) return -1;
#define LOADD(f) f = (PFN_##f)vkGetDeviceProcAddr(V.dev, #f); if (!f) return -1;
    VK_DEVICE(LOADD)
    vkGetDeviceQueue(V.dev, V.qf, 0, &V.q);

    V.rp_target = make_rp(VK_FORMAT_R8G8B8A8_UNORM, 0);
    VkDescriptorSetLayoutBinding lb = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo lci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &lb };
    vkCreateDescriptorSetLayout(V.dev, &lci, NULL, &V.dsl);
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VX_MAX + VT_MAX * 2 + 16 };
    VkDescriptorPoolCreateInfo pci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, ps.descriptorCount, 1, &ps };
    if (vkCreateDescriptorPool(V.dev, &pci, NULL, &V.dpool) != VK_SUCCESS) return -1;
    VkPushConstantRange pr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
    VkPipelineLayoutCreateInfo plc = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &V.dsl, 1, &pr };
    vkCreatePipelineLayout(V.dev, &plc, NULL, &V.pl);
    VkSamplerCreateInfo smp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                0, VK_FALSE, 1, VK_FALSE, VK_COMPARE_OP_ALWAYS, 0, 0, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE };
    vkCreateSampler(V.dev, &smp, NULL, &V.samp);
    V.vs = module(spv_common_vert, sizeof spv_common_vert);
    V.fs[SH_TEX] = module(spv_tex_frag, sizeof spv_tex_frag);
    V.fs[SH_GRAD] = module(spv_grad_frag, sizeof spv_grad_frag);
    V.fs[SH_COLOR] = module(spv_color_frag, sizeof spv_color_frag);
    V.fs[SH_BLUR] = module(spv_blur_frag, sizeof spv_blur_frag);
    VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, V.qf };
    vkCreateCommandPool(V.dev, &cpi, NULL, &V.cpool);
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, V.cpool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2 };
    vkAllocateCommandBuffers(V.dev, &cai, V.cbs);
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };
    VkSemaphoreCreateInfo sci2 = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
    for (int f = 0; f < 2; f++) {
        vkCreateFence(V.dev, &fci, NULL, &V.fences[f]);
        vkCreateSemaphore(V.dev, &sci2, NULL, &V.sems_img[f]);
        vkCreateSemaphore(V.dev, &sci2, NULL, &V.sems_done[f]);
    }
    V.cb = V.cbs[0];
    if (make_buffer(VB_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &V.vb, &V.vbm, &V.vbp) ||
        make_buffer(ST_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &V.st, &V.stm, &V.stp)) return -1;
    if (make_swapchain(w, h)) return -1;
    if (!pipe(SH_TEX, 0) || !V.pipe_present) { if (info) snprintf(info, ninfo, "no se pudieron crear los pipelines"); return -1; }
    /* textura blanca 1x1 para los rellenos lisos */
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    if (make_img(&V.white, 1, 1, 0) || upload(&V.white, 1, 1, fill_premul, white)) return -1;
    V.ok = 1;
    return 0;
}

void vk_shutdown(void) {
    if (!V.dev) return;
    if (V.recording) submit_wait();
    vkDeviceWaitIdle(V.dev);
    wait_frame();
    for (int i = 0; i < VT_MAX; i++) if (vt[i].used) { destroy_img(&vt[i].rt); if (vt[i].has_alt) destroy_img(&vt[i].alt); vt[i].used = 0; }
    for (int i = 0; i < VX_MAX; i++) if (vtex_used[i]) { destroy_img(&vtex[i]); vtex_used[i] = 0; }
    destroy_img(&V.white);
    destroy_swapchain();
    vkDestroyDevice(V.dev, NULL);
    if (V.surf) vkDestroySurfaceKHR(V.inst, V.surf, NULL);
    vkDestroyInstance(V.inst, NULL);
    V.dev = VK_NULL_HANDLE; V.ok = 0;
}

/* ---------------- las operaciones de dibujo (mismo contrato que plat_gpu_*) ---------------- */
int vk_target(int w, int h) {
    for (int i = 0; i < VT_MAX; i++) {
        if (vt[i].used) continue;
        if (make_img(&vt[i].rt, w, h, 1)) return -1;
        vt[i].used = 1; vt[i].has_alt = 0;
        return i;
    }
    return -1;
}
void vk_target_free(int id) {
    if (id < 0 || id >= VT_MAX || !vt[id].used) return;
    if (V.cur == id) end_pass();
    kill_later(&vt[id].rt);
    if (vt[id].has_alt) kill_later(&vt[id].alt);
    vt[id].used = 0;
}
int vk_tex(int id, const uint8_t *rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return -1;
    if (id >= 0 && id < VX_MAX && vtex_used[id] && vtex[id].w == w && vtex[id].h == h) {
        int t = V.cur;
        if (upload(&vtex[id], w, h, fill_premul, rgba)) return -1;
        if (t >= 0) bind_target(t);
        return id;
    }
    if (id >= 0 && id < VX_MAX && vtex_used[id]) { kill_later(&vtex[id]); vtex_used[id] = 0; }
    int slot = -1;
    for (int k = 0; k < VX_MAX; k++) { int i = (vtex_next + k) % VX_MAX; if (!vtex_used[i]) { slot = i; break; } }
    if (slot < 0) return -1;
    int t = V.cur;
    if (make_img(&vtex[slot], w, h, 0) || upload(&vtex[slot], w, h, fill_premul, rgba)) { destroy_img(&vtex[slot]); return -1; }
    if (t >= 0) bind_target(t);
    vtex_used[slot] = 1;
    vtex_next = slot + 1;
    return slot;
}
void vk_tex_free(int id) {
    if (id < 0 || id >= VX_MAX || !vtex_used[id]) return;
    kill_later(&vtex[id]);
    vtex_used[id] = 0;
}
void vk_draw(int target, int src_rt, int src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh,
             const double m[6], double alpha, int mode) {
    VImg *t;
    if (src_rt) { if (src < 0 || src >= VT_MAX || !vt[src].used || src == target) return; t = &vt[src].rt; }
    else { if (src < 0 || src >= VX_MAX || !vtex_used[src]) return; t = &vtex[src]; }
    if (alpha <= 0 || !bind_target(target)) return;
    float u0 = (float)(sx / t->w), u1 = (float)((sx + sw) / t->w), v0 = (float)(sy / t->h), v1 = (float)((sy + sh) / t->h);
    double px[4] = { dx, dx, dx + dw, dx + dw }, py[4] = { dy, dy + dh, dy + dh, dy };
    float X[4], Y[4];
    for (int i = 0; i < 4; i++) { X[i] = (float)(m[0] * px[i] + m[2] * py[i] + m[4]); Y[i] = (float)(m[1] * px[i] + m[3] * py[i] + m[5]); }
    uint8_t a = (uint8_t)(alpha >= 1 ? 255 : alpha * 255 + 0.5), c[4] = { a, a, a, a };
    Vert *v = verts(pipe(SH_TEX, mode), t->ds, 6);
    vtx(v + 0, X[0], Y[0], u0, v0, c); vtx(v + 1, X[1], Y[1], u0, v1, c); vtx(v + 2, X[2], Y[2], u1, v1, c);
    vtx(v + 3, X[0], Y[0], u0, v0, c); vtx(v + 4, X[2], Y[2], u1, v1, c); vtx(v + 5, X[3], Y[3], u1, v0, c);
}
void vk_fill(int target, const double *pts, int n, double r, double g, double b, double a, int mode) {
    if (n < 3 || (a <= 0 && mode != 2) || !bind_target(target)) return;
    if (a > 1) a = 1;
    uint8_t c[4] = { (uint8_t)(r * a), (uint8_t)(g * a), (uint8_t)(b * a), (uint8_t)(a * 255 + 0.5) };
    Vert *v = verts(pipe(SH_TEX, mode), V.white.ds, (uint32_t)(n - 2) * 3);
    for (int i = 1; i + 1 < n; i++, v += 3) {
        vtx(v, (float)pts[0], (float)pts[1], 0.5f, 0.5f, c);
        vtx(v + 1, (float)pts[2 * i + 2], (float)pts[2 * i + 3], 0.5f, 0.5f, c);
        vtx(v + 2, (float)pts[2 * i], (float)pts[2 * i + 1], 0.5f, 0.5f, c);
    }
}
int vk_fill_grad(int target, const double *pts, int n, const double *grad, double alpha, int mode) {
    if (n < 3 || !grad || !bind_target(target)) return -1;
    int ns = (int)grad[4];
    if (ns < 1) return 0;
    if (ns > 4) ns = 4;
    struct { float inv[2], pad[2], g[4], n[4], pos[4], col[16]; } pc;
    memset(&pc, 0, sizeof pc);
    VImg *ti = &vt[target].rt;
    pc.inv[0] = 2.0f / (float)ti->w; pc.inv[1] = 2.0f / (float)ti->h;
    for (int i = 0; i < 4; i++) pc.g[i] = (float)grad[i];
    pc.n[0] = (float)ns; pc.n[1] = (float)(alpha > 1 ? 1 : alpha);
    for (int i = 0; i < ns; i++) {
        const double *st = grad + 5 + 5 * i;
        pc.pos[i] = (float)st[0];
        pc.col[4 * i] = (float)(st[1] / 255.0); pc.col[4 * i + 1] = (float)(st[2] / 255.0); pc.col[4 * i + 2] = (float)(st[3] / 255.0); pc.col[4 * i + 3] = (float)st[4];
    }
    flush_batch();                                          /* cada degradado lleva sus constantes */
    uint8_t c[4] = { 255, 255, 255, 255 };
    Vert *v = verts(pipe(SH_GRAD, mode), V.white.ds, (uint32_t)(n - 2) * 3);
    for (int i = 1; i + 1 < n; i++, v += 3) {
        const double *q[3] = { pts, pts + 2 * i + 2, pts + 2 * i };
        for (int k = 0; k < 3; k++) vtx(v + k, (float)q[k][0], (float)q[k][1], (float)q[k][0], (float)q[k][1], c);
    }
    push(&pc, sizeof pc);
    flush_batch();
    push(pc.inv, 8);
    return 0;
}

/* cuadrado de todo el lienzo (uv de 0 a 1) */
static void full_quad(VkPipeline p, VkDescriptorSet ds, int w, int h) {
    uint8_t c[4] = { 255, 255, 255, 255 };
    Vert *v = verts(p, ds, 6);
    vtx(v + 0, 0, 0, 0, 0, c); vtx(v + 1, 0, (float)h, 0, 1, c); vtx(v + 2, (float)w, (float)h, 1, 1, c);
    vtx(v + 3, 0, 0, 0, 0, c); vtx(v + 4, (float)w, (float)h, 1, 1, c); vtx(v + 5, (float)w, 0, 1, 0, c);
}
static int ensure_alt(VTarget *g) {
    if (g->has_alt) return 0;
    if (make_img(&g->alt, g->rt.w, g->rt.h, 1)) return -1;
    g->has_alt = 1;
    return 0;
}
/* pase sobre una zona: de la imagen a la de reserva con el shader y de vuelta tal cual */
static int zone_pass(int target, int sh, const void *pc, uint32_t pcs, const int rc[4]) {
    if (target < 0 || target >= VT_MAX || !vt[target].used) return -1;
    VTarget *g = &vt[target];
    int x0 = rc[0] < 0 ? 0 : rc[0], y0 = rc[1] < 0 ? 0 : rc[1], x1 = rc[2] > g->rt.w ? g->rt.w : rc[2], y1 = rc[3] > g->rt.h ? g->rt.h : rc[3];
    if (x1 <= x0 || y1 <= y0) return 0;
    begin();
    end_pass();
    if (ensure_alt(g)) return -1;
    VkRect2D sc = { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
    barrier();
    begin_pass(&g->alt, V.rp_target, g->alt.fb, g->rt.w, g->rt.h, 0);
    vkCmdSetScissor(V.cb, 0, 1, &sc);
    push(pc, pcs);
    full_quad(pipe(sh, 2), g->rt.ds, g->rt.w, g->rt.h);
    end_pass();
    barrier();
    begin_pass(&g->rt, V.rp_target, g->rt.fb, g->rt.w, g->rt.h, 0);
    vkCmdSetScissor(V.cb, 0, 1, &sc);
    full_quad(pipe(SH_TEX, 2), g->alt.ds, g->rt.w, g->rt.h);
    end_pass();
    return 0;
}
int vk_color(int target, int kind, const float m9[9], const float k2[2], const int rc[4]) {
    (void)kind;
    if (target < 0 || target >= VT_MAX || !vt[target].used) return -1;
    struct { float inv[2], res[2], m0[4], m1[4], m2[4], k[4]; } pc;
    memset(&pc, 0, sizeof pc);
    VImg *ti = &vt[target].rt;
    pc.inv[0] = 2.0f / (float)ti->w; pc.inv[1] = 2.0f / (float)ti->h; pc.res[0] = (float)ti->w; pc.res[1] = (float)ti->h;
    for (int i = 0; i < 3; i++) { pc.m0[i] = m9[i]; pc.m1[i] = m9[3 + i]; pc.m2[i] = m9[6 + i]; }
    pc.k[0] = k2[0]; pc.k[1] = k2[1];
    return zone_pass(target, SH_COLOR, &pc, sizeof pc, rc);
}
int vk_blur(int target, int r, const int rc[4]) {
    if (target < 0 || target >= VT_MAX || !vt[target].used) return -1;
    struct { float inv[2], res[2], rect[4], blur[4]; } pc;
    memset(&pc, 0, sizeof pc);
    VImg *ti = &vt[target].rt;
    pc.inv[0] = 2.0f / (float)ti->w; pc.inv[1] = 2.0f / (float)ti->h; pc.res[0] = (float)ti->w; pc.res[1] = (float)ti->h;
    for (int i = 0; i < 4; i++) pc.rect[i] = (float)rc[i];
    pc.blur[2] = (float)r;
    for (int pass = 0; pass < 2; pass++) {
        pc.blur[0] = 1; pc.blur[1] = 0;
        if (zone_pass(target, SH_BLUR, &pc, sizeof pc, rc)) return -1;
        pc.blur[0] = 0; pc.blur[1] = 1;
        if (zone_pass(target, SH_BLUR, &pc, sizeof pc, rc)) return -1;
    }
    return 0;
}

/* ---------------- shaders GLSL del proyecto (con shaderc, si esta instalado) ---------------- */
typedef void *(*PFN_sc_init)(void);
typedef void *(*PFN_sc_compile)(void *, const char *, size_t, int, const char *, const char *, void *);
typedef int (*PFN_sc_status)(void *);
typedef size_t (*PFN_sc_len)(void *);
typedef const char *(*PFN_sc_bytes)(void *);
typedef void (*PFN_sc_release)(void *);
static struct { void *comp; PFN_sc_compile compile; PFN_sc_status status; PFN_sc_len len; PFN_sc_bytes bytes, err; PFN_sc_release release; } SC;
static int shaderc_ready(void) {
    if (V.shaderc_tried) return V.shaderc != NULL;
    V.shaderc_tried = 1;
#ifdef _WIN32
    static const char *libs[] = { "shaderc_shared.dll", NULL };
#else
    static const char *libs[] = { "libshaderc_shared.so.1", "libshaderc_shared.so", NULL };
#endif
    V.shaderc = vk_open(libs);
    if (!V.shaderc) { fprintf(stderr, "Source 2D engine (Vulkan): sin shaderc no se pueden usar los shaders del proyecto (se dibuja sin ellos)\n"); return 0; }
    PFN_sc_init init = (PFN_sc_init)vk_sym(V.shaderc, "shaderc_compiler_initialize");
    SC.compile = (PFN_sc_compile)vk_sym(V.shaderc, "shaderc_compile_into_spv");
    SC.status = (PFN_sc_status)vk_sym(V.shaderc, "shaderc_result_get_compilation_status");
    SC.len = (PFN_sc_len)vk_sym(V.shaderc, "shaderc_result_get_length");
    SC.bytes = (PFN_sc_bytes)vk_sym(V.shaderc, "shaderc_result_get_bytes");
    SC.err = (PFN_sc_bytes)vk_sym(V.shaderc, "shaderc_result_get_error_message");
    SC.release = (PFN_sc_release)vk_sym(V.shaderc, "shaderc_result_release");
    if (!init || !SC.compile || !SC.status || !SC.len || !SC.bytes || !SC.release) { V.shaderc = NULL; return 0; }
    SC.comp = init();
    return SC.comp != NULL;
}
static char *rep(const char *s, const char *from, const char *to) {
    size_t fl = strlen(from), tl = strlen(to), count = 0;
    for (const char *p = s; (p = strstr(p, from)) != NULL; p += fl) count++;
    char *out = malloc(strlen(s) + count * tl + 1), *w = out;
    if (!out) return NULL;
    const char *p = s, *q;
    while ((q = strstr(p, from)) != NULL) { memcpy(w, p, (size_t)(q - p)); w += q - p; memcpy(w, to, tl); w += tl; p = q + fl; }
    strcpy(w, p);
    return out;
}
/* GLSL ES 1.0 del editor (v_uv, u_tex, texture2D, gl_FragColor, u_time...) -> GLSL 450 de Vulkan */
static char *user_glsl(const char *src) {
    static const char *HEAD =
        "#version 450\n"
        "layout(push_constant) uniform PC { vec2 inv; vec2 res; vec3 cam; float time; float amount; } s2d_pc;\n"
        "layout(set = 0, binding = 0) uniform sampler2D u_tex;\n"
        "layout(location = 0) in vec2 v_uv;\n"
        "layout(location = 1) in vec4 s2d_col;\n"
        "layout(location = 0) out vec4 s2d_out;\n"
        "#define u_time s2d_pc.time\n#define u_res s2d_pc.res\n#define u_cam s2d_pc.cam\n#define u_amount s2d_pc.amount\n";
    const char *drop[] = { "precision highp float;", "precision mediump float;", "precision lowp float;", "varying vec2 v_uv;", "uniform sampler2D u_tex;",
                           "uniform float u_time;", "uniform vec2 u_res;", "uniform vec3 u_cam;", "uniform float u_amount;" };
    char *a = s2d_dup(src), *b;
    for (size_t i = 0; a && i < sizeof drop / sizeof drop[0]; i++) { b = rep(a, drop[i], ""); free(a); a = b; }
    if (a) { b = rep(a, "texture2D(", "texture("); free(a); a = b; }
    if (a) { b = rep(a, "gl_FragColor", "s2d_out"); free(a); a = b; }
    if (!a) return NULL;
    char *out = malloc(strlen(HEAD) + strlen(a) + 1);
    if (out) { strcpy(out, HEAD); strcat(out, a); }
    free(a);
    return out;
}
static VkPipeline user_pipe(const char *glsl) {
    for (int i = 0; i < V.nuser; i++) if (!strcmp(V.user[i].src, glsl)) return V.user[i].pipe;
    if (V.nuser >= 32) return VK_NULL_HANDLE;
    int i = V.nuser++;
    V.user[i].src = s2d_dup(glsl);
    V.user[i].pipe = VK_NULL_HANDLE;
    if (!shaderc_ready()) return VK_NULL_HANDLE;
    char *code = user_glsl(glsl);
    if (!code) return VK_NULL_HANDLE;
    void *res = SC.compile(SC.comp, code, strlen(code), 1 /* fragmento */, "capa.frag", "main", NULL);
    free(code);
    if (!res) return VK_NULL_HANDLE;
    if (SC.status(res) == 0) {
        VkShaderModule m = module((const uint32_t *)SC.bytes(res), SC.len(res));
        if (m) { V.user[i].pipe = make_pipe(m, 2, V.rp_target); vkDestroyShaderModule(V.dev, m, NULL); }
    } else fprintf(stderr, "Source 2D engine (Vulkan): shader de capa no valido: %s\n", SC.err ? SC.err(res) : "");
    SC.release(res);
    return V.user[i].pipe;
}
int vk_shader(int target, const char *glsl, double t, const float cam[3], float amount) {
    if (target < 0 || target >= VT_MAX || !vt[target].used || !glsl) return -1;
    VkPipeline p = user_pipe(glsl);
    if (!p) return -1;
    VTarget *g = &vt[target];
    begin();
    end_pass();
    if (ensure_alt(g)) return -1;
    struct { float inv[2], res[2], cam[3], time, amount; } pc = { { 2.0f / (float)g->rt.w, 2.0f / (float)g->rt.h }, { (float)g->rt.w, (float)g->rt.h },
                                                                   { cam[0], cam[1], cam[2] ? cam[2] : 1.0f }, (float)t, amount };
    barrier();
    begin_pass(&g->alt, V.rp_target, g->alt.fb, g->rt.w, g->rt.h, 0);
    push(&pc, sizeof pc);
    full_quad(p, g->rt.ds, g->rt.w, g->rt.h);
    end_pass();
    VImg tmp = g->rt; g->rt = g->alt; g->alt = tmp;
    return 0;
}

/* ---------------- leer y presentar ---------------- */
int vk_read(int target, int x, int y, int w, int h, uint8_t *out) {
    if (target < 0 || target >= VT_MAX || !vt[target].used || !out || w <= 0 || h <= 0) return -1;
    VImg *im = &vt[target].rt;
    VkDeviceSize need = (VkDeviceSize)im->w * im->h * 4;
    if (need > V.rb_size) {
        if (V.rb) { if (V.recording) submit_wait(); vkDestroyBuffer(V.dev, V.rb, NULL); mem_free(&V.rbm); V.rb = VK_NULL_HANDLE; }
        if (make_buffer(need, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &V.rb, &V.rbm, &V.rbp)) { V.rb_size = 0; return -1; }
        V.rb_size = need;
    }
    begin();
    end_pass();
    barrier();
    VkBufferImageCopy bc = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)im->w, (uint32_t)im->h, 1 } };
    vkCmdCopyImageToBuffer(V.cb, im->img, VK_IMAGE_LAYOUT_GENERAL, V.rb, 1, &bc);
    submit_wait();
    const uint8_t *src = V.rbp;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int sx = x + i, sy = y + j;
        uint8_t *d = out + ((size_t)j * w + i) * 4;
        if (sx < 0 || sy < 0 || sx >= im->w || sy >= im->h) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
        const uint8_t *p = src + ((size_t)sy * im->w + sx) * 4;
        int a = p[3];
        if (!a) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
        for (int c = 0; c < 3; c++) { int v = a == 255 ? p[c] : (p[c] * 255 + a / 2) / a; d[c] = (uint8_t)(v > 255 ? 255 : v); }
        d[3] = (uint8_t)a;
    }
    return 0;
}
void vk_resized(void) { V.need_resize = 1; }
void vk_present(int target, int ww, int wh, int pixel_perfect, int *dx, int *dy, int *dw, int *dh) {
    if (target < 0 || target >= VT_MAX || !vt[target].used) return;
    VImg *g = &vt[target].rt;
    begin();
    end_pass();
    if (V.need_resize || !V.sc || V.ext.width != (uint32_t)ww || V.ext.height != (uint32_t)wh) {
        submit_wait();
        vkDeviceWaitIdle(V.dev);
        destroy_swapchain();
        V.need_resize = 0;
        if (make_swapchain(ww, wh)) return;                 /* minimizada: no se presenta */
        begin();
    }
    int W0 = (int)V.ext.width, H0 = (int)V.ext.height;
    double sc = fmin((double)W0 / g->w, (double)H0 / g->h);
    if (pixel_perfect && sc >= 1) sc = floor(sc);
    int W = (int)floor(g->w * sc), H = (int)floor(g->h * sc);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    *dw = W; *dh = H; *dx = (W0 - W) / 2; *dy = (H0 - H) / 2;
    uint32_t idx = 0;
    VkResult ar = vkAcquireNextImageKHR(V.dev, V.sc, UINT64_MAX, V.sems_img[V.fi], VK_NULL_HANDLE, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) { V.need_resize = 1; submit_wait(); return; }
    barrier();
    begin_pass(NULL, V.rp_present, V.scfb[idx], W0, H0, 1);
    uint8_t c[4] = { 255, 255, 255, 255 };
    float x0 = (float)*dx, y0 = (float)*dy, x1 = x0 + (float)W, y1 = y0 + (float)H;
    Vert *v = verts(V.pipe_present, g->ds, 6);
    vtx(v + 0, x0, y0, 0, 0, c); vtx(v + 1, x0, y1, 0, 1, c); vtx(v + 2, x1, y1, 1, 1, c);
    vtx(v + 3, x0, y0, 0, 0, c); vtx(v + 4, x1, y1, 1, 1, c); vtx(v + 5, x1, y0, 1, 0, c);
    end_pass();
    vkEndCommandBuffer(V.cb);
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &V.sems_img[V.fi], &ws, 1, &V.cb, 1, &V.sems_done[V.fi] };
    vkQueueSubmit(V.q, 1, &si, V.fences[V.fi]);
    V.recording = 0; V.sub[V.fi] = 1;
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &V.sems_done[V.fi], 1, &V.sc, &idx, NULL };
    VkResult pr = vkQueuePresentKHR(V.q, &pi);
    V.fi ^= 1;                                              /* el siguiente se graba en la otra ranura */
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) V.need_resize = 1;
}
int vk_active(void) { return V.ok; }
void vk_target_size(int id, int *w, int *h) { *w = *h = 0; if (id >= 0 && id < VT_MAX && vt[id].used) { *w = vt[id].rt.w; *h = vt[id].rt.h; } }
