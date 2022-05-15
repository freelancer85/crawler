#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

//Default index page, if uri is only /
static const char * index_page = "index.html";

// Create a server listen socket
static int server_socket(const int port){
  int on = 1;
  struct sockaddr_in addr;

  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock < 0){
    perror("socket");
    return -1;
  }

  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) == -1){
    perror("setsockopt");
    return -1;
  }

  memset(&addr, 0, sizeof(struct sockaddr_in));

  addr.sin_family	= AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); //any ip address
  addr.sin_port	= htons(port);              //our listening port

  if(bind(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_in)) < 0 ){
    perror("bind");
    return -1;
  }

  if(listen(sock, 5) < 0){
    perror("listen");
    return -1;
  }

  return sock;
}

static int accept_client(const int sock){
  char ip[INET_ADDRSTRLEN];
  struct sockaddr_in client_addr;
  size_t len = sizeof(struct sockaddr_in);

  const int client_sock = accept(sock, (struct sockaddr *) &client_addr, (socklen_t *) &len);
  if(client_sock < 0){
    perror("accept");
    return -1;
  }

  inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);

  printf("Peer %s connected on port %d\n", ip, ntohs(client_addr.sin_port));

  return client_sock;
}

//receive headers sent by client
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

//Check if request is valid
static int validate_request(const int sock ,char * buf, const size_t buf_size){
  char html[200];
  int fd, buf_len, html_len;

  //extract requested page
  const char * method = strtok(buf, " ");
  const char * uri = strtok(NULL, " ");

  if((method == NULL) || (uri == NULL)){
    //make an HTML with the error
    html_len = snprintf(html, sizeof(html), "<html><body><h1>%s</h1></body></html>", "Bad Request");
    buf_len = snprintf(buf, buf_size, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: Closed\r\n\r\n", 400, "Bad Request", html_len);
    if( (write(sock, buf,   buf_len) != buf_len) ||
        (write(sock, html, html_len) != html_len)){
      perror("write");
    }
    return -1;
  }

  if(strcmp(method, "GET") != 0){

    html_len = snprintf(html, sizeof(html), "<html><body><h1>%s</h1></body></html>", "Method Not Allowed");
    buf_len = snprintf(buf, buf_size, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: Closed\r\n\r\n", 405, "Method Not Allowed", html_len);
    if( (write(sock, buf,   buf_len) != buf_len) ||
        (write(sock, html, html_len) != html_len)){
      perror("write");
    }
    return -1;
  }

  //if uri starts with "/"
  if(uri[0] == '/'){  //ex. /index.html
    uri++;
    if(uri[0] == '\0'){ //if user requested /
      uri = index_page; //redurect him to index.html
    }
  }

  if(access(uri, F_OK) != 0){
    html_len = snprintf(html, sizeof(html), "<html><body><h1>%s</h1></body></html>", "Not Found");
    buf_len = snprintf(buf, buf_size, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: Closed\r\n\r\n", 404, "Not Found", html_len);
    if( (write(sock, buf,   buf_len) != buf_len) ||
        (write(sock, html, html_len) != html_len)){
      perror("write");
    }
    return -1;
  }

  //open the requested file
  fd = open(uri, O_RDONLY);

  if(fd == -1){ //if open failed
    //send an error message to client
    html_len = snprintf(html, sizeof(html), "<html><body><h1>%s</h1></body></html>", "Internal Server Error");
    buf_len = snprintf(buf, buf_size, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: Closed\r\n\r\n", 500, "Internal Server Error", html_len);
    if( (write(sock, buf,   buf_len) != buf_len) ||
        (write(sock, html, html_len) != html_len)){
      perror("write");
    }

    perror("open");
    return -1;
  }

  //return file descriptor
  return fd;
}

static int http_handler(const int sock){
  char buf[8*1024]; //8 kb buffer
  char html[200];
	int html_len, buf_len, fd;
  struct stat st;

  buf_len = receive_headers(sock, buf, sizeof(buf), "\r\n\r\n");
  if(buf_len <= 0){
    fprintf(stderr, "Error: Failed to receive request\n");
    return -1;
  }

  fd = validate_request(sock, buf, sizeof(buf));
  if(fd < 0){
    return -1;
  }

  if(fstat(fd, &st) == -1){
    //send an error message to client
    html_len = snprintf(html, sizeof(html), "<html><body><h1>%s</h1></body></html>", "Internal Server Error");
    buf_len = snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: Closed\r\n\r\n", 500, "Internal Server Error", html_len);
    if( (write(sock, buf,   buf_len) != buf_len) ||
        (write(sock, html, html_len) != html_len)){
      perror("write");
    }
    perror("fstat");
    return -1;
  }

  //send OK reply
  buf_len = snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\nContent-Length: %lu\r\nConnection: Closed\r\n\r\n", 200, "OK", st.st_size);
  if(write(sock, buf, buf_len) != buf_len){
    perror("write");
  }

  //send the file data
  while((buf_len = read(fd, buf, sizeof(buf))) > 0){
    write(sock, buf, buf_len);
  }

  close(fd);


  return 0;
}

int main(void){

  const int sock = server_socket(8000);

  while(1){
    const int con = accept_client(sock);
    if(con < 0){
      break;
    }

    http_handler(con);
    shutdown(con, SHUT_RDWR);
    close(con);
  }

  shutdown(sock, SHUT_RDWR);
  close(sock);

  return 0;
}
