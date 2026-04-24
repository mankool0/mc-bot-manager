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

    char* pathCopy = strdup(buf);
    char* base = basename(pathCopy);
    int isPrism = (strcmp(base, "prismlauncher") == 0
                   || strcmp(base, "PrismLauncher") == 0
                   || strcmp(base, "prismrun") == 0);
    free(pathCopy);
    if (!isPrism) {
        return;
    }

    if (!getenv("MCBM_HOOK_SOCKET")) {
        return;
    }

    Dl_info info;
    if (dladdr((void*)loadHook, &info)) {
        char* libPathCopy = strdup(info.dli_fname);
        char* dir = dirname(libPathCopy);

        char realHookPath[512];
        snprintf(realHookPath, sizeof(realHookPath), "%s/libprismhook_core.so", dir);
        free(libPathCopy);

        dlopen(realHookPath, RTLD_LAZY | RTLD_GLOBAL);
    }
}
