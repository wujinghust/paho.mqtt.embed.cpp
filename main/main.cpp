#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "MQTTClient.h"
#include "../MQTTPacket/MQTTPacket.h"
//#include "FP.cpp"

#define DEFAULT_STACK_SIZE -1


class IPStack 
{
public:    
    IPStack()
    {
    	mysock=-1;
    }
    
	int Socket_error(const char* aString)
	{

		if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)
		{
			if (strcmp(aString, "shutdown") != 0 || (errno != ENOTCONN && errno != ECONNRESET))
				printf("Socket error %s in %s for socket %d\n", strerror(errno), aString, mysock);
		}
		return errno;
	}

    int connect(const char* hostname, int port)  //����TCP����
    {
		int type = SOCK_STREAM;
		struct sockaddr_in address;
		int rc = -1;
		sa_family_t family = AF_INET;
		struct addrinfo *result = NULL;
		struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};

		if ((rc = getaddrinfo(hostname, NULL, &hints, &result)) == 0)
		{
			struct addrinfo* res = result;

			/* prefer ip4 addresses */
			while (res)
			{
				if (res->ai_family == AF_INET)
				{
					result = res;
					break;
				}
				res = res->ai_next;
			}

			if (result->ai_family == AF_INET)
			{
				address.sin_port = htons(port);
				address.sin_family = family = AF_INET;
				address.sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
			}
			else
				rc = -1;

			freeaddrinfo(result);
		}

		if (rc == 0)
		{
			mysock = socket(family, type, 0);
			if (mysock != -1)
			{
				int opt = 1;

				//if (setsockopt(mysock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt)) != 0)
				//	printf("Could not set SO_NOSIGPIPE for socket %d", mysock);
				
				rc = ::connect(mysock, (struct sockaddr*)&address, sizeof(address));
			}
		}

        return rc;
    }

    int read(unsigned char* buffer, int len, int timeout_ms)
    {
		struct timeval interval = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
		if (interval.tv_sec < 0 || (interval.tv_sec == 0 && interval.tv_usec <= 0))
		{
			interval.tv_sec = 0;
			interval.tv_usec = 100;
		}

		setsockopt(mysock, SOL_SOCKET, SO_RCVTIMEO, (char *)&interval, sizeof(struct timeval));

		//printf("reading %d bytes\n", len);
		int rc = ::recv(mysock, buffer, (size_t)len, 0);
		if (rc == -1)
			Socket_error("read");
		//printf("read %d bytes\n", rc);
		return rc;
    }
    
    int write(unsigned char* buffer, int len, int timeout)
    {
		struct timeval tv;

		tv.tv_sec = 0;  /* 30 Secs Timeout */
		tv.tv_usec = timeout * 1000;  // Not init'ing this can cause strange errors

		setsockopt(mysock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
		int	rc = ::write(mysock, buffer, len);
		//printf("write rc %d\n", rc);
		return rc;
    }

	int disconnect()
	{
		return ::close(mysock);
	}
    
private:

    int mysock; 
    
};


class Countdown  //��ʱ��
{
public:
    Countdown()
    { 
	
    }

    Countdown(int ms)
    { 
		countdown_ms(ms);
    }
    

    bool expired()  //�ж��Ƿ�ʱ
    {
		struct timeval now, res;
		gettimeofday(&now, NULL);
		timersub(&end_time, &now, &res);		
		//printf("left %d ms\n", (res.tv_sec < 0) ? 0 : res.tv_sec * 1000 + res.tv_usec / 1000);
		//if (res.tv_sec > 0 || res.tv_usec > 0)
		//	printf("expired %d %d\n", res.tv_sec, res.tv_usec);
        return res.tv_sec < 0 || (res.tv_sec == 0 && res.tv_usec <= 0);
    }
    

    void countdown_ms(int ms)  //�����ʱ��
    {
		struct timeval now;
		gettimeofday(&now, NULL);
		struct timeval interval = {ms / 1000, (ms % 1000) * 1000};
		//printf("interval %d %d\n", interval.tv_sec, interval.tv_usec);
		timeradd(&now, &interval, &end_time);
    }

    
    void countdown(int seconds) //�����ʱ��
    {
		struct timeval now;
		gettimeofday(&now, NULL); //��ȡϵͳ��ǰʱ��
		struct timeval interval = {seconds, 0};
		timeradd(&now, &interval, &end_time);  //��now��interval�ĺͣ��������ʱ�䣬���������end_time�ṹ����
    }

    
    int left_ms()   //��ʣ��ʱ��
    {
		struct timeval now, res;
		gettimeofday(&now, NULL);
		timersub(&end_time, &now, &res);  //��ʱ��֮�����ʣ��ʱ�䣬���������res�ṹ����
		//printf("left %d ms\n", (res.tv_sec < 0) ? 0 : res.tv_sec * 1000 + res.tv_usec / 1000);
        return (res.tv_sec < 0) ? 0 : res.tv_sec * 1000 + res.tv_usec / 1000;
    }
    
private:

	struct timeval end_time;
};


int arrivedcount = 0;

void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;

	printf("Message %d arrived: qos %d, retained %d, dup %d, packetid %d\n", 
		++arrivedcount, message.qos, message.retained, message.dup, message.id);
    printf("Payload %.*s\n", message.payloadlen, (char*)message.payload);
}


int main(int argc, char* argv[])
{   
    IPStack ipstack = IPStack();  //����TCP/IP��Ľ������ӣ�����д����
    float version = 0.3;
    const char* topic = "mbed-sample"; //����
    
    printf("Version is %f\n", version);
              
    MQTT::Client<IPStack, Countdown> client = MQTT::Client<IPStack, Countdown>(ipstack);
    
    const char* hostname = "192.168.23.3"; //"m2m.eclipse.org";
    int port = 1883;

    if (argc > 1)
    	hostname = argv[1];

    if (argc > 2)
    	port = atoi(argv[2]);  //�����ַ�������Ķ˿ں�ת����������Ĭ�϶˿ں�Ϊ1883

    printf("Connecting to %s:%d\n", hostname, port);
    int rc = ipstack.connect(hostname, port);   //����TCP/IP�������
	if (rc != 0)
	    printf("rc from TCP connect is %d\n", rc);
 
	printf("MQTT connecting\n");

	/********�������ӱ���**************/
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;   //MQTT���ӱ���
    data.MQTTVersion = 3;
    data.keepAliveInterval=30;    //������ӣ��ı�����ʱ��Ϊ30s
    data.cleansession=0;    //������ӣ��ı�����ỰΪ0����ʾ������ĵĿͻ��������ˣ�Ҫ����Ϊ��Ҫ���͵���Ϣ��QoSΪ1��QoSΪ2����
    						//������������ʱ���轫��Щ��Ϣ���ͣ����ͻ��˳�ʱ�䲻���ӣ���Ҫ����һ������ֵ����
    data.clientID.cstring = (char*)"mbed-icraggs";  //���ÿͻ�ID


    rc = client.connect(data);   //����MQTT�����ӣ��ɹ��Ż�0
	if (rc != 0)
	    printf("rc from MQTT connect is %d\n", rc);
	printf("MQTT connected\n");
    
     rc = client.subscribe("+", MQTT::QOS2, messageArrived);       //��������  ��+��Ϊͨ���
    if (rc != 0)
        printf("rc from MQTT subscribe is %d\n", rc);

    MQTT::Message message;

    // QoS 0
    char buf[100];
    sprintf(buf, "Hello World!  QoS 0 message from app version %f", version);
    message.qos = MQTT::QOS0;   //����ȼ�0
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf)+1;
    rc = client.publish(topic, message);
    while (arrivedcount == 0)
        client.yield(100);   //�ȴ�Ӧ����
        
    // QoS 1
	printf("Now QoS 1\n");
    sprintf(buf, "Hello World!  QoS 1 message from app version %f", version);
    message.qos = MQTT::QOS1;
    message.payloadlen = strlen(buf)+1;
    rc = client.publish(topic, message);
    while (arrivedcount == 1)
        client.yield(100);
        
    // QoS 2
    sprintf(buf, "Hello World!  QoS 2 message from app version %f", version);
    message.qos = MQTT::QOS2;
    message.payloadlen = strlen(buf)+1;
    rc = client.publish(topic, message);
    while (arrivedcount == 2)
        client.yield(100);
    
    rc = client.unsubscribe(topic);
    if (rc != 0)
        printf("rc from unsubscribe was %d\n", rc);
    
    rc = client.disconnect();
    if (rc != 0)
        printf("rc from disconnect was %d\n", rc);
    
    ipstack.disconnect();
    
    printf("Finishing with %d messages received\n", arrivedcount);
    
    return 0;
}


