#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Ecode_t; typedef uint32_t nvm3_ObjectKey_t; struct nvm3_Handle_t;
#define ECODE_NVM3_OK 0
extern nvm3_Handle_t* nvm3_defaultHandle;
Ecode_t nvm3_initDefault(void);
Ecode_t nvm3_writeData(nvm3_Handle_t*, nvm3_ObjectKey_t, const void*, size_t);
Ecode_t nvm3_readData(nvm3_Handle_t*, nvm3_ObjectKey_t, void*, size_t);
Ecode_t nvm3_getObjectInfo(nvm3_Handle_t*, nvm3_ObjectKey_t, uint32_t*, size_t*);
Ecode_t nvm3_deleteObject(nvm3_Handle_t*, nvm3_ObjectKey_t);
bool nvm3_repackNeeded(nvm3_Handle_t*); Ecode_t nvm3_repack(nvm3_Handle_t*);
