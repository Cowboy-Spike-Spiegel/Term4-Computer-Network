#include <stdio.h>
#include <string.h>

#include "protocol.h"

#define MAX_SEQ 15
#define NR_BUFS ((MAX_SEQ+1)/2)

#define DATA_TIMER  3000       //֡��ʱʱ����
#define ACK_TIMER 240          //ack�ĳ�ʱ���
//֡����
#define FRAME_DATA 1
#define FRAME_ACK  2
#define FRAME_NAK  3

typedef unsigned char f_kind;
typedef unsigned char f__ack;
typedef unsigned char f__seq;

struct FRAME { 
	f_kind kind;
	f__ack ack;
	f__seq seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;
};

int no_nak=1;                        //��־�Ƿ��Ѿ����͹�nak
int nak = 0;
static int phl_ready = 0;
unsigned char oldest_frame=MAX_SEQ+1;

//�ж�֡�ţ�ack�� b���Ƿ��ڴ�����
static int between(unsigned char a,unsigned char b,unsigned char c)  
{
   if(((a<=b)&&(b<c))||((c<a)&&(a<=b))||((b<c)&&(c<a)))
		return 1;
	else
		return 0;

}

static void put_frame(unsigned char *frame, int len)//����У���crc
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(f_kind fk,f__seq frame_nr,f__ack frame_expected,unsigned char buffer[NR_BUFS][PKT_LEN])
{//��������֡����ack����nak
    struct FRAME s;
    
    s.kind = fk;
    s.seq = frame_nr;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	if(fk==FRAME_DATA)
	{
		memcpy(s.data, buffer[frame_nr % NR_BUFS], PKT_LEN);//���Ʒ��鵽֡��
		dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);
        put_frame((unsigned char *)&s, 3 + PKT_LEN);   //����У���
		start_timer(frame_nr%NR_BUFS, DATA_TIMER);     //������ʱ��
	}
	else if(fk == FRAME_NAK)
	{
		nak = 1;
	    put_frame((unsigned char *)&s, 3);            
	}
	else if(fk == FRAME_ACK)
	{
		dbg_frame("Send ACK  %d\n", s.ack);
        put_frame((unsigned char *)&s, 3);
	}
	phl_ready = 0;
	stop_ack_timer();                          //û�б�Ҫ����ack��ʱ��
}

/*
void main(int argc, char **argv)
{
	int event, arg;
    struct FRAME f;
    int len = 0;
    int i;
	static unsigned char ack_expected=0, next_frame_to_send=0;
	static unsigned char frame_expected=0, too_far=NR_BUFS;
    static unsigned char nbuffered;
	int arrived[NR_BUFS];
	static unsigned char out_buf[NR_BUFS][PKT_LEN], in_buf[NR_BUFS][PKT_LEN];

    protocol_init(argc, argv); 
    lprintf("Designed by XXHH(2020211409-��˧), build: " __DATE__"  "__TIME__"\n");

	for (i = 0; i < NR_BUFS; i++)   //��ǽ��շ��Ļ������������ǿ�
	{
		arrived[i] = 0;
	}
    enable_network_layer();

	while(1)
	{
		event = wait_for_event(&arg);
		switch (event)
		{
			case NETWORK_LAYER_READY:
				nbuffered++;                                                          //����δ��֡��Ŀ++
				get_packet(out_buf[next_frame_to_send % NR_BUFS]);                    //�õ�������뻺������
				send_data_frame(FRAME_DATA,next_frame_to_send,frame_expected,out_buf);//��������֡
				next_frame_to_send=(next_frame_to_send + 1) % ( MAX_SEQ + 1);         //��Ž���ģ����ӷ�
				break;

			case PHYSICAL_LAYER_READY:
				phl_ready = 1;
				break;

			case FRAME_RECEIVED:
				len = recv_frame((unsigned char *)&f, sizeof f);
                if (len < 5 || crc32((unsigned char *)&f, len) != 0)
				{	//У��ͳ����ͷ���nak�����ش�
					dbg_event("**** Receiver Error, Bad CRC Checksum\n");
					if (nak == 0)                                              //���nak=0����ʾû�з���nak֡����
					{
						send_data_frame(FRAME_NAK, 0, frame_expected, out_buf);//����nak֡��ackΪframe_expected
						dbg_event("Send NAK frame\n");
					}
				   	    
                    break;
				}
				if (f.kind == FRAME_DATA)
			    {
					if ((f.seq != frame_expected) && nak == 0)                 //������кŴ��󣬲���û�з���nak֡������
					{
						send_data_frame(FRAME_NAK, 0, frame_expected, out_buf);
					}
					else
					{
						start_ack_timer(ACK_TIMER);
					}
					//������к��ڽ��ܴ����в��ң�������Ϊ0�������ݷ��뻺����
					if(between(frame_expected, f.seq, too_far)==1&&arrived[f.seq % NR_BUFS] == 0)
					{
						dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
						arrived[f.seq % NR_BUFS] = 1;
						memcpy(in_buf[f.seq%NR_BUFS], f.data, len-4-3);
                        //��С�ڵ�ǰ���кŵ�֡˳�η��͵������
						while(arrived[frame_expected%NR_BUFS])
						{
							put_packet(in_buf[frame_expected % NR_BUFS], len-7);
							nak=0;
							arrived[frame_expected%NR_BUFS]=0;//��ջ�������־λ

							frame_expected=(frame_expected+1) % (MAX_SEQ + 1);
							too_far=(too_far + 1)% (MAX_SEQ + 1);//���շ���������+1
							start_ack_timer(ACK_TIMER);
						}
					}
			    }
				if((f.kind==FRAME_NAK)&&between(ack_expected,(f.ack+1) % (MAX_SEQ + 1), next_frame_to_send)==1)
				{
					//���ͷ��ӵ�nak,�ش���ʧ�����󣩵�֡
					dbg_frame("Recv NAK %d\n", f.ack);
					send_data_frame(FRAME_DATA,(f.ack + 1)%(MAX_SEQ+1), frame_expected, out_buf);
				}
				while(between(ack_expected,f.ack,next_frame_to_send)==1)//ȷ��֡�Ѿ�����
				{
					nbuffered--;
					stop_timer(ack_expected % NR_BUFS);//�ر�֡�Ķ�ʱ��
					ack_expected=(ack_expected + 1) % (MAX_SEQ + 1);//�������޼�һ
				}
				break;

			case DATA_TIMEOUT:
                dbg_event("---- DATA %d timeout\n", arg); 
                send_data_frame(FRAME_DATA,ack_expected, frame_expected, out_buf);//��ʱ���ش�
                break;
			case ACK_TIMEOUT:
				//�޷������������͵�������ackȷ��֡
				dbg_event("---- ACK %d timeout\n", arg);
				send_data_frame(FRAME_ACK, 0, frame_expected, out_buf);
		}

        if (nbuffered < NR_BUFS && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
	}
}*/
