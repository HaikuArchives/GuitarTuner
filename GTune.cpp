/*
 * Guitar Tuner 1.0
 *
 * Written in april 1997 by K.P. van Beem (patrick@aobh.xs4all.nl / 2:280/464.2)
 *
 * Written as example of the use of the multy-threaded FFT looper (or was it the
 * other way around? :) ).
 *
 * This programm performs an FFT at app. 1.5s of sample data sampled at
 * 5510 Hz. This gives an accuracy of ± 0.34 Hz. The sample-data comes in every
 * app. 1/3s, and is then shifted in the total sample array, which is then
 * recalculated and displayed. The range is 0 to 2755 Hz (in 0.67 Hz steps)
 *
 * The sample-data is then matched against the basic frequencies of the whole
 * notes and displayed. I use it to tune my acoustic guitar. Hence the name
 * of the program :-).
 *
 * Notes:
 * - The reordering of the total FFT translation ain't necsecairy here. But for
 *   the example, I let it in. Reordering of just the one maximum value would
 *   be enough here.
 * - I'm aware of the fact that during the search for the maximum value, the
 *   array may change due to a new FFT calculation. The seach should be contained
 *   within the FFT loop. A job for the future (of you?).
 * - I know I should sample at 44100 Hz. I don't.
 * - The program should be more configurable for it's task. e.g. the accuracy.
 * - The match against the basic notes is not optimal. And there are more notes...
 *   but my guitar is always at EADGBE (I'm only playing it for 1.5 years now :) )
 * - My first real experience with C++ is on this BeBox, so sorry for my probably
 *   bad C++ style. My C is much better. Realy...
 *
 */

#define DEBUG
#include <support/Debug.h>

#include "FFT.h"

#define SAMPLESIZE 8192
#define SIZEPOWER    13
#define SAMPLERATE 5510

/* Global shared data between the subscriber and the FFT looper. */
static float 	idat[SAMPLESIZE];
static float	real[SAMPLESIZE];
static float 	imag[SAMPLESIZE];
FFTLooper			*fftl;

// Basic frequencies. Pairs: Actual frequency, Lower-bound range.
static float GroundF[] = { 
220.0,	208.0, 			// A
246.95,	233.475,		// B
261.7,	254.325,		// C
293.7,	277.7,			// D
329.7,	311.7,			// E
349.3,	339.5,			// F
392.0,	370.65,			// G
440.0,	416.0				// Dummy for upper-bound of G (following A2)
};



/*********************************************************
 * class MeterView: public BView
 *
 * A view which presents the user with a centered meter
 * with a 'needle'. The meter is shaded 3-2-1-2-3 in
 * red - yellow - green - yellow - red.
 *********************************************************/
class MeterView: public BView
{
public:
	MeterView(BRect rect, const char *name = NULL, ulong sizemode = B_FOLLOW_TOP | B_FOLLOW_LEFT, ulong flags = B_WILL_DRAW)
	:BView(rect, name, sizemode, flags)
	{
		CurPos = rect.Width() / 2;
	}

	void SetValue(float NewValue)	// Value is between -1 and 1.
	{
		BRect &vr(Bounds());
		if(NewValue < -1) NewValue = -1;
		if(NewValue >  1) NewValue =  1;
		SetDrawingMode(B_OP_INVERT);
		StrokeLine(BPoint(CurPos, vr.top), BPoint(CurPos, vr.bottom));
		CurPos = Bounds().left + Bounds().Width() * (NewValue+1) / 2;
		StrokeLine(BPoint(CurPos, vr.top), BPoint(CurPos, vr.bottom));
	}

private:
	float	CurPos;

	void AttachedToWindow(void)
	{
		Draw(Bounds());
	}

	void Draw(BRect updrec)
	{
		uchar	fase	= 1;
		float Step	= Bounds().Width() / 11;
		float Pos		= Bounds().left;
		float End;
		uchar r			= 255, g = 0;

		SetDrawingMode(B_OP_COPY);
		while(fase < 6 && Pos <= updrec.right)
		{
			SetHighColor(r,g,0);
			switch (fase)
			{
			case 1: case 5:
				End = Pos	+ 3 * Step;
				g 	=  255;
				break;
			case 2: case 4:
				End = Pos	+ 2*Step;
				if(fase == 2)	r = 0;
				else					g = 0;
				break;
			case 3:
				End = Pos	+ Step;
				r		=  255;
				break;
			} // switch
			if(End >= updrec.left) FillRect(BRect(Pos, updrec.top, End, updrec.bottom));
			Pos = End;
			fase++;
		} // while
		SetDrawingMode(B_OP_INVERT);
		StrokeLine(BPoint(CurPos, updrec.top), BPoint(CurPos, updrec.bottom));
	}
};


/***********************************************
 * Main window (UI)
 *
 ***********************************************/
class mwin: public BWindow
{
public:
	mwin():BWindow(BRect(100,100,370,240), "Guitar tuner", B_TITLED_WINDOW, 0)
	{
		const C1 = 10, C2 = 150;

		BView *tg = new BView(BRect(0,0,270,140),NULL, B_FOLLOW_ALL, B_WILL_DRAW | B_NAVIGABLE);
		tg->SetViewColor(235,235,235);
		AddChild(tg);

		tg->AddChild(new BStringView(BRect(C1,10,C2-1,30),NULL,"Measured:"));
		tg->AddChild(new BStringView(BRect(C1,40,C2-1,60),NULL,"Scaled to:"));
		tg->AddChild(new BStringView(BRect(C1,70,C2-1,90),NULL,"Compared to:"));

		tg->AddChild(HZ 	= new BStringView(BRect(C2, 10,C2+100, 30), NULL, "Initializing"));
		tg->AddChild(FRef = new BStringView(BRect(C2, 40,C2+100, 60), NULL, NULL));
		tg->AddChild(Ref 	= new BStringView(BRect(C2, 70,C2+100, 90), NULL, NULL));
		tg->AddChild(MV 	= new MeterView  (BRect(C2,100,C2+100,120)));


		// Quickly set some common attributes.
		Lock();
		BView *TView = tg->ChildAt(0);
		while(TView) {
			if(TView->Frame().left == C1) TView->SetViewColor(235,235,235);
			if(is_kind_of(TView, BStringView)) {
				cast_as(TView, BStringView)->SetFontName("Arial MT");
				cast_as(TView, BStringView)->SetFontSize(20);
			}
			TView = TView->NextSibling();
		}
		Unlock();

		Show();
	}
	

private:
	BStringView *HZ, *Ref, *FRef;
	MeterView		*MV;

	void MessageReceived(BMessage *msg)
	{
		switch(msg->what) {
		case 'calc':
			
			/* Find the maximum value */
			float *f=&real[1], max=0;
			short fmax=0;
			for(int i=1; i < SAMPLESIZE; i++, f++)
				if(abs(*f) > max) {
					max		= abs(*f); 
					fmax 	= i;
				}
			if( fmax > SAMPLESIZE>>1 )
				fmax = SAMPLESIZE - fmax;

			/* Put values in the view. */
			float freq = fmax * (float(SAMPLERATE) / float(SAMPLESIZE));
			char 	buf[32];
			int		n;
			sprintf(buf, "%.2f", freq);
			HZ->SetText(buf);
			if (freq == 0) break;
			while( freq < GroundF[1]  ) freq *= 2;
			while( freq > GroundF[15] ) freq /= 2;
			for(n=3; n < 16; n+=2)
				if (freq < GroundF[n]) break;
			sprintf(buf, "%.2f", freq);
			FRef->SetText(buf);
			sprintf(buf, "%.2f (%c)", GroundF[n-3], 'A' - 1 + n/2 );
			Ref->SetText(buf);
			MV->SetValue( ((freq - GroundF[n-3]) / (GroundF[n] - GroundF[n-2])) * 4 );

			break;
		}
	}

	bool QuitRequested()
	{
		be_app->PostMessage(B_QUIT_REQUESTED);
		return TRUE;
	}

} *MainWin;





/********************************************************
 * The audio subscriber and his functions.
 *
 ********************************************************/
class MyAS
{
private:
	bool DoBuf(short *buffer, long count);
	static bool EntryHook(void *arg, char * buffer, long count)
	{ return ((MyAS*)arg)->DoBuf((short*)buffer, count/2); }

	BAudioSubscriber	*as;

public:
	MyAS();
	~MyAS()
	{
		as->Unsubscribe();	
	}
};


MyAS::MyAS()
{
	as = new BAudioSubscriber("GTune subscriber");
	if(as->Subscribe(B_ADC_STREAM, B_SHARED_SUBSCRIBER_ID, FALSE) < B_NO_ERROR) {
		(new BAlert(NULL,"Failed to allocate the Audio subscriber","OK"))->Go();
		return;
	}
	as->SetADCInput(B_MIC_IN);
	as->BoostMic(TRUE);
	as->SetADCSampleInfo(2, 1, B_BIG_ENDIAN, B_LINEAR_SAMPLES);
	as->SetSamplingRate(SAMPLERATE);
	as->EnterStream(NULL, FALSE, this, EntryHook, NULL, TRUE);
}



bool MyAS::DoBuf(short *buffer, long count)
{
	float *f;
	short	*s;
	int i;
	
	memmove(idat, &idat[count], (SAMPLESIZE-count) * sizeof(float));
	for(s=buffer, f=&idat[SAMPLESIZE - count], i=count; i>0; i--) {
		*f++ = *s++;
	}
	if(!fftl->IsBusy()) {
		memcpy(real, idat, SAMPLESIZE * sizeof(float));
		memset(imag, 0,    SAMPLESIZE * sizeof(float));
		fftl->FFT(real, imag, new BMessage('calc'), MainWin);
	}
	return TRUE;
}




/********************************************************
 * The application.
 *
 ********************************************************/
class GTune: public BApplication
{
private:
	void AboutRequested(void);
	void ReadyToRun();
	
	MyAS *as;

public:
	GTune(): BApplication('gtun') {};
	~GTune()
	{
		delete as;
		fftl->Quit();
	}
};



void GTune::ReadyToRun()
{
	memset(idat, 0, SAMPLESIZE * sizeof(float));
	MainWin	= new mwin();
	fftl		= new FFTLooper(SIZEPOWER);
	as			= new MyAS();
}


void GTune::AboutRequested(void)
{
	(new BAlert(NULL,"GuitarTuner V1.0 ("__DATE__")\n(c) 1997 by Patrick van Beem\n"
	"e-mail: patrick@aobh.xs4all.nl", "OK"))->Go();
}


/****************************************************
 * int main()
 *
 * Guess what...
 ****************************************************/
int main()
{	
	GTune *myApp;

	myApp = new GTune();
	myApp->Run();
	delete(myApp);
	return(0);
}


