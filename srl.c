#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "main.h"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include "win_compat.h"
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif

// Custom input wrapper - no more readline dependency!

// Input buffer and cursor management
#define MAX_INPUT_LENGTH 1024

// Custom input wrapper state
static char *srl_lineRead = 0;
static int srl_searchMode = 0;
static int srl_searchCount = 0;
static const char* (*srl_prompt)(void);
static void (*srl_completeHook)(const char* text, const char* full_command_line);
static char* (*srl_generator)(int* list_index, const char* text, int len);

// Two-stage tab completion tracking
static char srl_lastCompletionBuffer[MAX_INPUT_LENGTH];
static int srl_lastCompletionCursor = -1;
static int srl_lastCompletionlength = -1;

// Forward declarations
static void srl_resetTabTracking(void);
static void srl_enableRawMode(void);
static void srl_disableRawMode(void);
static void srl_refreshLine(void);
static void srl_addToHistory(const char* line);
static void srl_handleTabCompletion(void);
static void srl_loadHistory(void);

// Terminal control
#ifdef _WIN32
static HANDLE srl_hStdin;
static DWORD srl_originalMode;
#else
static struct termios srl_originalTermios;
#endif
int srl_insideQuotesFlag = 0;
static int srl_terminalSetup = 0;
static int srl_terminalWidth = 0;
static int srl_terminalHeight = 0;
static int srl_terminalMinRow = 0;

// Input buffer and cursor management
static char srl_inputBuffer[MAX_INPUT_LENGTH];
static int srl_cursorPos = 0;
static int srl_bufferLength = 0;
static char srl_killBuffer[MAX_INPUT_LENGTH] = {0};
static char srl_searchBuffer[MAX_INPUT_LENGTH] = {0};
static char srl_searchPrompt[MAX_INPUT_LENGTH] = {0};

// History management
#define MAX_HISTORY_ENTRIES 100
static char* srl_history[MAX_HISTORY_ENTRIES];
static int srl_historyCount = 0;
static int srl_historyIndex = -1;

// Reset tab completion tracking
static void
srl_resetTabTracking(void)
{
  srl_lastCompletionCursor = -1;
  srl_lastCompletionlength = -1;
  if ( srl_searchMode) {
    srl_searchMode = 0;
    srl_refreshLine();
  }    
}



// Terminal control functions
static void
srl_enableRawMode(void)
{
  if (srl_terminalSetup) return;
  
#ifdef _WIN32
  srl_hStdin = GetStdHandle(STD_INPUT_HANDLE);
  GetConsoleMode(srl_hStdin, &srl_originalMode);
  SetConsoleMode(srl_hStdin, ENABLE_PROCESSED_INPUT);
#else
  tcgetattr(STDIN_FILENO, &srl_originalTermios);
  struct termios raw = srl_originalTermios;
  
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_cflag |= CS8;
  raw.c_oflag &= ~(OPOST);
  
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
  srl_terminalSetup = 1;
}

static void
srl_disableRawMode(void)
{
  if (!srl_terminalSetup) return;
  
  // Robust terminal cleanup: move cursor to column 1 and clear line
  printf("\r\033[K"); // \r = move to column 1, \033[K = clear to end of line
  fflush(stdout);
  
#ifdef _WIN32
  SetConsoleMode(srl_hStdin, srl_originalMode);
#else
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &srl_originalTermios);
#endif
  srl_terminalSetup = 0;
}

// Cursor and display functions

#ifdef _WIN32
static int
srl_getTerminalSize(int *rows, int *cols)
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  
  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    if (cols) *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (rows) *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
  } else {
    return -1; // failed to get console info
  }
}

static int
srl_getCursorPosition(int *row, int *col)
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  
  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    if (col) *col = csbi.dwCursorPosition.X + 1; // 1-based column
    if (row) *row = csbi.dwCursorPosition.Y + 1; // 1-based row
    return 0;
  } else {
    return -1; // Failed
  }
}

#else
static int
srl_getTerminalSize(int *rows, int *cols)
{
  char buf[64];
  int i = 0;
  
  // Hide cursor, save cursor position, ESC 7, Mmve as far down and right as possible, move to bottom-right corner, request cursor position
  printf("\033[?25l\0337\033[999B\033[999C\033[6n");
  fflush(stdout);
  
  // Read response: ESC [ row ; col R
  while (i < (int)(sizeof(buf) - 1)) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i + 1] = '\0';
  
  // Restore cursor position, show cursor
  printf("\0338\033[?25h");
  fflush(stdout);
  
  // Parse cursor position: ESC [ rows ; cols R
  int r = 0, c = 0;
  if (sscanf(buf, "\033[%d;%dR", &r, &c) == 2) {
    if (rows) *rows = r;
    if (cols) *cols = c;
    return 0;
  }
  
  return -1;
}

static int
srl_getCursorPosition(int *row, int *col)
{
  char buf[32];
  int i = 0;
  
  // Send cursor position report request
  printf("\033[6n");
  fflush(stdout);
  
  // Read response
  while (i < (int)(sizeof(buf) - 1)) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i + 1] = '\0';
  
  // Parse response: ESC [ rows ; cols R
  int _row, _col;
  if (sscanf(buf, "\033[%d;%dR", &_row, &_col) == 2) {
    if (row) {
      *row = _row;
    }
    if (col) {
      *col = _col;
    }
    return 0;
  }
  return -1;
}
#endif

static void
srl_terminalCursorForward(void)
{
  srl_resetTabTracking();
  if (srl_cursorPos < srl_bufferLength) {
    srl_cursorPos++;
    srl_refreshLine();
  }
}


static void
srl_terminalCursorBack(void)
{
  srl_resetTabTracking();
  if (srl_cursorPos > 0) {
    srl_cursorPos--;
  }
  srl_refreshLine();
}

static const char *
srl_getPrompt(void)
{
  if (srl_searchMode) {
    strcpy(srl_searchPrompt, "(reverse-i-search)`");
    strlcat(srl_searchPrompt, srl_searchBuffer, sizeof(srl_searchPrompt));
    strlcat(srl_searchPrompt, "':",  sizeof(srl_searchPrompt));
    return srl_searchPrompt;      
  }
  if (srl_prompt) {
    return srl_prompt();
  } else {
    return "";
  }
}


static void
srl_refreshLine(void)
{
  int total_width = strlen(srl_getPrompt()) + srl_bufferLength;
  int rows = (total_width / srl_terminalWidth);
  int cursor_row = srl_terminalHeight-rows;
  int cursor_width = strlen(srl_getPrompt()) + srl_cursorPos + 1;
  int cursor_rows = (cursor_width / srl_terminalWidth);
  int cursor_cols = cursor_width % srl_terminalWidth;  

  printf("\033[?25l"); // hide cursor
  
  while (srl_terminalMinRow > cursor_row) {
    printf("\033D"); // scroll      
    srl_terminalMinRow--;
  }

  printf("\033[%d;0H", srl_terminalMinRow);  // move cursor to v,h
  printf("%s",  srl_getPrompt());
  printf("%.*s", srl_bufferLength, srl_inputBuffer);
  printf("\033[J\r"); //clear screen from cursor down    
  printf("\033[%d;%dH", srl_terminalMinRow+cursor_rows, cursor_cols);  // move cursor to v,h

  printf("\033[?25h"); // Show cursor
  
  fflush(stdout);
}

// History functions
static void
srl_historySelectNext(void)
{
  srl_resetTabTracking();
  if (srl_historyIndex >= 0) {
    srl_historyIndex++;
    if (srl_historyIndex >= srl_historyCount) {
      srl_historyIndex = -1;
      srl_inputBuffer[0] = '\0';
      srl_bufferLength = 0;
      srl_cursorPos = 0;
    } else {
      strcpy(srl_inputBuffer, srl_history[srl_historyIndex]);
      srl_bufferLength = strlen(srl_inputBuffer);
      srl_cursorPos = srl_bufferLength;
    }
    srl_refreshLine();
  }
}

static void
srl_historySelectPrev(void)
{
  srl_resetTabTracking(); // Reset tab completion tracking
  if (srl_historyCount > 0) {
    if (srl_historyIndex == -1) srl_historyIndex = srl_historyCount - 1;
    else if (srl_historyIndex > 0) srl_historyIndex--;
    
    if (srl_historyIndex >= 0) {
      strcpy(srl_inputBuffer, srl_history[srl_historyIndex]);
      srl_bufferLength = strlen(srl_inputBuffer);
      srl_cursorPos = srl_bufferLength;
      srl_refreshLine();
    }
  }
}

static const char*
srl_findHistoryMatch(const char* candidate, int n)
{
  if (!candidate || n < 0) return NULL;

  int match_index = 0;
  const char* last_content = NULL;

  for (int i = MAX_HISTORY_ENTRIES - 1; i >= 0; i--) {
    const char* entry = srl_history[i];
    if (entry && strstr(entry, candidate)) {
      if (!last_content || strcmp(entry, last_content) != 0) {
        if (match_index == n) {
          return entry;
        }
        last_content = entry;
        match_index++;
      }
    }
  }

  return NULL;
}

static void
srl_addToHistory(const char* line)
{
  if (!line || !*line) return;
  
  // Check if same as last entry
  if (srl_historyCount > 0 && strcmp(srl_history[srl_historyCount-1], line) == 0) {
    return;
  }
  
  if (srl_historyCount >= MAX_HISTORY_ENTRIES) {
    free(srl_history[0]);
    for (int i = 0; i < MAX_HISTORY_ENTRIES-1; i++) {
      srl_history[i] = srl_history[i+1];
    }
    srl_historyCount = MAX_HISTORY_ENTRIES - 1;
  }
  
  srl_history[srl_historyCount++] = strdup(line);
}

// Tab completion handling
static void
srl_handleTabCompletion(void)
{
  if (!srl_completeHook || !srl_generator) return;
  
  // Extract text for completion from current buffer position
  char completion_text[MAX_INPUT_LENGTH];
  int completion_start = srl_cursorPos;
  
  // Smart completion start detection - handle quotes properly
  // Find the opening quote by looking for unmatched quotes
  int quote_start = -1;
  int quote_count = 0;
  for (int i = srl_cursorPos - 1; i >= 0; i--) {
    if (srl_inputBuffer[i] == '"') {
      quote_count++;
      // If odd number of quotes, this is an opening quote
      if (quote_count % 2 == 1) {
	quote_start = i;
      } else {
	// Even number means we've closed a quote, reset
	quote_start = -1;
      }
    }
  }
  
  if (quote_start >= 0) {
    // We're inside quotes - completion starts after the quote
    completion_start = quote_start + 1;
  } else {
    // Normal completion - go back until space or start of line
    while (completion_start > 0 && srl_inputBuffer[completion_start-1] != ' ') {
      completion_start--;
    }
  }
  
  int completion_len = srl_cursorPos - completion_start;
  strncpy(completion_text, &srl_inputBuffer[completion_start], completion_len);
  completion_text[completion_len] = '\0';
  
  // Store quote context in a simple global flag for the completion generator
  srl_insideQuotesFlag = (quote_start >= 0);
  
  // Extract quoted path information if needed (declare at function scope)
  char full_quoted_path[1000];
  int quoted_len = 0;
  
  // For quoted paths, we need to pass the full quoted path context, not just the completion text
  if (quote_start >= 0) {
    // Extract the full path inside quotes for proper base directory parsing
    // Find the closing quote or use cursor position
    int quote_end = srl_cursorPos;
    for (int i = quote_start + 1; i < srl_cursorPos; i++) {
      if (srl_inputBuffer[i] == '"') {
	quote_end = i;
	break;
      }
    }
    quoted_len = quote_end - quote_start - 1;  // Content between quotes
    strncpy(full_quoted_path, &srl_inputBuffer[quote_start + 1], quoted_len);
    full_quoted_path[quoted_len] = '\0';
    
    srl_completeHook(full_quoted_path, srl_inputBuffer);
  } else {
    srl_completeHook(completion_text, srl_inputBuffer);
  }
  
  // Collect all possible completions to handle ambiguity
  char* completions[50]; // Max 50 completions
  int completion_count = 0;
  int list_index = 0;
  
  // Gather all matching completions
  char* completion;
  while ((completion = srl_generator(&list_index, completion_text, completion_len)) != NULL && completion_count < 50) {
    completions[completion_count++] = completion;
  }
  
  if (completion_count == 0) {
    // No completions found
    return;
  } else if (completion_count == 1) {
    // Single completion - use it directly
    completion = completions[0];
  } else {
    // Multiple completions - find longest common prefix and complete partially
    
    // Find longest common prefix among all completions (beyond what user already typed)
    int extended_common_len = 0;
    if (completion_count > 1) {
      char* first = completions[0];
      int first_len = strlen(first);
      
      // Start comparison from where the user's input would end in the completion
      // Find where user input ends in the first completion
      int user_part_end = -1;
      
      // For quoted paths, we need to find where the typed part ends
      if (quote_start >= 0) {
	// User typed something like "My Files/NetMon/n", we want to find where 'n' ends
	// in completions like "My Files/NetMon/NetMon.info"
	user_part_end = strlen(full_quoted_path);
      } else {
	user_part_end = completion_len;
      }
      
      // Find common prefix starting from where user input ends (case-insensitive)
      for (int i = user_part_end; i < first_len; i++) {
	char c = first[i];
	int all_match = 1;
	for (int j = 1; j < completion_count; j++) {
	  if (i >= (int)strlen(completions[j]) || tolower(completions[j][i]) != tolower(c)) {
	    all_match = 0;
	    break;
	  }
	}
	if (all_match) {
	  extended_common_len = i + 1;
	} else {
	  break;
	}
      }
    }
    
    // Check if this is a consecutive tab press (two-stage completion)
    int is_consecutive_tab = (srl_lastCompletionCursor == srl_cursorPos && 
			      srl_lastCompletionlength == srl_bufferLength &&
			      strncmp(srl_lastCompletionBuffer, srl_inputBuffer, srl_bufferLength) == 0);
    
    // If we can complete more than what's currently typed, do partial completion
    if (extended_common_len > 0 && !is_consecutive_tab) {
      // First tab: Complete to the longest common prefix (no suggestions yet)
      char* partial_completion = malloc(extended_common_len + 1);
      strncpy(partial_completion, completions[0], extended_common_len);
      partial_completion[extended_common_len] = '\0';
      
      // Replace the input with the partial completion
      int replace_len = completion_len;
      int new_len = extended_common_len;
      int size_diff = new_len - replace_len;
      
      if (srl_bufferLength + size_diff < MAX_INPUT_LENGTH) {
	// Make space in buffer if needed
	if (size_diff > 0) {
	  memmove(&srl_inputBuffer[srl_cursorPos + size_diff], 
		  &srl_inputBuffer[srl_cursorPos], 
		  srl_bufferLength - srl_cursorPos);
	} else if (size_diff < 0) {
	  memmove(&srl_inputBuffer[srl_cursorPos + size_diff], 
		  &srl_inputBuffer[srl_cursorPos], 
		  srl_bufferLength - srl_cursorPos);
	}
	
	// Insert the partial completion
	memcpy(&srl_inputBuffer[srl_cursorPos - replace_len], partial_completion, new_len);
	srl_cursorPos += size_diff;
	srl_bufferLength += size_diff;
        
	srl_refreshLine();
      }
      
      
      // Update tab completion tracking for next tab press
      srl_lastCompletionCursor = srl_cursorPos;
      srl_lastCompletionlength = srl_bufferLength;
      memcpy(srl_lastCompletionBuffer, srl_inputBuffer, srl_bufferLength);
      
      free(partial_completion);
      return; // Don't show suggestions on first tab - only partial completion
    }
    
    // Second tab or no partial completion possible - display all matches
    if (is_consecutive_tab || extended_common_len == 0) {
      printf("\n");
      if (srl_terminalMinRow < srl_terminalHeight) {      
	srl_terminalMinRow++;
      }
      // Move cursor to column 1 for proper alignment of suggestions
      printf("\r");
      
      // Find common directory prefix among all completions for display
      int display_prefix_len = 0;
      if (completion_count > 1) {
	// Find the last '/' or ':' in the first completion
	char* first = completions[0];
	char* last_sep = NULL;
	for (char* p = first; *p; p++) {
	  if (*p == '/' || *p == ':') {
	    last_sep = p + 1; // Point after the separator
	  }
	}
	if (last_sep) {
	  display_prefix_len = last_sep - first;
	  // Verify all completions share this prefix
	  for (int i = 1; i < completion_count; i++) {
	    if (strncmp(first, completions[i], display_prefix_len) != 0) {
	      display_prefix_len = 0; // No common prefix
	      break;
	    }
	  }
	}
      }
      
      for (int i = 0; i < completion_count; i++) {
	// First strip quotes from the full completion, then apply prefix logic
	char* full_completion = completions[i];
	char clean_completion[1024];
        
	// Strip quotes from the full completion first (handle both leading and trailing quotes)
	strcpy(clean_completion, full_completion);
	int needs_cleaning = 0;
        
	// Remove leading quote if present
	if (clean_completion[0] == '"' && strlen(clean_completion) > 1) {
	  memmove(clean_completion, clean_completion + 1, strlen(clean_completion));
	  needs_cleaning = 1;
	}
        
	// Remove trailing quote if present
	int len = strlen(clean_completion);
	if (len > 0 && clean_completion[len - 1] == '"') {
	  clean_completion[len - 1] = '\0';
	  needs_cleaning = 1;
	}
        
	if (needs_cleaning) {
	  full_completion = clean_completion;
	}
        
	// Now apply directory prefix stripping to the clean completion
	char* display_name = full_completion + display_prefix_len;
        
	printf("%s  ", display_name);
      }
      printf("\n");
      if (srl_terminalMinRow < srl_terminalHeight) {
	srl_terminalMinRow++;
      }
      
      // Properly refresh the current line using our existing refresh function
      srl_refreshLine();
      fflush(stdout);
      
      // Free all completion strings
      for (int i = 0; i < completion_count; i++) {
	free(completions[i]);
      }
      
      // Reset tab completion tracking after showing suggestions
      srl_lastCompletionCursor = -1;
      srl_lastCompletionlength = -1;
      
      return;
    }
  }
  
  // Single completion processing continues here
  {
    // Calculate how much text to replace
    int replace_len = completion_len;
    int new_len = strlen(completion);
    
    // Make space in buffer if needed
    int size_diff = new_len - replace_len;
    if (srl_bufferLength + size_diff >= MAX_INPUT_LENGTH) {
      free(completion);
      return; // Buffer would overflow
    }
    
    // Move text after cursor if growing
    if (size_diff > 0) {
      memmove(&srl_inputBuffer[srl_cursorPos + size_diff], 
	      &srl_inputBuffer[srl_cursorPos], 
	      srl_bufferLength - srl_cursorPos);
    } else if (size_diff < 0) {
      memmove(&srl_inputBuffer[srl_cursorPos + size_diff], 
	      &srl_inputBuffer[srl_cursorPos], 
	      srl_bufferLength - srl_cursorPos);
    }
    
    // Replace the text
    memcpy(&srl_inputBuffer[completion_start], completion, new_len);
    
    // Update positions
    srl_bufferLength += size_diff;
    srl_cursorPos = completion_start + new_len;
    
    // Perfect display: No corruption, no extra spaces!
    srl_refreshLine();
  }
  
  // Free all completion strings
  for (int i = 0; i < completion_count; i++) {
    free(completions[i]);
  }
}

char *
srl_gets(void)
{
  srl_enableRawMode();
  
  srl_searchMode = 0;
  srl_searchBuffer[0] = 0;
  srl_getTerminalSize(&srl_terminalHeight, &srl_terminalWidth);
  srl_getCursorPosition(&srl_terminalMinRow, 0);
  
  if (srl_lineRead) {
    free(srl_lineRead);
    srl_lineRead = NULL;
  }
  
  
  // Initialize input state
  srl_cursorPos = 0;
  srl_bufferLength = 0;
  srl_historyIndex = -1;
  memset(srl_inputBuffer, 0, MAX_INPUT_LENGTH);
  
  // Display prompt
  printf("%s",  srl_getPrompt());
  fflush(stdout);
  
  // Main input loop
  while (1) {
    int c;
#ifdef _WIN32
    c = _getch(); // Windows console input
    // Windows _getch() returns special codes for arrow keys
    if (c == 0 || c == 224) { // Extended key prefix
      c = _getch(); // Get the actual key code
      switch (c) {
      case 72: // Up arrow
	srl_historySelectPrev();
	continue;
        
      case 80: // Down arrow
	srl_historySelectNext();
	continue;
        
      case 77: // Right arrow
	srl_terminalCursorForward();
	continue;
        
      case 75: // Left arrow
	srl_terminalCursorBack();
	continue;
        
      default:
	continue; // Ignore other extended keys
      }
    }
#else
    // Linux/Unix: read one character at a time
    unsigned char ch;
    ssize_t result = read(STDIN_FILENO, &ch, 1);
    if (result != 1) {
      if (result == 0) {
	// EOF - exit gracefully
	break;
      }
      continue;
    }
    c = (int)ch; // Ensure proper character conversion
#endif
    if (c == '\r' || c == '\n') {
      // Enter pressed - finish input
      if (srl_searchMode) {
	srl_searchMode = 0;
	srl_refreshLine();
      }
      printf("\n");
      break;
      
    } else if (c == 1) { // ^a
      srl_resetTabTracking(); // Reset tab completion tracking
      srl_cursorPos = 0;	  
      srl_refreshLine();
    } else if (c == 2) { // ^b
      srl_terminalCursorBack();
    } else if (c == 4) { // ^d
      srl_resetTabTracking(); // Reset tab completion tracking
      if (srl_bufferLength > srl_cursorPos) {
	memmove(&srl_inputBuffer[srl_cursorPos], 
		&srl_inputBuffer[srl_cursorPos+1], 
		srl_bufferLength - srl_cursorPos);
	srl_bufferLength--;
	srl_refreshLine();
      }
    } else if (c == 5) { // ^e
      srl_resetTabTracking(); // Reset tab completion tracking
      if (srl_cursorPos < srl_bufferLength) {
	srl_cursorPos = srl_bufferLength;	  	  
      }
      srl_refreshLine();	  
    } else if (c == 6) { // ^f
      srl_terminalCursorForward();
    } else if (c == 11) { // ^k
      srl_resetTabTracking();
      memset(srl_killBuffer, 0, sizeof(srl_killBuffer));
      strncpy(srl_killBuffer, &srl_inputBuffer[srl_cursorPos], srl_bufferLength-srl_cursorPos);
      srl_inputBuffer[srl_cursorPos] = 0;
      srl_bufferLength = srl_cursorPos;
      srl_refreshLine();
    } else if (c == 12) { // ^l
      srl_resetTabTracking();
      memset(srl_inputBuffer, 0, MAX_INPUT_LENGTH);	  
      srl_terminalMinRow = 1;
      srl_bufferLength = srl_cursorPos = 0;
      printf("\033[H");
      srl_refreshLine();
    } else if (c == 14) { // ^n
      srl_historySelectNext();
    } else if (c == 16) { // ^p
      srl_historySelectPrev();
    } else if (c == 18) { // ^r
      if (!srl_searchMode) {	    
	srl_searchMode = 1;
	srl_searchCount = 0;
	srl_searchBuffer[0] = 0;
      } else {
	const char* match = srl_findHistoryMatch(srl_searchBuffer, ++srl_searchCount);
	if (match) {
	  strlcpy(srl_inputBuffer, match, sizeof(srl_inputBuffer));
	  srl_bufferLength = strlen(srl_inputBuffer);
	}
	srl_refreshLine();	    
      }
      srl_refreshLine();
    } else if (c == 25) { // ^y
      srl_resetTabTracking();	  
      char temp[MAX_INPUT_LENGTH] = {0};
      strncpy(temp, &srl_inputBuffer[srl_cursorPos], srl_bufferLength-srl_cursorPos);	  
      srl_inputBuffer[srl_cursorPos] = 0;
      strlcat(srl_inputBuffer, srl_killBuffer, sizeof(srl_inputBuffer));
      srl_cursorPos = strlen(srl_inputBuffer);
      strlcat(srl_inputBuffer, temp, sizeof(srl_inputBuffer));
      srl_bufferLength = strlen(srl_inputBuffer);
      srl_refreshLine();	  	  
    } else if (c == '\t') {
      // Tab pressed - handle completion
      srl_handleTabCompletion();
      
    } else if (c == 127 || c == '\b') {
      // Backspace
      if (srl_searchMode) {
	const int len = strlen(srl_searchBuffer);
	if (len > 0) {
	  srl_searchBuffer[len-1] = 0;
	}
	srl_refreshLine();
      } else {
	srl_resetTabTracking(); // Reset tab completion tracking	      
	if (srl_cursorPos > 0) {
	  memmove(&srl_inputBuffer[srl_cursorPos-1], 
		  &srl_inputBuffer[srl_cursorPos], 
		  srl_bufferLength - srl_cursorPos);
	  srl_cursorPos--;
	  srl_bufferLength--;
	  srl_refreshLine();
	}
      }
      
    } else if (c == 27) {
      // Escape sequence - handle arrow keys
      char seq[3];
      if (srl_searchMode) {
	srl_searchMode = 0;
	srl_refreshLine();
      }
#ifdef _WIN32
      seq[0] = _getch();
      // Windows console sends different escape sequences
      // Handle both Windows format (ESC + single char) and ANSI format
      if (seq[0] == 'H') {
	// Windows: Up arrow = ESC H
	srl_historySelectPrev();
	continue;
      } else if (seq[0] == 'P') {
	// Windows: Down arrow = ESC P
	srl_historySelectNext();
	continue;
      } else if (seq[0] == 'M') {
	// Windows: Right arrow = ESC M
	srl_terminalCursorForward();
	continue;
      } else if (seq[0] == 'K') {
	// Windows: Left arrow = ESC K
	srl_terminalCursorBack();
	continue;
      } else if (seq[0] == '[') {
	// ANSI escape sequence (fallback)
	seq[1] = _getch();
      } else {
	continue; // Unknown escape sequence
      }
#else
      // Simple approach: read the next two characters for ANSI escape sequences
      if (read(STDIN_FILENO, &seq[0], 1) != 1)  continue;
      
      if (seq[0] != '[')  {
	switch (seq[0]) {
	case 'b': // alt-b
	  srl_resetTabTracking();
	  while (srl_cursorPos > 0 && srl_inputBuffer[srl_cursorPos-1] == ' ') {
	    srl_cursorPos--;
	  }
	  while (srl_cursorPos > 0 && srl_inputBuffer[srl_cursorPos-1] != ' ') {
	    srl_cursorPos--;
	  }
	  srl_refreshLine();
	  break;
	case 'f': // alt-f
	  srl_resetTabTracking();
	  while (srl_cursorPos < srl_bufferLength && srl_inputBuffer[srl_cursorPos+1] != ' ') {
	    srl_cursorPos++;
	  }
	  while (srl_cursorPos < srl_bufferLength && srl_inputBuffer[srl_cursorPos+1] == ' ') {
	    srl_cursorPos++;
	  }
	  while (srl_cursorPos < srl_bufferLength && srl_inputBuffer[srl_cursorPos] == ' ') {
	    srl_cursorPos++;
	  }				
	  srl_refreshLine();
	  break;		
	}
	continue;
      }
      
      if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
#endif
      
      if (seq[0] == '[') {
	switch (seq[1]) {
	case 'A': // Up arrow - previous history
	  srl_historySelectPrev();
	  break;
          
	case 'B': // Down arrow - next history
	  srl_historySelectNext();
	  break;
          
	case 'C': // Right arrow
	  srl_terminalCursorForward();
	  break;
          
	case 'D': // Left arrow
	  srl_terminalCursorBack();
	  break;
	}
      }
      
    } else if (c >= 32 && c <= 126) {
      if (srl_searchMode) {
	if (strlen(srl_searchBuffer) < MAX_INPUT_LENGTH - 1) {
	  char temp[2] = {c, 0};
	  strlcat(srl_searchBuffer, temp, sizeof(srl_searchBuffer));
	  const char* match = srl_findHistoryMatch(srl_searchBuffer, srl_searchCount);
	  if (match) {
	    strlcpy(srl_inputBuffer, match, sizeof(srl_inputBuffer));
	    srl_bufferLength = strlen(srl_inputBuffer);
	  }
	  srl_refreshLine();
	}
      } else {
	srl_resetTabTracking(); // Reset tab completion tracking	      
	// Regular printable character
	if (srl_bufferLength < MAX_INPUT_LENGTH - 1) {
	  // Make space for new character
	  memmove(&srl_inputBuffer[srl_cursorPos + 1], 
		  &srl_inputBuffer[srl_cursorPos], 
		  srl_bufferLength - srl_cursorPos);
	  
	  srl_inputBuffer[srl_cursorPos] = c;
	  srl_cursorPos++;
	  srl_bufferLength++;
	  srl_refreshLine();
	}
      }
    }
  }
  
  srl_disableRawMode();
  
  // Null-terminate and create result
  srl_inputBuffer[srl_bufferLength] = '\0';
  srl_lineRead = strdup(srl_inputBuffer);
  
  // Add to history if non-empty
  if (srl_bufferLength > 0) {
    srl_addToHistory(srl_lineRead);
  }
  
  return srl_lineRead;
}


char*
srl_escapeSpaces(const char* str)
{
  if (!str) {
    return 0;
  }
  int numSpaces = 0;
  int length = strlen(str);
  for (int i = 0; i < length; i++) {
    if (str[i] == ' ') {
      numSpaces++;
    }
  }
  char* ptr = malloc(strlen(str)+numSpaces+1);
  int j = 0;
  for (unsigned int i = 0; i < strlen(str); i++) {
    if (str[i] == ' ') {
      ptr[j++] ='\\';
    }
    ptr[j++] = str[i];
  }
  ptr[j] = 0;
  return ptr;
}

void
srl_writeHistory(void)
{
  // Custom history writing - save our internal history to file
  FILE* file = fopen(util_getHistoryFile(), "w");
  if (file) {
    for (int i = 0; i < srl_historyCount; i++) {
      fprintf(file, "%s\n", srl_history[i]);
    }
    fclose(file);
  }
}


void
srl_cleanup(void)
{
  if (srl_lineRead) {
    free(srl_lineRead);
    srl_lineRead = 0;
  }
  
  // Cleanup custom input wrapper
  srl_disableRawMode();
  
  // Free history
  for (int i = 0; i < srl_historyCount; i++) {
    free(srl_history[i]);
  }
  srl_historyCount = 0;
}


// Custom history loading
static void srl_loadHistory(void)
{
  FILE* file = fopen(util_getHistoryFile(), "r");
  if (!file) return;
  
  char line[MAX_INPUT_LENGTH];
  while (fgets(line, sizeof(line), file) && srl_historyCount < MAX_HISTORY_ENTRIES) {
    // Remove trailing newline
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
    }
    
    if (strlen(line) > 0) {
      srl_history[srl_historyCount++] = strdup(line);
    }
  }
  
  fclose(file);
}


void
srl_init(const char*(*prompt)(void),void (*complete_hook)(const char* text, const char* full_command_line), char* (*generator)(int* list_index, const char* text, int len))
{
  // Initialize custom input wrapper
  srl_prompt = prompt;
  srl_generator = generator;
  srl_completeHook = complete_hook;
  
  // Initialize state
  srl_terminalSetup = 0;
  srl_historyCount = 0;
  srl_historyIndex = -1;
  srl_cursorPos = 0;
  srl_bufferLength = 0;
  
  // Load history from file
  srl_loadHistory();
  
  // Custom input wrapper is ready!
  // No more readline dependencies or limitations!
}
