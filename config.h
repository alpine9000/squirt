#pragma once

#ifdef SQUIRT_CONFIG

int
config_load(void);

void
config_main(int argc, char* argv[]);

const char *
config_getAlias(const char* hostname);

void
config_patchDefaultHostname(int* argc, char*** argv);

#else

#define config_load()
#define config_main(c, v)
#define config_patchDefaultHostname(c, v)
#define config_getAlias(hostname) (hostname)

#endif
