/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0;
int speed=0;
int failed=0;
int bytes=0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;//全局变量，指定http方法
int clients=1;//并发运行客户端的数量，默认是1
int force=0;//是否等待服务器响应，默认为不等待
int force_reload=0;
int proxyport=80;//代理服务器的端口号，默认80
char *proxyhost=NULL;//代理服务器的地址
int benchtime=30;//运行时间，默认为30s，可以通过-t修改
/* internal */
int mypipe[2];//为了创建管道
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

//struct option结构体，配合getopt_long函数使用
static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);//对http请求进行测试
static int bench(void);//调用子进程和管道，调用测试http函数
static void build_request(const char *url);//创建http请求

static void alarm_handler(int signal)
{
   timerexpired=1;
}	

static void usage(void)//打印提示输入的帮助信息
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"//不等待服务器端的回应
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"//发送重新请求编译指示
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"//使用代理服务请求
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])//main函数
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)//输入命令格式错误，打印提示消息
 {
	  usage();
          return 2;
 } 
//检查输入参数，并设置对应的选项
 while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   case  0 : break;
   case 'f': force=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);//打印版本信息 PROGRAM_VERSION是宏
   case 't': benchtime=atoi(optarg);break;	//修改运行时间，optarg表示命令后面的参数 webbench -t 30,30表示optarg    
   case 'p': 
	     /* proxy server parsing server:port */
	     tmp=strrchr(optarg,':');//strrchr()查找字符在指定字符串中从后面开始第一次出现的位置
	     proxyhost=optarg;//设定地址
	     if(tmp==NULL)
	     {
		     break;//没有设定p选项
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;//并发运行的客户端
  }
 }
  //optind为对应参数的下标位置
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
//使用中，最后为URL，对应为URL的位置，webbench -c 100 http://baidu.com:80/
 build_request(argv[optind]);//创建http请求(实际行就是构建了http的请求报文request)

 //打印一些输出的信息方法、HTTP版本号、并发运行客户端的数量、运行时间、
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();//创建管道和子进程，对HTTP请求进行测试
}

void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);//置字符串host前MAXHOSTNAMELEN字节为0，且包括'\0'
  bzero(request,REQUEST_SIZE);

  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }

//请求行信息：方法+空格+URL+空格+HTTP版本+CRLF

  strcat(request," ");

  if(NULL==strstr(url,"://"))//strstr判断是否有子串
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);//若无"://",则为非法的URL
	  exit(2);
  }
  if(strlen(url)>1500)//URL太长，非法
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  //从url中获取代理服务器的地址
  if(proxyhost==NULL)
	   if (0!=strncasecmp("http://",url,7)) 
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;//获得IP地址起始位置距离URL的偏移量
  /* printf("%d\n",i); */

  if(strchr(url+i,'/')==NULL) {//查找字符串中首次出现字符的位置
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                             }
//获取代理服务器的端口号
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
   {
	   strncpy(host,url+i,strchr(url+i,':')-url-i);//获取IP地址
	   bzero(tmp,10);
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);//获取:与/之间的值，即端口号
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
	   if(proxyport==0) proxyport=80;
   } else//不存在端口号的情况下
   {
     strncpy(host,url+i,strcspn(url+i,"/"));
   }
   // printf("Host=%s\n",host);
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   strcat(request,url);//若代理服务器的ip不为NULL，则不需要哦经过URL解析出IP地址，直接加上URL
  }
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  //以上是HTTP请求报文的请求行信息request

 //在空行之前以下是头部信息 (首部字段: 值\r\n)
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");//版本行
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  //空行信息
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}
//创建管道和子进程，对HTTP请求进行测试
/* vraci system rc error kod */
static int bench(void)
{
  int i,j,k;	
  pid_t pid=0;
  FILE *f;

  /* check avaibility of target server */
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  close(i);
  /* create pipe */
  if(pipe(mypipe))
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   if(pid <= (pid_t) 0)
	   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
		   break;
	   }
  }

  if( pid< (pid_t) 0)
  {
          fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0)//子进程，调用benchcore函数对http请求测试，获得speed、failded、bytes参数，写入管道文件
  {
    /* I am a child */
    if(proxyhost==NULL)
      benchcore(host,proxyport,request);//对HTTP请求进行测试
         else
      benchcore(proxyhost,proxyport,request);

         /* write results to pipe */
	 f=fdopen(mypipe[1],"w");//fdopen取一个现存的文件描述符，并使一个标准的I/O流与该描述符结合，常用于由创建管道和网络通信通道函数获得描述符，因为这些特殊的文件不能用I/O标准I/O函数fopen函数打开。
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
	 fprintf(f,"%d %d %d\n",speed,failed,bytes);
	 fclose(f);
	 return 0;
  } else
  {//父进程---从管道文件中读取数据输出
	  f=fdopen(mypipe[0],"r");
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);//_IONBF表示无缓存，直接从流中读入数据或直接向流中写入数据，而没有缓冲区
	  speed=0;
          failed=0;
          bytes=0;

	  while(1)//父进程读取管道数据，并做加法
	  {
		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		  if(--clients==0) break;
	  }
	  fclose(f);
//输出测试结果
  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  }
  return i;
}
//对http进行测试，得到speed、Bytes 、failed
void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500];//存放服务器请求所返回的数据
 int s,i;
 struct sigaction sa;

 /* setup alarm signal handler */
 sa.sa_handler=alarm_handler;//设置定时器，信号处理函数
 sa.sa_flags=0;
 /*
  sigaction(),检查或修改与指定信号相关联的处理动作
  */
 //超时会产生SIGALRAM信号，调用alrm_handler函数处理信号
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);
 alarm(benchtime);//开始设置闹钟，开始计时
 /*
  alarm(),设置信号SIGALRM在经过seconds指定的秒数之后传递给当前的进程，如果seconds=0,表示取消闹钟，并将剩下的时间返回
  */

 rlen=strlen(req);

 nexttry:while(1)//包含goto语句的while循环
 {
    if(timerexpired)
    {
		//计时结束，产生信号，信号处理函数将timerexpired置为 1，退出函数
       if(failed>0)
       {
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;
       }
       return;
    }
    s=Socket(host,port); //调用socket.c文件中的Socket函数，建立TCP的连接                         
    if(s<0) { failed++;continue;} 
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}//强向套接字文件中写入请求，和原请求长度不等，失败
    if(http10==0) //针对HTTP/0.9班版本的特殊处理，HTTP0.9是端连接，一次只能进行一次HTTP请求
	    if(shutdown(s,1)) { failed++;close(s);continue;}
	/*
	 shutdown(int sockfd,int how):用于任何类型的套接口禁止接收、禁止发送或禁止收发数据
how :
SHUT_RD(0):禁止读操作
SHUT_WR(1):禁止写操作
SHUT_RDWR(2):禁止读写
返回值：当没有错误发生，返回0，否则返回错误码
	 */

    if(force==0) 
    {
            /* read all available data from socket */
	    while(1)
	    {
              if(timerexpired) break; //超时，退出
	      i=read(s,buf,1500);//从socket中读取数据
              /* fprintf(stderr,"%d\n",i); */
	      if(i<0) //读取数据失败
              { 
                 failed++;
                 close(s);
                 goto nexttry;//跳转下次循环
              }
	       else//读取数据成功
		       if(i==0) break;
		       else
			       bytes+=i;
	    }
    }
    if(close(s)) {failed++;continue;}
    speed++;//HTTP请求成功
 }
}
