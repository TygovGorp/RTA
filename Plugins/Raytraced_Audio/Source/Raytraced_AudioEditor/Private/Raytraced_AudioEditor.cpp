#include "Modules/ModuleManager.h"

// No startup work needed: UHT registers the UFactory CDOs, which is all the
// editor requires to populate the New Asset and "Create New Asset" menus.
IMPLEMENT_MODULE(FDefaultModuleImpl, Raytraced_AudioEditor);
