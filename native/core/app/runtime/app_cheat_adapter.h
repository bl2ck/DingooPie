#ifndef DINGOO_PIE_APP_RUNTIME_APP_CHEAT_ADAPTER_H
#define DINGOO_PIE_APP_RUNTIME_APP_CHEAT_ADAPTER_H

struct NativeRuntime;

void appCheatBind(NativeRuntime* runtime);
void appCheatUnbind(NativeRuntime* runtime);
void appCheatApplyStartup(NativeRuntime* runtime);

#endif
