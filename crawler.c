#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <netdb.h>

// Connect to the specified host and port
static int client_socket(const char * host, const char * port){
  int err, sock;
  struct addrinfo hints, *result, *rp;

  memset(&hints, 0, sizeof(hints));

  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  err = getaddrinfo(host, port, &hints, &result);
  if (err != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
    return -1;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    // make a socket with details from result
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock == -1){ //if failed
      continue; //move to next result
    }

    //try to connect
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1){
      break;  //success
    }
    close(sock);
  }
  freeaddrinfo(result);

  return sock;
}

//Send a HTTP request over socket
static int send_request(const int sock, const char * host, char * req, const size_t req_size){

  const size_t req_len = snprintf(req, req_size, "GET / HTTP/1.1\r\nHost: %s\r\n\r\n\r\n", host);

  if(send(sock, req, req_len, 0) != req_len){
    perror("send");
    return -1;
  }

  return 0;
}

static int get_hdr_int(const char * buf, const char * key){
  char intbuf[20];

  //search for the key in buffer
  const char * hdr = strstr(buf, key);

  if(hdr){  //if found
    const char * hdr_end = strstr(hdr, "\r\n");
    if(hdr_end == NULL){  //if \r\n is not found
      return -1;
    }
    //copy header value to buf
    strncpy(intbuf, &hdr[strlen(key)+1], sizeof(intbuf));

    return atoi(intbuf);
  }else{
    return -1;
  }
}

static int receive_headers(const int sock, char * buf, const size_t buf_size, const char *hdr_end){

  int buf_offset = 0;
  const size_t hdr_end_len = strlen(hdr_end);

  while(buf_offset < buf_size){

    int buf_len = recv(sock, &buf[buf_offset], 1, 0);
		if(buf_len <= 0){
			perror("recv");
			return -1;
		}
    buf_offset += buf_len;


    //if we have the header end ( double \r\n)
    if((buf_offset >= hdr_end_len) && (strncmp(&buf[buf_offset - hdr_end_len], hdr_end, hdr_end_len) == 0)){
      break;
    }
  }
  return buf_offset;
}

//Receive content by size
static int receive_content(const int sock, int content_length, char * buf, size_t buf_size){

	while(content_length > 0){ //while we have data to receive
    //receive data
    const int buf_len = recv(sock, buf, buf_size, 0);
    if(buf_len < 0){
      perror("recv");
      break;
		}else if(buf_len == 0){
      break;
    }

    //print to screen
    fwrite(buf, 1, buf_len, stdout);
    content_length -= buf_len;
	}
  return 0;
}

//Receive chunked content
static int receive_chunked(const int sock, char * buf, size_t buf_size){

  //content receive loop
	while(1){

    //receive chunked line ( ends with \r\n)
    int buf_len = receive_headers(sock, buf, buf_size, "\r\n");
    if(buf_len <= 0){
      break;
    }

    //convert chunk size from hex to decimal
    const int content_length = strtol(buf, NULL, 16);

    //receive the data
    if(receive_content(sock, content_length, buf, buf_size) <= 0){ //if error
      break;  //stop
    }
	}

  return 0;
}

int main(const int argc, char * argv[]){
  char buf[8*1024]; //8 kb buffer
	int buf_len, content_length;

  if(argc != 3){  //we need exactly 2 arguments
    fprintf(stderr, "Usage: ./crawler host port\n");
    return EXIT_FAILURE;
  }

  const char * host = argv[1];  //first argument is the host
  const char * port = argv[2];  //second argument is the port

  //try to connect
  const int sock = client_socket(host, port);
  if(sock == -1){
    fprintf(stderr, "Error: Failed to connect to %s\n", host);
    return EXIT_FAILURE;
  }

  //send http request
  if(send_request(sock, host, buf, sizeof(buf)) < 0){
    fprintf(stderr, "Error: Failed to send request\n");
    close(sock);
    return EXIT_FAILURE;
  }

  //receive server reply
  buf_len = receive_headers(sock, buf, sizeof(buf), "\r\n\r\n");
  if(buf_len < 0){
    fprintf(stderr, "Error: Failed to receive response\n");
    close(sock);
    return EXIT_FAILURE;
  }

  // show header to screen
  fwrite(buf, 1, buf_len - 2, stderr);

  //search for content length
  content_length = get_hdr_int(buf, "Content-Length:");
  if(content_length > 0){
    receive_content(sock, content_length, buf, sizeof(buf));

  }else if(strstr(buf, "Transfer-Encoding: chunked") != NULL){     // reply may be chunked
    receive_chunked(sock, buf, sizeof(buf));

  }else{  //not content-length or transfer-encoding!
    fprintf(stderr, "Error: Unknown content length\n");
    close(sock);
    return EXIT_FAILURE;
  }

  shutdown(SHUT_RDWR, sock);
  close(sock);

  return EXIT_SUCCESS;
}
