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


class SubFFT;

class FFTLooper: public BLooper
{
public:
	FFTLooper(short nu);	// nu = the logarithm in base 2 of the sample size
											 	//			e.g. when the samplesize is 32, nu will be 5.
	~FFTLooper();
	inline bool IsBusy(void) { return (Phase > 0); }
	void FFT(float *real, float *imag, BMessage *msg, BLooper *looper);
	
private:
	float 		*real, *imag, *cos_, *sin_;
	short			nu, ssize, nThreads;
	short			Phase, MsgsReturned;
	BMessage 	*ReturnMessage;
	BLooper		*ReturnLooper;
	SubFFT		**Subs;

	void MessageReceived(BMessage *msg);
};

