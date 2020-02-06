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

void fprint_list(FILE* out, char** h)
{
  while(*h) {
    fputs(*h++, out);
    fputs("\n", out);
  }
}

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

void parse_json(const char* data)
{
  json_t *root = NULL;
  json_error_t error;
  root = json_loads(data, 0, &error);
  json_t *metrics;
  json_t *messages;
  json_t *msg_array_0;
  json_t *msg;
  const char *msg_str;

  metrics  = json_object_get(root, "metrics");
  if(!json_is_object(metrics))
  {
      fprintf(stderr, "error: metrics is not a json object\n");
  }

  messages = json_object_get(metrics, "messages");
  if(!json_is_array(messages))
  {
      fprintf(stderr, "error: messages is not a json array\n");
  }
  msg_array_0 = json_array_get(messages, 0);

  if(!json_is_object(msg_array_0))
  {
      fprintf(stderr, "error: first element of messages is not a json object\n");
  }
  msg = json_object_get(msg_array_0, "message");

  /* now print the first element of the messages array as a string */
  msg_str = json_string_value(msg);
  printf("message: %s\n", msg_str);
}

//TODO: transform data to json here?
// seems that the on_sse_event is called in flush function after a
// buffering and parsing period with flex
// call sequence: main -> sse_main -> http -> on_data callback -> parse_sse
void on_sse_event(char** headers, const char* data, const char* reply_url)
{
  char* result = 0;
  
  /* print out parsed data -- NOT JSON yet */
  fprint_list(stdout, headers);
  fputs(data, stdout);
  fputs("\n\n", stdout);

  /* example of parsing and converting to json */
  parse_json(data);

  if(reply_url) {
    printf("REPLY URL\n");
    char* body = result ? result : "";
    const char* reply_headers[] = {
      "Content-Type:",
      NULL
    };

    fprintf(stderr, "REPLY %s (%d byte)\n", reply_url, (int) strlen(body));
    http(HTTP_POST, reply_url, reply_headers, body, strlen(body), http_ignore_data, 0);
  }

  free(result);
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
