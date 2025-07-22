#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#include "main.h"
#include "common.h"
#include "argv.h"
#include "srl.h"
#include "exec.h"

typedef struct hostfile {
  char* localFilename;
  char* backupFilename;
  char** argv;
  char* remoteFilename;
  struct hostfile* next;
  dir_datestamp_t dateStamp;
  uint32_t remoteProtection;
} cli_hostfile_t;

// Forward declarations
static int cli_isLocalFileArgumentForExecution(const char* arg);
static char* cli_expandLocalPath(const char* path);

static char* cli_currentDir = 0;
static dir_entry_list_t *cli_dirEntryList = 0;
static char* cli_readLineBase = 0;
static char* cli_reconstructedText = 0;
static int cli_isLocalCompletion = 0;  // Flag for local filesystem completion

void
cli_cleanup(void)
{
  srl_write_history();

  if (cli_readLineBase) {
    free(cli_readLineBase);
    cli_readLineBase = 0;
  }

  dir_freeEntryLists();
  cli_dirEntryList = 0;

  if (cli_currentDir) {
    free(cli_currentDir);
    cli_currentDir = 0;
  }
}


static int
cli_changeDir(const char* dir)
{
  if (cli_currentDir) {
    free(cli_currentDir);
    cli_currentDir = 0;
  }

  cli_currentDir = malloc(strlen(dir)+1);
  strcpy(cli_currentDir, dir);
  return 1;
}


static const char*
cli_prompt(void)
{
  static char buffer[256];
  snprintf(buffer, sizeof(buffer), "1.%s> ", cli_currentDir);
  return buffer;
}


static int
cli_compareFile(const char* one, const char* two)
{
  int identical = 1, fd1 = -1, fd2 = -1;

  if (one == NULL && two == NULL) {
    identical = 1;
    goto cleanup;
  }

  if (one) {
    fd1 = open(one, O_RDONLY|_O_BINARY);
  }

  if (two) {
    fd2 = open(two, O_RDONLY|_O_BINARY);
  }

  if (fd1 == -1 && fd2 == -1) {
    identical = 1;
    goto cleanup;
  } else if (fd1 == -1 || fd2 == -1) {
    identical = 0;
    goto cleanup;
  }

  unsigned char c1, c2;
  int r1, r2;
  do {
    r1 = read(fd1, &c1, sizeof(c1));
    r2 = read(fd2, &c2, sizeof(c2));
    if (r1 != r2 || c1 != c2) {
      identical = 0;
      goto cleanup;
    }
  } while (r1 && r2);

 cleanup:

  if (fd1 >= 0) {
    close(fd1);
  }

  if (fd2 >= 0) {
    close(fd2);
  }

  return identical;
}


static char*
cli_duplicateFile(const char* from)
{
  int toLength = strlen(from) + strlen(".orig") + 1;
  char *to = malloc(toLength);
  snprintf(to, toLength, "%s.orig", from);

  int fd_to, fd_from;
  char buf[4096];
  ssize_t nread;
  int saved_errno;

  fd_from = open(from, O_RDONLY|_O_BINARY);
  if (fd_from < 0) {
    free(to);
    return 0;
  }

  fd_to = open(to, O_TRUNC|O_WRONLY|O_CREAT|_O_BINARY , 0666);
  if (fd_to < 0) {
    goto out_error;
  }

  while (nread = read(fd_from, buf, sizeof buf), nread > 0) {
    char *out_ptr = buf;
    ssize_t nwritten;

    do {
      nwritten = write(fd_to, out_ptr, nread);

      if (nwritten >= 0) {
	nread -= nwritten;
	out_ptr += nwritten;
      } else if (errno != EINTR) {
	goto out_error;
      }
    } while (nread > 0);
  }

  if (nread == 0) {
    if (close(fd_to) < 0) {
      fd_to = -1;
      goto out_error;
    }
    close(fd_from);

    /* Success! */
    return to;
  }

 out_error:
  saved_errno = errno;

  if (to) {
    free(to);
  }

  close(fd_from);
  if (fd_to >= 0)
    close(fd_to);

  errno = saved_errno;
  return 0;
}


static char*
cli_replaceChar(char* str, char find, char replace)
{
  char *current_pos = strchr(str,find);
  while (current_pos){
    *current_pos = replace;
    current_pos = strchr(current_pos,find);
  }
  return str;
}


static void
cli_freeHostFile(cli_hostfile_t* file)
{
  if (file) {
    if (file->localFilename) {
      unlink(file->localFilename);
      free(file->localFilename);
    }
    if (file->backupFilename) {
      unlink(file->backupFilename);
      free(file->backupFilename);
    }
    if (file->remoteFilename) {
      free(file->remoteFilename);
    }
    free(file);
  }
}


static cli_hostfile_t*
cli_convertFileToHost(cli_hostfile_t** list, const char* remote)
{
  cli_hostfile_t *file = calloc(1, sizeof(cli_hostfile_t));
  
  // Strip quotes from remote filename if present
  char* unquotedRemote = NULL;
  int remoteLen = strlen(remote);
  if ((remote[0] == '"' && remote[remoteLen-1] == '"' && remoteLen > 1) ||
      (remote[0] == '\'' && remote[remoteLen-1] == '\'' && remoteLen > 1)) {
    // Remove quotes
    unquotedRemote = malloc(remoteLen - 1);
    strncpy(unquotedRemote, remote + 1, remoteLen - 2);
    unquotedRemote[remoteLen - 2] = '\0';
    file->remoteFilename = unquotedRemote;
  } else {
    file->remoteFilename = strdup(remote);
  }
  
  char* local = strdup(file->remoteFilename);
  cli_replaceChar(local, ':', '/');

#ifdef _WIN32
  // On Windows, convert forward slashes to backslashes for proper path handling
  cli_replaceChar(local, '/', '\\');
#endif

  // Apply Windows reserved name handling to the local filename
  char* safeName = util_safeName(local);
  free(local);
  local = safeName;

  const char *tempPath = util_getTempFolder();
  int localFilenameLength = strlen(local) + strlen(tempPath) + 1;
  file->localFilename = malloc(localFilenameLength);
  snprintf(file->localFilename, localFilenameLength, "%s%s", tempPath, local);
  free(local);
  util_mkpath(file->localFilename);

  memset(&file->dateStamp, 0, sizeof(file->dateStamp));
  int error = squirt_suckFile(file->remoteFilename, 0, 0, file->localFilename, &file->remoteProtection);
  if (error == -ERROR_SUCK_ON_DIR) {
    fprintf(stderr, "error: failed to access remote directory %s\n", file->remoteFilename);
    free(file);
    return 0;
  } else {
    if (error < 0) {
      unlink(file->localFilename);
    }
    file->backupFilename = cli_duplicateFile(file->localFilename);
    if (*list) {
      cli_hostfile_t* ptr = *list;
      while (ptr->next) {
	ptr = ptr->next;
      }
      ptr->next = file;
    } else {
      *list = file;
    }
  }

  return file;
}


static int
cli_saveFileIfModified(cli_hostfile_t* file)
{
  int success = 0;
  
  // Check if the local file still exists (might have been deleted by local command)
  struct stat st;
  if (stat(file->localFilename, &st) != 0) {
    // File was deleted by local command, skip upload
    return 1; // Consider this "successful" - the file was intentionally removed
  }
  
  if (!file->backupFilename) {
    return 1;
  }
  
  int comparison = cli_compareFile(file->localFilename, file->backupFilename);
  
  if (comparison == 0) {
    success = squirt_file(file->localFilename, 0, file->remoteFilename, 1, 0) == 0;
    if (success) {
      success = protect_file(file->remoteFilename, file->remoteProtection, 0);
    }
  } else {
    success = 1; // No upload needed, but this is "successful"
  }

  return success;
}

static int
cli_hostCommand(int argc, char** argv)
{
  int success = 0;
  (void)argc,(void)argv;

  // Preserve original arguments BEFORE any processing
  char** originalArgv = malloc(sizeof(char*)*(argc+1));
  for (int i = 0; i < argc; i++) {
    originalArgv[i] = argv[i];
  }
  originalArgv[argc] = 0;

  cli_hostfile_t* list = 0;



  // Store original remote source path for protection bits (BEFORE argument processing)
  char* originalSourcePath = NULL;
  char* baseCommand = argv[0][0] == '!' ? &argv[0][1] : argv[0];
  int isCopyCommand = (strcmp(baseCommand, "cp") == 0 || strcmp(baseCommand, "copy") == 0);
  int destinationIndex = isCopyCommand && argc > 2 ? argc - 1 : -1;
  
  // For copy commands, store original source path before it gets modified
  if (isCopyCommand && argc >= 3) {
    int sourceIsLocal = cli_isLocalFileArgumentForExecution(argv[1]);
    int sourceIsRemote = !sourceIsLocal && argv[1][0] != '-';
    if (sourceIsRemote) {
      originalSourcePath = strdup(argv[1]);
    }
  }
  
  // Skip pre-download for copy commands that will be handled by hybrid copy logic
  if (!isCopyCommand) {
    for (int i = 1; i < argc; i++) {
      int isLocal = cli_isLocalFileArgumentForExecution(argv[i]);
      int isDestination = (i == destinationIndex);
      
      // Skip local files, destinations, command options, and shell operators
      if (!isLocal && !isDestination &&
          argv[i][0] != '-' && argv[i][0] != '|' && argv[i][0] != '>') {
        // This is a remote source file - transfer it to local temp directory
        cli_hostfile_t* hostFile = cli_convertFileToHost(&list, argv[i]);
        if (hostFile) {
	  hostFile->argv = &argv[i];
	  argv[i] = hostFile->localFilename;
        } else {
	  goto error;
        }
      }
    }
  }

  char** newArgv = malloc(sizeof(char*)*(argc+1));
  char** expandedPaths = malloc(sizeof(char*)*(argc+1));
  int expandedCount = 0;

  for (int i = 0; i < argc; i++) {
    if (i == 0) {
      // Strip the '!' prefix from the command
      newArgv[i] = argv[i][0] == '!' ? &argv[i][1] : argv[i];
    } else if (cli_isLocalFileArgumentForExecution(argv[i])) {
      char* expandedPath = cli_expandLocalPath(argv[i]);
      if (expandedPath) {
        newArgv[i] = expandedPath;
        expandedPaths[expandedCount++] = expandedPath; // Track for cleanup
      } else {
        newArgv[i] = argv[i];
      }
    } else {
      newArgv[i] = argv[i];
    }
  }
  newArgv[argc] = 0;

  // Check for hybrid operations (local-to-remote, remote-to-remote) using ORIGINAL arguments
  int sourceIsLocal = 0, sourceIsRemote = 0, destIsRemote = 0;
  char* sourcePath = NULL;
  char* destPath = NULL;
  if (isCopyCommand && argc == 3) {
    sourceIsLocal = cli_isLocalFileArgumentForExecution(originalArgv[1]);
    sourceIsRemote = !sourceIsLocal && originalArgv[1][0] != '-';
    destIsRemote = !cli_isLocalFileArgumentForExecution(originalArgv[2]) && originalArgv[2][0] != '-';

    
    if ((sourceIsLocal && destIsRemote) || (sourceIsRemote && destIsRemote) || (sourceIsRemote && !destIsRemote)) {
      
      // Get the source file path (either expanded local or downloaded temp file)
      sourcePath = newArgv[1];
      destPath = originalArgv[2];
      
      // Strip quotes from remote path if present (only for properly formed quoted strings)
      char unquotedRemotePath[PATH_MAX];
      int remotePathLen = strlen(destPath);
      int hasValidQuotes = 0;
      
      // Check for properly formed quoted strings
      if (remotePathLen >= 2) {
        char quoteChar = destPath[0];
        if ((quoteChar == '"' || quoteChar == '\'') && destPath[remotePathLen-1] == quoteChar) {
          // Check that the middle content doesn't contain the same quote character
          hasValidQuotes = 1;
          for (int i = 1; i < remotePathLen - 1; i++) {
            if (destPath[i] == quoteChar) {
              hasValidQuotes = 0; // Found quote character in the middle - invalid
              break;
            }
          }
        }
      }
      
      if (hasValidQuotes) {
        // Remove quotes from properly formed quoted string
        strncpy(unquotedRemotePath, destPath + 1, remotePathLen - 2);
        unquotedRemotePath[remotePathLen - 2] = '\0';
      } else {
        // Check for obviously malformed quote patterns that should be blocked
        int hasOnlyQuotes = 1;
        for (int i = 0; i < remotePathLen; i++) {
          if (destPath[i] != '"' && destPath[i] != '\'') {
            hasOnlyQuotes = 0;
            break;
          }
        }
        
        if (hasOnlyQuotes && remotePathLen > 0) {
          printf("Error: Invalid destination '%s' - contains only quote characters\n", destPath);
          printf("Hint: Use properly quoted strings like \"filename\" or unquoted filenames\n");
          goto cleanup;
        }
        
        strcpy(unquotedRemotePath, destPath);
      }
      // Extract filename for destination path construction
      char* filename;
      if (sourceIsLocal) {
        // For local files, extract filename from the original argument (not expanded path)
        char* originalLocalPath = originalArgv[1];
        // Skip the '!' prefix if present
        if (originalLocalPath[0] == '!') {
          originalLocalPath++;
        }
        // Handle both Unix-style (/) and Windows-style (\) path separators
        char* lastSlash = strrchr(originalLocalPath, '/');
        char* lastBackslash = strrchr(originalLocalPath, '\\');
        filename = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
        if (filename) {
          filename++; // Skip the path separator
        } else {
          filename = originalLocalPath; // No path separator, use whole string
        }
      } else {
        // For remote files, extract filename from original remote path (originalArgv[1])
        char* originalRemotePath = originalArgv[1];
        filename = strrchr(originalRemotePath, ':');
        if (filename) {
          filename++; // Skip the ':'
        } else {
          filename = originalRemotePath; // No colon, use whole string
        }
      }
      
      // Validate Amiga destination path
      if (destIsRemote) {
        // Check for invalid Unix-style paths as Amiga destinations
        // Note: "/" is valid in AmigaOS (parent directory), so we don't block it
        if (strcmp(unquotedRemotePath, "./") == 0 || 
            strcmp(unquotedRemotePath, ".") == 0 || strcmp(unquotedRemotePath, "../") == 0) {
          printf("Error: '%s' is not a valid Amiga destination\n", unquotedRemotePath);
          printf("Hint: Use Amiga-style paths like:\n");
          printf("  - Current directory: Use just the filename (e.g., 'cli.c') or double quotes (\"\")\n");
          printf("  - Parent directory: Use '/'\n");
          printf("  - Specific volume: Use 'Work:cli.c' or 'RAM:cli.c'\n");
          printf("  - Directory: Use 'Work:MyDir/cli.c'\n");
          goto cleanup;
        }
      }
      
      // Construct full destination path
      char fullDestPath[PATH_MAX];
      size_t destLen = strlen(unquotedRemotePath);
      if (destLen == 0) {
        // Empty string destination ("" in AmigaOS means current directory)
        // Just use the filename in current directory
        strncpy(fullDestPath, filename, sizeof(fullDestPath)-1);
        fullDestPath[sizeof(fullDestPath)-1] = '\0';
      } else if (destLen > 0 && (unquotedRemotePath[destLen-1] == ':' || unquotedRemotePath[destLen-1] == '/')) {
        // Directory destination (ends with : or /), append filename
        snprintf(fullDestPath, sizeof(fullDestPath), "%s%s", unquotedRemotePath, filename);
      } else {
        // File destination, use as-is
        strncpy(fullDestPath, unquotedRemotePath, sizeof(fullDestPath)-1);
        fullDestPath[sizeof(fullDestPath)-1] = '\0';
      }
      
      // For local-to-remote operations, read original protection bits BEFORE transfer
      uint32_t originalProtectionBits = 0;
      int hasOriginalProtectionBits = 0;
      if (sourceIsLocal && destIsRemote) {
        char dirPath[PATH_MAX];
        char* fileName;
        

        
        // Extract directory and filename from destination path
        char* lastColon = strrchr(fullDestPath, ':');
        if (lastColon) {
          // Amiga-style path (e.g., "Work:readme.txt")
          size_t dirLen = lastColon - fullDestPath + 1; // Include the colon
          strncpy(dirPath, fullDestPath, dirLen);
          dirPath[dirLen] = '\0';
          fileName = lastColon + 1;
        } else {
          // No colon found, assume current directory
          strcpy(dirPath, "");
          fileName = fullDestPath;
        }
        
        dir_entry_list_t* dirInfo = dir_read(dirPath);
        
        if (dirInfo) {
          // Search for the specific file in the directory listing
          dir_entry_t* entry = dirInfo->head;
          
          while (entry) {
            if (strcmp(entry->name, fileName) == 0) {
              originalProtectionBits = entry->prot;
              hasOriginalProtectionBits = 1;
              break;
            }
            entry = entry->next;
          }
          
          dir_freeEntryList(dirInfo);
        }
      }
      
      // Use squirt to transfer the file
      if (sourceIsRemote && !destIsRemote) {
        // Remote-to-local: download using squirt_suckFile
        // Strip quotes from source path if present
        char unquotedSourcePath[PATH_MAX];
        char* remoteSourcePath = originalArgv[1];
        int sourcePathLen = strlen(remoteSourcePath);
        if ((remoteSourcePath[0] == '"' && remoteSourcePath[sourcePathLen-1] == '"' && sourcePathLen > 1) ||
            (remoteSourcePath[0] == '\'' && remoteSourcePath[sourcePathLen-1] == '\'' && sourcePathLen > 1)) {
          // Remove quotes
          strncpy(unquotedSourcePath, remoteSourcePath + 1, sourcePathLen - 2);
          unquotedSourcePath[sourcePathLen - 2] = '\0';
          remoteSourcePath = unquotedSourcePath;
        }
        
        char* localDestPath = cli_expandLocalPath(originalArgv[2]);
        
        // Check for empty destination path
        if (!localDestPath || strlen(localDestPath) == 0) {
          printf("Error: No destination path specified\n");
          if (localDestPath && localDestPath != originalArgv[2]) {
            free(localDestPath);
          }
          return 0;
        }
        
        // Check for root directory write attempt
        if (strcmp(localDestPath, "/") == 0) {
          printf("Error: Write denied on root directory\n");
          if (localDestPath != originalArgv[2]) {
            free(localDestPath);
          }
          return 0;
        }
        
        // Check if destination is a directory and append filename if needed
        char finalDestPath[PATH_MAX];
        int localDestLen = strlen(localDestPath);
        if (localDestLen > 0 && (localDestPath[localDestLen-1] == '/' || localDestPath[localDestLen-1] == '\\')) {
          // Directory destination - extract filename from remote source and append
          // Look for the last directory separator first, then volume separator
          char* sourceFilename = strrchr(remoteSourcePath, '/');
          if (sourceFilename) {
            sourceFilename++; // Skip the '/'
          } else {
            sourceFilename = strrchr(remoteSourcePath, ':');
            if (sourceFilename) {
              sourceFilename++; // Skip the ':'
            } else {
              sourceFilename = remoteSourcePath; // No path separator, use whole string
            }
          }
          
          // Apply Windows reserved name handling to the extracted filename
          char* safeFilename = util_safeName(sourceFilename);
          if (!safeFilename) {
            printf("Error: Memory allocation failed for safe filename\n");
            if (localDestPath != originalArgv[2]) {
              free(localDestPath);
            }
            return 0;
          }
          
          snprintf(finalDestPath, sizeof(finalDestPath), "%s%s", localDestPath, safeFilename);
          free(safeFilename);
        } else {
          // File destination, use as-is
          strncpy(finalDestPath, localDestPath, sizeof(finalDestPath)-1);
          finalDestPath[sizeof(finalDestPath)-1] = '\0';
        }
        
        uint32_t protection;
        success = squirt_suckFile(remoteSourcePath, 0, 0, finalDestPath, &protection) >= 0;
        if (localDestPath != originalArgv[2]) {
          free(localDestPath);
        }
      } else {
        // Local-to-remote or remote-to-remote: upload using squirt_file

        success = squirt_file(sourcePath, 0, fullDestPath, 1, 0) == 0;
      }
      
      if (success) {
        // For remote-to-remote operations, preserve protection bits from source
        // For local-to-remote operations, preserve protection bits from existing destination
        if (sourceIsRemote && destIsRemote) {
          
          // Query source file protection bits by listing its directory
          char* sourceFilePath = originalSourcePath ? originalSourcePath : argv[1];
          char dirPath[PATH_MAX];
          char* fileName;
          
          // Extract directory and filename from source path
          char* lastColon = strrchr(sourceFilePath, ':');
          if (lastColon) {
            // Amiga-style path (e.g., "S:Startup-sequence")
            size_t dirLen = lastColon - sourceFilePath + 1; // Include the colon
            strncpy(dirPath, sourceFilePath, dirLen);
            dirPath[dirLen] = '\0';
            fileName = lastColon + 1;
          } else {
            // No colon found, assume current directory
            strcpy(dirPath, "");
            fileName = sourceFilePath;
          }
          
          dir_entry_list_t* dirInfo = dir_read(dirPath);
          
          if (dirInfo) {
            // Search for the specific file in the directory listing
            dir_entry_t* entry = dirInfo->head;
            int found = 0;
            
            while (entry && !found) {
              if (strcmp(entry->name, fileName) == 0) {
                uint32_t sourceProtection = entry->prot;
                
                // Apply protection bits to destination file
                if (protect_file(fullDestPath, sourceProtection, 0) != 0) {
                  // Protection bits setting failed, but don't crash - just continue
                  // The file transfer was successful, which is the primary goal
                }
                found = 1;
              }
              entry = entry->next;
            }
            
            dir_freeEntryList(dirInfo);
          }
        } else if (sourceIsLocal && destIsRemote && hasOriginalProtectionBits) {
          // For local-to-remote operations, apply the original protection bits that were read before transfer
          if (protect_file(fullDestPath, originalProtectionBits, 0) != 0) {
            // Protection bits setting failed, but don't crash - just continue
            // The file transfer was successful, which is the primary goal
          }
        }
      }
      
      // Clean up allocated memory
      if (originalSourcePath) {
        free(originalSourcePath);
      }
      
      // Return early to prevent double execution through normal command path
      free(newArgv);
      return success;
    } else {
      success = util_system(newArgv);
    }
  } else {
    success = util_system(newArgv);
  }
  free(newArgv);
  
  // Process transfer-back after successful command execution
  cli_hostfile_t* file = list;
  while (file) {
    cli_saveFileIfModified(file);
    cli_hostfile_t* save = file;
    file = file->next;
    
    // Restore original argv value (find it in originalArgv)
    for (int i = 1; i < argc; i++) {
      if (save->argv == &argv[i]) {
        *save->argv = originalArgv[i];
        break;
      }
    }
    
    cli_freeHostFile(save);
  }
  
  // Clean up expanded paths
  for (int i = 0; i < expandedCount; i++) {
    free(expandedPaths[i]);
  }
  free(expandedPaths);
  free(originalArgv);

 cleanup:
  // Clean up original source path if allocated
  if (originalSourcePath) {
    free(originalSourcePath);
  }
  
  util_rmdir(util_getTempFolder());
  return success;

 error:
  // Transfer-back is now handled in main flow above
  goto cleanup;
}


static int
cli_runCommand(char* line)
{
  int code = 0;
  int end = strlen(line) - 1;
  while (end >= 0 && line[end] == ' ') {
    line[end] = 0;
    end--;
  }

  // Remove trailing newline if present
  line[strcspn(line, "\n")] = 0;

  // Check for Amiga-style directory navigation
  if (line[0] == '/') {
    int slash_count = 0;
    while (line[slash_count] == '/') {
      slash_count++;
    }
    
    for (int i = 0; i < slash_count; i++) {
      if (exec_cmd(2, (char*[]){"cd", "/"}) != 0) {
        fprintf(stderr, "cd: / failed\n");
        return code;
      }
    }

    const char* new_path = cwd_read();
    if (new_path != NULL) {
      cli_changeDir(new_path);
      code = 1;
    } else {
      fprintf(stderr, "Failed to get new working directory\n");
    }
    return code;
  } else if (strncasecmp(line, "cd ", 3) == 0 || 
             (strchr(line, ':') != NULL && strchr(line, ' ') == NULL) || 
             (line[end] == '/' && strchr(line, ' ') == NULL) || 
             (strchr(line, '/') != NULL && strchr(line, ' ') == NULL) ||
             (line[0] == '"' && strchr(line + 1, '"') != NULL)) {  // Added check for quoted paths
    char* cmd = strdup(line);
    char* arg = cmd;

    // If the command starts with "CD " or "cd ", skip past it (case-insensitive)
    if (strncasecmp(cmd, "cd ", 3) == 0) {
      arg += 3;
    }

    // Skip leading spaces
    while (*arg == ' ') arg++;

    // Handle quoted arguments
    if (*arg == '"') {
      arg++; // Skip opening quote
      char* end_quote = strchr(arg, '"');
      if (end_quote) {
        *end_quote = '\0'; // Remove closing quote
      }
    }

    // Remove trailing slash if present, unless it's the only character
    int arg_len = strlen(arg);
    if (arg_len > 1 && arg[arg_len - 1] == '/') {
      arg[arg_len - 1] = '\0';
    }

    // Attempt to change directory
    if (exec_cmd(2, (char*[]){"cd", arg}) == 0) {
      const char* new_path = cwd_read();
      if (new_path) {
        cli_changeDir(new_path);
        code = 1;
      }
    } else {
      fprintf(stderr, "cd: %s failed\n", arg);
      // code remains 0 (failure)
    }
    free(cmd);
    return code;
  }

  // Handle other commands
  char** argv = argv_build((char*)line);
  int argc = argv_argc(argv);

  if (strcmp("endcli", argv[0]) == 0) {
    main_cleanupAndExit(EXIT_SUCCESS);
  } else if (argv[0][0] == '!') {
    code = cli_hostCommand(argc, argv);
  } else {
    // Authentic Amiga behavior: for single words, try directory navigation first
    // if it's not a known command
    if (argc == 1 && strchr(argv[0], ' ') == NULL) {
      // List of common Amiga commands that should NOT be treated as directories
      const char* known_commands[] = {
        "dir", "list", "copy", "delete", "rename", "makedir", "cd", "type",
        "echo", "cls", "clear", "version", "date", "time", "info", "which",
        "assign", "mount", "format", "protect", "comment", "relabel",
        "diskchange", "install", "run", "execute", "newshell", "endcli",
        "quit", "exit", "help", "?", "status", "stack", "path", "resident",
        "alias", "unalias", "setenv", "getenv", "unsetenv", "eval",
        NULL
      };
      
      int is_known_command = 0;
      for (int i = 0; known_commands[i] != NULL; i++) {
        if (strcasecmp(argv[0], known_commands[i]) == 0) {
          is_known_command = 1;
          break;
        }
      }
      
      if (!is_known_command) {
        // Try as directory navigation first
        if (exec_cmd(2, (char*[]){"cd", argv[0]}) == 0) {
          const char* new_path = cwd_read();
          if (new_path) {
            cli_changeDir(new_path);
            code = 1; // Success
            return code; // Don't try as command
          }
        }
        // If directory change failed, fall through to try as command
      }
    }
    
    // Execute as regular command
    code = exec_cmd(argc, argv);
  }

  argv_free(argv);
  return code;
}



static void
cli_onExit(void)
{
  main_cleanupAndExit(EXIT_SUCCESS);
}


static char**
cli_queryAmigaAssigns(int* count)
{
  // Query Amiga system for actual assigns, devices, and volumes
  // This provides truly authentic completion based on real system state
  
  *count = 0;
  char** assigns = NULL;
  int capacity = 50; // Initial capacity
  
  assigns = malloc(capacity * sizeof(char*));
  if (!assigns) return NULL;
  
  // Query actual Amiga system for real assigns using exec_captureCmd
  uint32_t error_code;
  char* assign_output = exec_captureCmd(&error_code, 1, (char*[]){"assign"});
  

  
  if (assign_output && error_code == 0) {
    // Parse assign command output: "AssignName: path"
    char* line = strtok(assign_output, "\n");
    while (line) {
      // Skip header lines like "Volumes:", "Directories:", "Devices:"
      if (strstr(line, "Volumes:") || strstr(line, "Directories:") || strstr(line, "Devices:") || strstr(line, "Denied volume requests:") || strstr(line, "disks:")) {
        line = strtok(NULL, "\n");
        continue;
      }
      
      // Find first non-space character (start of assign name)
      char* start = line;
      while (*start == ' ') start++;
      
      // Find first space after assign name
      char* space = strchr(start, ' ');
      if (space && space > start) {
        // Extract just the assign name and add colon
        int name_len = space - start;
        char* assign_name = malloc(name_len + 2); // +2 for colon and null
        if (assign_name) {
          strncpy(assign_name, start, name_len);
          assign_name[name_len] = ':';
          assign_name[name_len + 1] = '\0';
          
          // Add to list (resize if needed)
          if (*count >= capacity - 1) {
            capacity *= 2;
            assigns = realloc(assigns, capacity * sizeof(char*));
            if (!assigns) break;
          }
          
          // Check for duplicates (case-insensitive)
          int is_duplicate = 0;
          for (int j = 0; j < *count; j++) {
            if (strcasecmp(assigns[j], assign_name) == 0) {
              is_duplicate = 1;
              free(assign_name); // Free the duplicate
              break;
            }
          }
          
          if (!is_duplicate) {
            assigns[*count] = assign_name;
            (*count)++;
          }
        }
      }
      line = strtok(NULL, "\n");
    }
    free(assign_output);
  }
  
  // Query actual Amiga system for volumes/devices using info command
  char* info_output = exec_captureCmd(&error_code, 1, (char*[]){"info"});
  

  
  if (info_output && error_code == 0) {
    // Parse info command output to find volume names in "Volumes available:" section
    char* line = strtok(info_output, "\n");
    int in_volumes_section = 0;
    
    while (line) {
      // Check if we're entering the volumes section
      if (strstr(line, "Volumes available:")) {
        in_volumes_section = 1;
        line = strtok(NULL, "\n");
        continue;
      }
      
      // If we're in the volumes section, parse volume names
      if (in_volumes_section) {
        // Skip empty lines
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (strlen(trimmed) == 0) {
          line = strtok(NULL, "\n");
          continue;
        }
        
        // Look for volume names followed by [Mounted]
        char* mounted_pos = strstr(trimmed, " [Mounted]");
        if (mounted_pos) {
          // Extract volume name (everything before " [Mounted]")
          int volume_name_len = mounted_pos - trimmed;
          char* volume_name = malloc(volume_name_len + 2); // +2 for colon and null
          if (volume_name) {
            strncpy(volume_name, trimmed, volume_name_len);
            volume_name[volume_name_len] = ':';
            volume_name[volume_name_len + 1] = '\0';
            
            // Add volume to list (resize if needed)
            if (*count >= capacity - 1) {
              capacity *= 2;
              assigns = realloc(assigns, capacity * sizeof(char*));
              if (!assigns) {
                free(volume_name);
                break;
              }
            }
            
            // Check for duplicates (case-insensitive)
            int is_duplicate = 0;
            for (int j = 0; j < *count; j++) {
              if (strcasecmp(assigns[j], volume_name) == 0) {
                is_duplicate = 1;
                free(volume_name); // Free the duplicate
                break;
              }
            }
            
            if (!is_duplicate) {
              assigns[*count] = volume_name;
              (*count)++;
            }
          }
        }
      }
      
      line = strtok(NULL, "\n");
    }
    free(info_output);
  }
  
  // If no assigns/volumes found, add basic fallback
  if (*count == 0) {
    static int warning_shown = 0;
    if (!warning_shown) {
      printf("Warning: Unable to query Amiga system for assigns/volumes, using fallback list\n");
      warning_shown = 1;
    }
    const char* fallback_assigns[] = {
      "SYS:", "RAM:", "DF0:", NULL
    };
    
    for (int i = 0; fallback_assigns[i] != NULL; i++) {
      if (*count >= capacity - 1) {
        capacity *= 2;
        assigns = realloc(assigns, capacity * sizeof(char*));
        if (!assigns) break;
      }
      
      assigns[*count] = strdup(fallback_assigns[i]);
      if (assigns[*count]) (*count)++;
    }
  }
  
  // Null-terminate the array
  if (*count < capacity) {
    assigns[*count] = NULL;
  }
  

  
  return assigns;
}


static char*
cli_getAmigaAssignSuggestion(int* list_index, const char* match_text, int match_len)
{
  // Dynamic Amiga assign/device/volume detection by querying actual system
  // This provides authentic completion suggestions based on real Amiga state
  
  // Dynamic assign/device/volume detection by querying actual Amiga system
  static char** cached_assigns = NULL;
  static int cached_count = 0;
  static time_t last_cache_time = 0;
  
  // Refresh cache every 30 seconds or on first call
  time_t current_time = time(NULL);
  if (!cached_assigns || (current_time - last_cache_time) > 30) {
    // Free previous cache
    if (cached_assigns) {
      for (int i = 0; i < cached_count; i++) {
        free(cached_assigns[i]);
      }
      free(cached_assigns);
    }
    
    // Query Amiga system for actual assigns and volumes
    cached_assigns = cli_queryAmigaAssigns(&cached_count);
    last_cache_time = current_time;
  }
  
  // Use cached results (fallback to basic list if query fails)
  const char** assigns_to_use;
  int assign_count;
  
  if (cached_assigns && cached_count > 0) {
    assigns_to_use = (const char**)cached_assigns;
    assign_count = cached_count;

  } else {
    // Fallback to basic common assigns if dynamic query fails
    static const char* fallback_assigns[] = {
      "WORK:", "WORKBENCH:", "SYS:", "SYSTEM:", "C:", "S:", "L:", "LIBS:",
      "DEVS:", "FONTS:", "PREFS:", "T:", "RAM:", "DF0:", "DF1:", "DH0:", "DH1:",
      NULL
    };
    assigns_to_use = fallback_assigns;
    assign_count = 17; // Count of fallback assigns
  }
  
  // Count matching assigns first to know total matches
  int total_assign_matches = 0;
  for (int i = 0; i < assign_count && assigns_to_use[i]; i++) {
    if (strncasecmp(assigns_to_use[i], match_text, match_len) == 0) {
      total_assign_matches++;
    }
  }
  (void)total_assign_matches;
  
  // Simple assign completion - the caller manages the list_index properly
  // We just need to return the assign at the current relative position
  
  // Count how many file/directory matches we've already processed
  // by checking how many matches exist in the file list
  int file_matches = 0;
  if (cli_dirEntryList) {
    dir_entry_t* ptr = cli_dirEntryList->head;
    while (ptr) {
      // Skip .info files
      int name_len = strlen(ptr->name);
      if (name_len >= 5) {
        const char* extension = &ptr->name[name_len - 5];
        if (strcasecmp(extension, ".info") == 0) {
          ptr = ptr->next;
          continue;
        }
      }
      
      char buffer[PATH_MAX];
      snprintf(buffer, sizeof(buffer), "%s%s", cli_readLineBase, ptr->name);
      if (strncasecmp(buffer, match_text, match_len) == 0) {
        file_matches++;
      }
      ptr = ptr->next;
    }
  }
  
  // The assign index should be relative to the number of file matches
  int assign_relative_index = *list_index - file_matches;
  
  // Return the assign at the relative index
  int assign_index = 0;
  for (int i = 0; i < assign_count && assigns_to_use[i]; i++) {
    if (strncasecmp(assigns_to_use[i], match_text, match_len) == 0) {
      if (assign_index == assign_relative_index) {
        (*list_index)++;
        return strdup(assigns_to_use[i]);
      }
      assign_index++;
    }
  }
  
  return NULL;
}


// Local filesystem completion functions
static int
cli_isLocalCommand(const char* command_line)
{
  // Check if command starts with '!' (local command)
  if (command_line && command_line[0] == '!') {
    return 1;
  }
  return 0;
}

static int
cli_isLocalFileArgument(const char* arg)
{
  // Check if argument starts with '!' or '~' (local file)
  if (arg && (arg[0] == '!' || arg[0] == '~')) {
    return 1;
  }
  return 0;
}

static int
cli_isLocalFileArgumentForExecution(const char* arg)
{
  // Check if argument is a local file, handling quoted paths
  if (!arg) return 0;
  
  // Direct check for ! or ~ prefix
  if (arg[0] == '!' || arg[0] == '~') {
    return 1;
  }
  
  // Check for quoted local files: "~/path" or "!/path"
  if (arg[0] == '"' && strlen(arg) > 2) {
    if (arg[1] == '~' || arg[1] == '!') {
      return 1;
    }
  }
  
  // Check for single-quoted local files: '~/path' or '!/path'
  if (arg[0] == '\'' && strlen(arg) > 2) {
    if (arg[1] == '~' || arg[1] == '!') {
      return 1;
    }
  }
  
  return 0;
}

static char*
cli_expandLocalPath(const char* path)
{
  if (!path) return NULL;
  
  char* expanded = malloc(PATH_MAX);
  if (!expanded) return NULL;
  
  // Handle quoted paths by extracting the inner path
  const char* inner_path = path;
  int path_len = strlen(path);
  
  // Strip outer quotes if present
  if ((path[0] == '"' && path[path_len-1] == '"' && path_len > 1) ||
      (path[0] == '\'' && path[path_len-1] == '\'' && path_len > 1)) {
    // Create a temporary unquoted version
    char* temp_path = malloc(path_len);
    if (!temp_path) {
      free(expanded);
      return NULL;
    }
    strncpy(temp_path, path + 1, path_len - 2);
    temp_path[path_len - 2] = '\0';
    inner_path = temp_path;
  }
  
  if (inner_path[0] == '~') {
    // Expand ~ to home directory
    const char* home = getenv("HOME");
    if (home) {
      snprintf(expanded, PATH_MAX, "%s%s", home, inner_path + 1);
    } else {
      strcpy(expanded, inner_path);
    }
  } else if (inner_path[0] == '!') {
    // Remove ! prefix for local files
    strcpy(expanded, inner_path + 1);
  } else {
    strcpy(expanded, inner_path);
  }
  
  // Clean up temporary path if we created one
  if (inner_path != path) {
    free((char*)inner_path);
  }
  
  return expanded;
}

static char*
cli_getLocalFileSuggestion(int* list_index, const char* text, int len)
{
  (void)len; // Unused parameter
  static DIR* dir_handle = NULL;
  static char* dir_path = NULL;
  static int current_index = 0;
  
  // Reset on new completion
  if (*list_index == 0) {
    if (dir_handle) {
      closedir(dir_handle);
      dir_handle = NULL;
    }
    if (dir_path) {
      free(dir_path);
      dir_path = NULL;
    }
    current_index = 0;
    
    // Extract directory path from text
    char* expanded_text = cli_expandLocalPath(text);
    if (!expanded_text) return NULL;
    
    char* last_slash = strrchr(expanded_text, '/');
    if (last_slash) {
      *last_slash = '\0';
      // Handle root directory case: if path becomes empty, use "/"
      if (strlen(expanded_text) == 0) {
        dir_path = strdup("/");
      } else {
        dir_path = strdup(expanded_text);
      }
    } else {
      dir_path = strdup(".");
    }
    
    dir_handle = opendir(dir_path);
    free(expanded_text);
    
    if (!dir_handle) {
      if (dir_path) {
        free(dir_path);
        dir_path = NULL;
      }
      return NULL;
    }
  }
  
  if (!dir_handle) return NULL;
  
  // Get filename portion for matching
  char* expanded_text = cli_expandLocalPath(text);
  if (!expanded_text) return NULL;
  
  char* filename_part = strrchr(expanded_text, '/');
  if (filename_part) {
    filename_part++; // Skip the '/'
  } else {
    filename_part = expanded_text;
  }
  int filename_len = strlen(filename_part);
  
  struct dirent* entry;
  while ((entry = readdir(dir_handle)) != NULL) {
    // Skip . and ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    
    // Check if entry matches prefix (case-insensitive)
    if (strncasecmp(entry->d_name, filename_part, filename_len) == 0) {
      if (current_index == *list_index) {
        (*list_index)++;
        current_index++;
        
        // Build full path
        char* result = malloc(PATH_MAX);
        
        // Preserve the original prefix (! or ~) in completion results
        const char* prefix = "";
        if (text[0] == '!') {
          prefix = "!";
        } else if (text[0] == '~') {
          prefix = "~";
        }
        
        if (strcmp(dir_path, ".") == 0) {
          snprintf(result, PATH_MAX, "%s%s", prefix, entry->d_name);
        } else {
          // For ~ paths, we need to construct the path relative to the original ~ prefix
          if (text[0] == '~') {
            // Extract the path after ~ from the original text
            const char* home = getenv("HOME");
            if (home && strncmp(dir_path, home, strlen(home)) == 0) {
              // dir_path starts with home directory, so we can reconstruct with ~
              const char* relative_path = dir_path + strlen(home);
              if (strlen(relative_path) > 0) {
                snprintf(result, PATH_MAX, "~%s/%s", relative_path, entry->d_name);
              } else {
                snprintf(result, PATH_MAX, "~/%s", entry->d_name);
              }
            } else {
              // Fallback to full path with ~ prefix
              snprintf(result, PATH_MAX, "~/%s", entry->d_name);
            }
          } else {
            // Handle root directory to avoid double slashes
            if (strcmp(dir_path, "/") == 0) {
              snprintf(result, PATH_MAX, "%s/%s", prefix, entry->d_name);
            } else {
              snprintf(result, PATH_MAX, "%s%s/%s", prefix, dir_path, entry->d_name);
            }
          }
        }
        
        // Check if it's a directory and add trailing slash
        char full_path[PATH_MAX];
        if (strcmp(dir_path, "/") == 0) {
          snprintf(full_path, PATH_MAX, "/%s", entry->d_name);
        } else {
          snprintf(full_path, PATH_MAX, "%s/%s", dir_path, entry->d_name);
        }
        struct stat st;
        int is_directory = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
        
        // Add quoting for paths with spaces (like Amiga completion does)
        extern int _srl_inside_quotes_flag;
        int already_quoted = _srl_inside_quotes_flag;
        
        if (strchr(result, ' ') != NULL) {
          // Path contains spaces - needs quoting
          char* quoted_result = malloc(strlen(result) + 10);
          if (is_directory) {
            if (already_quoted) {
              sprintf(quoted_result, "%s/\"", result);
            } else {
              sprintf(quoted_result, "\"%s/\"", result);
            }
          } else {
            if (already_quoted) {
              sprintf(quoted_result, "%s\"", result);
            } else {
              sprintf(quoted_result, "\"%s\"", result);
            }
          }
          free(result);
          result = quoted_result;
        } else {
          // No spaces - just add trailing slash for directories
          if (is_directory) {
            strcat(result, "/");
          }
        }
        
        free(expanded_text);
        return result;
      }
      current_index++;
    }
  }
  
  // Cleanup when done
  if (dir_handle) {
    closedir(dir_handle);
    dir_handle = NULL;
  }
  if (dir_path) {
    free(dir_path);
    dir_path = NULL;
  }
  current_index = 0;
  
  free(expanded_text);
  return NULL;
}


static char*
cli_completeGenerator(int* list_index, const char* text, int len)
{
  // Check if we should use local filesystem completion
  if (cli_isLocalCompletion) {
    return cli_getLocalFileSuggestion(list_index, text, len);
  }
  
  // Use reconstructed text for quoted paths, otherwise use original text
  const char* match_text = cli_reconstructedText ? cli_reconstructedText : text;
  int match_len = cli_reconstructedText ? (int)strlen(cli_reconstructedText) : len;
  
  dir_entry_t* ptr;
  if (cli_dirEntryList) {
    ptr = cli_dirEntryList->head;
  } else {
    ptr = 0;
  }

  // Process all matches (directories and files) for ambiguous completion
  int current_index = 0;
  while (ptr) {
    dir_entry_t* entry = ptr;
    ptr = ptr->next;

    // Skip .info files (authentic Amiga shell behavior)
    int name_len = strlen(entry->name);
    if (name_len >= 5) {
      const char* extension = &entry->name[name_len - 5];
      if (strcasecmp(extension, ".info") == 0) {
        continue; // Skip .info files
      }
    }

    char buffer[PATH_MAX];
    snprintf(buffer, sizeof(buffer), "%s%s", cli_readLineBase, entry->name);

    // Check both directories and files in the same loop
    if (strncasecmp(buffer, match_text, match_len) == 0) {
      if (current_index == *list_index) {
        (*list_index)++;
        
        extern int _srl_inside_quotes_flag;
        int already_quoted = _srl_inside_quotes_flag;
        
        if (entry->type > 0) {
          // Directory match
          char* result = malloc(strlen(buffer) + 10);
          if (strchr(buffer, ' ') != NULL) {
            if (already_quoted) {
              sprintf(result, "%s/\"", buffer);
            } else {
              sprintf(result, "\"%s/\"", buffer);
            }
          } else {
            strcpy(result, buffer);
            strcat(result, "/");
          }
          return result;
        } else {
          // File match
          if (strchr(buffer, ' ') != NULL) {
            char* result = malloc(strlen(buffer) + 3);
            if (already_quoted) {
              sprintf(result, "%s\"", buffer);
            } else {
              sprintf(result, "\"%s\"", buffer);
            }
            return result;
          } else {
            return strdup(buffer);
          }
        }
      }
      current_index++;
    }
  }

  // After processing all file/directory matches, also check Amiga assigns/devices/volumes
  // This matches authentic Amiga shell behavior by including assigns alongside files
  if (match_len > 0) {
    char* assign_result = cli_getAmigaAssignSuggestion(list_index, match_text, match_len);
    if (assign_result) {
      return assign_result;
    }
  }

  return NULL;
}


static void
cli_completeHook(const char* text, const char* full_command_line)
{
  // Reset local completion flag
  cli_isLocalCompletion = 0;
  
  // ~ paths are ALWAYS local (Amiga doesn't have ~ expansion)
  if (text && text[0] == '~') {
    cli_isLocalCompletion = 1;
  }
  // ! paths are local only in local commands
  else if (full_command_line && cli_isLocalCommand(full_command_line)) {
    // Check if the current argument being completed is a local file
    if (cli_isLocalFileArgument(text)) {
      cli_isLocalCompletion = 1;
    }
  }
  
  if (cli_readLineBase) {
    free(cli_readLineBase);
  }
  if (cli_reconstructedText) {
    free(cli_reconstructedText);
    cli_reconstructedText = 0;
  }

  // Custom input wrapper: Simple text processing without readline dependencies
  char* full_path = strdup(text);
  // Note: cli_reconstructedText is now set only when inside quotes (from input wrapper)
  
  cli_readLineBase = strdup(full_path);

  // Only process Amiga filesystem if not doing local completion
  if (!cli_isLocalCompletion) {
    int ei = strlen(cli_readLineBase);
    while (ei >= 0 && cli_readLineBase[ei] != '/' && cli_readLineBase[ei] != ':') {
      cli_readLineBase[ei] = 0;
      ei--;
    }
    
    if (cli_dirEntryList) {
      dir_freeEntryList(cli_dirEntryList);
    }

    cli_dirEntryList = dir_read(cli_readLineBase);
  } else {
    // For local completion, we don't need Amiga directory listing
    if (cli_dirEntryList) {
      dir_freeEntryList(cli_dirEntryList);
      cli_dirEntryList = NULL;
    }
  }
  
  free(full_path);
}


void
cli_main(int argc, char* argv[])
{
  (void)argc,(void)argv;

  cli_dirEntryList = 0;
  cli_readLineBase = 0;

  if (argc != 2) {
    fatalError("incorrect number of arguments\nusage: %s hostname", main_argv0);
  }


  util_connect(argv[1]);
  util_onCtrlC(cli_onExit);

  srl_init(cli_prompt, cli_completeHook, cli_completeGenerator);

  do {
    const char* cwd = cwd_read();
    if (!cwd) {
      fatalError("failed to get cwd");
    }
    cli_changeDir(cwd);
    free((void*)cwd);
    char* command = srl_gets();
    if (command && strlen(command)) {
      cli_runCommand(command);
    }
  } while (1);
}
