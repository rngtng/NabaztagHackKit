#ifndef MY_SIMUNET_H_
#define MY_SIMUNET_H_

int simunetinit();
int checkNetworkEvents();

int tcpservercreate(int port);
int tcpopen(char* dstip,int dstport);
int tcpclose(int i);
void tcpenable(int i,int enable);
int tcpsend(int i,char* msg, int len);

int udpcreate(int port);
int udpclose(int port);
int udpsend(int localport,char* dstip,int dstport,char* msg, int len);

#endif // ! MY_SIMUNET_H_
