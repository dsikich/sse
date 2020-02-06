/*
 * This file is part of the sse package, copyright (c) 2011, 2012, @radiospiel.
 * It is copyrighted under the terms of the modified BSD license, see LICENSE.BSD.
 *
 * For more information see https://https://github.com/radiospiel/sse.
 */

#include <regex.h>
#include "sse.h"
#include "http.h"

/*
 * process command line.
 *
 * This method fills in the options structure. If the passed in options
 * are invalid it shows the help and exits the process.
 */
static void parse_arguments(int argc, char** argv);

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{  
  parse_sse(ptr, size * nmemb);
  return size * nmemb;
} 

static const char* verify_sse_response(CURL* curl) {
  #define EXPECTED_CONTENT_TYPE "text/event-stream"
  
  static const char expected_content_type[] = EXPECTED_CONTENT_TYPE;

  char* content_type;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type); 
  if(!content_type) content_type = "";

  if(!strncmp(content_type, expected_content_type, strlen(expected_content_type)))
    return 0;
  
  return "Invalid content_type, should be '" EXPECTED_CONTENT_TYPE "'.";
}

int sse_main(int argc, char** argv) 
{
  /* pass in arguments that will be used in REST call/connection*/
  parse_arguments(argc, argv);

  const char* headers[] = {
    "Accept: text/event-stream",
    NULL
  };

  /* calls libcurl */
  http(HTTP_GET, options.url, headers, 0, 0, on_data, verify_sse_response);
  return 0;
}

static char* help[] = {
  "",
  "sse [ <options> ] URL [ <command> ... ]",
  "",
  "sse connects to an URL, where it expects a stream of server sent events. "
  "On each incoming event it runs a command specified on the command line, passing "
  "in event data via process environment.",
  "",
  "Options include:",
  "",
  "  -a <ca>      ... set PEM CA file",
  "  -c <cert>    ... set PEM certificate file",
  "  -i           ... insecure: allow HTTP and non-certified HTTPS connections",
  "  -l <limit>   ... limit number of events",
  "  -v           ... be verbose; can be set multiple times",
  "",
  "On each incoming event the <command> is run. The event's data attribute is written "
  "to the command's standard input, all other attributes are written to the environment "
  "(as SSE_EVENT, SSE_ID, ... entries.)",
  "",
  "If a SSE \"reply\" attribute is set, sse also posts the command's result "
  "to the URL specified there.",
  NULL
};

static void usage() {
  fprint_list(stderr, help);
  fprintf(stderr, "\nThis is %s, compiled %s %s.\n\n", options.arg0, __DATE__, __TIME__);

  exit(1);
}

static void parse_arguments(int argc, char** argv)
{
  /* set default url and allow_insecure is always set to true for now */
  options.allow_insecure = 1;
  options.url = "https://10.25.24.156:8080/v1/stream/cray-logs-containers?batchsize=4&count=2&streamID=stream1";
  //options.url = "https://10.25.24.156:8080/v1/stream/cray-logs-containers";
    
  while(1) {
    int ch = getopt(argc, argv, "vic:a:l:?h");
    if(ch == -1) break;
    
    switch (ch) {
    case 'c': options.ssl_cert = optarg; break;
    case 'a': options.ca_info = optarg; break;
    case 'i': options.allow_insecure = 1; break;
    case 'l': options.limit = atol(optarg); break;
    case 'v': options.verbosity += 1; break;
    case '?':
    case 'h':
    default:
      usage();
    }
  }

  argc -= optind;
  argv += optind;

  if(*argv) {
    options.url = *argv++;
  }
  
  if(!options.url)
    usage();
    
  if(!options.allow_insecure) {
    if(strncmp(options.url, "https:", 6)) {
      fprintf(stderr, "Insecure connections not allowed, use -i, if necessary.\n");
      exit(1);
    }
  } 
}
