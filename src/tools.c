/*
 * This file is part of the sse package, copyright (c) 2011, 2012, @radiospiel.
 * It is copyrighted under the terms of the modified BSD license, see LICENSE.BSD.
 *
 * For more information see https://https://github.com/radiospiel/sse.
 */
#include <jansson.h>
#include "sse.h"
#include "http.h"

#if defined(__APPLE__) && defined(__MACH__)

extern char **environ;
static int execvpe(const char *program, char **argv, char **envp)
{
    char **saved = environ;
    environ = envp;
    
    int rc = execvp(program, argv);
    
    environ = saved;
    return rc;
}

#endif

static char* sse_environment[MAX_HEADERS];

/*
 * copy headers into sse_environment and prefix those with SSE_.
 */
static void build_sse_environment(char** headers)
{
  char** destHeader = sse_environment;

  while(*headers) {
    *destHeader = malloc(strlen(*headers) + 4);
    strcpy(*destHeader, "SSE_");
    strcat(*destHeader, *headers);

    headers++, destHeader++;
  }

  *destHeader = 0;
}

/*
 * print sse environment.
 */

void fprint_list(FILE* out, char** h)
{
  while(*h) {
    fputs(*h++, out);
    fputs("\n", out);
  }
}

/*
 * free sse environment.
 */
static void free_sse_environment()
{
  char* const* h = sse_environment;

  while(*h) {
    free(*h);
    ++h;
  }
  
  sse_environment[0] = 0;
}

static char* run_command(const char* data, char** command, char** environment);

void log_sse_event(char** headers, const char* data)
{
  char* event_id = 0;
  char* event_type = 0;
  char** p;
  for(p = headers; (!event_id || !event_type) && *p; ++p) {
    if(strncmp(*p, "ID=", 3) == 0)
      event_id = *p + 3;
    else if(strncmp(*p, "TYPE=", 5) == 0)
      event_type = *p + 5;
  }

  fprintf(stderr, "EVENT %s:%s (%d byte)\n", event_type ? event_type : "event", 
                      event_id ? event_id : "<none>", (int) strlen(data));
}

//TODO: transform data to json here?
// seems that the on_sse_event is called in flush function after a
// buffering period
// call sequence: main -> sse_main -> http -> on_data callback -> parse_sse
void on_sse_event(char** headers, const char* data, const char* reply_url)
{
  log_sse_event(headers, data);
  
  char* result = 0;
  
  json_t *root = NULL;
  json_error_t error;
  root = json_loads(data, 0, &error);
  json_t *ticket;
  ticket = json_object_get(root, "ticket");

  if(!json_is_object(root))
  {
      fprintf(stderr, "error: data is not a json object\n");
  }

  const char *str;
  str = json_string_value(ticket);
  printf("ticket: %s\n", str);

  if(options.command) {
    printf("IN IF\n");
    build_sse_environment(headers);

    result = run_command(data, options.command, sse_environment);
    free_sse_environment();
  }
  else {
    fprint_list(stdout, headers);
    fputs(data, stdout);
    fputs("\n\n", stdout);
  }

  if(reply_url) {
    char* body = result ? result : "";
    const char* reply_headers[] = {
      "Content-Type:",
      NULL
    };

    fprintf(stderr, "REPLY %s (%d byte)\n", reply_url, (int) strlen(body));
    http(HTTP_POST, reply_url, reply_headers, body, strlen(body), http_ignore_data, 0);
  }

  free(result);
  
  if(options.limit && 0 == --options.limit) {
    exit(0);
  }
}

/*
 * Run \a command, with the given \a environment, pass in \a data 
 * into the subprocess' STDIN, and return the subprocess' STDOUT.
 *
 * Make sure to free the returned memory.
 */
static char* run_command(const char* data, char** command, char** environment)
{
  // Build pipes for subprocess
  int pipe_to_child[2];
  if (pipe(pipe_to_child) != 0) die("pipe");

  int pipe_from_child[2];
  if (pipe(pipe_from_child) != 0) die("pipe");
  
  // Start subprocess
  pid_t pid = fork();
  if (pid == -1) die("fork");
  
  char* read_data = 0;
  
  if (pid == 0) { /* the child */
    dup2(pipe_to_child[FD_STDIN], FD_STDIN);
    close(pipe_to_child[FD_STDOUT]);
    
    dup2(pipe_from_child[FD_STDOUT], FD_STDOUT);
    close(pipe_from_child[FD_STDIN]);

    char* ret;
    asprintf(&ret, "Running %s\n", command[0]);
    logger(1, ret, 0, 0);
    free(ret);
    
    execvpe(command[0], command, environment);
    _die(command[0]);  /* die via _exit: a failed child should not flush parent files */
  }
  else { /* code for parent */ 
    close(pipe_to_child[FD_STDIN]);
    write_all(pipe_to_child[FD_STDOUT], data, strlen(data));
    close(pipe_to_child[FD_STDOUT]);

    close(pipe_from_child[FD_STDOUT]);
    read_all(pipe_from_child[FD_STDIN], &read_data, RESPONSE_LIMIT);
    close(pipe_from_child[FD_STDIN]);

    int status = 0;
    waitpid(pid, &status, 0);
    
    // Show results if something broke.
    if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
      fprintf(stderr, "child exited with stats %d\n", WEXITSTATUS(status));
    else if(WIFSIGNALED(status))
      fprintf(stderr, "child exited of signal %d\n", WTERMSIG(status));
  }
  
  return read_data;
}

/*
 * write dataLen bytes from data to the fd handle.
 */
int write_all(int fd, const char* data, unsigned dataLen) {
  const char *s = data, *e = data + dataLen;
  
  while(data < e) {
    int written = write(fd, data, e - data);
    if(written < 0)
      return -1;

    data += written;
  }

  return e - s;
}

/*
 * read data from fd handle, return a malloced area in the pResult 
 * buffer - this must be freed by the caller - and returns the number
 * of bytes read.
 */
int read_all(int fd, char** pResult, size_t limit) {
  char buf[8192]; 
  int length = 0;
  
  *pResult = 0;
  
  while(1) {
    size_t bytes_to_read = limit ? limit - length : sizeof(buf);
    if(!bytes_to_read) break;
    
    int bytes_read = read(fd, buf, bytes_to_read);

    if(bytes_read < 0) {
      free(*pResult);
      *pResult = 0;
      return -1;
    }

    if(bytes_read > 0) {
      *pResult = realloc(*pResult, length + bytes_read + 1);

      memcpy(*pResult + length, buf, bytes_read);
      length += bytes_read;
    }

    if(bytes_read == 0)
      break;
  }

  if(*pResult) {
    (*pResult)[length] = 0;
  }
  return length;
}

void die(const char* msg) {
  perror(msg); 
  exit(1);
}

void _die(const char* msg) {
  perror(msg); 
  _exit(1);
}

/*
 * logging: when \a options.verbosity is greater than or equal \a verbosity
 * this function writes out \a data to stderr.
 *
 * Note: this is not an excessively well-performing logging method. 
 */
void logger(int verbosity, const char* data, unsigned len, const char* sep)
{
  if(options.verbosity < verbosity) return;

  if(!len)
    len = strlen(data);

  if(sep)
    fputs(sep, stderr);
  
  for(; len--; ++data) {
    fputc(*data, stderr);
    
    if(sep && *data == '\n')
      fputs(sep, stderr);
  }
}

/*
 * string ends equal: returns pattern in string ends in pattern,
 * returns NULL otherwise.
 */
const char* streeq(const char* string, const char* pattern) {
  if(!*pattern || !*pattern) return string;
  if(!string || !*string) return NULL;
  
  size_t string_len = strlen(string);
  size_t pattern_len = strlen(pattern);
  if(string_len < pattern_len) return NULL;

  string += string_len - pattern_len;
  return strcmp(string, pattern) == 0 ? string : NULL;
}

/*
 * string starts equal: returns string after pattern if string starts
 * in pattern,
 * returns NULL otherwise.
 */
const char* strseq(const char* string, const char* pattern) {
  if(!*pattern || !*pattern) return string;
  if(!string || !*string) return NULL;
  
  size_t pattern_len = strlen(pattern);

  return strncmp(string, pattern, pattern_len) == 0 ? string + pattern_len : NULL;
}
