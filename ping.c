/********************************************************
 *  IP��ͷ��ʽ���ݽṹ������<netinet/ip.h>��  *
 *  ICMP���ݽṹ������<netinet/ip_icmp.h>��       *
 *  �׽��ֵ�ַ���ݽṹ������<netinet/in.h>��   *
 ********************************************************/
 
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <setjmp.h>
#include <errno.h>
 
#define PACKET_SIZE 4096
#define MAX_WAIT_TIME   5
#define MAX_NO_PACKETS  3
 
 
char *addr[];
char sendpacket[PACKET_SIZE];
char recvpacket[PACKET_SIZE];
int sockfd,datalen = 56;
int nsend = 0, nreceived = 0;
double temp_rtt[MAX_NO_PACKETS];
double all_time = 0;
double min = 0;
double max = 0;
double avg = 0;
double mdev = 0;
 
struct sockaddr_in dest_addr;
struct sockaddr_in from;
struct timeval tvrecv;
pid_t pid;
 
void statistics(int sig);
void send_packet(void);
void recv_packet(void);
void computer_rtt(void);
void tv_sub(struct timeval *out,struct timeval *in);
int pack(int pack_no);
int unpack(char *buf,int len);
unsigned short cal_checksum(unsigned short *addr,int len);
 
/*����rtt��С����ֵ��ƽ��ֵ������ƽ������*/
void computer_rtt()
{
    double sum_avg = 0;
    int i;
    min = max = temp_rtt[0];
    avg = all_time/nreceived;
 
    for(i=0; i<nreceived; i++){
        if(temp_rtt[i] < min)
            min = temp_rtt[i];
        else if(temp_rtt[i] > max)
            max = temp_rtt[i];
 
        if((temp_rtt[i]-avg) < 0)
            sum_avg += avg - temp_rtt[i];
        else
            sum_avg += temp_rtt[i] - avg; 
        }
    mdev = sum_avg/nreceived;
}
 
/****ͳ�����ݺ���****/
void statistics(int sig)
{
    computer_rtt();     //����rtt
    printf("\n------ %s ping statistics ------\n",addr[0]);
    printf("%d packets transmitted,%d received,%d%% packet loss,time %.f ms\n",
        nsend,nreceived,(nsend-nreceived)/nsend*100,all_time);
    printf("rtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3f ms\n",
        min,avg,max,mdev);
    close(sockfd);
    exit(1);
}
 
/****������㷨****/
unsigned short cal_chksum(unsigned short *addr,int len)
{
    int nleft = len;
    int sum = 0;
    unsigned short *w = addr;
    unsigned short check_sum = 0;
 
    while(nleft>1)       //ICMP��ͷ���֣�2�ֽڣ�Ϊ��λ�ۼ�
    {
        sum += *w++;
        nleft -= 2;
    }
 
    if(nleft == 1)      //ICMPΪ�����ֽ�ʱ��ת�����һ���ֽڣ������ۼ�
    {
        *(unsigned char *)(&check_sum) = *(unsigned char *)w;
        sum += check_sum;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    check_sum = ~sum;   //ȡ���õ�У���
    return check_sum;
}
 
/*����ICMP��ͷ*/
int pack(int pack_no)
{
    int i,packsize;
    struct icmp *icmp;
    struct timeval *tval;
    icmp = (struct icmp*)sendpacket;
    icmp->icmp_type = ICMP_ECHO; //ICMP_ECHO���͵����ͺ�Ϊ0
    icmp->icmp_code = 0;
    icmp->icmp_cksum = 0;
    icmp->icmp_seq = pack_no;    //���͵����ݱ����
    icmp->icmp_id = pid;
 
    packsize = 8 + datalen;     //���ݱ���СΪ64�ֽ�
    tval = (struct timeval *)icmp->icmp_data;
    gettimeofday(tval,NULL);        //��¼����ʱ��
    //У���㷨
    icmp->icmp_cksum =  cal_chksum((unsigned short *)icmp,packsize); 
    return packsize;
}
 
/****��������ICMP����****/
void send_packet()
{
    int packetsize;
    if(nsend < MAX_NO_PACKETS)
    {
        nsend++;
        packetsize = pack(nsend);   //����ICMP��ͷ
        //�������ݱ�
        if(sendto(sockfd,sendpacket,packetsize,0,
            (struct sockaddr *)&dest_addr,sizeof(dest_addr)) < 0)
        {
            perror("sendto error");
        }
    }
 
}
 
 
/****��������ICMP����****/
void recv_packet()
{
    int n,fromlen;
    extern int error;
    fromlen = sizeof(from);
    if(nreceived < nsend)
    {   
        //�������ݱ�
        if((n = recvfrom(sockfd,recvpacket,sizeof(recvpacket),0,
            (struct sockaddr *)&from,&fromlen)) < 0)
        {
            perror("recvfrom error");
        }
        gettimeofday(&tvrecv,NULL);     //��¼����ʱ��
        unpack(recvpacket,n);       //��ȥICMP��ͷ
        nreceived++;
    }
}
 
 
/******��ȥICMP��ͷ******/
int unpack(char *buf,int len)
{
    int i;
    int iphdrlen;       //ipͷ����
    struct ip *ip;
    struct icmp *icmp;
    struct timeval *tvsend;
    double rtt;
 
 
    ip = (struct ip *)buf;
    iphdrlen = ip->ip_hl << 2; //��IP����ͷ���ȣ���IP��ͷ���ȳ�4
    icmp = (struct icmp *)(buf + iphdrlen); //Խ��IPͷ��ָ��ICMP��ͷ
    len -= iphdrlen;    //ICMP��ͷ�����ݱ����ܳ���
    if(len < 8)      //С��ICMP��ͷ�ĳ����򲻺���
    {
        printf("ICMP packet\'s length is less than 8\n");
        return -1;
    }
    //ȷ�������յ���������ICMP�Ļ�Ӧ
    if((icmp->icmp_type == ICMP_ECHOREPLY) && (icmp->icmp_id == pid))
    {
        tvsend = (struct timeval *)icmp->icmp_data;
        tv_sub(&tvrecv,tvsend); //���պͷ��͵�ʱ���
        //�Ժ���Ϊ��λ����rtt
        rtt = tvrecv.tv_sec*1000 + tvrecv.tv_usec/1000;
        temp_rtt[nreceived] = rtt;
        all_time += rtt;    //��ʱ��
        //��ʾ��ص���Ϣ
        printf("%d bytes from %s: icmp_seq=%u ttl=%d time=%.1f ms\n",
                len,inet_ntoa(from.sin_addr),
                icmp->icmp_seq,ip->ip_ttl,rtt);
    }
    else return -1;
}
 
 
//����timeval���
void tv_sub(struct timeval *recvtime,struct timeval *sendtime)
{
    long sec = recvtime->tv_sec - sendtime->tv_sec;
    long usec = recvtime->tv_usec - sendtime->tv_usec;
    if(usec >= 0){
        recvtime->tv_sec = sec;
        recvtime->tv_usec = usec;
    }else{
        recvtime->tv_sec = sec - 1;
        recvtime->tv_usec = -usec;
    }
}
 
/*������*/
main(int argc,char *argv[])
{
    struct hostent *host;
    struct protoent *protocol;
    unsigned long inaddr = 0;
//  int waittime = MAX_WAIT_TIME;
    int size = 50 * 1024;
    addr[0] = argv[1];
    //����С������
    if(argc < 2)     
    {
        printf("usage:%s hostname/IP address\n",argv[0]);
        exit(1);
    }
    //����ICMPЭ��
    if((protocol = getprotobyname("icmp")) == NULL)
    {
        perror("getprotobyname");
        exit(1);
    }
 
    //����ʹ��ICMP��ԭʼ�׽��֣�ֻ��root��������
    if((sockfd = socket(AF_INET,SOCK_RAW,protocol->p_proto)) < 0)
    {
        perror("socket error");
        exit(1);
    }
 
    //����rootȨ�ޣ����õ�ǰȨ��
    setuid(getuid());
 
    /*�����׽��ֵĽ��ջ�������50K����������Ϊ�˼�С���ջ����������
      �����ԣ���������pingһ���㲥��ַ��ಥ��ַ����������������Ӧ��*/
    setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size));
    bzero(&dest_addr,sizeof(dest_addr));    //��ʼ��
    dest_addr.sin_family = AF_INET;     //�׽�������AF_INET(�����׽���)
 
    //�ж��������Ƿ���IP��ַ
    if(inet_addr(argv[1]) == INADDR_NONE)
    {
        if((host = gethostbyname(argv[1])) == NULL) //��������
        {
            perror("gethostbyname error");
            exit(1);
        }
        memcpy((char *)&dest_addr.sin_addr,host->h_addr,host->h_length);
    }
    else{ //��IP ��ַ
        dest_addr.sin_addr.s_addr = inet_addr(argv[1]);
    }
    pid = getpid();
    printf("PING %s(%s):%d bytes of data.\n",argv[1],
            inet_ntoa(dest_addr.sin_addr),datalen);
 
    //������ctrl+cʱ�����ж��źţ�����ʼִ��ͳ�ƺ���
    signal(SIGINT,statistics);  
    while(nsend < MAX_NO_PACKETS){
        sleep(1);       //ÿ��һ�뷢��һ��ICMP����
        send_packet();      //����ICMP����
        recv_packet();      //����ICMP����
    }
    return 0;
}