
#include "FSKDetector.h"

#include <math.h>
#include <ctype.h>
#if 1
#include <cstdio>
#include<string.h>
#include <time.h>  
#include <stdio.h>  
#include <stdlib.h>  
#include <sys/types.h>  
#include <sys/timeb.h>  
#include <string.h> 

#endif
#if 1
#include <cstdio>
#include<string.h>
#include <time.h>  
#include <stdio.h>  
#include <stdlib.h>  
#include <sys/types.h>  
#include <sys/timeb.h>  
#include <string.h> 
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#endif
using namespace std;
#define DEBUG 1
#if DEBUG	
ofstream *fskdebug,*fskcode;
static char buf[1024];
#endif
typedef int                 BOOL;
void fsk_debug(char* str);
extern BOOL GenerateFileName(char* prefix, char*filename);

FSKDetector::FSKDetector(int baudRate, int sampleRate, int bitPersample)
{
	bfsk_data_rate = baudRate;
	mbaudRate = baudRate;
	msampleRate = sampleRate;

	mSamplePerbit = 18;//msampleRate / mbaudRate;
	frameSize = 1024;//sampleRate*bitPersample / 8;
	mSamplePerByte = mSamplePerbit * 10 + mSamplePerbit / 2;
	frameCount = 0;
	MessageLength = 0;

	reinitialize();

	if (expect_data_string == NULL) {
		expect_data_string = expect_data_string_buffer;
		expect_n_bits = build_expect_bits_string(expect_data_string, bfsk_nstartbits, bfsk_n_data_bits, bfsk_nstopbits, invert_start_stop, 0, 0);
	}
//	debug_log("eds = '%s' (%lu)\n", expect_data_string, strlen(expect_data_string));

	//char expect_sync_string_buffer[64];
	if (expect_sync_string == NULL && bfsk_do_rx_sync && (long long)bfsk_sync_byte >= 0) {
		expect_sync_string = expect_sync_string_buffer;
		build_expect_bits_string(expect_sync_string, bfsk_nstartbits, bfsk_n_data_bits, bfsk_nstopbits, invert_start_stop, 1, bfsk_sync_byte);
	}
	else {
		expect_sync_string = expect_data_string;
	}

	expect_nsamples = nsamples_per_bit * expect_n_bits;

	memset(MessageBody, 0, sizeof(MessageBody));
#if DEBUG
	//fskdebug = new ofstream("output_data",ios::app);
	time_t timer;
	struct tm *tblock;
	char filename[128];
	timer = time(NULL);
	tblock = localtime(&timer);
	sprintf(buf, "\n########## time is: %s", asctime(tblock));
	//sprintf(filename, "data\\log\\logfsk-%d%02d%02d-%02d%02d%02d", tblock->tm_year + 1900, tblock->tm_mon + 1, tblock->tm_mday,
	//	tblock->tm_hour, tblock->tm_min, tblock->tm_sec);
	GenerateFileName("logfsk", filename);
	fskdebug = new ofstream(filename, ios::app);
	fskdebug->write(buf, strlen(buf));
	GenerateFileName("result_fsk", filename);
	fskcode = new ofstream(filename, ios::app);
#endif
}


FSKDetector::~FSKDetector()
{
#if DEBUG
	if (fskdebug != NULL)
		fskdebug->close();
	fskdebug = NULL;
	if (fskcode != NULL)
		fskcode->close();
	fskcode = NULL;
#endif
	fsk_plan_destroy(fskp);
}
int FSKDetector::calcFrequency(short* data)
{
	int numSamples = mSamplePerbit;
	int numCrossing = 0;
	char buf[100];
	int upwards=0 , downwards = 0;
	int maxSample=0,minSample=0;
	int maxIndex, minIndex;
	int harfcyclesample = 0;
	int level;
	int wards;
	int start=0;
	int gotozero,gotomax;

	int result;
	for (int i = 0; i < numSamples ; i++) {
		{
			memset(buf, 0, 100);
			sprintf(buf, "%d ", data[i]);
			fsk_debug(buf);
		}
	}
	fsk_debug("\n");
	for (int i = 0; i < numSamples-1; i++) {
		if (data[i] >= 0)
		{
			if (data[i + 1] > data[i])
			{
				gotomax++;
			}
			else
			{
				gotozero++;
			}
		}

	}

	level = maxIndex - minIndex;

	level = level < 0 ? -level : level;

	wards = upwards - downwards;

	wards = wards < 0 ? -wards : wards;

	if (level < 10000)
	{
		result = 1200;
	}
	else
	{
		if (wards < 2)
			result = 2200;
		else
			result = 1200;
	}
	memset(buf, 0, 100);
	sprintf(buf, "-----frequency : %d %d level:%d WW:%d\n", result, mSignalPointer,level,wards);
	fsk_debug(buf);
	return result;

}
int FSKDetector::CaculateCheckSum()
{
	unsigned char checksum = 0;
	char buf[64];
	for (int i = 0; i < MessageLength-1; i++)
	{
		checksum += MessageBody[i];
	}
	memset(buf, 0, 64);
	sprintf(buf, "-----checksum :0x%02x\n", checksum);
	fsk_debug(buf);
	checksum ^= 0xFF;
	checksum++;
	if (checksum == MessageBody[MessageLength -1])
		return 1;
	return 0;
}
void FSKDetector::reinitialize()
{

	mbaudRate = bfsk_data_rate;
	
	nsamples_per_bit = msampleRate / bfsk_data_rate;
	nsamples_overscan = nsamples_per_bit * fsk_frame_overscan + 0.5f;
	if (fsk_frame_overscan > 0.0f && nsamples_overscan == 0)
		nsamples_overscan = 1;

	frame_n_bits = bfsk_n_data_bits + bfsk_nstartbits + bfsk_nstopbits;
	frame_nsamples = nsamples_per_bit * frame_n_bits + 0.5f;


	unsigned int nbits = 0;
	nbits += 1;			// prev stop bit (last whole stop bit)
	nbits += 1;	// start bits
	nbits += 8;
	nbits += 1;			// stop bit (first whole stop bit)

						// FIXME EXPLAIN +1 goes with extra bit when scanning
	samplebuf_size = ceilf(nsamples_per_bit) * (nbits + 1);
	samplebuf_size *= 2; // account for the half-buf filling method

#define SAMPLE_BUF_DIVISOR 12
#ifdef SAMPLE_BUF_DIVISOR
						 // For performance, use a larger samplebuf_size than necessary
	if (samplebuf_size < msampleRate / SAMPLE_BUF_DIVISOR)
		samplebuf_size = msampleRate / SAMPLE_BUF_DIVISOR;
#endif


	if (bfsk_data_rate >= 400) {
		/*
		* Bell 202:     baud=1200 mark=1200 space=2200
		*/
		autodetect_shift = -(bfsk_data_rate * 5 / 6);
		if (bfsk_mark_f == 0)
			bfsk_mark_f = bfsk_data_rate / 2 + 600;
		if (bfsk_space_f == 0)
			bfsk_space_f = bfsk_mark_f - autodetect_shift;
		if (band_width == 0)
			band_width = 200;
	}
	else if (bfsk_data_rate >= 100) {
		/*
		* Bell 103:     baud=300 mark=1270 space=1070
		* ITU-T V.21:   baud=300 mark=1280 space=1080
		*/
		autodetect_shift = 200;
		if (bfsk_mark_f == 0)
			bfsk_mark_f = 1270;
		if (bfsk_space_f == 0)
			bfsk_space_f = bfsk_mark_f - autodetect_shift;
		if (band_width == 0)
			band_width = 50;	// close enough
	}
	else {
		/*
		* RTTY:     baud=45.45 mark/space=variable shift=-170
		*/
		autodetect_shift = 170;
		if (bfsk_mark_f == 0)
			bfsk_mark_f = 1585;
		if (bfsk_space_f == 0)
			bfsk_space_f = bfsk_mark_f - autodetect_shift;
		if (band_width == 0) {
			band_width = 10;	// FIXME chosen arbitrarily
		}
	}

	// defaults: 1 start bit, 1 stop bit
	if (bfsk_nstartbits < 0)
		bfsk_nstartbits = 1;
	if (bfsk_nstopbits < 0)
		bfsk_nstopbits = 1.0;


	if (bfsk_inverted_freqs) {
		float t = bfsk_mark_f;
		bfsk_mark_f = bfsk_space_f;
		bfsk_space_f = t;
	}

	/* restrict band_width to <= data rate (FIXME?) */
	if (band_width > bfsk_data_rate)
		band_width = bfsk_data_rate;

	// sanitize confidence search limit
	if (fsk_confidence_search_limit < fsk_confidence_threshold)
		fsk_confidence_search_limit = fsk_confidence_threshold;
	if (fskp != NULL)
	{
		fsk_plan_destroy(fskp);
	}
	fskp = fsk_plan_new(msampleRate, bfsk_mark_f, bfsk_space_f, band_width);

	expect_nsamples = nsamples_per_bit * expect_n_bits;

}

extern void updateFSKCode(char*str);
extern fille_samplebuffer fill_buffer;
extern void print_out(char*str);

static void
report_no_carrier(fsk_plan *fskp,
	unsigned int sample_rate,
	float bfsk_data_rate,
	float frame_n_bits,
	unsigned int nframes_decoded,
	size_t carrier_nsamples,
	float confidence_total,
	float amplitude_total)
{
	float nbits_decoded = nframes_decoded * frame_n_bits;
	char buf[512];
	memset(buf,0, 512);

#if 0
	sprintf(buf, "nframes_decoded=%u\n", nframes_decoded);
	fsk_debug(buf);
	print_out(buf);
	sprintf(buf, "nbits_decoded=%f\n", nbits_decoded);
	fsk_debug(buf);
	print_out(buf);
	sprintf(buf, "carrier_nsamples=%lu\n", carrier_nsamples);
	fsk_debug(buf);
	print_out(buf);
#endif
	float throughput_rate =
		nbits_decoded * sample_rate / (float)carrier_nsamples;
	sprintf(buf, "\n### NOCARRIER ndata=%u confidence=%.3f ampl=%.3f bps=%.2f",
		nframes_decoded,
		(double)(confidence_total / nframes_decoded),
		(double)(amplitude_total / nframes_decoded),
		(double)(throughput_rate));
	//print_out(buf);
	fsk_debug(buf);
#if 0
	fprintf(stderr, " bits*sr=%llu rate*nsamp=%llu",
		(unsigned long long)(nbits_decoded * sample_rate + 0.5),
		(unsigned long long)(bfsk_data_rate * carrier_nsamples));
#endif
	if ((unsigned long long)(nbits_decoded * sample_rate + 0.5f) == (unsigned long long)(bfsk_data_rate * carrier_nsamples)) {
		sprintf(buf, " (rate perfect) ###\n");
		fsk_debug(buf);
		print_out(buf);
	}
	else {
		float throughput_skew = (throughput_rate - bfsk_data_rate)
			/ bfsk_data_rate;
		sprintf(buf, " (%.1f%% %s) ###\n",
			(double)(fabsf(throughput_skew) * 100.0f),
			signbit(throughput_skew) ? "slow" : "fast"	
		);
		fsk_debug(buf);
		print_out(buf);
	}
}
void FSKDetector::ffwDetect()
{
	advance = 0;
	while (1) {


		/* Shift the samples in samplebuf by 'advance' samples */

		if (advance == samplebuf_size) {
			samples_nvalid = 0;
			advance = 0;
		}
		if (advance) {
			if (advance > samples_nvalid)
				break;
			memmove(samplebuf, samplebuf + advance,
				(samplebuf_size - advance) * sizeof(float));
			samples_nvalid -= advance;
		}

		if (samples_nvalid < samplebuf_size / 2) {
			float	*samples_readptr = samplebuf + samples_nvalid;
			size_t	read_nsamples = samplebuf_size / 2;
			int r;
			r = fill_buffer(samples_readptr, read_nsamples);
			if (r < 0) {
				//				fprintf(stderr, "simpleaudio_read: error\n");
				ret = -1;
				break;
			}
			samples_nvalid += r;
		}

		if (samples_nvalid == 0)
			break;

		/* Auto-detect carrier frequency */
		static int carrier_band = -1;
		if (carrier_autodetect_threshold > 0.0f && carrier_band < 0) {
			unsigned int i;
			float nsamples_per_scan = nsamples_per_bit;
			if (nsamples_per_scan > fskp->fftsize)
				nsamples_per_scan = fskp->fftsize;
			for (i = 0; i + nsamples_per_scan <= samples_nvalid;
				i += nsamples_per_scan) {
				carrier_band = fsk_detect_carrier(fskp,
					samplebuf + i, nsamples_per_scan,
					carrier_autodetect_threshold);
				if (carrier_band >= 0)
					break;
			}
			advance = i + nsamples_per_scan;
			if (advance > samples_nvalid)
				advance = samples_nvalid;
			if (carrier_band < 0) {

				continue;
			}

			// default negative shift -- reasonable?
			int b_shift = -(float)(autodetect_shift + fskp->band_width / 2.0f)
				/ fskp->band_width;
			if (bfsk_inverted_freqs)
				b_shift *= -1;
			/* only accept a carrier as b_mark if it will not result
			* in a b_space band which is "too low". */
			int b_space = carrier_band + b_shift;
			if (b_space < 1 || b_space >= fskp->nbands) {

				carrier_band = -1;
				continue;
			}

			fsk_set_tones_by_bandshift(fskp, /*b_mark*/carrier_band, b_shift);
		}

		/*
		* The main processing algorithm: scan samplesbuf for FSK frames,
		* looking at an entire frame at once.
		*/



		if (samples_nvalid < expect_nsamples)
		{
			//getFSKCode();
			break;
		}

		// try_max_nsamples
		// serves two purposes
		// 1. avoids finding a non-optimal first frame
		// 2. allows us to track slightly slow signals
		unsigned int try_max_nsamples;
		if (carrier)
			try_max_nsamples = nsamples_per_bit * 0.75f + 0.5f;
		else
			try_max_nsamples = nsamples_per_bit;
		try_max_nsamples += nsamples_overscan;

		// FSK_ANALYZE_NSTEPS Try 3 frame positions across the try_max_nsamples
		// range.  Using a larger nsteps allows for more accurate tracking of
		// fast/slow signals (at decreased performance).  Note also
		// FSK_ANALYZE_NSTEPS_FINE below, which refines the frame
		// position upon first acquiring carrier, or if confidence falls.
#define FSK_ANALYZE_NSTEPS		3
		unsigned int try_step_nsamples = try_max_nsamples / FSK_ANALYZE_NSTEPS;
		if (try_step_nsamples == 0)
			try_step_nsamples = 1;

		float confidence, amplitude;
		unsigned long long bits = 0;
		/* Note: frame_start_sample is actually the sample where the
		* prev_stop bit begins (since the "frame" includes the prev_stop). */
		unsigned int frame_start_sample = 0;

		unsigned int try_first_sample;
		float try_confidence_search_limit;

		try_confidence_search_limit = fsk_confidence_search_limit;
		try_first_sample = carrier ? nsamples_overscan : 0;

		confidence = fsk_find_frame(fskp, samplebuf, expect_nsamples,
			try_first_sample,
			try_max_nsamples,
			try_step_nsamples,
			try_confidence_search_limit,
			carrier ? expect_data_string : expect_sync_string,
			&bits,
			&amplitude,
			&frame_start_sample
		);

		int do_refine_frame = 0;

		if (confidence < peak_confidence * 0.75f) {
			do_refine_frame = 1;
			//			debug_log(" ... do_refine_frame rescan (confidence %.3f << %.3f peak)\n", confidence, peak_confidence);
			peak_confidence = 0;
		}

		// no-confidence if amplitude drops abruptly to < 25% of the
		// track_amplitude, which follows amplitude with hysteresis
		if (amplitude < track_amplitude * 0.25f) {
			confidence = 0;
		}

#define FSK_MAX_NOCONFIDENCE_BITS	20

		if (confidence <= fsk_confidence_threshold) {

			// FIXME: explain
			if (++noconfidence > FSK_MAX_NOCONFIDENCE_BITS)
			{
				carrier_band = -1;
				if (carrier) {
					if (1)
						report_no_carrier(fskp, msampleRate, bfsk_data_rate,
							frame_n_bits, nframes_decoded,
							carrier_nsamples, confidence_total, amplitude_total);
					if (MessageLength > 23)
					{

						if (1)
						{
							float nbits_decoded = nframes_decoded * frame_n_bits;
							float throughput_rate =
								nbits_decoded * msampleRate / (float)carrier_nsamples;

							//if (throughput_rate != bfsk_data_rate)


							float throughput_skew = fabsf((throughput_rate - bfsk_data_rate)
								/ bfsk_data_rate)*100.0f;
							if (throughput_skew > 0.9)
							{
								if (throughput_rate > 1160 && throughput_rate < 1250)
								{
									if (throughput_rate < bfsk_data_rate)
									{
										bfsk_data_rate = throughput_rate - 6;
									}
									else
										bfsk_data_rate = throughput_rate + 6;
								}
								else
									bfsk_data_rate = throughput_rate;
								reinitialize();

							}
						}
					}

					{
						char buf[100];
						memset(buf, 0, 100);
						sprintf(buf, "\n================\n");
						fsk_debug(buf);
						for (int ii = 0; ii < MessageLength; ii++)
						{
							memset(buf, 0, 100);
							sprintf(buf, "0x%02x ", MessageBody[ii]);
							fsk_debug(buf);
						}
						getFSKCode();

					}

					carrier = 0;
					carrier_nsamples = 0;
					confidence_total = 0;
					amplitude_total = 0;
					nframes_decoded = 0;
					track_amplitude = 0.0;
					MessageLength = 0;
					memset(MessageBody, 0, sizeof(MessageBody));


				}
			}

			/* Advance the sample stream forward by try_max_nsamples so the
			* next time around the loop we continue searching from where
			* we left off this time.		*/
			advance = try_max_nsamples;
			//			debug_log("@ NOCONFIDENCE=%u advance=%u\n", noconfidence, advance);
			continue;
		}

		// Add a frame's worth of samples to the sample count
		carrier_nsamples += frame_nsamples;

		if (carrier) {

			// If we already had carrier, adjust sample count +start -overscan
			carrier_nsamples += frame_start_sample;
			carrier_nsamples -= nsamples_overscan;

		}
		else {

			// We just acquired carrier.
#if 0
			if (!quiet_mode) {
				if (bfsk_data_rate >= 100)
					fprintf(stderr, "### CARRIER %u @ %.1f Hz ",
					(unsigned int)(bfsk_data_rate + 0.5f),
						(double)(fskp->b_mark * fskp->band_width));
				else
					fprintf(stderr, "### CARRIER %.2f @ %.1f Hz ",
					(double)(bfsk_data_rate),
						(double)(fskp->b_mark * fskp->band_width));
			}

			if (!quiet_mode)
				fprintf(stderr, "###\n");
#endif
			carrier = 1;
			databits_decode_ascii8(0, 0, 0, 0); // reset the frame processor

			do_refine_frame = 1;
			//			debug_log(" ... do_refine_frame rescan (acquired carrier)\n");
		}

		if (do_refine_frame)
		{
			if (confidence < INFINITY && try_step_nsamples > 1) {
				// FSK_ANALYZE_NSTEPS_FINE:
				// Scan again, but try harder to find the best frame.
				// Since we found a valid confidence frame in the "sloppy"
				// fsk_find_frame() call already, we're sure to find one at
				// least as good this time.
#define FSK_ANALYZE_NSTEPS_FINE		8
				try_step_nsamples = try_max_nsamples / FSK_ANALYZE_NSTEPS_FINE;
				if (try_step_nsamples == 0)
					try_step_nsamples = 1;
				try_confidence_search_limit = INFINITY;
				float confidence2, amplitude2;
				unsigned long long bits2;
				unsigned int frame_start_sample2;
				confidence2 = fsk_find_frame(fskp, samplebuf, expect_nsamples,
					try_first_sample,
					try_max_nsamples,
					try_step_nsamples,
					try_confidence_search_limit,
					carrier ? expect_data_string : expect_sync_string,
					&bits2,
					&amplitude2,
					&frame_start_sample2
				);
				if (confidence2 > confidence) {
					bits = bits2;
					amplitude = amplitude2;
					frame_start_sample = frame_start_sample2;
				}
			}
		}

		track_amplitude = (track_amplitude + amplitude) / 2;
		if (peak_confidence < confidence)
			peak_confidence = confidence;
		//		debug_log("@ confidence=%.3f peak_conf=%.3f amplitude=%.3f track_amplitude=%.3f\n",
		//			confidence, peak_confidence, amplitude, track_amplitude);

		confidence_total += confidence;
		amplitude_total += amplitude;
		nframes_decoded++;
		noconfidence = 0;

		// Advance the sample stream forward past the junk before the
		// frame starts (frame_start_sample), and then past decoded frame
		// (see also NOTE about frame_n_bits and expect_n_bits)...
		// But actually advance just a bit less than that to allow
		// for tracking slightly fast signals, hence - nsamples_overscan.
		advance = frame_start_sample + frame_nsamples - nsamples_overscan;

		//		debug_log("@ nsamples_per_bit=%.3f n_data_bits=%u "
					//" frame_start=%u advance=%u\n",
					//nsamples_per_bit, bfsk_n_data_bits,
					//frame_start_sample, advance);

				// chop off the prev_stop bit
		if (bfsk_nstopbits != 0.0f)
			bits = bits >> 1;


		/*
		* Send the raw data frame bits to the backend frame processor
		* for final conversion to output data bytes.
		*/

		// chop off framing bits
		bits = bit_window(bits, bfsk_nstartbits, bfsk_n_data_bits);
		if (bfsk_msb_first) {
			bits = bit_reverse(bits, bfsk_n_data_bits);
		}
		//debug_log("Input: %08x%08x - Databits: %u - Shift: %i\n", (unsigned int)(bits >> 32), (unsigned int)bits, bfsk_n_data_bits, bfsk_nstartbits);

		unsigned int dataout_size = 4096;
		char dataoutbuf[4096];
		unsigned int dataout_nbytes = 0;

		// suppress printing of bfsk_sync_byte bytes
		if (bfsk_do_rx_sync) {
			if (dataout_nbytes == 0 && bits == bfsk_sync_byte)
				continue;
		}

		dataout_nbytes += databits_decode_ascii8(dataoutbuf + dataout_nbytes,
			dataout_size - dataout_nbytes,
			bits, (int)bfsk_n_data_bits);

		if (dataout_nbytes == 0)
			continue;

		/*
		* Print the output buffer to stdout
		*/
#if 1
		{
			char *p = dataoutbuf;
			for (; dataout_nbytes; p++, dataout_nbytes--) {
				//char printable_char = isprint(*p) || isspace(*p) ? *p : '.';
				MessageBody[MessageLength++] = (unsigned char)*p;
			}
		}
#endif

	} /* end of the main loop */
}

int FSKDetector::decodeFrame(short* data,char* code)
{
	int firstQuad, secondQuad, thirdQuad, fourQuad;
	int tempFrame[128];
	int buffer[200];
	char buf[100];
	int countOne = 0, countZero = 0;
	int i;
	int high=0, low=0;
	int index = 0;
	int bitShift = 0;
	short result = 0;
	double test = 0;
	int counter=0, count1=0;
	int temp = 0;
	firstQuad = secondQuad = thirdQuad = fourQuad=0;
	// x0>=0  
	//    x0>x1
	for (i = 0; i < mSamplePerByte; i++)
	{
		if (data[i] >= 0)
		{
			if (countZero > 0)
			{
				tempFrame[index++] = countZero;
				countZero = 0;
			}
			countOne++;
			buffer[i] = 1;
		}
		else
		{
			if (countOne > 0)
			{
				tempFrame[index++] = countOne;
				countOne = 0;
			}
			countZero++;
			buffer[i] = 0;
		}
	}
	for (i = 0; i < index; i++)
	{
		counter += tempFrame[i];
		memset(buf, 0, 100);
		sprintf(buf, "%d ", tempFrame[i]);
		//fsk_debug(buf);
		if (tempFrame[i] >= 7)
		{
			low++;
			if (high > 0)
			{
				count1 += 5;//tempFrame[i] / 2;
				
				temp = count1/18;
				for (int j = 0; j < temp; j++)
				{
					//result |= 1 << bitShift;
					bitShift++;
					if (bitShift >= 10)
					{
						goto out;
					}
				}
				count1 = 0;
				high = 0;
			}
			count1 += tempFrame[i];
		}
		else
		{
			high++;
			if(low>0)
			{
				count1 += 3;//tempFrame[i] / 2;
				temp = count1 / 18;
				for (int j = 0; j < temp; j++)
				{
					result |= 1 << bitShift;
					bitShift++;
					if (bitShift >= 10)
					{
						counter -= tempFrame[i];
						goto out;
					}
					
				}
				count1 = 0;
				low = 0;
			}
			count1 += tempFrame[i];
		}
	}
	if (bitShift < 10)
	{
		if (low > 0)
		{
			count1 += 5;
			temp = count1 / 18;
			for (int j = 0; j < temp; j++)
			{
				result |= 1 << bitShift;
				bitShift++;
				count1 = 0;
			}
			low = 0;
		}
	}
out:
//	memset(buf, 0, 100);
//	sprintf(buf, "%d ", sbuf[i]);
	//fsk_debug("\n");
	if (bitShift < 10)
	{
		*code = 0xff;
		counter = tempFrame[0]/2+1;
	}
	else
		*code = (result >> 1) & 0xff;

	if ((result & 0x01) == 0x01)
		setStatus(SEARCHING_START_BIT);
	memset(buf, 0, 100);
	sprintf(buf, "=========== 0x%04x 0x%02x\n", result,(*code)&0xFF);
	//fsk_debug(buf);
	return counter;
}
void FSKDetector::HexMsgPrint(unsigned char * ptrData, int Length)
{

}
int FSKDetector::calcFrequencyZerocrossing(short* data,int * position) 
{
	int numSamples = mSamplePerbit;
	int numCrossing = 0;
	char buf[100],sbuf[100];
	int firstPoint,endPoint;
	int i = 0;
	int countOne=0,countZero=0;
	int index;
	int result = 0;
#if 1
	for (i = 0; i < numSamples; i++) {
		if (data[i] >= 0)
			sbuf[i] = 1;
		else
			sbuf[i] = 0;
	}
	if (sbuf[0] ==1)
	{
		countOne++;

		for (i = 1; i < numSamples; i++)
		{
			if (sbuf[i] == 1)
			{
				countOne++;
			}
			else
				break;
		}
		for (; i < numSamples; i++)
		{
			if (sbuf[i]== 0)
			{
				countZero++;
			}
			else
				break;
		}
		if (countOne > 10 || countOne < 4)
		{
			countOne = 0;
			{
				for (; i < numSamples; i++)
				{
					if (sbuf[i] == 1)
					{
						countOne++;
					}
					else
						break;
				}
			}
		}

	}
	else
	{
		countZero++;
		for (i = 1; i < numSamples; i++)
		{
			if (sbuf[i] == 0)
			{
				countZero++;
			}
			else
				break;
		}
		for (; i < numSamples; i++)
		{
			if (sbuf[i]== 1)
			{
				countOne++;
			}
			else
				break;
		}
		if (countZero > 10 || countZero < 4)
		{
			countZero = 0;
			{
				for (; i < numSamples; i++)
				{
					if (sbuf[i] == 0)
					{
						countZero++;
					}
					else
						break;
				}
			}
		}
	}

	index = i;
	
//	if (countOne < 7 || countZero < 7)
	//{
		//for (i = index; i < numSamples - 1; i++)
		//{
			//if (sbuf[i] != sbuf[i + 1])
			//{
//				break;
//			}

//		}
//		if (i < numSamples)
//			index = i + 1;
//		else
//			index = numSamples;
//	}
//	if (index < 18)
//		index = 18;
	//mSignalPointer += index;
	*position = index;
#if 0
	for (i = 0; i < numSamples; i++) {
		{
			memset(buf, 0, 100);
			sprintf(buf, "%d ", data[i]);
			fsk_debug(buf);
		}
	}

	fsk_debug("\n");

	for (i = 0; i < numSamples; i++) {
		{
			memset(buf, 0, 100);
			sprintf(buf, "%d ", sbuf[i]);
			fsk_debug(buf);
		}
	}

	fsk_debug("\n");
	memset(buf, 0, 100);
	sprintf(buf, "one %d zero %d mSignalPointer %d index %d\n", countOne,countZero, mSignalPointer,index);
	fsk_debug(buf);
#endif
	if (countOne + countZero < 13 && (countOne < 7) && (countZero < 7) && (countOne>3) && (countZero>3))
		result =  2200;
	else
	{
		if (countOne > 11 || countZero > 11)
			result =  200;
		else
		{
			if (countOne >= 7 || countZero >= 7)
				result = 1200;
			else
				result = 200;
		}
	}
//	memset(buf, 0, 100);
//	sprintf(buf, "--------freq : %d\n", result);
//	fsk_debug(buf);
	return result;
#endif
	for (i = 0; i < numSamples - 1; i++) {
		if ((data[i] > 0 && data[i + 1] <= 0)
			|| (data[i] < 0 && data[i + 1] >= 0)) {

			numCrossing++;
		}
		memset(buf, 0, 100);
		sprintf(buf, "%d ", data[i]);
		//fsk_debug(buf);
	}
	double numSecondsRecorded = (double)numSamples
		/ (double)msampleRate;
	double numCycles = numCrossing / 2;
	double frequency = numCycles / numSecondsRecorded;
	{
		memset(buf, 0, 100);
		sprintf(buf, "%d\n", data[numSamples-1]);
		//fsk_debug(buf);
		memset(buf, 0, 100);
		sprintf(buf, "-----numCrossing : %d %d mDecoderStatus %d\n", numCrossing, mSignalPointer, mDecoderStatus);
		//fsk_debug(buf);
	}
	if (numCrossing == 1 || numCrossing == 2)
		return 1200;
	else if (numCrossing == 3 || numCrossing == 4)
		return 2200;
	else
		return 200;
//	return (int)(frequency+0.5);
}
//check silence
double FSKDetector::rootMeanSquared(short* data) {
	double ms = 0;

	for (int i = 0; i < mSamplePerbit; i++) {
		ms += data[i] * data[i];
	}

	ms /= mSamplePerbit;

	return sqrt(ms);
}

int FSKDetector::determineState(int frequency, double rms)
{

	STATE state = UNKNOWN;

	if (rms <= 2000) {
		state = SILENCE;
		return state;
	}

	if (frequency == 1200) {
		state = HIGH;
	}
	else if (frequency = 2200) {
		state = LOW;
	}
	else
	{
		state = SILENCE;
	}

	return state;
}

void FSKDetector::getFrameData(int position) {

	mSignalPointer = position;
	if (mDecoderStatus == DECODING)
	{
		for (int j = 0; j < mSamplePerByte; j++) {
			mSampleBuffer[j] = mSignal[position + j];
		}
	}
	else
	{
		for (int j = 0; j < mSamplePerbit; j++) {
			mSampleBuffer[j] = mSignal[position + j];
		}
	}
}
void  FSKDetector::processSearchOneSerial()
{

	int pos;

	if (mSignalPointer <= mSignalEnd - mSamplePerbit*10)
	{

		getFrameData(mSignalPointer);



		double rrms = rootMeanSquared(mSampleBuffer);

		if (rrms < 2000)
		{
			mSignalPointer += mSamplePerbit / 2;

			return;
		}

		int freq = calcFrequencyZerocrossing(mSampleBuffer, &pos);

		STATE state = (STATE)determineState(freq, rrms);

		if (state == LOW && mDecoderStatus == SEARCHING_SIGNAL) {
			//found pre-carrier bit
			//			nextStatus(); //start searching for start bit
			mDecoderStatus = SEARCHING_START_BIT;
		}

		if (mDecoderStatus == SEARCHING_START_BIT && freq == 2200 && state == LOW) {
			//found start bit

			mSignalPointer += mSamplePerbit / 4; //shift 0.5 period forward

												 //			nextStatus(); //begin decoding
			mDecoderStatus = DECODING;

			return;
		}

		mSignalPointer++;
	}
}
void  FSKDetector::processIterationSearch() 
{
	int pos;

	if (mSignalPointer <= mSignalEnd - mSamplePerbit)
	{

		getFrameData(mSignalPointer);

		double rrms = rootMeanSquared(mSampleBuffer);

		if (rrms < 2000)
		{
			mSignalPointer += mSamplePerbit / 2;

			return;
		}

		int freq = calcFrequencyZerocrossing(mSampleBuffer,&pos);

		STATE state = (STATE)determineState(freq, rrms);

		if (state==LOW && mDecoderStatus==SEARCHING_SIGNAL) {
			//found pre-carrier bit
//			nextStatus(); //start searching for start bit
			mDecoderStatus = SEARCHING_START_BIT;
		}

		if (mDecoderStatus==SEARCHING_START_BIT && freq == 2200 && state==LOW) {
			//found start bit

			mSignalPointer += mSamplePerbit / 4; //shift 0.5 period forward

//			nextStatus(); //begin decoding
			mDecoderStatus = DECODING;

			return;
		}

		mSignalPointer++;
	}
	else {
//		trimSignal(); //get rid of data that is already processed

//		flushData();

//		setStatus(IDLE);
	}
}

void FSKDetector::processIterationDecode() 
{

	unsigned char currentbit = 0;
	int detected = 0;
	int pos;
	int ret;
	char data;
	char buf[100];
	if(mSignalPointer <= mSignalEnd - mSamplePerByte) {

		getFrameData(mSignalPointer);

		double rms = rootMeanSquared(mSampleBuffer);
		if (rms < 2000)
		{
			mSignalPointer += mSamplePerbit / 2;
			return;
		}
		//int freq = calcFrequencyZerocrossing(mSampleBuffer,&pos);
		ret = decodeFrame(mSampleBuffer,&data);

		STATE state;// = (STATE)determineState(freq, rms);
#if 0
		switch (freq)
		{
		case 1200:
			state = HIGH;
			break;
		case 2200:
			state = LOW;
			break;
		default:
			state = UNKNOWN;
			break;
		}
#endif
#if 0
		switch (state)
		{
		case HIGH:
		{
			if(mCurrentBit==0)
				setStatus(SEARCHING_START_BIT);
			else if (mCurrentBit == 9)
			{
				detected = 1;
				mCurrentBit = 0;
				setStatus(SEARCHING_START_BIT);
			}
			else
			{
				currentbit = (state == HIGH) ? 1 : 0;
				oneCode |= currentbit << (mCurrentBit - 1);
				mCurrentBit++;
			}
		}
			break;
		case LOW:
		{
			if (mCurrentBit == 0)
				mCurrentBit++;
			else if (mCurrentBit == 9)
			{
				detected = 0;
				mCurrentBit = 0;
				oneCode = 0;
				setStatus(SEARCHING_START_BIT);
				
			}
			else
			{
				currentbit = (state == HIGH) ? 1 : 0;
				oneCode |= currentbit << (mCurrentBit - 1);
				mCurrentBit++;
			}
		}
			break;
		default:
		{
			detected = 0;
			mCurrentBit = 0;
			oneCode = 0;
			setStatus(SEARCHING_START_BIT);
			
		}
			return;
		}
#endif
#if 0
		if ((mCurrentBit == 0) && (state==LOW)) {
			//start bit

			//prepare buffers
//			mBitBuffer = new StringBuffer();
			mCurrentBit++;
		}
		else if ((mCurrentBit == 0) && (state==HIGH)) {
			//post-carrier bit(s)

			//go searching for a new transmission
			setStatus(SEARCHING_START_BIT);
		}
		else if ((mCurrentBit == 9) && (state==HIGH)) {
			//end bit
			detected = 1;
			mCurrentBit = 0;
		}
		else if ((mCurrentBit > 0 && mCurrentBit < 9) && ((state==HIGH) || (state==LOW))) {

//			mBitBuffer.insert(0, ));
			currentbit = (state == HIGH) ? 1 : 0;
			result |= currentbit << (mCurrentBit - 1);
			mCurrentBit++;
		}
		else {
			//corrupted data, clear bit buffer

//			mBitBuffer = new StringBuffer();
			mCurrentBit = 0;

			setStatus(SEARCHING_START_BIT);
		}
#endif
		//mSignalPointer += pos;
		
		if (ret > 0)
		{

			//if (detected) 
			{
				if (currentReceive < 258)
				{

					if (data != -1)
					{
						if (isMsgStart == 0)
						{
							if (data == 0x04 || data == 0x08)
								isMsgStart = 1;
						}
						if (isMsgStart == 1)
						{
							if (currentReceive == 1)
							{
								MessageLength = data + 3;//one type one length one checksum
							}
							MessageBody[currentReceive++] = data;
							if (currentReceive == MessageLength)
							{
								if (CaculateCheckSum() == 1)
									fskDetected = 1;
								setStatus(SEARCHING_SIGNAL);
							}
						}
					}
					//				oneCode = 0;
				}
				else
				{// decode one message
					// no space to save the result
				}
				memset(buf, 0, 100);
				sprintf(buf, "***************************%d \n", mSignalPointer);
				//fsk_debug(buf);
				detected = 0;
				mSignalPointer += ret;
				return;
			}
		}

		mSignalPointer += mSamplePerbit;
		setStatus(SEARCHING_START_BIT);
	}
	else {

//		trimSignal(); //get rid of data that is already processed

//		flushData();

//		setStatus(IDLE, DECODING); //we need to wait for more data to continue decoding
	}
}

void FSKDetector::setStatus(DecoderStatus status)
{
	setStatus(status, IDLE);
}

void FSKDetector::setStatus(DecoderStatus status, DecoderStatus paused)
{
	mDecoderStatus = status;
	mDecoderStatusPaused = paused;
}
int FSKDetector::FSK_detect(short samples[])
{
	return 0;
}
int FSKDetector::fskDetecting(short inputFrame[])
{


	int ii;
	ffwDetect();
	return 0;
	if (mSignalEnd<samplebuf_size / 2)
	{
		for (ii = 0; ii < frameSize; ii++)
		{
			mSignal[ii + mSignalEnd] = inputFrame[ii];
		}
		frameCount += frameSize;
		mSignalEnd += frameSize;

//		while (mSignalPointer <= (mSignalEnd - mSamplePerByte))
		while (mSignalPointer <= (mSignalEnd - samplebuf_size/2))
		{
			ffwDetect();
		//	if (advance > samples_nvalid)
		//		break;
#if 0
			switch (mDecoderStatus)
			{

			case IDLE:
				break;

			case SEARCHING_SIGNAL:
			case SEARCHING_START_BIT:

				processIterationSearch();
				break;

			case DECODING:
				processIterationDecode();

			default:
				break;
			}
#endif
		}
		
		for (ii = 0; ii < mSignalEnd - mSignalPointer; ii++)
		{
			mSignal[ii] = mSignal[mSignalPointer + ii];
		}
		mSignalPointer = 0;
		mSignalEnd = ii;
	}
	else
	{
		
	}
	return fskDetected;
}

bool FSKDetector::getFSKCode()
{
	if (CaculateCheckSum())
	{
		if (MessageLength > 2)
		{
			if (MessageBody[0] == 0x04)
			{
				if (MessageBody[1] == MessageLength - 3)
				{
					char buf[255];
					int i = 0;
					for (i = 0; i < MessageLength - 3; i++)
					{
						buf[i] = MessageBody[i + 2] & 0x7f;
					}
					buf[i] = 0;
					fskcode->write(buf, strlen(buf));
					fskcode->write("\n", 1);
					updateFSKCode(buf);
				}
			}
			else if (MessageBody[0] == 0x80)
			{
				if (MessageBody[1] == MessageLength - 3)
				{
					char buf[255];
					unsigned char subMsg[6];
					char subMsgLen[6];
					int i = 0, j = 0;
					int subMsgCount = 0, length;
					j = 2;
					while (j < MessageLength - 1)
					{
						switch (MessageBody[j++])
						{
						case 0x01:
							subMsg[subMsgCount] = 0x01;
							subMsgLen[subMsgCount] = MessageBody[j++];
							length = subMsgLen[subMsgCount];
							while (length-- > 0)
							{
								buf[i++] = MessageBody[j++] & 0x7f;
							}
							buf[i++] = '|';
							subMsgCount++;
							break;
						case 0x02:
							subMsg[subMsgCount] = 0x02;
							subMsgLen[subMsgCount] = MessageBody[j++];
							length = subMsgLen[subMsgCount];
							while (length-- > 0)
							{
								buf[i++] = MessageBody[j++] & 0x7f;
							}
							buf[i++] = '|';
							subMsgCount++;
							break;
						case 0x04:
							subMsg[subMsgCount] = 0x04;
							subMsgLen[subMsgCount] = MessageBody[j++];
							length = subMsgLen[subMsgCount];
							while (length-- > 0)
							{
								buf[i++] = MessageBody[j++] & 0x7f;
							}
							buf[i++] = '|';
							subMsgCount++;
							break;
						case 0x07:
							subMsg[subMsgCount] = 0x07;
							subMsgLen[subMsgCount] = MessageBody[j++];
							length = subMsgLen[subMsgCount];
							while (length-- > 0)
							{
								buf[i++] = MessageBody[j++] & 0x7f;
							}
							buf[i++] = '|';
							subMsgCount++;
							break;
						case 0x08:
							subMsg[subMsgCount] = 0x08;
							subMsgLen[subMsgCount] = MessageBody[j++];
							length = subMsgLen[subMsgCount];
							while (length-- > 0)
							{
								buf[i++] = MessageBody[j++] & 0x7f;
							}
							buf[i++] = '|';
							subMsgCount++;
							break;
						case 0xE1:
							subMsg[subMsgCount] = 0xE1;
							subMsgLen[subMsgCount] = MessageBody[j++];
							length = subMsgLen[subMsgCount];
							while (length-- > 0)
							{
								buf[i++] = MessageBody[j++] & 0x7f;
							}
							buf[i++] = '|';
							subMsgCount++;
							break;
						default:
							break;
						}
					}
					buf[i] = 0;
					fskcode->write(buf, strlen(buf));
					fskcode->write("\n", 1);
					updateFSKCode(buf);

				}
			}
		}
		return true;
	}
	return false;
}
void fsk_debug(char* str)
{
#if DEBUG
	fskdebug->write(str, strlen(str));
#endif
}