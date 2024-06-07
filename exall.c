#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <sys/stat.h>

#include "main.h"
#include "util.h"
#include "dir.h"
#include "exall.h"

static char*
exall_scanString(FILE* fp)
{
  char buffer[108] = {0};
  int c = fscanf(fp, "%*[^:]:%107[^\n]%*c", buffer);
  if (c == 1) {
    char* str = malloc(strlen(buffer)+1);
    if (str) {
      strcpy(str, buffer);
    }
    return str;
  } else {
    return 0;
  }
}


static int
exall_scanInt(FILE* fp)
{
  int number = 0;
  if (fscanf(fp, "%*[^:]:%d%*c", &number) == 1) {
    return number;
  } else {
    return -1;
  }
}


static char*
exall_scanComment(FILE* fp)
{
  char buffer[128] = {0};
  int len;
  char c;

  do {
    len = fread(&c, 1, 1, fp);
  } while (len > 0 && c != ':');

  size_t i = 0;
  do {
    len = fread(&buffer[i], 1, 1, fp);
    i++;
  } while (len > 0 && i < sizeof(buffer));

  char* comment = 0;
  if (strlen(buffer) > 0) {
    comment = malloc(strlen(buffer)+1);
    if (comment) {
      strcpy((char*)comment, buffer);
    }
  }

  return comment;
}


int
exall_saveExAllData(dir_entry_t* entry, const char* path)
{
  const char* baseName = util_amigaBaseName(path);
  const char* ident = SQUIRT_EXALL_INFO_DIR_NAME;
  util_mkdir(ident, 0777);
  char* name = malloc(strlen(baseName)+1+strlen(ident));
  if (!name) {
    return 0;
  }
  sprintf(name, "%s%s", ident, baseName);
  FILE *fp = fopen(name, "w");

  if (!fp) {
    free(name);
    return 0;
  }


  struct timeval tv ;
  int sec = entry->ds.ticks / 50;
  tv.tv_sec = (DIR_AMIGA_EPOC_ADJUSTMENT_DAYS*24*60*60)+(entry->ds.days*(24*60*60)) + (entry->ds.mins*60) + sec;
  tv.tv_usec = (entry->ds.ticks - (sec * 50)) * 200;
  time_t _time = tv.tv_sec;

  struct tm *tm = gmtime(&_time);
  struct utimbuf ut;

  struct stat st;

  if (stat(baseName, &st) != 0) {
    fatalError("failed to get file attributes of %s\n", baseName);
  }

  ut.actime = st.st_atime;
  ut.modtime = mktime(tm);

  if (
#ifdef _WIN32
      !S_ISDIR(st.st_mode) &&
#endif
      utime(baseName, &ut) != 0) {
    fatalError("failed to set file attributes of %s\n", baseName);
  }

  fprintf(fp, "name:%s\n", entry->name);
  fprintf(fp, "type:%d\n", entry->type);
  fprintf(fp, "size:%d\n", entry->size);
  fprintf(fp, "prot:%d\n", entry->prot);
  fprintf(fp, "days:%d\n", entry->ds.days);
  fprintf(fp, "mins:%d\n", entry->ds.mins);
  fprintf(fp, "ticks:%d\n", entry->ds.ticks);
  if (entry->comment) {
    fprintf(fp, "comment:%s", entry->comment);
  } else {
    fprintf(fp, "comment:");
  }
  fclose(fp);

  free(name);
  return 1;
}



int
exall_readExAllData(dir_entry_t* entry, const char* path)
{
   if (!entry) {
    fatalError("readExAllData called with null entry");
  }
  const char* baseName = util_amigaBaseName(path);
  const char* ident = SQUIRT_EXALL_INFO_DIR_NAME;
  util_mkdir(ident, 0777);
  char* name = malloc(strlen(baseName)+1+strlen(ident));
  if (!name) {
    return 0;
  }
  sprintf(name, "%s%s", ident, baseName);
  FILE *fp = fopen(name, "r+");
  if (!fp) {
    char* cwd = getcwd(0, 0);
    if (!cwd) {
      perror("cwd failed");
    }
    fprintf(stderr, "unable to open %s cwd = %s\n", name, cwd);
    free(name);
    return 0;
  }
  entry->name = exall_scanString(fp);
  entry->type = exall_scanInt(fp);
  entry->size = exall_scanInt(fp);
  entry->prot = exall_scanInt(fp);
  entry->ds.days = exall_scanInt(fp);
  entry->ds.mins = exall_scanInt(fp);
  entry->ds.ticks = exall_scanInt(fp);
  entry->comment = exall_scanComment(fp);

  fclose(fp);
  free(name);

  return 1;
}


int
exall_identicalExAllData(dir_entry_t* one, dir_entry_t* two)
{

  int commentIdentical =
    ((one->comment != 0 && strstr(one->comment, "Erstellt") != NULL) &&
     (two->comment != 0 && strstr(two->comment, "Erstellt") != NULL))
    ||
    ((one->comment == 0 && two->comment == 0)|| (one->comment != 0 && two->comment != 0 && strcmp(one->comment, two->comment) == 0));

  int nameIdentical = one->name != NULL && two->name != NULL && strcmp(one->name, two->name) == 0;
  int typeIdentical = one->type == two->type;
  int sizeIdentical = one->size == two->size;
  int protIdentical = one->prot == two->prot;
  int daysIdentical = one->ds.days == two->ds.days;
  int minsIdentical = one->ds.mins == two->ds.mins;
  int ticksIdentical = one->ds.ticks == two->ds.ticks;

  int identical =
    nameIdentical &&
    typeIdentical &&
    sizeIdentical &&
    protIdentical &&
    daysIdentical &&
    minsIdentical &&
    ticksIdentical &&
    commentIdentical;


#if 0
  if (!identical) {
    printf("name: %d, type: %d, size: %d, prot: %d, days: %d, mins: %d, ticks: %d, comment: %d\n",
	   nameIdentical, typeIdentical, sizeIdentical, protIdentical, daysIdentical, minsIdentical, ticksIdentical, commentIdentical);

    printf("name: >%s<>%s< %d\n", one->name, two->name, strcmp(one->name, two->name) );
    printf("comment: %d\n", commentIdentical);
    printf("comment: %d %d\n", one->comment == 0, two->comment == 0);
    if (one->comment != 0) {
      printf("comment 1:>%s<\n", one->comment);
    }
    if (two->comment != 0) {
      printf("comment 2:>%s<\n", two->comment);
    }
    printf("type %d %d %d\n", one->type, two->type, one->type == two->type);
    printf("size %d %d %d\n", one->size, two->size, one->size == two->size);
    printf("prot %d %d %d\n", one->prot, two->prot, one->prot == two->prot);
    printf("days %d %d %d\n", one->ds.days, two->ds.days, one->ds.days == two->ds.days);
    printf("mins %d %d %d\n", one->ds.mins, two->ds.mins, one->ds.mins == two->ds.mins);
    printf("ticks %d %d %d\n", one->ds.ticks, two->ds.ticks, one->ds.ticks == two->ds.ticks);
  }
#endif

  return identical;
}
