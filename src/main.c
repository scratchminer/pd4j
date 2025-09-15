#include <pd_api.h>

#include "pd4j/class.h"
#include "pd4j/class_loader.h"
#include "pd4j/lua_glue.h"
#include "pd4j/memory.h"
#include "pd4j/utf8.h"

PlaydateAPI *pd;

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg) {
	(void)arg;
	
	if (event == kEventInit) {
		pd = playdate;
		
		uint8_t *name;
		size_t sz = pd4j_utf8_to_java(&name, "Main", 4);
		
		pd4j_class_loader *loader = pd4j_class_loader_get_boot();
		pd4j_class_reference *class = pd4j_class_loader_load(loader, NULL, name);
		
		if (class != NULL) {
			pd4j_class_reference_destroy(class);
		}
		pd4j_free(name, sz);
	}
	else if (event == kEventInitLua) {
		pd4j_lua_glue_register();
	}
	
	return 0;
}
