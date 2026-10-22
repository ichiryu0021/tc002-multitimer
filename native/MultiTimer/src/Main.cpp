#include "entry/EasyUIContext.h"
#include "uart/UartContext.h"
#include "manager/ConfigManager.h"
#include <os/SystemProperties.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

void onEasyUIInit(EasyUIContext *pContext) {
	// 防砖标志必须尽早点位；放在 mainLogic 的 onUI_init 里也可以，
	// 但 UI 初始化失败时这里仍能先告诉系统“应用已正常启动”。
	SystemProperties::setString("sys.zkapp.state", "running");
}

void onEasyUIDeinit(EasyUIContext *pContext) {
	UARTCONTEXT->closeUart();
}

const char* onStartupApp(EasyUIContext *pContext) {
	return "mainActivity";
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
