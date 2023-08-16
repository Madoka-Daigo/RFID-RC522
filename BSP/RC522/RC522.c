#include "stm32f1xx_hal.h"
#include "RC522.h"
#include "stdio.h"
#include "usart.h"
#include "string.h"
#include "tim.h"

#define osDelay HAL_Delay


#define RS522_RST(N) HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, N==1?GPIO_PIN_SET:GPIO_PIN_RESET)
#define RS522_NSS(N) HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, N==1?GPIO_PIN_SET:GPIO_PIN_RESET)

extern SPI_HandleTypeDef hspi2;


void MFRC_Init(void)
{
    RS522_NSS(1);
    RS522_RST(1);
}

static uint8_t ret;  //��Щ������HAL���׼�ⲻͬ�ĵط�����д������
uint8_t SPI2_RW_Byte(uint8_t byte)
{
    HAL_SPI_TransmitReceive(&hspi2, &byte, &ret, 1, 10);//��byte д�룬������һ��ֵ����������ret
    return   ret;//�����byte �ĵ�ַ����ȡʱ�õ�Ҳ��ret��ַ��һ��ֻд��һ��ֵ10
}

void MFRC_WriteReg(uint8_t addr, uint8_t data)
{
    uint8_t AddrByte;
    AddrByte = (addr << 1 ) & 0x7E; //�����ַ�ֽ�
    RS522_NSS(0);                   //NSS����
    SPI2_RW_Byte(AddrByte);         //д��ַ�ֽ�
    SPI2_RW_Byte(data);             //д����
    RS522_NSS(1);                   //NSS����
}

uint8_t MFRC_ReadReg(uint8_t addr)
{
    uint8_t AddrByte, data;
    AddrByte = ((addr << 1 ) & 0x7E ) | 0x80;   //�����ַ�ֽ�
    RS522_NSS(0);                               //NSS����
    SPI2_RW_Byte(AddrByte);                     //д��ַ�ֽ�
    data = SPI2_RW_Byte(0x00);                  //������
    RS522_NSS(1);                               //NSS����
    return data;
}

void MFRC_SetBitMask(uint8_t addr, uint8_t mask)
{
    uint8_t temp;
    temp = MFRC_ReadReg(addr);                  //�ȶ��ؼĴ�����ֵ
    MFRC_WriteReg(addr, temp | mask);           //�������������д��Ĵ���
}

void MFRC_ClrBitMask(uint8_t addr, uint8_t mask)
{
    uint8_t temp;
    temp = MFRC_ReadReg(addr);                  //�ȶ��ؼĴ�����ֵ
    MFRC_WriteReg(addr, temp & ~mask);          //�������������д��Ĵ���
}

void MFRC_CalulateCRC(uint8_t *pInData, uint8_t len, uint8_t *pOutData)
{
    //0xc1 1        2           pInData[2]
    uint8_t temp;
    uint32_t i;
    MFRC_ClrBitMask(MFRC_DivIrqReg, 0x04);                  //ʹ��CRC�ж�
    MFRC_WriteReg(MFRC_CommandReg, MFRC_IDLE);              //ȡ����ǰ�����ִ��
    MFRC_SetBitMask(MFRC_FIFOLevelReg, 0x80);               //���FIFO�����־λ
    for(i = 0; i < len; i++)                                //����CRC���������д��FIFO
    {
        MFRC_WriteReg(MFRC_FIFODataReg, *(pInData + i));
    }
    MFRC_WriteReg(MFRC_CommandReg, MFRC_CALCCRC);           //ִ��CRC����
    i = 100000;
    do
    {
        temp = MFRC_ReadReg(MFRC_DivIrqReg);                //��ȡDivIrqReg�Ĵ�����ֵ
        i--;
    }
    while((i != 0) && !(temp & 0x04));                      //�ȴ�CRC�������
    pOutData[0] = MFRC_ReadReg(MFRC_CRCResultRegL);         //��ȡCRC������
    pOutData[1] = MFRC_ReadReg(MFRC_CRCResultRegM);
}

char MFRC_CmdFrame(uint8_t cmd, uint8_t *pInData, uint8_t InLenByte, uint8_t *pOutData, uint16_t *pOutLenBit)
{
    uint8_t lastBits;
    uint8_t n;
    uint32_t i;
    char status = MFRC_ERR;
    uint8_t irqEn   = 0x00;
    uint8_t waitFor = 0x00;

    /*�����������ñ�־λ*/
    switch(cmd)
    {
        case MFRC_AUTHENT:                  //Mifare��֤
            irqEn = 0x12;
            waitFor = 0x10;                 //idleIRq�жϱ�־
            break;
        case MFRC_TRANSCEIVE:               //���Ͳ���������
            irqEn = 0x77;
            waitFor = 0x30;                 //RxIRq��idleIRq�жϱ�־
            break;
    }

    /*��������֡ǰ׼��*/
    MFRC_WriteReg(MFRC_ComIEnReg, irqEn | 0x80);    //���ж�
    MFRC_ClrBitMask(MFRC_ComIrqReg, 0x80);          //����жϱ�־λSET1
    MFRC_WriteReg(MFRC_CommandReg, MFRC_IDLE);      //ȡ����ǰ�����ִ��
    MFRC_SetBitMask(MFRC_FIFOLevelReg, 0x80);       //���FIFO�����������־λ

    /*��������֡*/
    for(i = 0; i < InLenByte; i++)                  //д���������
    {
        MFRC_WriteReg(MFRC_FIFODataReg, pInData[i]);
    }
    MFRC_WriteReg(MFRC_CommandReg, cmd);            //ִ������
    if(cmd == MFRC_TRANSCEIVE)
    {
        MFRC_SetBitMask(MFRC_BitFramingReg, 0x80);  //��������
    }
    i = 300000;                                     //����ʱ��Ƶ�ʵ���,����M1�����ȴ�ʱ��25ms
    do
    {
        n = MFRC_ReadReg(MFRC_ComIrqReg);
        i--;
    }
    while((i != 0) && !(n & 0x01) && !(n & waitFor));     //�ȴ��������
    MFRC_ClrBitMask(MFRC_BitFramingReg, 0x80);          //ֹͣ����

    /*������յ�����*/
    if(i != 0)
    {
        if(!(MFRC_ReadReg(MFRC_ErrorReg) & 0x1B))
        {
            status = MFRC_OK;
            if(n & irqEn & 0x01)
            {
                status = MFRC_NOTAGERR;
            }
            if(cmd == MFRC_TRANSCEIVE)
            {
                n = MFRC_ReadReg(MFRC_FIFOLevelReg);
                lastBits = MFRC_ReadReg(MFRC_ControlReg) & 0x07;
                if (lastBits)
                {
                    *pOutLenBit = (n - 1) * 8 + lastBits;
                }
                else
                {
                    *pOutLenBit = n * 8;
                }
                if(n == 0)
                {
                    n = 1;
                }
                if(n > MFRC_MAXRLEN)
                {
                    n = MFRC_MAXRLEN;
                }
                for(i = 0; i < n; i++)
                {
                    pOutData[i] = MFRC_ReadReg(MFRC_FIFODataReg);
                }
            }
        }
        else
        {
            status = MFRC_ERR;
        }
    }

    MFRC_SetBitMask(MFRC_ControlReg, 0x80);               //ֹͣ��ʱ������
    MFRC_WriteReg(MFRC_CommandReg, MFRC_IDLE);            //ȡ����ǰ�����ִ��

    return status;
}

void PCD_Reset(void)
{
    /*Ӳ��λ*/
    RS522_RST(1);//�õ���λ����
    osDelay(2);
    RS522_RST(0);
    osDelay(2);
    RS522_RST(1);
    osDelay(2);

    /*��λ*/
    MFRC_WriteReg(MFRC_CommandReg, MFRC_RESETPHASE);
    osDelay(2);

    /*��λ��ĳ�ʼ������*/
    MFRC_WriteReg(MFRC_ModeReg, 0x3D);              //CRC��ʼֵ0x6363
    MFRC_WriteReg(MFRC_TReloadRegL, 30);            //��ʱ����װֵ
    MFRC_WriteReg(MFRC_TReloadRegH, 0);
    MFRC_WriteReg(MFRC_TModeReg, 0x8D);             //��ʱ������
    MFRC_WriteReg(MFRC_TPrescalerReg, 0x3E);        //��ʱ��Ԥ��Ƶֵ
    MFRC_WriteReg(MFRC_TxAutoReg, 0x40);            //100%ASK

    PCD_AntennaOff();                               //������
    osDelay(2);
    PCD_AntennaOn();                                //������
    
    printf("��ʼ�����\n");
}

void PCD_AntennaOn(void)
{
    uint8_t temp;
    temp = MFRC_ReadReg(MFRC_TxControlReg);
    if (!(temp & 0x03))
    {
        MFRC_SetBitMask(MFRC_TxControlReg, 0x03);
    }
}

void PCD_AntennaOff(void)
{
    MFRC_ClrBitMask(MFRC_TxControlReg, 0x03);
}

void PCD_Init(void)
{
    MFRC_Init();      //MFRC�ܽ�����
    PCD_Reset();      //PCD��λ  ����ʼ������
    PCD_AntennaOff(); //�ر�����
    PCD_AntennaOn();   //��������
}

char PCD_Request(uint8_t RequestMode, uint8_t *pCardType)
{
    int status;
    uint16_t unLen;
    uint8_t CmdFrameBuf[MFRC_MAXRLEN];

    MFRC_ClrBitMask(MFRC_Status2Reg, 0x08);//���ڲ��¶ȴ�����
    MFRC_WriteReg(MFRC_BitFramingReg, 0x07); //�洢ģʽ������ģʽ���Ƿ��������͵�
    MFRC_SetBitMask(MFRC_TxControlReg, 0x03);//���õ����ź�13.56MHZ

    CmdFrameBuf[0] = RequestMode;

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 1, CmdFrameBuf, &unLen);

    if((status == PCD_OK) && (unLen == 0x10))
    {
        *pCardType = CmdFrameBuf[0];
        *(pCardType + 1) = CmdFrameBuf[1];
    }

    return status;
}

char PCD_Anticoll(uint8_t *pSnr)
{
    char status;
    uint8_t i, snr_check = 0;
    uint16_t  unLen;
    uint8_t CmdFrameBuf[MFRC_MAXRLEN];

    MFRC_ClrBitMask(MFRC_Status2Reg, 0x08);
    MFRC_WriteReg(MFRC_BitFramingReg, 0x00);
    MFRC_ClrBitMask(MFRC_CollReg, 0x80);

    CmdFrameBuf[0] = PICC_ANTICOLL1;
    CmdFrameBuf[1] = 0x20;

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 2, CmdFrameBuf, &unLen);

    if(status == PCD_OK)
    {
        for(i = 0; i < 4; i++)
        {
            *(pSnr + i)  = CmdFrameBuf[i];
            snr_check ^= CmdFrameBuf[i];
        }
        if(snr_check != CmdFrameBuf[i])
        {
            status = PCD_ERR;
        }
    }

    MFRC_SetBitMask(MFRC_CollReg, 0x80);
    return status;
}

char PCD_Select(uint8_t *pSnr)
{
    char status;
    uint8_t i;
    uint16_t unLen;
    uint8_t CmdFrameBuf[MFRC_MAXRLEN];

    CmdFrameBuf[0] = PICC_ANTICOLL1;
    CmdFrameBuf[1] = 0x70;
    CmdFrameBuf[6] = 0;
    for(i = 0; i < 4; i++)
    {
        CmdFrameBuf[i + 2] = *(pSnr + i);
        CmdFrameBuf[6]  ^= *(pSnr + i);
    }
    MFRC_CalulateCRC(CmdFrameBuf, 7, &CmdFrameBuf[7]);

    MFRC_ClrBitMask(MFRC_Status2Reg, 0x08);

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 9, CmdFrameBuf, &unLen);

    if((status == PCD_OK) && (unLen == 0x18))
    {
        status = PCD_OK;
    }
    else
    {
        status = PCD_ERR;
    }
    return status;
}

char PCD_AuthState(uint8_t AuthMode, uint8_t BlockAddr, uint8_t *pKey, uint8_t *pSnr)
{
    char status;
    uint16_t unLen;
    uint8_t i, CmdFrameBuf[MFRC_MAXRLEN];
    CmdFrameBuf[0] = AuthMode;
    CmdFrameBuf[1] = BlockAddr;
    for(i = 0; i < 6; i++)
    {
        CmdFrameBuf[i + 2] = *(pKey + i);
    }
    for(i = 0; i < 4; i++)
    {
        CmdFrameBuf[i + 8] = *(pSnr + i);
    }

    status = MFRC_CmdFrame(MFRC_AUTHENT, CmdFrameBuf, 12, CmdFrameBuf, &unLen);
    if((status != PCD_OK) || (!(MFRC_ReadReg(MFRC_Status2Reg) & 0x08)))
    {
        status = PCD_ERR;
    }

    return status;
}

char PCD_WriteBlock(uint8_t BlockAddr, uint8_t *pData)
{
    char status;
    uint16_t unLen;
    uint8_t i, CmdFrameBuf[MFRC_MAXRLEN];

    CmdFrameBuf[0] = PICC_WRITE;
    CmdFrameBuf[1] = BlockAddr;
    MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);

    if((status != PCD_OK) || (unLen != 4) || ((CmdFrameBuf[0] & 0x0F) != 0x0A))
    {
        status = PCD_ERR;
    }

    if(status == PCD_OK)
    {
        for(i = 0; i < 16; i++)
        {
            CmdFrameBuf[i] = *(pData + i);
        }
        MFRC_CalulateCRC(CmdFrameBuf, 16, &CmdFrameBuf[16]);

        status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 18, CmdFrameBuf, &unLen);

        if((status != PCD_OK) || (unLen != 4) || ((CmdFrameBuf[0] & 0x0F) != 0x0A))
        {
            status = PCD_ERR;
        }
    }

    return status;
}

char PCD_ReadBlock(uint8_t BlockAddr, uint8_t *pData)
{
    char status;
    uint16_t unLen;
    uint8_t i, CmdFrameBuf[MFRC_MAXRLEN];

    CmdFrameBuf[0] = PICC_READ;
    CmdFrameBuf[1] = BlockAddr;
    MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);
    if((status == PCD_OK) && (unLen == 0x90))
    {
        for(i = 0; i < 16; i++)
        {
            *(pData + i) = CmdFrameBuf[i];
        }
    }
    else
    {
        status = PCD_ERR;
    }

    return status;
}

char PCD_Value(uint8_t mode, uint8_t BlockAddr, uint8_t *pValue)
{
    //0XC1        1           Increment[4]={0x03, 0x01, 0x01, 0x01};
    char status;
    uint16_t unLen;
    uint8_t i, CmdFrameBuf[MFRC_MAXRLEN];

    CmdFrameBuf[0] = mode;
    CmdFrameBuf[1] = BlockAddr;
    MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);

    if((status != PCD_OK) || (unLen != 4) || ((CmdFrameBuf[0] & 0x0F) != 0x0A))
    {
        status = PCD_ERR;
    }

    if(status == PCD_OK)
    {
        for(i = 0; i < 16; i++)
        {
            CmdFrameBuf[i] = *(pValue + i);
        }
        MFRC_CalulateCRC(CmdFrameBuf, 4, &CmdFrameBuf[4]);
        unLen = 0;
        status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 6, CmdFrameBuf, &unLen);
        if(status != PCD_ERR)
        {
            status = PCD_OK;
        }
    }

    if(status == PCD_OK)
    {
        CmdFrameBuf[0] = PICC_TRANSFER;
        CmdFrameBuf[1] = BlockAddr;
        MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);

        status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);

        if((status != PCD_OK) || (unLen != 4) || ((CmdFrameBuf[0] & 0x0F) != 0x0A))
        {
            status = PCD_ERR;
        }
    }
    return status;
}

char PCD_BakValue(uint8_t sourceBlockAddr, uint8_t goalBlockAddr)
{
    char status;
    uint16_t  unLen;
    uint8_t CmdFrameBuf[MFRC_MAXRLEN];

    CmdFrameBuf[0] = PICC_RESTORE;
    CmdFrameBuf[1] = sourceBlockAddr;
    MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);
    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);
    if((status != PCD_OK) || (unLen != 4) || ((CmdFrameBuf[0] & 0x0F) != 0x0A))
    {
        status = PCD_ERR;
    }

    if(status == PCD_OK)
    {
        CmdFrameBuf[0] = 0;
        CmdFrameBuf[1] = 0;
        CmdFrameBuf[2] = 0;
        CmdFrameBuf[3] = 0;
        MFRC_CalulateCRC(CmdFrameBuf, 4, &CmdFrameBuf[4]);
        status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 6, CmdFrameBuf, &unLen);
        if(status != PCD_ERR)
        {
            status = PCD_OK;
        }
    }

    if(status != PCD_OK)
    {
        return PCD_ERR;
    }

    CmdFrameBuf[0] = PICC_TRANSFER;
    CmdFrameBuf[1] = goalBlockAddr;
    MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);
    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);
    if((status != PCD_OK) || (unLen != 4) || ((CmdFrameBuf[0] & 0x0F) != 0x0A))
    {
        status = PCD_ERR;
    }

    return status;
}

char PCD_Halt(void)
{
    char status;
    uint16_t unLen;
    uint8_t CmdFrameBuf[MFRC_MAXRLEN];

    CmdFrameBuf[0] = PICC_HALT;
    CmdFrameBuf[1] = 0;
    MFRC_CalulateCRC(CmdFrameBuf, 2, &CmdFrameBuf[2]);

    status = MFRC_CmdFrame(MFRC_TRANSCEIVE, CmdFrameBuf, 4, CmdFrameBuf, &unLen);

    return status;
}


uint8_t readUid[5]; //����
uint8_t CT[3]; 		//������
uint8_t DATA[16];	//�������

uint8_t KEY_A[6]= {0xff,0xff,0xff,0xff,0xff,0xff};
uint8_t KEY_B[6]= {0xff,0xff,0xff,0xff,0xff,0xff};


uint8_t status;
uint8_t addr=0x00; // 0���� 0��

void Cardcompare()
{
	    uint8_t i;

		status = PCD_Request(0x52, CT);						//�ҵ�������0
		if(!status)  //Ѱ���ɹ�
		{
				status = PCD_ERR;
				status = PCD_Anticoll(readUid);//����ײ
		}
		
		if(!status) //����ײ�ɹ�
		{
			status = PCD_ERR;
            printf("��������Ϊ��%x%x%x\r\n",CT[0],CT[1],CT[2]); //��ȡ�������� //��ȡ��������
			printf("���ţ�%x-%x-%x-%x\r\n",readUid[0],readUid[1],readUid[2],readUid[3]);
			HAL_Delay(1000);
            status=PCD_Select(readUid);
		}	
		
		if(!status) //ѡ���ɹ�
		{
				status = PCD_ERR;
				// ��֤A��Կ ���ַ ���� SN 
				status = PCD_AuthState(0x60, addr, KEY_A, readUid);
				if(status == PCD_OK)//��֤A�ɹ�
				{
						printf("A��Կ��֤�ɹ�\r\n");
						HAL_Delay(1000);
				}
				else
				{
						printf("A��Կ��֤ʧ��\r\n");
						HAL_Delay(1000);
				}
								
				// ��֤B��Կ ���ַ ���� SN  
				status = PCD_AuthState(0x61, addr, KEY_B, readUid);
				if(status == PCD_OK)//��֤B�ɹ�
				{
						printf("B��Կ��֤�ɹ�\r\n");
				}
				else
				{
						printf("B��Կ��֤ʧ��\r\n");					
				}
				HAL_Delay(1000);
		}	
		
		if(status == PCD_OK)//��֤����ɹ������Ŷ�ȡ0��
		{
            status = PCD_ERR;
            status = PCD_ReadBlock(addr, DATA); 
            if(status == PCD_OK)//�����ɹ�
            {		
                printf("0����0��DATA:");
                for(i = 0; i < 16; i++)
                {
                    printf("%02x", DATA[i]);
                }
                printf("\r\n");	
            }
            else
            {
                printf("����ʧ��\r\n");
            }
                    HAL_Delay(1000);
		}
		
}

