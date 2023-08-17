#include <stdio.h>
#include <string.h>

#include "protocol.h"

//������
#define MAX_SEQ 15
#define NR_BUFS ((MAX_SEQ+1)/2)

//��ʱ��
#define DATA_TIMER  3712       //֡��ʱʱ����
#define ACK_TIMER	263       //ack�ĳ�ʱ���
	
//֡����
#define FRAME_DATA 1
#define FRAME_ACK  2
#define FRAME_NAK  3

typedef unsigned char f_kind;
typedef unsigned char f__ack;
typedef unsigned char f__seq;

struct FARME {
	f_kind kind;
	f__ack ack;
	f__seq seq;
	unsigned char data[PKT_LEN];
	unsigned int  padding;
};
typedef struct {
	int nak;                    //�Ƿ���nak
	int phl_ready;              //������Ƿ�׼����
}other_param;

//�ж�����Ƿ��ڴ�������
static int judge_between(unsigned char a, unsigned char b, unsigned char c)
{
	return ((a <= b && b < c) || ((a <= b||b<c) && c < a));
}

//����CRCУ��
static void create_crc(unsigned char *frame, int len , other_param *nak_and_phl)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	nak_and_phl->phl_ready = 0;
}

//����һ��
static void send_data_frame(other_param *nak_and_phl, f_kind fk, f__seq frame_nr, f__ack frame_expected, unsigned char buffer[NR_BUFS][PKT_LEN])
{
	struct FARME frame;

	frame.kind = fk;
	frame.seq = frame_nr;
	frame.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	if (frame.kind == FRAME_DATA)
	{
		memcpy(frame.data, buffer[frame_nr % NR_BUFS], PKT_LEN);//���Ʒ��鵽֡��
		dbg_frame((char *)"Send DATA %d %d, ID %d\n", frame.seq, frame.ack, *(short *)frame.data);
		//lprintf("Send DATA %d %d, ID %d\n", frame.seq, frame.ack, *(short *)frame.data);
		create_crc((unsigned char *)&frame, 3 + PKT_LEN , nak_and_phl);   //����У���
		start_timer(frame_nr%NR_BUFS, DATA_TIMER);     //������ʱ��
	}
	else if(frame.kind == FRAME_ACK)
	{
		dbg_frame((char *)"Send ACK  %d\n", frame.ack);
		create_crc((unsigned char *)&frame, 3 , nak_and_phl);
	}
	else
	{
		nak_and_phl->nak = 1;
		//lprintf("Send NAK!\n");
		create_crc((unsigned char *)&frame, 3, nak_and_phl);
	}
	nak_and_phl->phl_ready = 0;
	stop_ack_timer();
}

void main(int argc, char **argv)
{
	int event, arg;
	struct FARME frame;
	int i=0 ,len = 0;
	int arrived[NR_BUFS];
	other_param nak_and_phl;
	static unsigned char ack_expected = 0, next_frame_to_send = 0;
	static unsigned char frame_expected = 0, too_far = NR_BUFS;
	static unsigned char nbuffered=0;
	static unsigned char out_buf[NR_BUFS][PKT_LEN], in_buf[NR_BUFS][PKT_LEN];


	nak_and_phl.nak = 0;
	nak_and_phl.phl_ready = 0;
	protocol_init(argc, argv);
	lprintf((char *)"Designed by �����-���-��˧, build: " __DATE__"  \n");

	for (i = 0; i < NR_BUFS; i++)   //��ǽ��շ��Ļ������������ǿ�
	{
		arrived[i] = 0;
	}
	enable_network_layer();

	while (1)
	{
		event = wait_for_event(&arg);
		switch (event)
		{
			case  NETWORK_LAYER_READY:
				nbuffered++;                                       //����δ����֡��һ
				get_packet(out_buf[next_frame_to_send % NR_BUFS]); //�����ݷ��뷢�ͻ�����
				send_data_frame(&nak_and_phl ,FRAME_DATA, next_frame_to_send, frame_expected, out_buf);//��������֡
				next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);    //��һ��Ҫ���͵�֡��ż�һ
				break;
			case PHYSICAL_LAYER_READY:
				nak_and_phl.phl_ready = 1;
				break;
			case FRAME_RECEIVED:
				len = recv_frame((unsigned char *)&frame, sizeof frame);
				if (len < 5 || crc32((unsigned char *)&frame, len) != 0)             //CRCУ�����
				{	
					dbg_event((char *)"**** Receiver Error, Bad CRC Checksum\n");
					if (nak_and_phl.nak == 0)                                         //���nak=0����ʾû�з���nak֡����
					{
						send_data_frame(&nak_and_phl ,FRAME_NAK, 0, frame_expected, out_buf);//����nak֡��ackΪframe_expected
						lprintf("Send NAK frame\n");
					}

					break;
				}
				if (frame.kind == FRAME_DATA)
				{
					//���������Ų��ڽ��ܴ���
					if (!judge_between(frame_expected, frame.seq, too_far) && nak_and_phl.nak == 0)
					{
						send_data_frame(&nak_and_phl, FRAME_NAK, 0, frame_expected, out_buf);
					}
					else
					{
						start_ack_timer(ACK_TIMER);
					}
					if (judge_between(frame_expected, frame.seq, too_far) == 1 && arrived[frame.seq % NR_BUFS] == 0)
					{
						dbg_frame((char *)"Recv DATA %d %d, ID %d\n", frame.seq, frame.ack, *(short *)frame.data);
						arrived[frame.seq % NR_BUFS] = 1;
						memcpy(in_buf[frame.seq%NR_BUFS], frame.data, len - 4 - 3);
						//��С�ڵ�ǰ���кŵ�֡˳�η��͵������
						while (arrived[frame_expected%NR_BUFS])
						{
							put_packet(in_buf[frame_expected % NR_BUFS], len - 7);
							nak_and_phl.nak = 0;
							arrived[frame_expected%NR_BUFS] = 0;//��ջ�������־λ

							frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
							too_far = (too_far + 1) % (MAX_SEQ + 1);//���շ���������+1
							start_ack_timer(ACK_TIMER);
						}
					}
				}
				if ((frame.kind == FRAME_NAK) && judge_between(ack_expected, (frame.ack + 1) % (MAX_SEQ + 1), next_frame_to_send) == 1)
				{
					//���ͷ��ӵ�nak,�ش���ʧ�����󣩵�֡
					dbg_frame((char *)"Recv NAK %d\n", frame.ack);
					send_data_frame(&nak_and_phl ,FRAME_DATA, (frame.ack + 1) % (MAX_SEQ + 1), frame_expected, out_buf);
				}
				while (judge_between(ack_expected, frame.ack, next_frame_to_send) == 1)//ȷ��֡�Ѿ�����
				{
					nbuffered--;
					stop_timer(ack_expected % NR_BUFS);//�ر�֡�Ķ�ʱ��
					ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);//�������޼�һ
				}
				break;
			case DATA_TIMEOUT:
				dbg_event((char *)"---- DATA %d timeout\n", arg);
				lprintf((char *)"---- DATA %d timeout\n"  , ack_expected);
				stop_timer(ack_expected%NR_BUFS);//�رն�ʱ��
				send_data_frame(&nak_and_phl ,FRAME_DATA, ack_expected, frame_expected, out_buf);//��ʱ���ش�
				break;
			case ACK_TIMEOUT:
				//�޷������������͵�������ackȷ��֡
				dbg_event((char *)"---- ACK %d timeout\n", arg);
				lprintf((char *)"---- ACK %d timeout\n", frame_expected);
				stop_ack_timer();
				send_data_frame(&nak_and_phl, FRAME_ACK, 0, frame_expected, out_buf);
		}
		if (nbuffered < NR_BUFS && nak_and_phl.phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}