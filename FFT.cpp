
/****************************************************************************************
 *
 * FFT.cpp
 *
 * Multi-threaded FFT calculation BLooper.
 *
 * Written in april 1997 by 
 *	K.P. van Beem (patrick@aobh.xs4all.nl / 2:280/464.2)
 *	Parlevinker 5
 *	2152 LC  Nieuw-Vennep
 *	The Netherlands
 *
 *
 * Usage: Create the object FFTLooper and call FFTLooper::FFT(). It will then start
 * calculating the FFT from the given buffers using the predefined 2^nu amount of
 * samples. Calculation is done asynchronous of the calling programm. When calculation
 * is done, the BMessage parameter is sent to the BLooper parameter.
 * When calculation, no calls are accepted.
 *
 * This code is based on the FFT program of E.O. Brigham, translated from Fortran to C
 * by Jim Pisano and optimised by Tinic. I translated it to C++ and made it
 * multi-threaded, using the features the BeOS offers. Thank you Be and JLG!
 *
 * ToDo: Queing incomming calculation requests...
 *
 * Notes:
 * - Only works when you have at least twice as mucht data than processors :)
 *
 **************************************************************************************/

//#define DEBUG
#include <support/Debug.h>

#include "FFT.h"


/**********************************************
 * Fast bit-swap routine
 **********************************************/
inline short bit_swap(short i,short nu )
{
	ushort ib=0;
	for(short i1=0;i1<nu;i1++)
	{
		ib=(ib<<1)+(i&0x01);i=i>>1;
	}		
	return( ib );
}

/**********************************************
 * Simple swap routine.
 **********************************************/
inline void swap(float &x1, float &x2)
{
	float temp = x1;
	x1 = x2;
	x2 = temp;
	return;
}


/********************************************************
 * A sub-looper that calculates part of the FFT for a shared
 * data-set.
 ********************************************************/
class SubFFT: public BLooper
{
public:
	SubFFT(short nu, short nThreads, short ThreadNr, float *sintab, float *costab, BLooper *ReplyLooper);
	void Init(float *real, float *imag);
	
private:
	short 	nu, nu1, n2, l;
	short		nthreads, threadnr;
	float		*real, *imag, *sin_, *cos_;
	BLooper	*MainLooper;	// My daddy :)

	void MessageReceived(BMessage *msg);
};



SubFFT::SubFFT(short nu, short nThreads, short ThreadNr, float *sintab, float *costab, BLooper *ReplyLooper)
{
	// Shared data area's by the sub-loopers and the main looper.
	MainLooper	= ReplyLooper;
	this->nu		= nu;
	nthreads		=	nThreads;
	threadnr		=	ThreadNr;
	sin_				= sintab;
	cos_				= costab;
	Run();
}
	
// Init should be called before each new full FFT calculation.
void SubFFT::Init(float *real, float *imag)
{
	this->real 	= real;
	this->imag	=	imag;
	nu1					= nu - 1;
	n2					= 1 << nu1;
	l						= 0;
}
	
/*
 * The main calculation part. I realize that a big part of this
 * routine is double, but a high level of parametrizing slows down
 * the loop. I haven't done any measurments though...
 */
void SubFFT::MessageReceived(BMessage *msg)
{
	short k, k1,k2,kend;
	float tr,ti;
	short arg;
	float c,s;

	// And let's calculate!
	switch(msg->what)
	{
	case 'step':
		kend	= (1<<nu) -1;
		k			= threadnr;
		do
		{
			for(short i=threadnr; i<n2; i += nthreads)
			{
				arg = bit_swap(k/(1<<nu1), nu);
				c = cos_[arg];
				s = sin_[arg];
				k1 = k;
				k2 = k+n2;
				tr = (real[k2]*c)+(imag[k2]*s);
				ti = (imag[k2]*c)-(real[k2]*s);
				real[k2] = real[k1]-tr;
				imag[k2] = imag[k1]-ti;
				real[k1]+=tr;
				imag[k1]+=ti;
				k+=nthreads;
			}
			k += n2;
		} 
		while(k < kend);		
		nu1--;
		n2>>=1;
		l++;
		break;

	case 'all':
		short kstart = (threadnr * n2) << 1;
		kend				 = kstart + (n2 << 1) - 1;
		while(l < nu)
		{
			k = kstart;
			do
			{
				for(short i=0;i<n2;i++)
				{
					arg = bit_swap(k/(1<<nu1),nu);
					c = cos_[arg];
					s = sin_[arg];
					k1 = k;
					k2 = k+n2;
					tr = (real[k2]*c)+(imag[k2]*s);
					ti = (imag[k2]*c)-(real[k2]*s);
					real[k2] = real[k1]-tr;
					imag[k2] = imag[k1]-ti;
					real[k1]+=tr;
					imag[k1]+=ti;
					k++;
				}
				k+=n2;
			} 
			while(k < kend);		
			nu1--;
			n2>>=1;
			l++;
		}
		break;

	} // switch

	MainLooper->PostMessage(DetachCurrentMessage());
}




/***********************************************************
 * FFTLooper::FFTLooper(short nu)
 *
 * The main thing...
 * nu = the ;logarithm in base 2 of the sample size
 *			e.g. when the samplesize is 32, nu will be 5.
 ***********************************************************/
FFTLooper::FFTLooper(short nu)
{
	// Precalculated values.
	this->nu		=	nu;
	ssize				= 1<<nu;
	cos_				= new float[ssize];
	sin_				= new float[ssize];
	for(int i=0;i<ssize;i++)
	{
		cos_[i]=cos((2.0*M_PI*i)/ssize);
		sin_[i]=sin((2.0*M_PI*i)/ssize);
	}
	
	// Create multi-thread environment
	system_info si;
	get_system_info(&si);
	// The number of threads should be a power of 2,
	// at least 2 and at least the numer of cpu's (for efficiency)
	for(nThreads = 2; nThreads < si.cpu_count; nThreads <<= 1);
	Subs = new SubFFT*[nThreads];
	for(int i=0; i < nThreads; i++) Subs[i] = new SubFFT(nu, nThreads, i, sin_, cos_, this);

	Run();
}
	

FFTLooper::~FFTLooper()
{
	PRINT(("~FFTLooper()\n"));
	for(int i=0; i < nThreads; i++) Subs[i]->Quit();
	delete [] Subs;
	delete [] cos_;
	delete [] sin_;
}


void FFTLooper::FFT(float *real, float *imag, BMessage *reply, BLooper *replyto)
{
	if(IsBusy()) return;
	Phase = 2;
	
	this->real		= real;
	this->imag		= imag;
	ReturnMessage	= reply;
	ReturnLooper	= replyto;

	// Start the thing up...
	for(int i=0; i < nThreads; i++) {
		Subs[i]->Init(real, imag);
		Subs[i]->PostMessage('step');
	}
	MsgsReturned = 0;
}

	
void FFTLooper::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
	case 'step':
		// Synchronize all threads.
		if(++MsgsReturned < nThreads) return;
		for(int i=0; i < nThreads; i++)
			Subs[i]->PostMessage((Phase < nThreads)?'step':'all');
		Phase <<= 1;
		MsgsReturned = 0;
		break;

	case 'all':
		// Synchronize all threads.
		if(++MsgsReturned < nThreads) return;
		// Sort output
		short k, ib;
		for(k=0; k<ssize; k++) {
			ib = bit_swap(k,nu);
			if(ib > k) {
				swap(real[k], real[ib]);
				swap(imag[k], imag[ib]);
			}
		}

		ReturnLooper->PostMessage(ReturnMessage);
		Phase = 0;	// This 'frees' the looper to accept msg's again.
		break;
	}
}
