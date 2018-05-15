/*
	Author: James Pate Williams, Jr. (c) 2016

	Audio Capture to RIFF Wave File
*/

#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <commdlg.h>
#include <winsock.h>
#include <mmsystem.h>
#include <process.h>
#include <stdlib.h>
#include <sys\stat.h>
#include <Windowsx.h>

#include <queue>
#include <concrt.h>

#include "resource.h"

#include "simpleaudio.h"

#include "DtmfDetector.hpp"
#include "FSKDetector.h"

#include "modules/audio_processing/agc/agc.h"

//using namespace webrtc;
//webrtc::Agc *agc_ = new webrtc::Agc();

//fsk 8k 1 8bit
//dtmf 8k 1 16bit
#define SAMPLERATE 8000
#define CHANNELS 1//2
#define BITSPERSAMPLE 16//8 

//int SAMPLERATE = 8000;//dtmf 8k fsk 48k
//int BITSPERSAMPLE = 16;//dtmf 16 fsk 8 

#define NUMBERBUTTONS 5
#define ACBSIZE CHANNELS * 8000 * 10 * 15
#define UW_WIDTH 1012
#define UW_HEIGHT 800
#define bzero(s, n) memset((s), 0, (n));

#define MAX_BUFFER_SIZE 8000

static const char*DATA_PATH = "data";
static const char*DTMF_PATH = "data\\dtmf";
static const char*FSK_PATH = "data\\fsk";
static const char*LOG_PATH = "data\\log";
static const char*RESULT_PATH = "data\\result";
static char PROG_PATH[256];

static DWORD old_volume;

static int Found_code = 0;
static int decode_count = 0;
typedef struct MyStruct AudioBuffer;
struct MyStruct
{
	unsigned char* buffer;
	int length;
	AudioBuffer *next;
};

AudioBuffer *FSKHead=nullptr;
AudioBuffer *DtmfHead = nullptr;
AudioBuffer *BufferCache = nullptr;

fille_samplebuffer fill_buffer;
char copyright[256], title[256];
char data_[4] = { 'd', 'a', 't', 'a' };
char fmt_[4] = { 'f', 'm', 't', ' ' };
char junk[4] = { 'J', 'U', 'N', 'K' };
char riff[4] = { 'R', 'I', 'F', 'F' };
char wave[4] = { 'W', 'A', 'V', 'E' };
int handle = -1, audioLength = 0, audioPtr = 0;
int acbSize = ACBSIZE, fileCount, page;
typedef enum _ReadCDRIFFWaveFileError {
	NoError = 0,
	RIFFCorrupt = 1,
	cbSizeFileCorrupt = 2,
	WAVECorrupt = 3,
	fmt_Corrupt = 4,
	cbSizeSubChunkCorrupt = 5,
	AudioFormatError = 6,
	NumChannelsError = 7,
	SampleRateError = 8,
	ByteRateError = 9,
	BlockAlignError = 10,
	BitsPerSampleError = 11,
	JUNKCorrupt = 12,
	cbSizeJunkCorrupt = 13,
	DATACorrupt = 14,
	AudioLengthError = 15
} ReadCDRIFFWaveFileError;

BOOL drawSelected = FALSE, playSelected = FALSE, recdSelected = FALSE;
BOOL recording = FALSE, playing = FALSE, stopped = FALSE;
BOOL bDetectStop = TRUE;
BYTE audioCaptureBuffer[ACBSIZE];
BYTE fskAudioBuffer[ACBSIZE];
BYTE dtmfAudioBuffer[ACBSIZE];
BYTE waveHdrBuffer[16000];
//BYTE waveHdrBuffer[BITSPERSAMPLE*1024/8];
CRITICAL_SECTION cs;

HANDLE hIOMutex;
HANDLE hUIMutex;
HINSTANCE hInst;
HWAVEIN waveIn;
HWAVEOUT waveOut;
HWND hWnd;
HWND hwndButton[NUMBERBUTTONS];
WAVEFORMATEX waveformat;
WAVEHDR waveHeader;

BYTE* ptrBufSave, ptrBufDetect;
int BufSavePos, BufDetctPos;
int saveIndex, detectIndex;

DtmfDetector *dtmfDetector;
FSKDetector *fskDetector;

simpleaudio *sa_file,*wav_file;

int DetectPos = 0;
int DetectMode=0;//0 dtmf 1 fsk
int numberCount = 0;
int frameSize = 1000;

HANDLE hDetectThread,hAGCThread;
HANDLE hDtmfThread, hFskThread;
DWORD dwDetectThreadID,hAGCThreadID;
DWORD dwDtmfThreadID, dwFskThreadID;

HWND hWndFSKView,hwndCheckBox,hwndTextBaudRate,hWndDTMFView,hWndResetButton,hWndTextVol;
short dtmfbuf[1024];
int preTelCount = 0 ,newCount=0;
using namespace std;
queue <AudioBuffer*> queueFSKBuffer;
queue <AudioBuffer*> queueDTMFBuffer;
queue <AudioBuffer*> queueCache;
queue <AudioBuffer*> queueAgcBuffer;
//extern void print_debug(char* str);

const static UINT_PTR IDS_TIMER1 = 0x111;

char debugBuffer[1024];

BYTE fskBuffer[MAX_BUFFER_SIZE+1000];
static int fskLength;

AudioBuffer* allocBuffer()
{
	AudioBuffer* ret, temp;
	if (0)
	{
		
		if (BufferCache != nullptr)
		{
			ret = BufferCache;
			BufferCache = ret->next;
			ret->next = nullptr;
		}
		else
		{
			ret = (AudioBuffer*)malloc(sizeof(AudioBuffer));
			ret->next = nullptr;
			ret->buffer = (unsigned char *)malloc(MAX_BUFFER_SIZE);
			ret->length = 0;
		}
	}
	else
	{
		if (queueCache.empty())
		{
			ret = (AudioBuffer*)malloc(sizeof(AudioBuffer));
			ret->next = nullptr;
			ret->buffer = (unsigned char *)malloc(MAX_BUFFER_SIZE);
			ret->length = 0;
		}
		else
		{
			ret = queueCache.front();
			queueCache.pop();
		}
	}
	return ret;
}

void freeBuffer(AudioBuffer*buffer) 
{
	if (0)
	{
		if (BufferCache == nullptr)
		{
			BufferCache = buffer;
		}
		else
		{
			buffer->next = BufferCache;
			BufferCache = buffer;
		}
	}
	else
	{
		queueCache.push(buffer);
	}
}

static void addBuffer(char*buf,int len) 
{
	AudioBuffer *fskbuf, *dtmfbuf,*agcBuffer;
	int length=(len>MAX_BUFFER_SIZE)? MAX_BUFFER_SIZE:len;
	WaitForSingleObject(hIOMutex, INFINITE);
	fskbuf = allocBuffer();
	dtmfbuf = allocBuffer();
	agcBuffer = allocBuffer();
	//sprintf(debugBuffer, "add dtmf buffer %d len:%d\n", dtmfbuf,len);
	//OutputDebugString(debugBuffer);
	//sprintf(debugBuffer, "add fsk buffer %d len:%d\n", fskbuf,len);
	//OutputDebugString(debugBuffer);
	
	if (fskbuf != nullptr) 
	{
		memcpy(fskbuf->buffer, buf, length);
		fskbuf->length = length;
		queueFSKBuffer.push(fskbuf);
	}
	if (dtmfbuf != nullptr)
	{
		memcpy(dtmfbuf->buffer, buf, length);
		dtmfbuf->length = length;
		queueDTMFBuffer.push(dtmfbuf);
	}
	if (agcBuffer != nullptr)
	{
		memcpy(agcBuffer->buffer, buf, length);
		agcBuffer->length = length;
		queueAgcBuffer.push(agcBuffer);
	}
	ReleaseMutex(hIOMutex);
}

static void SetMicVolume(const DWORD dwVolume)
{
	MMRESULT                        result;
	HMIXER                          hMixer;
	MIXERLINE                       ml = { 0 };
	MIXERLINECONTROLS               mlc = { 0 };
	MIXERCONTROL                    mc = { 0 };
	MIXERCONTROLDETAILS             mcd = { 0 };
	MIXERCONTROLDETAILS_UNSIGNED    mcdu = { 0 };

	WAVEFORMATEX waveformat;
	UINT hID;

	mixerGetID((HMIXEROBJ)waveIn, &hID, MIXER_OBJECTF_HWAVEIN);
	// get a handle to the mixer device
	result = mixerOpen(&hMixer, hID, 0, 0, MIXER_OBJECTF_MIXER);
	if (MMSYSERR_NOERROR == result)
	{
		ml.cbStruct = sizeof(MIXERLINE);
		ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_WAVEIN;

		// get the speaker line of the mixer device
		result = mixerGetLineInfo((HMIXEROBJ)hMixer, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE);
		if (MMSYSERR_NOERROR == result)
		{
			mlc.cbStruct = sizeof(MIXERLINECONTROLS);
			mlc.dwLineID = ml.dwLineID;
			mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
			mlc.cControls = 1;
			mlc.pamxctrl = &mc;
			mlc.cbmxctrl = sizeof(MIXERCONTROL);

			// get the volume controls associated with the speaker line
			result = mixerGetLineControls((HMIXEROBJ)hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);
			if (MMSYSERR_NOERROR == result)
			{
				mcdu.dwValue = dwVolume;

				mcd.cbStruct = sizeof(MIXERCONTROLDETAILS);
				mcd.hwndOwner = 0;
				mcd.dwControlID = mc.dwControlID;
				mcd.paDetails = &mcdu;
				mcd.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
				mcd.cChannels = 1;

				// set the volume
				result = mixerSetControlDetails((HMIXEROBJ)hMixer, &mcd, MIXER_SETCONTROLDETAILSF_VALUE);
				if (MMSYSERR_NOERROR == result)
					OutputDebugString("Volume changed!");
				else
					OutputDebugString("mixerSetControlDetails() failed");
			}
			else
				OutputDebugString("mixerGetLineControls() failed");
		}
		else
			OutputDebugString("mixerGetLineInfo() failed");

		mixerClose(hMixer);
	}
	else
		OutputDebugString("mixerOpen() failed");
}

//====================================================================

static DWORD GetVolume(void)
{
	DWORD                           dwVolume = -1;
	MMRESULT                        result;
	HMIXER                          hMixer;
	MIXERLINE                       ml = { 0 };
	MIXERLINECONTROLS               mlc = { 0 };
	MIXERCONTROL                    mc = { 0 };
	MIXERCONTROLDETAILS             mcd = { 0 };
	MIXERCONTROLDETAILS_UNSIGNED    mcdu = { 0 };

	UINT hID;

	// get a handle to the mixer device
	//result = mixerOpen(&hMixer, MIXER_OBJECTF_MIXER, 0, 0, 0);
	mixerGetID((HMIXEROBJ)waveIn, &hID, MIXER_OBJECTF_HWAVEIN);
	result = mixerOpen(&hMixer, hID, 0, 0, MIXER_OBJECTF_MIXER);
	if (MMSYSERR_NOERROR == result)
	{
		ml.cbStruct = sizeof(MIXERLINE);
		ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_WAVEIN;//MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

																// get the speaker line of the mixer device
		result = mixerGetLineInfo((HMIXEROBJ)hMixer, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE);
		if (MMSYSERR_NOERROR == result)
		{
			mlc.cbStruct = sizeof(MIXERLINECONTROLS);
			mlc.dwLineID = ml.dwLineID;
			mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
			mlc.cControls = 1;
			mlc.pamxctrl = &mc;
			mlc.cbmxctrl = sizeof(MIXERCONTROL);

			// get the volume controls associated with the speaker line
			result = mixerGetLineControls((HMIXEROBJ)hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);
			if (MMSYSERR_NOERROR == result)
			{
				mcd.cbStruct = sizeof(MIXERCONTROLDETAILS);
				mcd.hwndOwner = 0;
				mcd.dwControlID = mc.dwControlID;
				mcd.paDetails = &mcdu;
				mcd.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
				mcd.cChannels = 1;

				// get the volume
				result = mixerGetControlDetails((HMIXEROBJ)hMixer, &mcd, MIXER_SETCONTROLDETAILSF_VALUE);
				if (MMSYSERR_NOERROR == result)
					dwVolume = mcdu.dwValue;
				else
					OutputDebugString("mixerGetControlDetails() failed");
			}
			else
				OutputDebugString("mixerGetLineControls() failed");
		}
		else
			OutputDebugString("mixerGetLineInfo() failed");

		mixerClose(hMixer);
	}
	else
		OutputDebugString("mixerOpen() failed");

	return (dwVolume);
}
float AnalyzePreproc(const short* audio, size_t length) {
	size_t num_clipped = 0;
	for (size_t i = 0; i < length; ++i) {
		if (audio[i] == 32767 || audio[i] == -32768)
			++num_clipped;
	}
	return 1.0f * num_clipped / length;
}
DWORD WINAPI  AgcDetectProc(LPVOID param)
{
	short * sample;
	char buffer[255];
	float result = 0.0;
	AudioBuffer * current;
	int pos = 0;
	int count = 80;
	int length = 0;
	while (!bDetectStop)
	{
		WaitForSingleObject(hIOMutex, INFINITE);
		if (!queueAgcBuffer.empty())
		{
			current = queueAgcBuffer.front();
			queueAgcBuffer.pop();
			ReleaseMutex(hIOMutex);
			pos = 0;
			length = current->length >> 1;
			sample = (short*)current->buffer;
			while (pos < length)
			{
				result = AnalyzePreproc(sample+pos, count);
				pos += count;
				if (result > 0.05)
				{
					memset(buffer, 0, 255);
					sprintf(buffer, "level %f\r\n", result);
					OutputDebugString(buffer);
				}
			}
			WaitForSingleObject(hIOMutex, INFINITE);
			freeBuffer(current);
			ReleaseMutex(hIOMutex);
		}
		else
		{
			ReleaseMutex(hIOMutex);
			Sleep(10);
		}
	}
	return 0;
}
void InitializeOpenFilename(OPENFILENAME *openFilename) {
	static char szFilter[] = "WAVE Files (*.wav)\0*.wav\0\0";

	memset(openFilename, 0, sizeof(*openFilename));
	openFilename->lStructSize = sizeof(*openFilename);
	openFilename->lpstrFilter = szFilter;
	openFilename->nMaxFile = _MAX_PATH;
	openFilename->nMaxFileTitle = _MAX_FNAME + _MAX_EXT;
}

BOOL FindFileHandle(char *filename, BOOL output) {

	if (output)
		handle = open(filename, O_BINARY | O_WRONLY | O_CREAT | O_TRUNC, S_IWRITE);
	else
		handle = _open(filename, O_BINARY | O_RDONLY, S_IREAD);

	if (handle == - 1) {
		MessageBox(hWnd, "can't open file", "Warning Message",
			MB_ICONEXCLAMATION | MB_OK);
		return FALSE;
	}
	return TRUE;
}

MMRESULT FindSuitableInputDevice(HWND hwndProc)
{
	return waveInOpen(&waveIn, WAVE_MAPPER, &waveformat, (DWORD_PTR)hwndProc,
		(DWORD_PTR)hInst, CALLBACK_WINDOW);
}

MMRESULT FindSuitableOutputDevice(HWND hwndProc)
{
	return waveOutOpen(&waveOut, WAVE_MAPPER, &waveformat, (DWORD_PTR)hwndProc,
		(DWORD_PTR)hInst, CALLBACK_WINDOW);
}

BOOL EqualBuffer(BYTE *buffer1, BYTE *buffer2, int length)
{
	BOOL equal = TRUE;
	int i;

	for (i = 0; equal && i < length; i++)
		equal = buffer1[i] == buffer2[i];

	return equal;
}

void ClearWave(HWND hwnd, int buttonsY)
{
	HBRUSH wb = (HBRUSH)GetStockObject(WHITE_BRUSH);
	HDC hdc = GetDC(hwnd);
	RECT r, rect;

	GetClientRect(hwnd, &rect);

	r = rect;
	r.top = rect.top + buttonsY;
	
	FillRect(hdc, &r, wb);
	ReleaseDC(hwnd, hdc);
}

void DrawLine(HDC hdc, int x1, int y1, int x2, int y2)
{
	MoveToEx(hdc, x1, y1, NULL);
	LineTo(hdc, x2, y2);
}

void DrawWave(HWND hwnd, int buttonsY)
{
	HDC hdc = GetDC(hwnd);
	RECT rect;

	double xInter, xSlope;
	double yInter, ySlope;
	
	int i, width, height;
	int oldX, oldY;
	int dMax, dMin;
	int iMax, iMin;
	int xMax, xMin;
	int yMax, yMin;

	GetClientRect(hwnd, &rect);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top - buttonsY;
	xMin = 0;
	xMax = width - 1;
	yMin = height - 1;
	yMax = buttonsY;

	dMin = +127;
	dMax = -128;
	iMin = 0;
	iMax = 256;

	for (i = 0; i < audioLength; i++)
	{
		signed char bb = audioCaptureBuffer[i];

		if (bb < dMin)
			dMin = bb;

		if (bb > dMax)
			dMax = bb;
	}

	xSlope = 1.0 / ((double)(iMax - iMin) / (xMax - xMin));
	ySlope = 1.0 / ((double)(dMax - dMin) / (yMin - yMax));
	xInter = xMin - xSlope * iMin;
	yInter = yMax - ySlope * dMin;
	
	for (i = 0; i < iMax; i++)
	{
		signed char d = audioCaptureBuffer[256 * page + i];
		int x = (int)(xSlope * i + xInter);
		int y = (int)(ySlope * d + yInter);

		if (i == 0)
		{
			oldX = x;
			oldY = y;
			continue;
		}
		
		DrawLine(hdc, oldX, oldY, x, y);
		oldX = x;
		oldY = y;
	}

	ReleaseDC(hwnd, hdc);
}

ReadCDRIFFWaveFileError ReadAudioCDRIFFFile()
{
	BYTE DATA[4], FMT_[4], JUNK[4], RIFF[4], WAVE[4];
	char junkBuffer[4096];
	int cdSizeData, cbSizeFile, cbSizeJunk, cbSizeSubChunk, x;
	short audioFormat;
	short numChannels;
	int sampleRate;
	int byteRate;
	short blockAlign;
	short bitsPerSample;
	short pad;
	
	read(handle, RIFF, 4);

	if (!EqualBuffer((BYTE*)riff, RIFF, 4))
		return RIFFCorrupt;

	read(handle, &cbSizeFile, sizeof(cbSizeFile));

	if (cbSizeFile < 0)
		return cbSizeFileCorrupt;

	read(handle, WAVE, 4);

	if (!EqualBuffer((BYTE*)wave, WAVE, 4))
		return WAVECorrupt;

	read(handle, FMT_, 4);

	if (!EqualBuffer((BYTE*)fmt_, FMT_, 4))
		return fmt_Corrupt;

	read(handle, &cbSizeSubChunk, sizeof(cbSizeSubChunk));

//	if (cbSizeSubChunk != 18)
//		return cbSizeSubChunkCorrupt;
	if (cbSizeSubChunk == 16)
	{
		read(handle, &audioFormat, sizeof(audioFormat));

		if (audioFormat != WAVE_FORMAT_PCM)
			return AudioFormatError;

		read(handle, &numChannels, sizeof(numChannels));

		if (numChannels != CHANNELS)
			return NumChannelsError;

		read(handle, &sampleRate, sizeof(sampleRate));

		if (sampleRate != SAMPLERATE)
			return SampleRateError;

		read(handle, &byteRate, sizeof(byteRate));

		//	if (byteRate != SAMPLERATE * CHANNELS * BITSPERSAMPLE / 8)
		//		return ByteRateError;

		read(handle, &blockAlign, sizeof(blockAlign));

		//	if (blockAlign != CHANNELS * BITSPERSAMPLE / 8)
		//		return BlockAlignError;

		read(handle, &bitsPerSample, sizeof(bitsPerSample));

		//if (bitsPerSample != BITSPERSAMPLE)
		//	BITSPERSAMPLE = bitsPerSample;
		//		return BitsPerSampleError;

		//	read(handle, &pad, sizeof(pad));

		for (x = 0; x < 4096; x++)
			junkBuffer[x] = 0;

		//	read(handle, JUNK, 4);

		//	if (!EqualBuffer((BYTE*)junk, JUNK, 4))
		//		return JUNKCorrupt;

		//	read(handle, &cbSizeJunk, sizeof(cbSizeJunk));

		//	if (cbSizeJunk < 0 || cbSizeJunk > 4096)
		//		return cbSizeJunkCorrupt;

		//	read(handle, junkBuffer, cbSizeJunk);
	}
	else
	{
		read(handle, &audioFormat, sizeof(audioFormat));

		if (audioFormat != WAVE_FORMAT_PCM)
			return AudioFormatError;

		read(handle, &numChannels, sizeof(numChannels));

		if (numChannels != CHANNELS)
			return NumChannelsError;

		read(handle, &sampleRate, sizeof(sampleRate));

		if (sampleRate != SAMPLERATE)
			return SampleRateError;

		read(handle, &byteRate, sizeof(byteRate));

		//	if (byteRate != SAMPLERATE * CHANNELS * BITSPERSAMPLE / 8)
		//		return ByteRateError;

		read(handle, &blockAlign, sizeof(blockAlign));

		//	if (blockAlign != CHANNELS * BITSPERSAMPLE / 8)
		//		return BlockAlignError;

		read(handle, &bitsPerSample, sizeof(bitsPerSample));

		//if (bitsPerSample != BITSPERSAMPLE)
		//	BITSPERSAMPLE = bitsPerSample;
		//		return BitsPerSampleError;

			read(handle, &pad, sizeof(pad));

		for (x = 0; x < 4096; x++)
			junkBuffer[x] = 0;

			read(handle, JUNK, 4);

			if (!EqualBuffer((BYTE*)junk, JUNK, 4))
				return JUNKCorrupt;

			read(handle, &cbSizeJunk, sizeof(cbSizeJunk));

			if (cbSizeJunk < 0 || cbSizeJunk > 4096)
				return cbSizeJunkCorrupt;

			read(handle, junkBuffer, cbSizeJunk);
	}

	read(handle, DATA, 4);

	if (!EqualBuffer((BYTE*)data_, DATA, 4))
		return DATACorrupt;

	read(handle, &audioLength, sizeof(audioLength));

	if (audioLength < 0 || audioLength > acbSize)
		return AudioLengthError;

	read(handle, audioCaptureBuffer, audioLength);
	audioPtr = 0;
	close(handle);
	return NoError;
}

void WriteAudioCDRIFFFile()
{
	char junkBuffer[4096];
	int chunkSize = 0, junkSize = 0, k, x;

	for (x = audioLength; x < audioLength + 4096; x++)
	{
		k = (x + 54) % 4096;

		if (k == 0)
		{
			junkSize = (x + 46) % 4096 - 46;
			chunkSize = audioLength + (x + 46) % 4096;
			break;
		}
	}

	write(handle, riff, 4);
	write(handle, &chunkSize, sizeof(chunkSize));
	write(handle, wave, 4);
	write(handle, fmt_, 4);

	int subChunkSize1 = 18;
	short audioFormat = 1;
	short numChannels = CHANNELS;
	int sampleRate = SAMPLERATE;
	int byteRate = SAMPLERATE * CHANNELS * BITSPERSAMPLE / 8;
	short blockAlign = CHANNELS * BITSPERSAMPLE / 8;
	short bitsPerSample = BITSPERSAMPLE;
	short pad = 0;

	write(handle, &subChunkSize1, sizeof(subChunkSize1));
	write(handle, &audioFormat, sizeof(audioFormat));
	write(handle, &numChannels, sizeof(numChannels));
	write(handle, &sampleRate, sizeof(sampleRate));
	write(handle, &byteRate, sizeof(byteRate));
	write(handle, &blockAlign, sizeof(blockAlign));
	write(handle, &bitsPerSample, sizeof(bitsPerSample));
	write(handle, &pad, sizeof(pad));

	for (x = 0; x < junkSize; x++)
		junkBuffer[x] = 0;

	write(handle, junk, 4);
	write(handle, &junkSize, sizeof(junkSize));

	write(handle, junkBuffer, junkSize);

	write(handle, data_, 4);
	write(handle, &audioLength, sizeof(audioLength));
	
	write(handle, audioCaptureBuffer, audioLength);
	close(handle);
	audioLength = 0;
}


void updateFSKCode(char* str)
{
	decode_count++;
	memset(debugBuffer, 0, 1024);
	wsprintf(debugBuffer, "%d", decode_count);
	Edit_SetText(hwndTextBaudRate, debugBuffer);
	WaitForSingleObject(hUIMutex, INFINITE);
	Found_code = 1;
	memset(debugBuffer, 0, 1024);
	wsprintf(debugBuffer, "%s \r\n",str);
	Edit_SetSel(hWndFSKView, Edit_GetTextLength(hWndFSKView), Edit_GetTextLength(hWndFSKView));
	Edit_ReplaceSel(hWndFSKView, debugBuffer);
	/*
	numberCount++;
	if (numberCount % 3 == 0)
	{
		memset(debugBuffer, 0, 1024);
		wsprintf(debugBuffer, "\r\n", str);
		Edit_SetSel(hwndTextView, Edit_GetTextLength(hwndTextView), Edit_GetTextLength(hwndTextView));
		Edit_ReplaceSel(hwndTextView, debugBuffer);
	}
	*/
	ReleaseMutex(hUIMutex);
}

void updateDTMFCode(char* str)
{
	decode_count++;
	memset(debugBuffer, 0, 1024);
	wsprintf(debugBuffer, "%d", decode_count);
	Edit_SetText(hwndTextBaudRate, debugBuffer);
	WaitForSingleObject(hUIMutex, INFINITE);
	Found_code = 1;
	memset(debugBuffer, 0, 1024);
	wsprintf(debugBuffer, "%s \r\n", str);
	Edit_SetSel(hWndDTMFView, Edit_GetTextLength(hWndDTMFView), Edit_GetTextLength(hWndDTMFView));
	Edit_ReplaceSel(hWndDTMFView, debugBuffer);
	/*
	numberCount++;
	if (numberCount % 3 == 0)
	{
	memset(debugBuffer, 0, 1024);
	wsprintf(debugBuffer, "\r\n", str);
	Edit_SetSel(hwndTextView, Edit_GetTextLength(hwndTextView), Edit_GetTextLength(hwndTextView));
	Edit_ReplaceSel(hwndTextView, debugBuffer);
	}
	*/
	ReleaseMutex(hUIMutex);
}

static inline void
sc2f_array(signed char *src, int count, float *dest, float normfact)
{
	while (--count >= 0)
		dest[count] = ((float)src[count]) * normfact;
} /* sc2f_array */

static inline void
uc2f_array(unsigned char *src, int count, float *dest, float normfact)
{
	while (--count >= 0)
		dest[count] = (((int)src[count]) - 128) * normfact;
} /* uc2f_array */

static inline void
les2f_array(short *src, int count, float *dest, float normfact)
{
	short	value;

	while (--count >= 0)
	{
		value = src[count];
		//value = LE2H_16(value);
		dest[count] = ((float)value) * normfact;
	};
} /* les2f_array */

static inline void
bes2f_array(short *src, int count, float *dest, float normfact)
{
	short			value;

	while (--count >= 0)
	{
		value = src[count];
		//value = _byteswap_ushort(value);
		dest[count] = ((float)value) * normfact;
	};
} /* bes2f_array */
//#if 0
int fille_samplebuffer_from_buffer(float* samples_readptr, size_t read_nsamples)
{
	int dataLeft = fskLength - DetectPos;
	if (bDetectStop)
		return -1;
	//memset(debugBuffer, 0, 200);
	//sprintf(debugBuffer, "Detect Position is %d audio length is %d\n", DetectPos, fskLength);
	//OutputDebugString(debugBuffer);
	//print_debug(debugBuffer);

	if (BITSPERSAMPLE == 16)
		dataLeft = dataLeft >> 1;
	if (dataLeft > 0)
	{
		if (dataLeft > read_nsamples)
		{
			switch (BITSPERSAMPLE)
			{
			case 8:
				uc2f_array(&fskBuffer[DetectPos], read_nsamples, samples_readptr, 1.0 / ((float)0x80));
				DetectPos += read_nsamples;
				break;

			case 16:
				les2f_array((short*)&fskBuffer[DetectPos], read_nsamples, samples_readptr, 1.0 / ((float)0x8000));
				DetectPos += read_nsamples<<1;
				break;

			default:
				break;
			}
				
			
			
			//memset(debugBuffer, 0, 200);
			//sprintf(debugBuffer, "Detect Position is %d audio length is %d\n", DetectPos,audioLength);
			//OutputDebugString(debugBuffer);
			return read_nsamples;
		}
		else
		{
			WaitForSingleObject(hIOMutex, INFINITE);
			if (!queueFSKBuffer.empty())
			{
				int left_;
				if (BITSPERSAMPLE == 16)
					left_ = dataLeft << 1;
				else
					left_ = dataLeft;
				memcpy(&fskBuffer[0], &fskBuffer[DetectPos], left_);
				AudioBuffer* temp = queueFSKBuffer.front();
				queueFSKBuffer.pop();
				//sprintf(debugBuffer, "get fsk 0 buffer %d %d\n", temp, queueFSKBuffer.size());
				//OutputDebugString(debugBuffer);
				memcpy(&fskBuffer[left_], temp->buffer, temp->length);
				fskLength = temp->length + left_;
				DetectPos = 0;
				freeBuffer(temp);
			}
			ReleaseMutex(hIOMutex);
			Sleep(10);
			return -1;
			switch (BITSPERSAMPLE)
			{
			case 8:
				uc2f_array(&fskBuffer[DetectPos], dataLeft, samples_readptr, 1.0 / ((float)0x80));
				DetectPos += dataLeft;
				return dataLeft;
				break;

			case 16:
				les2f_array((short*)&fskBuffer[DetectPos], dataLeft, samples_readptr, 1.0 / ((float)0x8000));
				DetectPos += dataLeft<<1;
				return dataLeft;
				break;

			default:
				break;
			}

		}

	}
	/*
	else if (audioLength < DetectPos)
	{
			dataLeft = acbSize - DetectPos;
			if (dataLeft > read_nsamples)
			{
				switch (BITSPERSAMPLE)
				{
				case 8:
					uc2f_array(&fskBuffer[DetectPos], read_nsamples, samples_readptr, 1.0 / ((float)0x80));
					DetectPos += read_nsamples;
					return read_nsamples;
					break;

				case 16:
					les2f_array((short*)&fskBuffer[DetectPos], read_nsamples, samples_readptr, 1.0 / ((float)0x8000));
					DetectPos += read_nsamples << 1;
					return read_nsamples;
					break;

				default:
					break;
				}

			}
			else
			{
				if (audioLength + dataLeft > read_nsamples)
				{
					switch (BITSPERSAMPLE)
					{
					case 8:
						uc2f_array(&fskBuffer[DetectPos], dataLeft, samples_readptr, 1.0 / ((float)0x80));
						DetectPos = 0;
						uc2f_array(&fskBuffer[DetectPos], read_nsamples - dataLeft, samples_readptr + dataLeft, 1.0 / ((float)0x80));
						DetectPos = read_nsamples - dataLeft;
						return read_nsamples;
						break;

					case 16:
						les2f_array((short*)&fskBuffer[DetectPos], dataLeft, samples_readptr, 1.0 / ((float)0x8000));
						DetectPos = 0;
						uc2f_array(&fskBuffer[DetectPos], read_nsamples - dataLeft, samples_readptr + dataLeft, 1.0 / ((float)0x8000));
						DetectPos = (read_nsamples - dataLeft) << 1;
						return read_nsamples;
						break;

					default:
						break;
					}
					DetectPos = 0;
					//return dataLeft >> 1;
				}
				else
				{
					Sleep(500);
					return -1;
				}
			}

	}
	*/
	else
	{
		WaitForSingleObject(hIOMutex, INFINITE);
		if (!queueFSKBuffer.empty())
		{
			AudioBuffer* temp = queueFSKBuffer.front();
			queueFSKBuffer.pop();
			//sprintf(debugBuffer, "get fsk 1 buffer %d\n", temp);
			//OutputDebugString(debugBuffer);
			memcpy(fskBuffer, temp->buffer, temp->length);
			fskLength = temp->length;
			DetectPos = 0;
			freeBuffer(temp);
		}
		ReleaseMutex(hIOMutex);
		Sleep(10);
		return -1;
	}
	return -1;
}
//#else
int fille_samplebuffer_from_file(float* samples_readptr, size_t read_nsamples)
{
	return simpleaudio_read(sa_file, samples_readptr, read_nsamples);
}
//#endif

DWORD WINAPI TestFSK(LPVOID lpPara)
{
	//int i;
	while (!bDetectStop)
	{
		fskDetector->fskDetecting(dtmfbuf);
	}
	OutputDebugString("the detect thread is stopping\n");
	//DetectPos = 0;
	return 0;
}


DWORD WINAPI TestDtmf(LPVOID lpPara)
{
	int length=0, position=0;
	BYTE buffer[MAX_BUFFER_SIZE];
	while (!bDetectStop)
	{
		{
			if (length == 0)
			{
				WaitForSingleObject(hIOMutex, INFINITE);
				if (!queueDTMFBuffer.empty())
				{
					AudioBuffer* temp = queueDTMFBuffer.front();
					queueDTMFBuffer.pop();
					//sprintf(debugBuffer, "get dtmf buffer %d\n", temp);
					//OutputDebugString(debugBuffer);
					memcpy(buffer, temp->buffer, temp->length);
					length = temp->length;
					freeBuffer(temp);
				}
				ReleaseMutex(hIOMutex);
			}
			int dataLeft = length - position;

			if (dataLeft> frameSize * 2)
			{
				int pos = position;
				for (int j = 0; j < frameSize; ++j)
				{
					pos++;
					dtmfbuf[j] = ((buffer[pos++] << 8) & 0xFF00);

				}
				dtmfDetector->dtmfDetecting(dtmfbuf, frameSize);
				position += frameSize * 2;

				//memset(debugBuffer, 0, 200);
				//sprintf(debugBuffer, "Detect Position is %d audio length is %d\n", DetectPos, audioLength);
				//OutputDebugString(debugBuffer);
			}
			else if (dataLeft > 0)
			{
				int pos = position;
				for (int j = 0; j < dataLeft / 2; ++j)
				{
					pos++;
					dtmfbuf[j] = ((buffer[pos++] << 8) & 0xFF00);

				}
				dtmfDetector->dtmfDetecting(dtmfbuf, dataLeft / 2);
				//position += dataLeft;
				length = 0;
				position = 0;
				//memset(debugBuffer, 0, 200);
				//sprintf(debugBuffer, "Detect Position is %d audio length is %d\n", DetectPos, audioLength);
				//OutputDebugString(debugBuffer);
			}
			/*
			else if (audioLength < DetectPos)
			{
				dataLeft = acbSize - DetectPos;
				int pos = DetectPos;
				if (dataLeft> frameSize * 2)
				{
					int pos = DetectPos;
					for (int j = 0; j < frameSize; ++j)
					{
						pos++;
						dtmfbuf[j] = ((audioCaptureBuffer[pos++] << 8) & 0xFF00);

					}
					dtmfDetector->dtmfDetecting(dtmfbuf, frameSize);
					DetectPos += frameSize * 2;

					memset(debugBuffer, 0, 200);
					//sprintf(debugBuffer, "Detect Position is %d audio length is %d\n", DetectPos, audioLength);
					//OutputDebugString(debugBuffer);
				}
				else if (dataLeft > 0)
				{
					int pos = DetectPos;
					for (int j = 0; j < dataLeft / 2; ++j)
					{
						pos++;
						dtmfbuf[j] = ((audioCaptureBuffer[pos++] << 8) & 0xFF00);

					}
					dtmfDetector->dtmfDetecting(dtmfbuf, dataLeft / 2);
					DetectPos = 0;
					//
					if (dtmfDetector->getIndexDialButtons() > 0)
					{
					newCount = dtmfDetector->getIndexDialButtons();
					if (preTelCount == 0)
					{
					preTelCount = newCount;
					SetTimer(hWnd, IDS_TIMER1, 500, NULL);
					}
					else if (preTelCount < newCount)
					{
					KillTimer(hWnd, IDS_TIMER1);
					SetTimer(hWnd, IDS_TIMER1, 500, NULL);
					preTelCount = newCount;
					}
					}
					//
					memset(debugBuffer, 0, 200);
					//sprintf(debugBuffer, "Detect Position is %d audio length is %d\n", DetectPos, audioLength);
					//OutputDebugString(debugBuffer);
				}
			}
			*/
			else
			{
				Sleep(30);
			}
		}
	}
	OutputDebugString("the detect thread is stopping\n");
	//DetectPos = 0;
	return 0;
}
#if 0
void TestFSK()
{

#if 1
	int filesize = 0;
	unsigned char sbuf[2048];
	int ret = 0;
	int i, j;
	_lseek(handle, 0, SEEK_SET);
	filesize = _lseek(handle, 0, SEEK_END);
	_lseek(handle, 0, SEEK_SET);
	ret = _read(handle, sbuf, 2048);
	while (ret>0)
	{
		i = 0;
		for (int j = 0; j < 1024; ++j)
		{
			//dtmfbuf[j] = (waveHeader.lpData[j]-128) << 8;
			//dtmfbuf[j] = dtmfbuf[j] | waveHeader.lpData[i++];

			dtmfbuf[j] = ((sbuf[i++] << 8) & 0xFF00);
				dtmfbuf[j] |= (sbuf[i++]&0xff);
		}
		if (fskDetector->fskDetecting(dtmfbuf) == 1)
		{
			SDMFMsg msg;
//			fskDetector->getFSKCode((unsigned char*)&msg);
			memset(debugBuffer, 0, 1024);
			wsprintf(debugBuffer, "%s\r\n", msg.MsgBody);
			Edit_SetSel(hWndDTMFView, Edit_GetTextLength(hWndDTMFView), Edit_GetTextLength(hWndDTMFView));
			Edit_ReplaceSel(hWndDTMFView, debugBuffer);
		}
		ret = _read(handle, sbuf, 2048);
	}
#endif
}
#endif
void print_out(char* str)
{
//	OutputDebugString(str);
}
BOOL GenerateFileName(char* prefix, char*filename) 
{
	if (prefix != NULL&& filename != NULL)
	{
		time_t timer;
		struct tm *tblock;
		timer = time(NULL);
		tblock = localtime(&timer);
		if(!strcmp(prefix,"dtmf"))
		sprintf(filename, "%s\\%s\\dtmf-%d%02d%02d-%02d%02d%02d.wav",PROG_PATH, DTMF_PATH, tblock->tm_year + 1900, tblock->tm_mon + 1, tblock->tm_mday,
			tblock->tm_hour , tblock->tm_min, tblock->tm_sec);
		if (!strcmp(prefix, "fsk"))
			sprintf(filename, "%s\\%s\\fsk-%d%02d%02d-%02d%02d%02d.wav", PROG_PATH, FSK_PATH, tblock->tm_year + 1900, tblock->tm_mon + 1, tblock->tm_mday,
				tblock->tm_hour , tblock->tm_min, tblock->tm_sec);
		if (!strncmp(prefix, "log",3))
			sprintf(filename, "%s\\%s\\%s-%d%02d%02d-%02d%02d%02d", PROG_PATH, LOG_PATH, prefix,tblock->tm_year + 1900, tblock->tm_mon + 1, tblock->tm_mday,
				tblock->tm_hour, tblock->tm_min, tblock->tm_sec);
		if (!strncmp(prefix, "result", 6))
			sprintf(filename, "%s\\%s\\%s-%d%02d%02d-%02d%02d%02d", PROG_PATH, RESULT_PATH, prefix, tblock->tm_year + 1900, tblock->tm_mon + 1, tblock->tm_mday,
				tblock->tm_hour, tblock->tm_min, tblock->tm_sec);
		return TRUE;
	}
	return FALSE;
}
void SetVolume()
{
	char buf[128];
	memset(buf, 0, 128);
	sprintf(buf, "%d", GetVolume()*100/65535);
	Edit_SetText(hWndTextVol, buf);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	char none[32];
	ReadCDRIFFWaveFileError error;
	//MMRESULT error;
	struct {
		long style;
		char *text;
	} button[] = {// { BS_PUSHBUTTON, "Draw Wave Data" },
				  // { BS_PUSHBUTTON, "Start Playing"},
				   { BS_PUSHBUTTON, "Start Dtmf Recording" },
				   //{ BS_PUSHBUTTON, "Clear Wave Drawing" },
				   //{ BS_PUSHBUTTON, "Wave Data Page Up"  },
				   //{ BS_PUSHBUTTON, "Wave Data Page Dn"  }
	};
	DWORD dWord1 = 0, dWord2 = 0;
	HDC hdc;
	PAINTSTRUCT ps;
	RECT rect;
	UINT cbWaveHeader, count = 0, i, nDevices, uMessage = 0;
	WSADATA wsaData;
	OPENFILENAME openFilename;
	MMRESULT result;
	static char filename[_MAX_FNAME + 1] = {'\0'};
	static char fileTitle[_MAX_FNAME + _MAX_EXT + 1] = {'\0'};
	static int cxClient, cyClient;
	static int buttonsY, j, chunkSize = 0, junkSize = 0, maxPages, playLength, x, y;
	static BOOL mouseOn = FALSE, rf;
	static RECT channelRect[16];
	
	switch (iMsg) {
		case WM_COMMAND:
			if (LOWORD(wParam) == 0)
			{
				BOOL openFile;
				MMRESULT result;

				if (!bDetectStop)
				{
					MessageBox(hWnd, "Stop Detect First",
						"Error Message", MB_ICONSTOP | MB_OK);
					return 0;
				}

				InitializeOpenFilename(&openFilename);
				openFilename.lpstrFile = filename;
				openFilename.lpstrFileTitle = fileTitle;
				openFilename.Flags = 0;

				if (GetOpenFileName(&openFilename)) {
					Found_code = 0;
					//openFile = FindFileHandle(filename, FALSE);
					fill_buffer = fille_samplebuffer_from_file;
					sa_file = simpleaudio_open_stream(SA_BACKEND_FILE, filename, SA_STREAM_RECORD, SA_SAMPLE_FORMAT_FLOAT, SAMPLERATE, 1, "", filename);
				//	simpleaudio_get_rate(sa_file);
					if (sa_file/*openFile*/) {
						//ReadAudioCDRIFFFile();
						//if (DetectMode == 1)
						{
							DetectPos = 0;
							bDetectStop = false;
							fskDetector = new FSKDetector(1200, simpleaudio_get_rate(sa_file), BITSPERSAMPLE);
							fskDetector->fskDetecting(dtmfbuf);
							delete fskDetector;
							bDetectStop = true;
							DetectPos = 0;
							audioLength = 0;
							fskDetector = NULL;
							simpleaudio_close(sa_file);
						
						}
						//else
						if(Found_code == 0)
						{
						//	simpleaudio_close(sa_file);
							if (0)
							{
								openFile = FindFileHandle(filename, FALSE);
								ReadAudioCDRIFFFile();
							}
							else
							{
								sa_file = simpleaudio_open_stream(SA_BACKEND_FILE, filename, SA_STREAM_RECORD, SA_SAMPLE_FORMAT_S16, SAMPLERATE, 1, "", filename);
								if (sa_file)
								{
									int len = 0;
									audioLength = 0;
									len = simpleaudio_read(sa_file, &audioCaptureBuffer[audioLength], 1000);
									while (len > 0)
									{
										audioLength += len<<1;
										len = simpleaudio_read(sa_file, &audioCaptureBuffer[audioLength], 1000);
									}
									simpleaudio_close(sa_file);
								}
								//audioLength = audioLength << 1;
								//openFile = FindFileHandle("test.wav", TRUE);
								//WriteAudioCDRIFFFile();
								
							}
							DetectPos = 0; 
							
							dtmfDetector = new DtmfDetector(1000);
							while (DetectPos < audioLength)
							{
								int j = 0;
								int dataLeft = audioLength - DetectPos;
								if (dataLeft > 2000)
								{
									dataLeft = 2000;
								}
								for (int j = 0; j < dataLeft / 2; ++j)
								{
									//DetectPos++;
									//dtmfbuf[j] = ((audioCaptureBuffer[DetectPos++] << 8) & 0xFF00);
									dtmfbuf[j] = ((audioCaptureBuffer[DetectPos++]) & 0x00FF);
									dtmfbuf[j] |= ((audioCaptureBuffer[DetectPos++] << 8) & 0xFF00);

								}
								dtmfDetector->dtmfDetecting(dtmfbuf, dataLeft/2);
							}
							delete dtmfDetector;
							DetectPos = 0;
							audioLength = 0;
							dtmfDetector = NULL;
						}
//						page = 0;
//						maxPages = audioLength / 256;
//						DrawWave(hwnd, buttonsY);
						//simpleaudio_close(sa_file);
						sa_file = NULL;
					}
				}
			}
			else if (LOWORD(wParam) == 1) {
				if (playing == FALSE && playSelected == FALSE)
				{
					BOOL openFile;
					MMRESULT result;

					InitializeOpenFilename(&openFilename);
					openFilename.lpstrFile = filename;
					openFilename.lpstrFileTitle = fileTitle;
					openFilename.Flags = 0;

					if (GetOpenFileName(&openFilename)) {
						openFile = FindFileHandle(filename, FALSE);

						if (openFile) {
							playSelected = TRUE;
							result = FindSuitableOutputDevice(hwnd);

							if (result != MMSYSERR_NOERROR)
							{
								MessageBox(hWnd, "could not find suitable output device",
									"Error Message", MB_ICONSTOP | MB_OK);
								close(handle);
								exit(1);
							}

							//EnableWindow(hwndButton[0], FALSE);
							//EnableWindow(hwndButton[2], FALSE);
							SetWindowText((HWND)lParam, "Stop Playing");
						}
					}
				}
				else {
					stopped = TRUE;
					waveOutClose(waveOut);
					//EnableWindow(hwndButton[0], TRUE);
					//EnableWindow(hwndButton[2], TRUE);
					playSelected = playing = FALSE;
					audioLength = 0;
					SetWindowText((HWND)lParam, "Start Playing");
				}
			}
			else if (LOWORD(wParam) == 2) {
				if (recording == FALSE && recdSelected == FALSE)
				{
					BOOL openFile;
					//MMRESULT result;

					InitializeOpenFilename(&openFilename);
					openFilename.lpstrFile = filename;
					openFilename.lpstrFileTitle = fileTitle;
					openFilename.Flags = OFN_OVERWRITEPROMPT;

					if (DetectMode == 1)
						openFile = GenerateFileName("fsk", filename);
					else
						openFile = GenerateFileName("dtmf", filename);

					//if (GetOpenFileName(&openFilename)) {
					if (openFile) {
						//openFile = FindFileHandle(filename, TRUE);
						wav_file = simpleaudio_open_stream(SA_BACKEND_FILE, filename, SA_STREAM_PLAYBACK, SA_SAMPLE_FORMAT_S16, SAMPLERATE, 1, "", filename);
						if (wav_file) {
							recdSelected = TRUE;
							
							result = FindSuitableInputDevice(hwnd);

							if (result != MMSYSERR_NOERROR)
							{
								MessageBox(hWnd, "could not find suitable input device",
									"Error Message", MB_ICONSTOP | MB_OK);
								
								close(handle);
								exit(1);
							}
							old_volume = GetVolume();
							SetMicVolume(65535/5);
							SetVolume();
							//if (DetectMode == 1)
							{
								char baudrate[12];
								Edit_GetText(hwndTextBaudRate, baudrate, 12);
								fill_buffer = fille_samplebuffer_from_buffer;
								int brate = atoi(baudrate);
								//if (brate > 1100 && brate < 1300)
								//	fskDetector = new FSKDetector(brate, SAMPLERATE, BITSPERSAMPLE);
								//else
									fskDetector = new FSKDetector(1200, SAMPLERATE, BITSPERSAMPLE);
							}
							//else
							{
								dtmfDetector = new DtmfDetector(1000);
							}
							bDetectStop = FALSE;
							DetectPos = 0;
							fskLength = 0;
							while (!queueDTMFBuffer.empty())
							{
								AudioBuffer* temp = queueDTMFBuffer.front();
								freeBuffer(temp);
								queueDTMFBuffer.pop();
							}
							while (!queueFSKBuffer.empty())
							{
								AudioBuffer* temp = queueFSKBuffer.front();
								freeBuffer(temp);
								queueFSKBuffer.pop();
							}
							//hDetectThread = CreateThread(NULL, 0, TestFSK, &audioLength, 0, &dwDetectThreadID);
							hDtmfThread = CreateThread(NULL, 0, TestDtmf, &audioLength, 0, &dwDtmfThreadID);
							hFskThread = CreateThread(NULL, 0, TestFSK, &audioLength, 0, &dwFskThreadID);
							//hAGCThread = CreateThread(NULL, 0, AgcDetectProc, &audioLength, 0, &hAGCThreadID);
							//EnableWindow(hwndButton[0], FALSE);
							//EnableWindow(hwndButton[1], FALSE);
							SetWindowText((HWND)lParam, "Stop");
						}
					}
				}
				else {
					stopped = TRUE;
					decode_count = 0;
					SetMicVolume(old_volume);
					waveInStop(waveIn);
					waveInClose(waveIn);
					EnableWindow((HWND)lParam, FALSE);
					//EnableWindow(hwndButton[1], TRUE);
					recdSelected = FALSE;
					KillTimer(hWnd, IDS_TIMER1);
					SetWindowText((HWND)lParam, "Start");
				}
			}
			else if (LOWORD(wParam) == 3)
			{
				ClearWave(hwnd, buttonsY);
				Edit_SetText(hWndDTMFView,"this is only a test");
			}
			else if (LOWORD(wParam) == 4) //volume down
			{
				//page = (page + 1) % maxPages;
				//ClearWave(hwnd, buttonsY);
				//DrawWave(hwnd, buttonsY);
				//SetVolume();
			}
			else if (LOWORD(wParam) == 5) //volume up
			{
				//page = (page - 1) % maxPages;
				//if (page < 0)
				//	page = 0;
				//ClearWave(hwnd, buttonsY);
				//DrawWave(hwnd, buttonsY);
			}
			else if (LOWORD(wParam) == 7)
			{
				if (!bDetectStop)
				{
					MessageBox(hWnd, "Stop Detect First",
						"Error Message", MB_ICONSTOP | MB_OK);
					return 0;
				}
				int status = Button_GetCheck(hwndCheckBox);
				if (status == 0)
				{
					Button_SetCheck(hwndCheckBox, 1);
					DetectMode = 0;
					//BITSPERSAMPLE = 16;
					//SAMPLERATE = 8000;

				}
				else
				{
					Button_SetCheck(hwndCheckBox, 0);
					DetectMode = 1;
					//BITSPERSAMPLE = 8;
					//SAMPLERATE = 48000;
				}
				waveformat.cbSize = 0;
				waveformat.nAvgBytesPerSec = SAMPLERATE * CHANNELS * BITSPERSAMPLE / 8;
				waveformat.nBlockAlign = CHANNELS * BITSPERSAMPLE / 8;
				waveformat.wBitsPerSample = BITSPERSAMPLE;
				waveformat.nSamplesPerSec = SAMPLERATE;

			}
			else if (LOWORD(wParam) == 11)
			{
				if (!bDetectStop)
				{
					MessageBox(hWnd, "Stop Detect First",
						"Error Message", MB_ICONSTOP | MB_OK);
					return 0;
				}
				Edit_SetText(hWndFSKView, "");
				Edit_SetText(hWndDTMFView, "");
				Edit_SetText(hwndTextBaudRate, "0");
				decode_count = 0;
			}
			return 0;
		case WM_CREATE:
			/* create buttons */
		{
			int cxChar, cyChar, height, x, y;
			HDC hDC = GetDC(hwnd);
			RECT crect;
			TEXTMETRIC textMetric;

			CreateDirectory(DATA_PATH, NULL);
			CreateDirectory(DTMF_PATH, NULL);
			CreateDirectory(FSK_PATH, NULL);
			CreateDirectory(LOG_PATH, NULL);
			CreateDirectory(RESULT_PATH, NULL);

			hIOMutex = CreateMutex(NULL, FALSE, NULL);
			hUIMutex = CreateMutex(NULL, FALSE, NULL);

			GetTextMetrics(hDC, &textMetric);
			GetClientRect(hwnd, &crect);
			cxChar = textMetric.tmAveCharWidth;
			cyChar = textMetric.tmHeight + textMetric.tmExternalLeading;
			height = 8 * cyChar / 4;
			x = cxChar;
			y = cyChar;
			buttonsY = 4 * cyChar + height;

	//		for (i = 0; i < NUMBERBUTTONS; i++) {
				int width = 20 * cxChar;
//hwndButton[i] =
				hwndButton[2] = CreateWindow("button", "Start",
					WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 100, y, 100, height,
					hwnd, (HMENU)2, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
				x += width + 4 * cxChar;

		//	}	
				hwndButton[3] = CreateWindow("button", "vol-",
					WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 100, y+50, 80, height,
					hwnd, (HMENU)4, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

				hWndTextVol = CreateWindow("edit", "0.0",
					WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 200, y + 60, 90, cyChar + 4,
					hwnd, (HMENU)12, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

				hwndButton[4] = CreateWindow("button", "vol+",
					WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 300, y+50, 80, height,
					hwnd, (HMENU)5, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

			CreateWindow("button", "Test",
					WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 350, y, 100, height,
					hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

			hwndTextBaudRate=CreateWindow("edit", "Test",
				WS_CHILD | WS_VISIBLE | WS_BORDER |ES_NUMBER|ES_CENTER, 550, y+cyChar/2, 100, cyChar+4,
				hwnd, (HMENU)8, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

			hwndCheckBox = CreateWindow("button", "dtmf", WS_CHILD | WS_VISIBLE |BS_CHECKBOX,
						250, y,60, height,
						hwnd, (HMENU)7, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			hWndDTMFView = CreateWindow("edit", "", WS_CHILD | WS_VISIBLE|WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
				100, 100, 350, 600,
				hwnd, (HMENU)6, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

			hWndFSKView = CreateWindow("edit", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
				500, 100, 350, 600,
				hwnd, (HMENU)10, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

			hWndResetButton = CreateWindow("button", "reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
				700, y, 100, height,
				hwnd, (HMENU)11, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

			if (DetectMode == 0)
			{
				Button_SetCheck(hwndCheckBox, 1);
			}
			else
			{
				Button_SetCheck(hwndCheckBox, 0);
			}
				InitializeCriticalSection(&cs);
				waveformat.cbSize = 0;
				waveformat.nAvgBytesPerSec = SAMPLERATE * CHANNELS * BITSPERSAMPLE / 8;
				waveformat.nBlockAlign = CHANNELS * BITSPERSAMPLE / 8;
				waveformat.nChannels = CHANNELS;
				waveformat.nSamplesPerSec = SAMPLERATE;
				waveformat.wBitsPerSample = BITSPERSAMPLE;
				waveformat.wFormatTag = WAVE_FORMAT_PCM;

			}
			return 0;
		case WM_DESTROY:
			if (playing)
			{
				waveOutClose(waveOut);
			}
			if (recording)
			{
				waveInStop(waveIn);
				waveInClose(waveIn);
			}

			CloseHandle(hIOMutex);
			CloseHandle(hUIMutex);

			if (dtmfDetector != NULL)
				delete dtmfDetector;
			if (fskDetector != NULL)
				delete fskDetector;

			while (!queueDTMFBuffer.empty())
			{
				AudioBuffer* temp = queueDTMFBuffer.front();
				freeBuffer(temp);
				queueDTMFBuffer.pop();
			}
			while (!queueFSKBuffer.empty())
			{
				AudioBuffer* temp = queueFSKBuffer.front();
				freeBuffer(temp);
				queueFSKBuffer.pop();
			}

			while (BufferCache != nullptr) 
			{
				AudioBuffer *temp = BufferCache;
				BufferCache = temp->next;
				free(temp->buffer);
				free(temp);
			}
			while (!queueCache.empty())
			{
				AudioBuffer* temp = queueCache.front();
				queueCache.pop();
				if (temp != nullptr)
				{
					free(temp->buffer);
					free(temp);
				}
			}
			PostQuitMessage(0);
			return 0;
		case WM_TIMER:
			{
			if (0/*wParam == IDS_TIMER1*/)
			{
				if (preTelCount > 0 && preTelCount == newCount)
				{
					if (dtmfDetector != NULL)
					{
						memset(debugBuffer, 0, 1024);
						wsprintf(debugBuffer, "%s       ", dtmfDetector->getDialButtonsArray());
						Edit_SetSel(hWndDTMFView, Edit_GetTextLength(hWndDTMFView), Edit_GetTextLength(hWndDTMFView));
						Edit_ReplaceSel(hWndDTMFView, debugBuffer);
						numberCount++;
						if (numberCount % 3 == 0)
						{
							memset(debugBuffer, 0, 1024);
							wsprintf(debugBuffer, "\r\n");
							Edit_SetSel(hWndDTMFView, Edit_GetTextLength(hWndDTMFView), Edit_GetTextLength(hWndDTMFView));
							Edit_ReplaceSel(hWndDTMFView, debugBuffer);
						}
						memset(debugBuffer, 0, 1024);
						wsprintf(debugBuffer, "Tel:%s\n", dtmfDetector->getDialButtonsArray());
						//print_debug(debugBuffer);
						dtmfDetector->zerosIndexDialButton();
						preTelCount = 0;
						newCount = 0;
						KillTimer(hWnd, IDS_TIMER1);
					}
				}
			}
			}
			return 0;
		case WIM_CLOSE:
			if (recording)
			{
				EnterCriticalSection(&cs);
				recording = FALSE;
				stopped = FALSE;
				simpleaudio_close(wav_file);
				//WriteAudioCDRIFFFile();
				LeaveCriticalSection(&cs);

				bDetectStop = TRUE;
				WaitForSingleObject(hDetectThread, INFINITE);
				CloseHandle(hDetectThread);
				WaitForSingleObject(hDtmfThread, INFINITE);
				CloseHandle(hDtmfThread);
				WaitForSingleObject(hFskThread, INFINITE);
				CloseHandle(hFskThread);
				hDetectThread = NULL;
				if (fskDetector != NULL)
					delete fskDetector;
				fskDetector = NULL;
				if (dtmfDetector != NULL)
					delete dtmfDetector;
				dtmfDetector = NULL;
				EnableWindow(hwndButton[2], TRUE);

			}

			return 0;
		case WIM_DATA:
			if (recording)
			{
				EnterCriticalSection(&cs);

			//	for (i = 0; i < waveHeader.dwBytesRecorded; i++)
			//	{
			//			audioCaptureBuffer[audioLength + i] = waveHeader.lpData[i];
			//	}

				memcpy(&audioCaptureBuffer[audioLength], waveHeader.lpData, waveHeader.dwBytesRecorded);

				audioLength = (audioLength + waveHeader.dwBytesRecorded) % acbSize;
				addBuffer(waveHeader.lpData, waveHeader.dwBytesRecorded);
				if (wav_file)
				{
					simpleaudio_write(wav_file, waveHeader.lpData, waveHeader.dwBytesRecorded >> 1);
				}
				waveInAddBuffer(waveIn, &waveHeader, sizeof(waveHeader));
				LeaveCriticalSection(&cs);
			}
			return 0;
		case WIM_OPEN:
			if (recdSelected)
			{
				EnterCriticalSection(&cs);
				bzero(&waveHeader, sizeof(waveHeader))
				if (waveInPrepareHeader(waveIn, &waveHeader,
					sizeof(waveHeader)) != MMSYSERR_NOERROR)
				{
					MessageBox(hWnd, "could not prepare wave header",
						"Warning Message", MB_ICONWARNING | MB_OK);
					waveInClose(waveIn);
				}
				if (DetectMode == 0)
					waveHeader.dwBufferLength = 8000;//8000;
				else if (DetectMode == 1)
					waveHeader.dwBufferLength = 8000;//8000;//48000;
				waveHeader.lpData = (LPSTR)waveHdrBuffer;
				bzero(&waveHdrBuffer, sizeof(waveHdrBuffer))
				LeaveCriticalSection(&cs);
				if (waveInStart(waveIn) != MMSYSERR_NOERROR)
				{
					MessageBox(hWnd, "could not start input device",
						"Warning Message", MB_ICONWARNING | MB_OK);
					waveInClose(waveIn);
				}
				EnterCriticalSection(&cs);
				if ((error = (ReadCDRIFFWaveFileError)waveInAddBuffer(waveIn, &waveHeader,
					sizeof(waveHeader))) != NoError)
				{
					MessageBox(hWnd, "could not add buffer",
						"Warning Message", MB_ICONWARNING | MB_OK);
					waveInClose(waveIn);
				}
				LeaveCriticalSection(&cs);
				recording = TRUE;
				stopped = FALSE;
			}
			return 0;
		case WOM_CLOSE:
			if (playing)
			{
				EnterCriticalSection(&cs);
				playing = FALSE;
				stopped = FALSE;
				stopped = TRUE;
				waveOutClose(waveOut);
				//EnableWindow(hwndButton[0], TRUE);
				//EnableWindow(hwndButton[2], TRUE);
				playSelected = FALSE;
				SetWindowText((HWND)lParam, "Start Playing");
				LeaveCriticalSection(&cs);
			}
			return 0;
		case WOM_DONE:
			if (playing)
			{
				EnterCriticalSection(&cs);
				if (audioPtr + sizeof(waveHdrBuffer) < audioLength)
					playLength = sizeof(waveHdrBuffer);
				else
					playLength = audioLength % sizeof(waveHdrBuffer);
				for (i = 0; i < playLength; i++)
					waveHeader.lpData[i] = audioCaptureBuffer[audioPtr + i];
				waveHeader.dwBufferLength = playLength;
				waveOutWrite(waveOut, &waveHeader, sizeof(waveHeader));
				audioPtr += playLength;
				LeaveCriticalSection(&cs);
			}
			return 0;
		case WOM_OPEN:
			if (playSelected)
			{
				if ((error = ReadAudioCDRIFFFile()) != NoError)
				{
					MessageBox(hWnd, "could not read RIFF Wave File",
						"Error Message", MB_ICONSTOP | MB_OK);
					close(handle);
					exit(1);
				}
				EnterCriticalSection(&cs);
				if (waveOutPrepareHeader(waveOut, &waveHeader,
					sizeof(waveHeader)) != MMSYSERR_NOERROR)
				{
					MessageBox(hWnd, "could not prepare wave header",
						"Warning Message", MB_ICONWARNING | MB_OK);
					waveOutClose(waveOut);
				}
				waveHeader.lpData = (LPSTR)waveHdrBuffer;
				if (audioPtr + sizeof(waveHdrBuffer) < audioLength)
					playLength = sizeof(waveHdrBuffer);
				else
					playLength = audioLength % sizeof(waveHdrBuffer);
				for (i = 0; i < playLength; i++)
					waveHeader.lpData[i] = audioCaptureBuffer[audioPtr + i];
				waveHeader.dwBufferLength = playLength;
				if (waveOutWrite(waveOut, &waveHeader,sizeof(waveHeader)) != MMSYSERR_NOERROR)
				{
					MessageBox(hWnd, "could not add buffer",
						"Warning Message", MB_ICONWARNING | MB_OK);
					waveOutClose(waveOut);
				}
				audioPtr += playLength;
				LeaveCriticalSection(&cs);
				playing = TRUE;
				stopped = FALSE;
			}
			return 0;
	}
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
				   PSTR szCmdLine, int iCmdShow) {
	static char szAppName[] = "Dtmf & FSK decode";
	static char title[] = "Dtmf & FSK decode";
	MSG msg;
	WNDCLASSEX  wndclass;

	GetCurrentDirectory(255, PROG_PATH);

	wndclass.cbSize        = sizeof (wndclass);
	wndclass.style         = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc   = WndProc;
	wndclass.cbClsExtra    = 0;
	wndclass.cbWndExtra    = 0;
	wndclass.hInstance     = hInstance;
	wndclass.hIcon         = NULL;
	wndclass.hCursor       = LoadCursor (NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszMenuName  = NULL;
	wndclass.lpszClassName = szAppName;
	wndclass.hIconSm       = NULL;
	RegisterClassEx (&wndclass);
	hWnd = CreateWindow(szAppName, title, WS_OVERLAPPEDWINDOW,
						CW_USEDEFAULT, CW_USEDEFAULT, UW_WIDTH, UW_HEIGHT,
						NULL, NULL, hInstance, NULL);

	hInst = hInstance;
	ShowWindow(hWnd, iCmdShow);
	UpdateWindow(hWnd) ;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}