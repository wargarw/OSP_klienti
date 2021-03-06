/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <zlib.h>
#include "hashset.h"

#define TABLE 32768
//#define TABLE 7

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);
void sent_count(int client, int count);
void sent_OK(int client);
int inf(const void *src, int srcLen, void *dst, int dstLen) ;
void parse_words(char * buf);

hashset_t set;

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(int client){
 	char buf[1024];
	char buf2[1024];
	char  * decomp;
 	char method[255];
 	char url[255];
	char path[512];
	int numchars;
 	size_t i, j;
	unsigned char k,l;
	struct stat st;
	int length=0;	
	char * data_buf;
	int len,len2;
	int comp_res;

 	numchars=get_line(client, buf, sizeof(buf)); /* POST /osp/myserver/data HTTP/1.1 */
	printf("head=%s\n",buf);

	i=0;	
	j=0;

 	while (!ISspace(buf[j]) && (i < sizeof(method) - 1)){
  		method[i] = buf[j];
  		i++;
		j++;
 	}
 	method[i] = '\0';

 	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")){
  		unimplemented(client);
  		return;
 	}
 	
	/* preskocime mezery */
	while (ISspace(buf[j]) && (j < sizeof(buf))){
		j++;
	}

	/* URL pozadavku - resource */
	i = 0; 	
	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))){
		url[i] = buf[j];
		i++;
		j++;
	}
	url[i] = '\0';

	if (strcasecmp(method, "POST") == 0){
		/* chybny resource*/
		if (strcmp(url,"/osp/myserver/data")){
			close(client);
			return;
		}
		k=0;
		while(1){
			get_line(client, buf2, sizeof(buf2));
			/*newlajna pred prilohou*/
			if (buf2[0]=='\n'){
				break;
			}
			/* delka prilohy je ve 4. lajne hlavicky*/
			if (k==3){
				l=16; /*delka prilohy je na 16 pozici*/
				while (buf2[l]!='\n' && buf2[l]!='\r' 
						&& buf2[l]!='\0' ){
					length*=10;
					length+=buf2[l]-48;
					l++;
				}	
			}
			k++;
		}
		printf("delka compressed=%d\n",length);
		data_buf = calloc(length,sizeof(char));
		recv(client,data_buf,length,0);
		//len=*((int *)&(data_buf[length-4]));
		len=0;
		printf("delka decompressed=%d\n",len);
		if (len==0) len=2*length;
		decomp = calloc(len,sizeof(char));
		len2=len;
		while(1){
			comp_res=inf(data_buf, length, decomp, len);
			if (comp_res>0){
				len2=comp_res;
				break;
			}
			if (comp_res==Z_MEM_ERROR){
				free(decomp);
				len=2*len;
				decomp = calloc(len,sizeof(char));
				printf("malo pameti, zvetsuji buffer\n");
				continue;
			}
			if (comp_res==Z_DATA_ERROR){
				printf("corrupted data\n");
				break;
			}
			if (comp_res==Z_BUF_ERROR){
				free(decomp);
				len=2*len;
				decomp = calloc(len,sizeof(char));
				printf("buf err malo pameti, zvetsuji buffer\n");
				continue;
			}
			printf("neco jineho\n");
			break;
		}
		
		
		
		
		
		printf("skuecna delka decompressed=%d\n",len2);
		decomp[len2-1]='\0'; /*na konci dat je newline */
		printf("decomp=%s\n",decomp);
		parse_words(decomp);
		free(data_buf);
		free(decomp);
		sent_OK(client); /*pokud tohle neodeslu pred zavrenim, klient
				si zahlasi :empty response: */
		close(client);
		return;

	}

	if (strcasecmp(method, "GET") == 0){
		if (!strcmp(url,"/osp/myserver/count")){
			printf("slova=%d\n",hashset_num_items(set));
			sent_count(client,hashset_num_items(set));
			hashset_destroy(set);
			set = hashset_create(TABLE);
			if (set == NULL) {
				printf("failed to create hashset instance\n");
			}
		}
 	}

	        if (stat(path, &st) == -1) {
                while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
                        numchars = get_line(client, buf, sizeof(buf));
                }
                //not_found(client);
        }

 	close(client);
}



/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];

 fgets(buf, sizeof(buf), resource);
 while (!feof(resource))
 {
  send(client, buf, strlen(buf), 0);
  fgets(buf, sizeof(buf), resource);
 }
}


/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0);
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';
 
 return(i);
}

/**********************************************************************/
/* Testovaci metoda - vraci pouze cislo */
/**********************************************************************/
void sent_count(int client, int count){
 	char buf[1024];

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, SERVER_STRING);
 	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);

 	sprintf(buf, "%d\r\n",count);
	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Odpoved klientovi - vse je OK */
/**********************************************************************/
void sent_OK(int client){
 	char buf[1024];

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, SERVER_STRING);
 	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);
}



/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
  error_die("socket");
 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
  error_die("bind");
 if (*port == 0)  /* if dynamically allocating a port */
 {
  int namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   error_die("getsockname");
  *port = ntohs(name.sin_port);
 }
 if (listen(httpd, 5) < 0)
  error_die("listen");
 return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</TITLE></HEAD>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void){
 	int server_sock = -1;
 	u_short port = 8080;
 	int client_sock = -1;
 	struct sockaddr_in client_name;
 	int client_name_len = sizeof(client_name);

 	server_sock = startup(&port);
 	printf("httpd running on port %d\n", port);

	set = hashset_create(TABLE);
	if (set == NULL) {
		printf("failed to create hashset instance\n");
             	close(server_sock);
		return 1;
        }
	
 	while (1){
  		client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  		if (client_sock == -1){
   			error_die("accept");
		}
  		accept_request(client_sock); 
 	}
 	close(server_sock);
	return(0);
}
/**********************************************************************/
/* Dekomprimuje obsah bufferu a ulozi ho do dalsihoi bufferu*/
/**********************************************************************/
int inf(const void *src, int srcLen, void *dst, int dstLen) {
    z_stream strm  = {0};
    strm.total_in  = strm.avail_in  = srcLen;
    strm.total_out = strm.avail_out = dstLen;
    strm.next_in   = (Bytef *) src;
    strm.next_out  = (Bytef *) dst;

    strm.zalloc = Z_NULL;
    strm.zfree  = Z_NULL;
    strm.opaque = Z_NULL;

    int err = -1;
    int ret = -1;

    err = inflateInit2(&strm, (15 + 32)); //15 window bits, and the +32 tells zlib to to detect if using gzip or zlib
    if (err == Z_OK) {
        err = inflate(&strm, Z_FINISH);
        if (err == Z_STREAM_END) {
            ret = strm.total_out;
        }
        else {
             inflateEnd(&strm);
             return err;
        }
    }
    else {
        inflateEnd(&strm);
        return err;
    }

    inflateEnd(&strm);
    return ret;
}
/**********************************************************************/
/* Dekomprimuje obsah bufferu a ulozi ho do dalsihoi bufferu*/
/**********************************************************************/
void parse_words(char * buf){
	int main_index=0; /*iterace pres bajty v bufferu*/
	int word_index=0; /* itrace pres bajty v aktualnim sloce */
	char word[126];

	while(buf[main_index]!='\0'){
		if (buf[main_index]==' '){
			if (word_index){ /*delka slova neni nula*/
				word[word_index]='\0';
				hashset_add(set, (void *)word, word_index);
				word_index=0;
			}
		}else{
			word[word_index]=buf[main_index];
			word_index++;
		}
		main_index++;
	}
	if (word_index){ /*delka slova neni nula*/
		word[word_index]='\0';
		hashset_add(set, (void *)word, word_index);
	}
}