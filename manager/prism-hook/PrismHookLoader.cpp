#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

__attribute__((constructor))
static void loadHook()
{
    char buf[256];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return;
    buf[len] = '\0';

    if (!strstr(buf, "prismlauncher") && !strstr(buf, "PrismLauncher") && !strstr(buf, "prismrun"))
        return;

    if (!getenv("MCBM_HOOK_SOCKET"))
        return;

    Dl_info info;
    if (dladdr((void*)loadHook, &info)) {
        char* pathCopy = strdup(info.dli_fname);
        char* dir = dirname(pathCopy);

        char realHookPath[512];
        snprintf(realHookPath, sizeof(realHookPath), "%s/libprismhook_core.so", dir);
        free(pathCopy);

        dlopen(realHookPath, RTLD_LAZY | RTLD_GLOBAL);
    }
}
