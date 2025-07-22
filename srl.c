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
static char* srl_line_read = 0;
static const char* (*_srl_prompt)(void);
static void (*_srl_complete_hook)(const char* text, const char* full_command_line);
static char* (*_srl_generator)(int* list_index, const char* text, int len);

// Two-stage tab completion tracking
static char last_completion_buffer[MAX_INPUT_LENGTH];
static int last_completion_cursor = -1;
static int last_completion_length = -1;

// Forward declarations
static void reset_tab_tracking(void);
static void enable_raw_mode(void);
static void disable_raw_mode(void);
static void refresh_line(void);
static void add_to_history(const char* line);
static void handle_tab_completion(void);
static void load_history(void);

// Reset tab completion tracking
static void reset_tab_tracking(void) {
    last_completion_cursor = -1;
    last_completion_length = -1;
}

// Terminal control
#ifdef _WIN32
static HANDLE hStdin;
static DWORD original_mode;
#else
static struct termios original_termios;
#endif
int _srl_inside_quotes_flag = 0;
static int terminal_setup = 0;
static int terminal_width = 0;
static int terminal_height = 0;
static int terminal_min_row = 0;

// Input buffer and cursor management
static char input_buffer[MAX_INPUT_LENGTH];
static int cursor_pos = 0;
static int buffer_length = 0;
static char kill_buffer[MAX_INPUT_LENGTH] = {0};

// History management
#define MAX_HISTORY_ENTRIES 100
static char* history[MAX_HISTORY_ENTRIES];
static int history_count = 0;
static int history_index = -1;

// Terminal control functions
static void enable_raw_mode(void) {
    if (terminal_setup) return;
    
#ifdef _WIN32
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &original_mode);
    SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);
#else
    tcgetattr(STDIN_FILENO, &original_termios);
    struct termios raw = original_termios;
    
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cflag |= CS8;
    raw.c_oflag &= ~(OPOST);
    
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
    terminal_setup = 1;
}

static void disable_raw_mode(void) {
    if (!terminal_setup) return;
    
    // Robust terminal cleanup: move cursor to column 1 and clear line
    printf("\r\033[K"); // \r = move to column 1, \033[K = clear to end of line
    fflush(stdout);
    
#ifdef _WIN32
    SetConsoleMode(hStdin, original_mode);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
#endif
    terminal_setup = 0;
}

// Cursor and display functions

#ifdef _WIN32
static int
srl_get_terminal_size(int *rows, int *cols) {
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
srl_get_cursor_position(int *row, int *col) {
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
srl_get_terminal_size(int *rows, int *cols)
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
srl_get_cursor_position(int *row, int *col) {
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
refresh_line(void)
{
  int total_width = strlen(_srl_prompt()) + buffer_length;
  int rows = (total_width / terminal_width);
  int cursor_row = terminal_height-rows;
  int cursor_width = strlen(_srl_prompt()) + cursor_pos + 1;
  int cursor_rows = (cursor_width / terminal_width);
  int cursor_cols = cursor_width % terminal_width;  

  printf("\033[?25l"); // hide cursor
  
  while (terminal_min_row > cursor_row) {
    printf("\033D"); // scroll      
    terminal_min_row--;
  }

  printf("\033[%d;0H", terminal_min_row);  // move cursor to v,h
  printf("%s", _srl_prompt ? _srl_prompt() : "");
  printf("%.*s", buffer_length, input_buffer);
  printf("\033[J\r"); //clear screen from cursor down    
  printf("\033[%d;%dH", terminal_min_row+cursor_rows, cursor_cols);  // move cursor to v,h

  printf("\033[?25h"); // Show cursor
  
  fflush(stdout);
}

// History functions
static void add_to_history(const char* line) {
    if (!line || !*line) return;
    
    // Check if same as last entry
    if (history_count > 0 && strcmp(history[history_count-1], line) == 0) {
        return;
    }
    
    if (history_count >= MAX_HISTORY_ENTRIES) {
        free(history[0]);
        for (int i = 0; i < MAX_HISTORY_ENTRIES-1; i++) {
            history[i] = history[i+1];
        }
        history_count = MAX_HISTORY_ENTRIES - 1;
    }
    
    history[history_count++] = strdup(line);
}

// Tab completion handling
static void handle_tab_completion(void) {
    if (!_srl_complete_hook || !_srl_generator) return;
    
    // Extract text for completion from current buffer position
    char completion_text[MAX_INPUT_LENGTH];
    int completion_start = cursor_pos;
    
    // Smart completion start detection - handle quotes properly
    // Find the opening quote by looking for unmatched quotes
    int quote_start = -1;
    int quote_count = 0;
    for (int i = cursor_pos - 1; i >= 0; i--) {
        if (input_buffer[i] == '"') {
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
        while (completion_start > 0 && input_buffer[completion_start-1] != ' ') {
            completion_start--;
        }
    }
    
    int completion_len = cursor_pos - completion_start;
    strncpy(completion_text, &input_buffer[completion_start], completion_len);
    completion_text[completion_len] = '\0';
    
    // Store quote context in a simple global flag for the completion generator
    _srl_inside_quotes_flag = (quote_start >= 0);
    
    // Extract quoted path information if needed (declare at function scope)
    char full_quoted_path[1000];
    int quoted_len = 0;
    
    // For quoted paths, we need to pass the full quoted path context, not just the completion text
    if (quote_start >= 0) {
        // Extract the full path inside quotes for proper base directory parsing
        // Find the closing quote or use cursor position
        int quote_end = cursor_pos;
        for (int i = quote_start + 1; i < cursor_pos; i++) {
            if (input_buffer[i] == '"') {
                quote_end = i;
                break;
            }
        }
        quoted_len = quote_end - quote_start - 1;  // Content between quotes
        strncpy(full_quoted_path, &input_buffer[quote_start + 1], quoted_len);
        full_quoted_path[quoted_len] = '\0';

        _srl_complete_hook(full_quoted_path, input_buffer);
    } else {
        _srl_complete_hook(completion_text, input_buffer);
    }
    
    // Collect all possible completions to handle ambiguity
    char* completions[50]; // Max 50 completions
    int completion_count = 0;
    int list_index = 0;
    
    // Gather all matching completions
    char* completion;
    while ((completion = _srl_generator(&list_index, completion_text, completion_len)) != NULL && completion_count < 50) {
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
        int is_consecutive_tab = (last_completion_cursor == cursor_pos && 
                                  last_completion_length == buffer_length &&
                                  strncmp(last_completion_buffer, input_buffer, buffer_length) == 0);
        
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
            
            if (buffer_length + size_diff < MAX_INPUT_LENGTH) {
                // Make space in buffer if needed
                if (size_diff > 0) {
                    memmove(&input_buffer[cursor_pos + size_diff], 
                           &input_buffer[cursor_pos], 
                           buffer_length - cursor_pos);
                } else if (size_diff < 0) {
                    memmove(&input_buffer[cursor_pos + size_diff], 
                           &input_buffer[cursor_pos], 
                           buffer_length - cursor_pos);
                }
                
                // Insert the partial completion
                memcpy(&input_buffer[cursor_pos - replace_len], partial_completion, new_len);
                cursor_pos += size_diff;
                buffer_length += size_diff;
                
                refresh_line();
            }
            
            
            // Update tab completion tracking for next tab press
            last_completion_cursor = cursor_pos;
            last_completion_length = buffer_length;
            memcpy(last_completion_buffer, input_buffer, buffer_length);
            
            free(partial_completion);
            return; // Don't show suggestions on first tab - only partial completion
        }
        
        // Second tab or no partial completion possible - display all matches
        if (is_consecutive_tab || extended_common_len == 0) {
            printf("\n");
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
        
            // Properly refresh the current line using our existing refresh function
            refresh_line();
            fflush(stdout);
        
            // Free all completion strings
            for (int i = 0; i < completion_count; i++) {
                free(completions[i]);
            }
            
            // Reset tab completion tracking after showing suggestions
            last_completion_cursor = -1;
            last_completion_length = -1;
            
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
        if (buffer_length + size_diff >= MAX_INPUT_LENGTH) {
            free(completion);
            return; // Buffer would overflow
        }
        
        // Move text after cursor if growing
        if (size_diff > 0) {
            memmove(&input_buffer[cursor_pos + size_diff], 
                   &input_buffer[cursor_pos], 
                   buffer_length - cursor_pos);
        } else if (size_diff < 0) {
            memmove(&input_buffer[cursor_pos + size_diff], 
                   &input_buffer[cursor_pos], 
                   buffer_length - cursor_pos);
        }
        
        // Replace the text
        memcpy(&input_buffer[completion_start], completion, new_len);
        
        // Update positions
        buffer_length += size_diff;
        cursor_pos = completion_start + new_len;
        
        // Perfect display: No corruption, no extra spaces!
        refresh_line();
    }
    
    // Free all completion strings
    for (int i = 0; i < completion_count; i++) {
        free(completions[i]);
    }
}

char *
srl_gets(void)
{
    enable_raw_mode();

    srl_get_terminal_size(&terminal_height, &terminal_width);
    srl_get_cursor_position(&terminal_min_row, 0);
  
    if (srl_line_read) {
        free(srl_line_read);
        srl_line_read = NULL;
    }
    
    
    // Initialize input state
    cursor_pos = 0;
    buffer_length = 0;
    history_index = -1;
    memset(input_buffer, 0, MAX_INPUT_LENGTH);
    
    // Display prompt
    printf("%s", _srl_prompt ? _srl_prompt() : "");
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
                    reset_tab_tracking();
                    if (history_count > 0) {
                        if (history_index == -1) history_index = history_count - 1;
                        else if (history_index > 0) history_index--;
                        
                        if (history_index >= 0) {
                            strcpy(input_buffer, history[history_index]);
                            buffer_length = strlen(input_buffer);
                            cursor_pos = buffer_length;
                            refresh_line();
                        }
                    }
                    continue;
                    
                case 80: // Down arrow
                    reset_tab_tracking();
                    if (history_index >= 0) {
                        history_index++;
                        if (history_index >= history_count) {
                            history_index = -1;
                            input_buffer[0] = '\0';
                            buffer_length = 0;
                            cursor_pos = 0;
                        } else {
                            strcpy(input_buffer, history[history_index]);
                            buffer_length = strlen(input_buffer);
                            cursor_pos = buffer_length;
                        }
                        refresh_line();
                    }
                    continue;
                    
                case 77: // Right arrow
                    reset_tab_tracking();
                    if (cursor_pos < buffer_length) {
                        cursor_pos++;
                        printf("\033[C");
                        fflush(stdout);
                    }
                    continue;
                    
                case 75: // Left arrow
                    reset_tab_tracking();
                    if (cursor_pos > 0) {
                        cursor_pos--;
                        printf("\033[D");
                        fflush(stdout);
                    }
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
            printf("\n");
            break;

	} else if (c == 1) { // ^a
	  reset_tab_tracking(); // Reset tab completion tracking
	  cursor_pos = 0;	  
	  refresh_line();
	} else if (c == 2) { // ^b
	  reset_tab_tracking();
	  if (cursor_pos > 0) {
	    cursor_pos--;
	  }
	  refresh_line();
	} else if (c == 4) { // ^d
	  reset_tab_tracking(); // Reset tab completion tracking
	  if (buffer_length > cursor_pos) {
	    memmove(&input_buffer[cursor_pos], 
		    &input_buffer[cursor_pos+1], 
		    buffer_length - cursor_pos);
	    buffer_length--;
	    refresh_line();
	  }
	} else if (c == 5) { // ^e
	  reset_tab_tracking(); // Reset tab completion tracking
	  if (cursor_pos < buffer_length) {
	    cursor_pos = buffer_length;	  	  
	  }
	  refresh_line();	  
	} else if (c == 6) { // ^f
	  reset_tab_tracking();
	  if (cursor_pos < buffer_length) {
	    cursor_pos++;
	    refresh_line();
	  }
	} else if (c == 11) { // ^k
	  reset_tab_tracking();
	  memset(kill_buffer, 0, sizeof(kill_buffer));
	  strncpy(kill_buffer, &input_buffer[cursor_pos], buffer_length-cursor_pos);
	  input_buffer[cursor_pos] = 0;
	  buffer_length = cursor_pos;
	  refresh_line();
	} else if (c == 12) { // ^l
	  reset_tab_tracking();
	  memset(input_buffer, 0, MAX_INPUT_LENGTH);	  
	  terminal_min_row = 1;
	  buffer_length = cursor_pos = 0;
	  printf("\033[H");
	  refresh_line();
	} else if (c == 25) { // ^y
	  reset_tab_tracking();	  
	  char temp[MAX_INPUT_LENGTH] = {0};
	  strncpy(temp, &input_buffer[cursor_pos], buffer_length-cursor_pos);	  
	  input_buffer[cursor_pos] = 0;
	  strlcat(input_buffer, kill_buffer, sizeof(input_buffer));
	  cursor_pos = strlen(input_buffer);
	  strlcat(input_buffer, temp, sizeof(input_buffer));
	  buffer_length = strlen(input_buffer);
	  refresh_line();	  	  
        } else if (c == '\t') {
            // Tab pressed - handle completion
            handle_tab_completion();
            
        } else if (c == 127 || c == '\b') {
            // Backspace
            reset_tab_tracking(); // Reset tab completion tracking
            if (cursor_pos > 0) {
                memmove(&input_buffer[cursor_pos-1], 
                       &input_buffer[cursor_pos], 
                       buffer_length - cursor_pos);
                cursor_pos--;
                buffer_length--;
                refresh_line();
            }
            
        } else if (c == 27) {
            // Escape sequence - handle arrow keys
            char seq[3];
#ifdef _WIN32
            seq[0] = _getch();
            // Windows console sends different escape sequences
            // Handle both Windows format (ESC + single char) and ANSI format
            if (seq[0] == 'H') {
                // Windows: Up arrow = ESC H
                reset_tab_tracking();
                if (history_count > 0) {
                    if (history_index == -1) history_index = history_count - 1;
                    else if (history_index > 0) history_index--;
                    
                    if (history_index >= 0) {
                        strcpy(input_buffer, history[history_index]);
                        buffer_length = strlen(input_buffer);
                        cursor_pos = buffer_length;
                        refresh_line();
                    }
                }
                continue;
            } else if (seq[0] == 'P') {
                // Windows: Down arrow = ESC P
                reset_tab_tracking();
                if (history_index >= 0) {
                    history_index++;
                    if (history_index >= history_count) {
                        history_index = -1;
                        input_buffer[0] = '\0';
                        buffer_length = 0;
                        cursor_pos = 0;
                    } else {
                        strcpy(input_buffer, history[history_index]);
                        buffer_length = strlen(input_buffer);
                        cursor_pos = buffer_length;
                    }
                    refresh_line();
                }
                continue;
            } else if (seq[0] == 'M') {
                // Windows: Right arrow = ESC M
                reset_tab_tracking();
                if (cursor_pos < buffer_length) {
                    cursor_pos++;
		    refresh_line();		    
                    fflush(stdout);
                }
                continue;
            } else if (seq[0] == 'K') {
                // Windows: Left arrow = ESC K
                reset_tab_tracking();
                if (cursor_pos > 0) {
                    cursor_pos--;
		    refresh_line();
                }
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
		reset_tab_tracking();
		while (cursor_pos > 0 && input_buffer[cursor_pos-1] == ' ') {
		   cursor_pos--;
		}
		while (cursor_pos > 0 && input_buffer[cursor_pos-1] != ' ') {
		  cursor_pos--;
		}
		refresh_line();
		break;
	      case 'f': // alt-f
		reset_tab_tracking();
		while (cursor_pos < buffer_length && input_buffer[cursor_pos+1] != ' ') {
		   cursor_pos++;
		}
		while (cursor_pos < buffer_length && input_buffer[cursor_pos+1] == ' ') {
		   cursor_pos++;
		}
		while (cursor_pos < buffer_length && input_buffer[cursor_pos] == ' ') {
		   cursor_pos++;
		}				
		refresh_line();
		break;		
	      }
	      continue;
	    }

            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
#endif

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': // Up arrow - previous history
                        reset_tab_tracking(); // Reset tab completion tracking
                        if (history_count > 0) {
                            if (history_index == -1) history_index = history_count - 1;
                            else if (history_index > 0) history_index--;
                            
                            if (history_index >= 0) {
                                strcpy(input_buffer, history[history_index]);
                                buffer_length = strlen(input_buffer);
                                cursor_pos = buffer_length;
                                refresh_line();
                            }
                        }
                        break;
                        
                    case 'B': // Down arrow - next history
                        reset_tab_tracking(); // Reset tab completion tracking
                        if (history_index >= 0) {
                            history_index++;
                            if (history_index >= history_count) {
                                history_index = -1;
                                input_buffer[0] = '\0';
                                buffer_length = 0;
                                cursor_pos = 0;
                            } else {
                                strcpy(input_buffer, history[history_index]);
                                buffer_length = strlen(input_buffer);
                                cursor_pos = buffer_length;
                            }
                            refresh_line();
                        }
                        break;
                        
                    case 'C': // Right arrow
                        reset_tab_tracking(); // Reset tab completion tracking
                        if (cursor_pos < buffer_length) {
			    cursor_pos++;
			    refresh_line();
                        }
                        break;
                        
                    case 'D': // Left arrow
                        reset_tab_tracking(); // Reset tab completion tracking
                        if (cursor_pos > 0) {
                            cursor_pos--;
			    refresh_line();			    
                        }
                        break;
                }
            }
            
        } else if (c >= 32 && c <= 126) {
            // Regular printable character
            reset_tab_tracking(); // Reset tab completion tracking
            if (buffer_length < MAX_INPUT_LENGTH - 1) {
                // Make space for new character
                memmove(&input_buffer[cursor_pos + 1], 
                       &input_buffer[cursor_pos], 
                       buffer_length - cursor_pos);
                
                input_buffer[cursor_pos] = c;
                cursor_pos++;
                buffer_length++;
		refresh_line();
            }
        }
    }
    
    disable_raw_mode();
    
    // Null-terminate and create result
    input_buffer[buffer_length] = '\0';
    srl_line_read = strdup(input_buffer);
    
    // Add to history if non-empty
    if (buffer_length > 0) {
        add_to_history(srl_line_read);
    }
    
    return srl_line_read;
}


char*
srl_escape_spaces(const char* str)
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
srl_write_history(void)
{
  // Custom history writing - save our internal history to file
  FILE* file = fopen(util_getHistoryFile(), "w");
  if (file) {
    for (int i = 0; i < history_count; i++) {
      fprintf(file, "%s\n", history[i]);
    }
    fclose(file);
  }
}


void
srl_cleanup(void)
{
  if (srl_line_read) {
    free(srl_line_read);
    srl_line_read = 0;
  }
  
  // Cleanup custom input wrapper
  disable_raw_mode();
  
  // Free history
  for (int i = 0; i < history_count; i++) {
    free(history[i]);
  }
  history_count = 0;
}


// Custom history loading
static void load_history(void) {
  FILE* file = fopen(util_getHistoryFile(), "r");
  if (!file) return;
  
  char line[MAX_INPUT_LENGTH];
  while (fgets(line, sizeof(line), file) && history_count < MAX_HISTORY_ENTRIES) {
    // Remove trailing newline
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
    }
    
    if (strlen(line) > 0) {
      history[history_count++] = strdup(line);
    }
  }
  
  fclose(file);
}


void
srl_init(const char*(*prompt)(void),void (*complete_hook)(const char* text, const char* full_command_line), char* (*generator)(int* list_index, const char* text, int len))
{
  // Initialize custom input wrapper
  _srl_prompt = prompt;
  _srl_generator = generator;
  _srl_complete_hook = complete_hook;
  
  // Initialize state
  terminal_setup = 0;
  history_count = 0;
  history_index = -1;
  cursor_pos = 0;
  buffer_length = 0;
  
  // Load history from file
  load_history();
  
  // Custom input wrapper is ready!
  // No more readline dependencies or limitations!
}
