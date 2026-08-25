#include "app/memory/app_framebuffer_mapping.h"

#include "app/cpu/native_runtime.h"
#include "frontend/video/framebuffer.h"

#include <stdio.h>

int appFramebufferInitialize(NativeRuntime* runtime)
{
    for (size_t index = 0; index < framebufferGuestAliasCount(); ++index)
    {
        const uint32_t alias = framebufferGuestAlias(index);
        bool duplicate = false;
        for (size_t previous = 0; previous < index; ++previous)
        {
            if (framebufferGuestAlias(previous) == alias)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        RuntimeError error = nativeRuntimeMapMemory(runtime, alias,
            VM_LCD_FB_SIZE, RUNTIME_PROT_ALL, framebufferPixels());
        if (error)
        {
            printf("framebuffer: failed to map alias 0x%08x: %u (%s)\n",
                alias, error, nativeRuntimeErrorString(error));
            return -1;
        }
    }
    framebufferReset();
    return 0;
}
