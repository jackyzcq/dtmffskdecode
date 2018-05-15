#pragma once

#include "fsk.h"
#include "databits.h"
// MsgType 0x04 SDMF,0x08 MDMF
//SDMF body max length 255 bytes, every byte ,the high bit is odd or even
// checksum include Type Length and body, mode 256 and 
typedef struct _SDMFMsg {
	unsigned char MsgType;
	unsigned char MsgLength;
	unsigned char MsgBody[255]; //Max lenght 255
	unsigned char checksum;
} SDMFMsg;

typedef int(*fille_samplebuffer) (float* samples_readptr, size_t read_nsamples);

//MDMF 
//parameter 
//0x01 date lenght 8
//0x02 number length 30(max)
//0x04 P/O 1
//0x07 name 50(max)
//0x08  P/O  1
//0xE1 message 120 (max)
typedef struct _MDMFMsg {
	unsigned char MsgType;
	unsigned char MsgLength;
	unsigned char DateLength;
	unsigned char date[9];
	unsigned char NumberLength;
	unsigned char number[31];
	unsigned char poNumber;
	unsigned char NameLength;
	unsigned char name[51];
	unsigned char poName;
	unsigned char msgLength;
	unsigned char message[121];
	unsigned char checksum;

}MDMFMsg;
class FSKDetector
{

protected:
	int calcFrequencyZerocrossing(short* data,int * position);
	void processIterationSearch();
	void processIterationDecode();
	void processSearchOneSerial();
	int decodeFrame(short* data,char * code);
	void HexMsgPrint(unsigned char * ptrData, int Length);
	
typedef	enum _STATE {
		HIGH, LOW, SILENCE, UNKNOWN
	} STATE;

typedef enum _DecoderStatus {
		IDLE, SEARCHING_SIGNAL, SEARCHING_START_BIT, DECODING
	}DecoderStatus;

	DecoderStatus mDecoderStatus = SEARCHING_SIGNAL;

	DecoderStatus mDecoderStatusPaused = IDLE;

	void setStatus(DecoderStatus status);
	void setStatus(DecoderStatus status, DecoderStatus paused);

	int frameSize,frameCount=0;
	char fskDetected = 0;
	int isMsgStart = 0;

	int FSK_detect(short samples[]);
	int determineState(int frequency, double rms);
	double rootMeanSquared(short* data);
	void getFrameData(int position);
	int calcFrequency(short* data);
	int CaculateCheckSum();
	void reinitialize();

	fsk_plan *fskp=NULL;

	void ffwDetect();

	int			ret = 0;

	int			carrier = 0;
	float		confidence_total = 0;
	float		amplitude_total = 0;
	unsigned int	nframes_decoded = 0;
	size_t		carrier_nsamples = 0;

	unsigned int	noconfidence = 0;
	unsigned int	advance = 0;

	// Fraction of nsamples_per_bit that we will "overscan"; range (0.0 .. 1.0)
	float fsk_frame_overscan = 0.5;
	//   should be != 0.0 (only the nyquist edge cases actually require this?)
	// for handling of slightly faster-than-us rates:
	//   should be >> 0.0 to allow us to lag back for faster-than-us rates
	//   should be << 1.0 or we may lag backwards over whole bits
	// for optimal analysis:
	//   should be >= 0.5 (half a bit width) or we may not find the optimal bit
	//   should be <  1.0 (a full bit width) or we may skip over whole bits
	// for encodings without start/stop bits:
	//     MUST be <= 0.5 or we may accidentally skip a bit
	//

	// ensure that we overscan at least a single sample
	unsigned int nsamples_overscan;

	// n databits plus bfsk_startbit start bits plus bfsk_nstopbit stop bits:
	float frame_n_bits;
	unsigned int frame_nsamples;

	float nsamples_per_bit;

	size_t	samplebuf_size;

	float	samplebuf[4096]; 
	size_t	samples_nvalid = 0;
	float	carrier_autodetect_threshold = 0.0;
	// fsk_confidence_threshold : signal-to-noise squelch control
	//
	// The minimum SNR-ish confidence level seen as "a signal".
	float fsk_confidence_threshold = 1.5;

	// fsk_confidence_search_limit : performance vs. quality
	//
	// If we find a frame with confidence > confidence_search_limit,
	// quit searching for a better frame.  confidence_search_limit has a
	// dramatic effect on peformance (high value yields low performance, but
	// higher decode quality, for noisy or hard-to-discern signals (Bell 103,
	// or skewed rates).
	float fsk_confidence_search_limit = 2.3f;
	float	bfsk_data_rate = 0.0;
	float band_width = 0;
	float bfsk_mark_f = 0;
	float bfsk_space_f = 0;
	unsigned int bfsk_inverted_freqs = 0;
	int bfsk_nstartbits = 1;
	float bfsk_nstopbits = 1.0;
	unsigned int bfsk_do_rx_sync = 0;
	unsigned int bfsk_do_tx_sync_bytes = 0;
	unsigned long long bfsk_sync_byte = -1;
	unsigned int bfsk_n_data_bits = 8;
	int bfsk_msb_first = 0;
	char *expect_data_string = NULL;
	char *expect_sync_string = NULL;
	unsigned int expect_n_bits;
	int invert_start_stop = 0;
	int autodetect_shift;
	char expect_data_string_buffer[64];
	char expect_sync_string_buffer[64];
	unsigned int expect_nsamples;
	float track_amplitude = 0.0;
	float peak_confidence = 0.0;

public:
	unsigned char MessageBody[258];
	int currentReceive=0;
	int MessageLength=0;
	short mSignal[2048];
	short mSampleBuffer[200];
	int mSamplePerbit;
	int mSamplePerByte;
	int mSignalPointer=0, mSignalEnd=0;
	int mCurrentBit=0;
	int msampleRate, mbaudRate;
	char oneCode;

	FSKDetector(int baudRate,int sampleRate,int bitPersample);
	~FSKDetector();
	int fskDetecting(short inputFrame[]);
	bool getFSKCode();

};

