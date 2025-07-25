#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "main.h"

#ifdef SQUIRT_CONFIG

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include "win_compat.h"
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#include <pwd.h>
#include "errno.h"
#endif

#define CONFIG_STRING_MAX  256
#define CONFIG_MAX_ALIASES 32

typedef enum {
  CONFIG_INT,
  CONFIG_FLOAT,
  CONFIG_STRING,
  CONFIG_STRING_PAIRS  // new
} config_type_t;

typedef struct {
  const char* key;
  const char* value;
} string_pair_t;

typedef struct {
  const char* name;        // config key
  config_type_t type;      // type of value
  void* value;             // pointer to variable or array
  size_t* count;           // optional count pointer (only for list types)
} config_item_t;

struct {
  char defaultHostname[CONFIG_STRING_MAX];
  string_pair_t aliases[CONFIG_MAX_ALIASES];
  size_t aliasCount;
} config = {0};

config_item_t config_items[] = {
  {"default", CONFIG_STRING, &config.defaultHostname, 0},
  { "alias", CONFIG_STRING_PAIRS, config.aliases, &config.aliasCount },
};

static const char*
config_getPath(void)
{
  static char path[1024];
  const char* appname = "squirt";
  const char* filename = "config.ini";
  
#ifdef _WIN32
  char appdata[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
    snprintf(path, sizeof(path), "%s\\%s\\%s", appdata, appname, filename);
    return path;
  }
#elif defined(__APPLE__)
  const char* home = getenv("HOME");
  if (!home) home = getpwuid(getuid())->pw_dir;
  snprintf(path, sizeof(path), "%s/Library/Application Support/%s/%s", home, appname, filename);
  return path;
#elif defined(__linux__)
  const char* xdg_config = getenv("XDG_CONFIG_HOME");
  if (!xdg_config) {
    const char* home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    snprintf(path, sizeof(path), "%s/.config/%s/%s", home, appname, filename);
  } else {
    snprintf(path, sizeof(path), "%s/%s/%s", xdg_config, appname, filename);
  }
  return path;
#endif
  
  return NULL; // Unsupported platform
}

#ifdef __linux
static int
config_ensureXDG(void)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  
  const char *path = NULL;
  
  if (xdg && *xdg) {
    path = xdg;
  } else if (home && *home) {
    static char fallback[512];
    snprintf(fallback, sizeof(fallback), "%s/.config", home);
    path = fallback;
  } else {
    fprintf(stderr, "No XDG_CONFIG_HOME or HOME set\n");
    return 0;
  }
  
  if (mkdir(path, 0755) != 0 && errno != EEXIST) {
    perror("mkdir failed");
    return 0;
  }
  
  return 1;
}
#endif

static void
config_createParentDirectory(const char* filepath)
{
#ifdef __linux
  config_ensureXDG();
#endif
  char dirbuf[1024];
  strncpy(dirbuf, filepath, sizeof(dirbuf));
  dirbuf[sizeof(dirbuf) - 1] = 0;
  
#ifdef _WIN32
  char* last_slash = strrchr(dirbuf, '\\');
#else
  char* last_slash = strrchr(dirbuf, '/');
#endif
  
  if (last_slash) {
    *last_slash = 0; // terminate at parent dir
    
#ifdef _WIN32
    CreateDirectoryA(dirbuf, NULL);
#else
    mkdir(dirbuf, 0755); // silently fails if already exists
#endif
  }
}

static int
config_save(void)
{
  const char* path = config_getPath();
  config_createParentDirectory(path);
  FILE* f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "Failed to open config file %s: ", path);
    perror("");
    return 0;
  }

  for (int i = 0; i < countof(config_items); i++) {
    config_item_t* item = &config_items[i];
    switch (item->type) {
    case CONFIG_INT:
      fprintf(f, "%s = %d\n", item->name, *(int*)item->value);
      break;
    case CONFIG_FLOAT:
      fprintf(f, "%s = %f\n", item->name, *(float*)item->value);
      break;
    case CONFIG_STRING:
      fprintf(f, "%s = %s\n", item->name, (char*)item->value);
      break;
    case CONFIG_STRING_PAIRS: {
      if (!item->count) break;
      string_pair_t* pairs = (string_pair_t*)item->value;
      for (size_t j = 0; j < *item->count; j++) {
        fprintf(f, "%s = %s,%s\n", item->name, pairs[j].key, pairs[j].value);
      }
      break;
    }
    }
  }

  fclose(f);
  return 1;
}

int
config_load(void)
{
  const char* path = config_getPath();
  FILE* f = fopen(path, "r");
  if (!f) return 0;

  char line[CONFIG_STRING_MAX*2];
  while (fgets(line, sizeof(line), f)) {
    char key[CONFIG_STRING_MAX], val[CONFIG_STRING_MAX];
    
    if (sscanf(line, " %255[^= ] %*[= ] %255[^\n]", key, val) != 2)
      continue;
    
    
    for (int i = 0; i < countof(config_items); i++) {
      config_item_t* item = &config_items[i];

      if (strcmp(item->name, key) == 0) {
        switch (item->type) {
        case CONFIG_INT:
          *(int*)item->value = atoi(val);
          break;
        case CONFIG_FLOAT:
          *(float*)item->value = (float)atof(val);
          break;
        case CONFIG_STRING:
          strlcpy((char*)item->value, val, CONFIG_STRING_MAX);
          break;
        case CONFIG_STRING_PAIRS: {
          // Parse format: "key:value"
          char* sep = strchr(val, ',');
          if (sep && item->count && *item->count < CONFIG_MAX_ALIASES) {
            *sep = 0;
            char* alias_key = val;
            char* alias_val = sep + 1;

            // strip whitespace
            while (*alias_key == ' ') alias_key++;
            while (*alias_val == ' ') alias_val++;

            string_pair_t* pairs = (string_pair_t*)item->value;
            size_t idx = (*item->count)++;

            pairs[idx].key = strdup(alias_key);
            pairs[idx].value = strdup(alias_val);
          }
          break;
        }
        }
        break;
      }
    }
  }

  fclose(f);
  return 1;
}

static const char*
config_getDefaultHost(void)
{
  if (strlen(config.defaultHostname)) {
    return config.defaultHostname;
  }
  return NULL;
}

static void
config_setDefaultHost(const char* hostname)
{
  if (hostname) {
    strlcpy(config.defaultHostname, hostname, sizeof(config.defaultHostname));
    config_save();
  }
}

static int
config_addAlias(const char* from, const char* to)
{
  for (size_t i = 0; i < config.aliasCount; i++) {
    if (config.aliases[i].value && config.aliases[i].key) {    
      if (strcmp(config.aliases[i].key, from) == 0) {
	free((void*)config.aliases[i].value);
	config.aliases[i].value = strdup(to);
	config_save();
	return 1;
      }
    }
  }
    
  if (config.aliasCount < countof(config.aliases)) {    
    config.aliases[config.aliasCount].key = strdup(from);
    config.aliases[config.aliasCount].value = strdup(to);
    config.aliasCount++;
    config_save();    
    return 1;
  }
  
  return 0;
}


static int
config_rmAlias(const char* from)
{
  const size_t total = config.aliasCount;
  for (size_t i = 0, j = 0; i < total; i++) {
    if (config.aliases[i].value && config.aliases[i].key) {    
      if (strcmp(config.aliases[i].key, from) != 0) {
	config.aliases[i].key = config.aliases[j].key;
	config.aliases[i].value = config.aliases[j++].value;	
      } else {
	printf("Removing alias %s\n", from);
	config.aliasCount--;
      }
    }
  }

  config_save();
      
  return 0;
}

static void
config_listAlias(void)
{
  for (size_t i = 0; i < config.aliasCount; i++) {
    printf("  alias hostname: %s => %s\n", config.aliases[i].key, config.aliases[i].value);
  }
}

const char *
config_getAlias(const char* hostname)
{
  for (size_t i = 0; i < config.aliasCount; i++) {
    if (strcmp(hostname, config.aliases[i].key) == 0) {
      return config.aliases[i].value;
    }
  }

  return hostname;
}

static void
config_dump(void)
{
  int n = printf("\nsquirt config (%s)\n", config_getPath());
  for (int i = 0; i < n-1; i++) {
    printf("=");
  }
  printf("\n");
  printf("default hostname: %s\n", config.defaultHostname[0] == 0 ? "--== no default ==--" : config.defaultHostname);
  config_listAlias();
  for (int i = 0; i < n-1; i++) {
    printf("=");
  }
  printf("\n");
}

void
config_main(int argc, char* argv[])
{
  const char* defaultHostname = config_getDefaultHost();
  
  if (argc == 2) {
    if (strcmp(argv[1], "--clear-default") == 0) {
      config_setDefaultHost("");
      printf("default hostname: --== no default ==--\n");
      return;
    } else if (strcmp(argv[1], "--alias") == 0) {
      config_listAlias();
      return;
    } else if (strcmp(argv[1], "--default") == 0) {
      printf("default hostname: %s\n", config.defaultHostname);
      return;
    } else if (strcmp(argv[1], "--show-config") == 0) {
      config_dump();
      return;
    }       
  } else if (argc == 3) {
    if (strcmp(argv[1], "--default") == 0) {
      config_setDefaultHost(argv[2]);
      defaultHostname = config_getDefaultHost();
      printf("default hostname: %s\n", defaultHostname);
      return;
    } else if (strcmp(argv[1], "--remove-alias") == 0) {
      config_rmAlias(argv[2]);
      return;
    }
  } else if (argc == 4) {
    if (strcmp(argv[1], "--alias") == 0) {
      config_addAlias(argv[2], argv[3]);
      printf("Added alias: %s=%s\n", argv[2], argv[3]);
      return;
    }
  }

  config_dump();
  fatalError("usage: %s [--clear-default] [--default hostname] --alias [alias hostname] --remove-alias [alias] --show-config", main_argv0);
 
}

static int
config_patchArgv(int* argc, char*** argv, const char* insert_str, int insert_pos)
{
  if (insert_pos < 0 || insert_pos > *argc) return 0;

  int new_argc = *argc + 1;
  char** new_argv = malloc(sizeof(char*) * (new_argc + 1)); // +1 for NULL

  if (!new_argv) return 0;

  for (int i = 0, j = 0; j < new_argc; j++) {
    if (j == insert_pos) {
      new_argv[j] = strdup(insert_str);
      if (!new_argv[j]) goto fail;
    } else {
      new_argv[j] = strdup((*argv)[i++]);
      if (!new_argv[j]) goto fail;
    }
  }

  new_argv[new_argc] = NULL;

  *argv = new_argv;
  *argc = new_argc;
  return 1;

fail:
  for (int k = 0; k < new_argc; k++) {
    if (new_argv[k]) free(new_argv[k]);
  }
  free(new_argv);
  return 0;
}


void
config_patchDefaultHostname(int* argc, char*** argv)
{
  const char* defaultHostname = 0;
  if ((*argc > 1 && !util_isHostname((*argv)[1])) || *argc == 1) {
    defaultHostname = config_getDefaultHost();    
    if (defaultHostname) {
      config_patchArgv(argc, argv, defaultHostname, 1);
    }
  }
}
#endif
